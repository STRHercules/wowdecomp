#pragma once
// Claude 2026-11-14: draws one 3.3.5 WMO (the inn) behind the character select cast. Self-contained: it does NOT use CMapObj / CMap
// (those need the world initialised; the glue screens never run CMap::Initialize). It reads the root and group files itself, keeps the
// geometry in plain vectors, and draws with the fixed-function-style path the world's RenderGroupColorTex uses (raw vertex arrays,
// no shaders), with the WMO's baked vertex colours as the lighting. Doodads (tables, bar, lamps) are created as scene M2 models.
// Driven by InnCast.cfg (WOW_INN_WMO...), off unless WOW_INN_WMO is set. Nothing here runs outside the character select.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Base/Handle.h"
#include "Engine/Source/Base/Status.h"
#include "Engine/Source/Gx/CGxTexFlags.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/Tempest/C44Matrix.h"
#include "Engine/Source/Tempest/C4Quaternion.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/Tempest/C2Vector.h"
#include "Engine/Source/Tempest/CImVector.h"

namespace InnWmo {

struct Settings {
    std::string path;       // WMO root file, e.g. World\wmo\Azeroth\Buildings\GoldshireInn\GoldshireInn.wmo (empty = off)
    float x = 0.0f, y = 0.0f, z = 0.0f, yaw = 0.0f, scale = 1.0f;   // placement in the backdrop model's space
    float bright = 1.6f;    // multiplier on the baked vertex colours (fixed-function modulate looks darker than the shaders)
    int32_t swapRB = -1;    // -1 automatic from the device colour format, 0 no, 1 yes
    int32_t doodads = 1;    // 1 = create the WMO's doodads (furniture) as scene models
    int32_t maxDoodads = 400;
    int32_t doodadSet = 2;      // MODF doodad set of the Goldshire inn placement (Set_C); the global set 0 is always added. -1 = every set (duplicates)
    uint32_t doodadFlags = 0;   // CreateModel flags for the furniture (0 = normal; 32 = the world's streaming-doodad flag)
    float clearRadius = 2.6f;  // scene units around (clearX, clearY): furniture below clearZ is removed (0 = keep everything)
    float clearX = -1.1f, clearY = 0.0f, clearZ = 1.6f;
};

struct Batch { uint32_t start; uint16_t count, vmin, vmax; uint8_t flags, material; };
struct BatchInfo { bool ready = false; float mn[3] = { 0.0f, 0.0f, 0.0f }, mx[3] = { 0.0f, 0.0f, 0.0f }; std::vector<float> tri; };   // tri: centre xyz + radius per triangle (WMO-local)
struct Group {
    std::vector<BatchInfo> info;   // v43: see-through line, one per batch (built lazily)
    std::vector<C3Vector> pos;
    std::vector<C2Vector> uv;
    std::vector<CImVector> raw;     // baked colours as stored
    std::vector<CImVector> lit;     // raw * bright (and R/B swapped when the device wants it)
    std::vector<uint16_t> idx;
    std::vector<Batch> batches;
};
struct Material { uint32_t flags = 0, blend = 0; std::string tex; CTexture* texture = nullptr; };
struct Doodad { std::string name; C3Vector pos; C4Quaternion rot; float scale = 1.0f; CM2Model* model = nullptr; bool failed = false; bool hidden = false; float fade = 1.0f; bool fadeHidden = false; };

struct State {
    std::string loadedPath;
    bool attempted = false, ok = false;
    std::vector<Group> groups;
    std::vector<Material> materials;
    std::vector<Doodad> doodads;
    float litBright = -1.0f;
    int32_t litSwap = -1;
    uint32_t drawnBatches = 0, skippedBatches = 0;
    bool focusActive = false;               // v43: a character is zoomed on; focus = its centre (world), set by the camera code every frame
    C3Vector focus = C3Vector(0.0f, 0.0f, 0.0f);
    uint32_t fadedDoodads = 0, fadedTris = 0;
    uint32_t lastLog = 0;
};
inline State& S() { static State s; return s; }

inline bool ReadFile(const std::string& name, std::vector<uint8_t>& out) {
    SFileImpl* file = nullptr;
    if (!SFileOpenFile(name.c_str(), &file) || !file) return false;
    const uint32_t size = SFileGetFileSize(file, nullptr);
    out.resize(size);
    uint32_t read = 0;
    const bool ok = size == 0 || SFileReadFile(file, out.data(), size, &read, nullptr, 0);
    SFileCloseFile(file);
    return ok && read == size;
}

// Chunk tags are stored byte-reversed in the file ("MOHD" is D,H,O,M).
inline bool TagIs(const uint8_t* p, const char* tag) { return p[0] == static_cast<uint8_t>(tag[3]) && p[1] == static_cast<uint8_t>(tag[2]) && p[2] == static_cast<uint8_t>(tag[1]) && p[3] == static_cast<uint8_t>(tag[0]); }
inline uint32_t U32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
inline float F32(const uint8_t* p) { float v; std::memcpy(&v, p, 4); return v; }

inline std::string ToM2(std::string name) {
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    return name + ".m2";
}

inline bool ParseGroup(const std::vector<uint8_t>& data, Group& g) {
    size_t at = 12;     // MVER chunk
    while (at + 8 <= data.size()) {
        const uint8_t* c = data.data() + at;
        const uint32_t size = U32(c + 4);
        if (at + 8 + size > data.size()) break;
        if (TagIs(c, "MOGP")) {
            size_t sub = at + 8 + 68;   // MOGP header is 68 bytes
            const size_t end = at + 8 + size;
            bool haveUv = false, haveCol = false;
            while (sub + 8 <= end) {
                const uint8_t* s = data.data() + sub;
                const uint32_t n = U32(s + 4);
                if (sub + 8 + n > end) break;
                const uint8_t* d = s + 8;
                if (TagIs(s, "MOVT")) { g.pos.resize(n / 12); std::memcpy(g.pos.data(), d, g.pos.size() * 12); }
                else if (TagIs(s, "MOVI")) { g.idx.resize(n / 2); std::memcpy(g.idx.data(), d, g.idx.size() * 2); }
                else if (TagIs(s, "MOTV") && !haveUv) { haveUv = true; g.uv.resize(n / 8); std::memcpy(g.uv.data(), d, g.uv.size() * 8); }
                else if (TagIs(s, "MOCV") && !haveCol) { haveCol = true; g.raw.resize(n / 4); std::memcpy(g.raw.data(), d, g.raw.size() * 4); }
                else if (TagIs(s, "MOBA")) {
                    const size_t count = n / 24;
                    for (size_t i = 0; i < count; i++) {
                        const uint8_t* b = d + i * 24;
                        Batch batch;
                        batch.start = U32(b + 12);
                        std::memcpy(&batch.count, b + 16, 2);
                        std::memcpy(&batch.vmin, b + 18, 2);
                        std::memcpy(&batch.vmax, b + 20, 2);
                        batch.flags = b[22];
                        batch.material = b[23];
                        g.batches.push_back(batch);
                    }
                }
                sub += 8 + n;
            }
            break;
        }
        at += 8 + size;
    }
    if (g.uv.size() < g.pos.size()) g.uv.resize(g.pos.size(), C2Vector());
    return !g.pos.empty() && !g.idx.empty() && !g.batches.empty();
}

inline void Load(const Settings& cfg, CM2Scene* scene) {
    State& s = S();
    s.attempted = true;
    s.ok = false;
    s.groups.clear();
    s.materials.clear();
    s.doodads.clear();
    s.loadedPath = cfg.path;
    std::vector<uint8_t> root;
    if (!ReadFile(cfg.path, root)) { sExtensionMgr->Log("InnWmo: cannot read %s", cfg.path.c_str()); return; }

    uint32_t nGroups = 0;
    std::vector<char> texNames;
    std::vector<char> doodadNames;
    std::vector<std::pair<uint32_t, uint32_t>> doodadSets;   // (first index, count) per MODS entry
    size_t at = 12;
    while (at + 8 <= root.size()) {
        const uint8_t* c = root.data() + at;
        const uint32_t size = U32(c + 4);
        if (at + 8 + size > root.size()) break;
        const uint8_t* d = c + 8;
        if (TagIs(c, "MOHD")) nGroups = U32(d + 4);
        else if (TagIs(c, "MOTX")) texNames.assign(reinterpret_cast<const char*>(d), reinterpret_cast<const char*>(d) + size);
        else if (TagIs(c, "MOMT")) {
            for (uint32_t i = 0; i + 64 <= size; i += 64) {
                Material m;
                m.flags = U32(d + i);
                m.blend = U32(d + i + 8);
                const uint32_t ofs = U32(d + i + 12);
                if (ofs < texNames.size()) m.tex = std::string(&texNames[ofs]);
                s.materials.push_back(m);
            }
        }
        else if (TagIs(c, "MODS")) {
            // doodad sets: 32 bytes each (name[20], startIndex, count, pad). The WMO stores several variants (Set_A/B/C) on top of each other;
            // the map placement (MODF) picks one (Goldshire inn: set 2), and the global set 0 is always drawn.
            for (uint32_t i = 0; i + 32 <= size; i += 32) doodadSets.push_back(std::make_pair(U32(d + i + 20), U32(d + i + 24)));
        }
        else if (TagIs(c, "MODN")) doodadNames.assign(reinterpret_cast<const char*>(d), reinterpret_cast<const char*>(d) + size);
        else if (TagIs(c, "MODD")) {
            for (uint32_t i = 0; i + 40 <= size; i += 40) {
                if (cfg.doodadSet >= 0 && !doodadSets.empty()) {
                    const uint32_t index = i / 40;
                    bool inSet = false;
                    for (size_t k = 0; k < doodadSets.size(); k++) {
                        if (k != 0 && static_cast<int32_t>(k) != cfg.doodadSet) continue;
                        if (index >= doodadSets[k].first && index < doodadSets[k].first + doodadSets[k].second) inSet = true;
                    }
                    if (!inSet) continue;
                }
                Doodad dd;
                const uint32_t ofs = U32(d + i) & 0xFFFFFF;
                if (ofs >= doodadNames.size()) continue;
                dd.name = std::string(&doodadNames[ofs]);
                dd.pos = C3Vector(F32(d + i + 4), F32(d + i + 8), F32(d + i + 12));
                dd.rot.x = F32(d + i + 16); dd.rot.y = F32(d + i + 20); dd.rot.z = F32(d + i + 24); dd.rot.w = F32(d + i + 28);
                dd.scale = F32(d + i + 32);
                s.doodads.push_back(dd);
            }
        }
        at += 8 + size;
    }

    std::string base = cfg.path;
    const size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    uint32_t loaded = 0, verts = 0;
    for (uint32_t i = 0; i < nGroups; i++) {
        char suffix[16];
        std::snprintf(suffix, sizeof(suffix), "_%03u.wmo", i);
        std::vector<uint8_t> bytes;
        Group g;
        if (ReadFile(base + suffix, bytes) && ParseGroup(bytes, g)) {
            verts += static_cast<uint32_t>(g.pos.size());
            s.groups.push_back(std::move(g));
            loaded++;
        }
    }
    // Textures: one request per distinct name; they stream in, batches whose texture is not ready yet are skipped that frame.
    for (Material& m : s.materials) {
        if (m.tex.empty()) continue;
        CStatus status;
        m.texture = TextureCreate(m.tex.c_str(), CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1, 0, 0, 0), &status, 0);
    }
    s.ok = loaded > 0;
    sExtensionMgr->Log("InnWmo: %s groups=%u/%u vertices=%u materials=%u doodads=%u ok=%d", cfg.path.c_str(), loaded, nGroups, verts,
        static_cast<uint32_t>(s.materials.size()), static_cast<uint32_t>(s.doodads.size()), s.ok ? 1 : 0);
    (void)scene;
}

inline void Relight(const Settings& cfg) {
    State& s = S();
    const bool deviceRgba = g_theGxDevicePtr && g_theGxDevicePtr->Caps().m_colorFormat == GxCF_rgba;
    const int32_t swap = cfg.swapRB >= 0 ? cfg.swapRB : (deviceRgba ? 1 : 0);
    if (s.litBright == cfg.bright && s.litSwap == swap) return;
    s.litBright = cfg.bright;
    s.litSwap = swap;
    for (Group& g : s.groups) {
        g.lit.resize(g.pos.size());
        for (size_t i = 0; i < g.pos.size(); i++) {
            CImVector c;
            if (i < g.raw.size()) c = g.raw[i]; else { c.r = c.g = c.b = 160; c.a = 255; }
            const float k = cfg.bright;
            uint32_t r = std::min(255u, static_cast<uint32_t>(c.r * k)), gg = std::min(255u, static_cast<uint32_t>(c.g * k)), b = std::min(255u, static_cast<uint32_t>(c.b * k));
            CImVector o;
            o.r = static_cast<uint8_t>(swap ? b : r);
            o.g = static_cast<uint8_t>(gg);
            o.b = static_cast<uint8_t>(swap ? r : b);
            o.a = 255;
            g.lit[i] = o;
        }
    }
}

// parent: the backdrop model's world transform (the WMO and its doodads sit in that space).
inline C44Matrix Placement(const Settings& cfg, const C44Matrix& parent) {
    C44Matrix local;
    local.RotateAroundZ(cfg.yaw);
    local.Scale(cfg.scale);
    local.d0 = cfg.x; local.d1 = cfg.y; local.d2 = cfg.z;
    return local * parent;
}

inline void PlaceDoodads(const Settings& cfg, const C44Matrix& placement, CM2Scene* scene, void* lightArg, void (*lightCallback)(CM2Model*, CM2Lighting*, void*)) {
    State& s = S();
    if (!cfg.doodads || !scene) return;
    int32_t made = 0;
    for (Doodad& d : s.doodads) {
        if (d.failed) continue;
        // Keep the stage clear: furniture whose scene position is within clearRadius of (clearX, clearY) and below clearZ is not created,
        // or hidden when it already exists (re-evaluated every frame, so moving the room in InnCast.cfg updates it live).
        if (cfg.clearRadius > 0.0f) {
            const C3Vector wp = d.pos * placement;
            const float dx = wp.x - cfg.clearX, dy = wp.y - cfg.clearY;
            const bool inside = dx * dx + dy * dy < cfg.clearRadius * cfg.clearRadius && wp.z < cfg.clearZ;
            if (inside) {
                if (d.model && !d.hidden) { d.model->SetGeometryVisible(0, 0xFFFF, 0); d.hidden = true; }
                continue;
            }
            if (d.hidden && d.model) { d.model->SetGeometryVisible(0, 0xFFFF, 1); d.hidden = false; }
        }
        if (!d.model) {
            if (made >= 8) break;     // spread creation over frames
            if (static_cast<int32_t>(&d - s.doodads.data()) >= cfg.maxDoodads) { d.failed = true; continue; }
            // The census (UPDATE 17) showed furniture created with the world's 0x20 flag never got drawn in this scene: such models only
            // join the scene's animate list through SetAnimating(1) (the glue's own camera model does the same, CGlueMgr.cpp), and nothing
            // called it. Default is now the plain flags 0 (like the character models) plus SetAnimating(1); WOW_INN_WMO_DOODAD_FLAGS=32 restores 0x20.
            d.model = scene->CreateModel(ToM2(d.name).c_str(), cfg.doodadFlags);
            made++;
            if (!d.model) { d.failed = true; continue; }
            d.model->m_fFlags |= 0x8000;
            d.model->m_lightingCallback = lightCallback;
            d.model->m_lightingArg = lightArg;
            d.model->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 1, 1);
        }
        // CM2Scene::Animate drains m_animateList every frame (it unlinks each model after SetupLighting), so a model must be re-linked
        // every frame to be animated and drawn. v14 called SetAnimating(1) once at creation, so each prop was animated for one frame only.
        d.model->SetAnimating(1);
        // Same per-frame flags CSimpleModel::OnLayerUpdate sets on its root model: 0x8 = put on the draw list, 0x10000 = emitters/particles.
        // CM2Scene::Animate clears both (~0x10008) when it pops the draw list, so they have to be set again every frame (v15 never set 0x8).
        d.model->m_fFlags |= 0x8 | 0x10000;
        C44Matrix m;
        m.Translate(d.pos);
        m.Rotate(d.rot);
        m.Scale(d.scale);
        m *= placement;
        d.model->m_worldTransform = m;
    }
}


// ---- v43: see-through camera line. When a character is zoomed on, anything between the camera and the character fades out. ----
// Furniture (doodads) fades with the model's own fade alpha (WOW_INN_FADE_MODE 1; 2 = hide instantly). Building geometry (walls, beams, built-in railings)
// has no per-object alpha, so the triangles on the line are left out of the draw (WOW_INN_FADE_WALLS=1).
struct FadeSettings {
    int32_t mode = 1;              // 0 off, 1 fade furniture, 2 hide furniture instantly
    int32_t walls = 1;             // 1 = also skip building triangles on the line
    float radius = 0.12f;          // half width of the line, scene units (a character is about 1 unit tall)
    float targetClear = 0.45f;     // the line stops this far before the character, so the seat / bar / table it uses stays
    float doodadMax = 1.0f;        // furniture bigger than this (sphere radius, scene units) never fades (rugs, big tables)
    float doodadShrink = 0.7f;     // furniture box radius is scaled by this (boxes are looser than the models)
    float time = 0.25f;            // seconds for a full fade
    float triMax = 0.6f;           // building triangles with a bounding radius up to this use the thick line, bigger ones (walls, floors) only count when the thin centre line pierces them
};

inline float SegPointDist2(const float* a, const float* b, const float* p) {
    const float ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
    const float ap[3] = { p[0] - a[0], p[1] - a[1], p[2] - a[2] };
    const float l2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
    float t = l2 > 1e-8f ? (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / l2 : 0.0f;
    t = std::min(1.0f, std::max(0.0f, t));
    const float dx = ap[0] - ab[0] * t, dy = ap[1] - ab[1] * t, dz = ap[2] - ab[2] * t;
    return dx * dx + dy * dy + dz * dz;
}

inline bool SegBox(const float* a, const float* b, const float* mn, const float* mx, float pad) {
    float t0 = 0.0f, t1 = 1.0f;
    for (int k = 0; k < 3; k++) {
        const float d = b[k] - a[k], lo = mn[k] - pad, hi = mx[k] + pad;
        if (std::fabs(d) < 1e-8f) { if (a[k] < lo || a[k] > hi) return false; }
        else {
            float u = (lo - a[k]) / d, v = (hi - a[k]) / d;
            if (u > v) std::swap(u, v);
            t0 = std::max(t0, u); t1 = std::min(t1, v);
            if (t0 > t1) return false;
        }
    }
    return true;
}

// Segment a->b against triangle p0 p1 p2 (Moller-Trumbore, both sides).
inline bool SegTri(const float* a, const float* b, const float* p0, const float* p1, const float* p2) {
    const float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
    const float e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
    const float e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
    const float h[3] = { d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0] };
    const float det = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
    if (std::fabs(det) < 1e-9f) return false;
    const float f = 1.0f / det;
    const float s[3] = { a[0] - p0[0], a[1] - p0[1], a[2] - p0[2] };
    const float u = f * (s[0] * h[0] + s[1] * h[1] + s[2] * h[2]);
    if (u < 0.0f || u > 1.0f) return false;
    const float q[3] = { s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0] };
    const float v = f * (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]);
    if (v < 0.0f || u + v > 1.0f) return false;
    const float t = f * (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]);
    return t >= 0.0f && t <= 1.0f;
}

// Bounding box of a batch and centre + radius of each of its triangles (WMO-local), built once on first use.
inline void BuildInfo(Group& g, size_t bi) {
    BatchInfo& in = g.info[bi];
    in.ready = true;
    const Batch& b = g.batches[bi];
    const uint32_t tris = b.count / 3;
    in.tri.assign(static_cast<size_t>(tris) * 4, 0.0f);
    bool first = true;
    for (uint32_t t = 0; t < tris; t++) {
        float* out = &in.tri[static_cast<size_t>(t) * 4];
        out[3] = -1.0f;
        const size_t at = static_cast<size_t>(b.start) + static_cast<size_t>(t) * 3;
        if (at + 2 >= g.idx.size()) continue;
        const uint32_t i0 = g.idx[at], i1 = g.idx[at + 1], i2 = g.idx[at + 2];
        if (i0 >= g.pos.size() || i1 >= g.pos.size() || i2 >= g.pos.size()) continue;
        const C3Vector* v[3] = { &g.pos[i0], &g.pos[i1], &g.pos[i2] };
        const float cx = (v[0]->x + v[1]->x + v[2]->x) / 3.0f, cy = (v[0]->y + v[1]->y + v[2]->y) / 3.0f, cz = (v[0]->z + v[1]->z + v[2]->z) / 3.0f;
        float r2 = 0.0f;
        for (int k = 0; k < 3; k++) {
            const float dx = v[k]->x - cx, dy = v[k]->y - cy, dz = v[k]->z - cz;
            r2 = std::max(r2, dx * dx + dy * dy + dz * dz);
            if (first) { in.mn[0] = in.mx[0] = v[k]->x; in.mn[1] = in.mx[1] = v[k]->y; in.mn[2] = in.mx[2] = v[k]->z; first = false; }
            else {
                in.mn[0] = std::min(in.mn[0], v[k]->x); in.mx[0] = std::max(in.mx[0], v[k]->x);
                in.mn[1] = std::min(in.mn[1], v[k]->y); in.mx[1] = std::max(in.mx[1], v[k]->y);
                in.mn[2] = std::min(in.mn[2], v[k]->z); in.mx[2] = std::max(in.mx[2], v[k]->z);
            }
        }
        out[0] = cx; out[1] = cy; out[2] = cz; out[3] = std::sqrt(r2);
    }
}

// Furniture on the line camera -> character fades out, everything else fades back in. Call every frame after PlaceDoodads.
inline void UpdateDoodadFade(const FadeSettings& f, const C3Vector& cam, float dt) {
    State& s = S();
    s.fadedDoodads = 0;
    float a[3] = { cam.x, cam.y, cam.z }, b[3] = { cam.x, cam.y, cam.z };
    bool active = f.mode > 0 && s.focusActive;
    if (active) {
        const float dx = s.focus.x - cam.x, dy = s.focus.y - cam.y, dz = s.focus.z - cam.z;
        const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float stop = len - f.targetClear;
        if (len < 0.001f || stop < 0.02f) active = false;
        else { b[0] = cam.x + dx / len * stop; b[1] = cam.y + dy / len * stop; b[2] = cam.z + dz / len * stop; }
    }
    const float step = f.time > 0.01f ? dt / f.time : 1.0f;
    for (Doodad& d : s.doodads) {
        if (!d.model) continue;
        bool blocking = false;
        if (active && !d.hidden && (d.model->m_fFlags & 1) && d.model->m_shared && d.model->m_shared->m_data) {
            const auto& ext = d.model->m_shared->m_data->m_bounds.m_extent;
            const C44Matrix& m = d.model->m_worldTransform;
            const float scale = std::sqrt(m.a0 * m.a0 + m.a1 * m.a1 + m.a2 * m.a2);
            const float hx = ext.t.x - ext.b.x, hy = ext.t.y - ext.b.y, hz = ext.t.z - ext.b.z;
            const float r = 0.5f * std::sqrt(hx * hx + hy * hy + hz * hz) * scale * f.doodadShrink;
            if (std::isfinite(r) && r > 0.0f && r <= f.doodadMax) {
                const C3Vector c = C3Vector((ext.b.x + ext.t.x) * 0.5f, (ext.b.y + ext.t.y) * 0.5f, (ext.b.z + ext.t.z) * 0.5f) * m;
                const float cc[3] = { c.x, c.y, c.z };
                const float tx = c.x - s.focus.x, ty = c.y - s.focus.y, tz = c.z - s.focus.z;
                const float nearTarget = r + 0.15f;
                if (tx * tx + ty * ty + tz * tz > nearTarget * nearTarget) {     // not the seat / prop the character is using
                    const float lim = r + f.radius;
                    blocking = SegPointDist2(a, b, cc) < lim * lim;
                }
            }
        }
        const float goal = blocking ? 0.0f : 1.0f;
        if (f.mode == 2) d.fade = goal;
        else if (d.fade < goal) d.fade = std::min(goal, d.fade + step);
        else if (d.fade > goal) d.fade = std::max(goal, d.fade - step);
        d.model->m_fadeAlpha = f.mode == 1 ? d.fade : 1.0f;
        const bool shouldHide = d.fade < 0.05f;
        if (shouldHide && !d.fadeHidden) { d.model->SetGeometryVisible(0, 0xFFFF, 0); d.fadeHidden = true; }
        else if (!shouldHide && d.fadeHidden) { d.model->SetGeometryVisible(0, 0xFFFF, 1); d.fadeHidden = false; }
        if (d.fade < 0.999f) s.fadedDoodads++;
    }
}

// The engine draws camera-relative: CM2Scene::Animate builds its view as Translate(-cameraPos) * (the Gx view matrix, which has no
// translation of its own), so a model matrix is a plain world matrix. Raw Gx draws must do the same, or the geometry stays glued to
// the camera while everything in the scene moves (what the first inn build did).
inline void Draw(const Settings& cfg, const C44Matrix& placement, const C3Vector& cameraPos, const FadeSettings& fade) {
    State& s = S();
    if (!s.ok) return;
    Relight(cfg);
    // v43: the camera -> character line in WMO-local space (building triangles on it are left out of the draw)
    bool cut = fade.walls != 0 && fade.mode > 0 && s.focusActive;
    float la[3] = { 0, 0, 0 }, lb[3] = { 0, 0, 0 }, lradius = 0.0f, ltriMax = 0.0f;
    if (cut) {
        const float dx = s.focus.x - cameraPos.x, dy = s.focus.y - cameraPos.y, dz = s.focus.z - cameraPos.z;
        const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float stop = len - fade.targetClear;
        const float scale = std::sqrt(placement.a0 * placement.a0 + placement.a1 * placement.a1 + placement.a2 * placement.a2);
        if (len < 0.001f || stop < 0.02f || scale < 1e-5f) cut = false;
        else {
            const C44Matrix inverse = placement.Inverse(placement.Determinant());
            const C3Vector wa = cameraPos, wb(cameraPos.x + dx / len * stop, cameraPos.y + dy / len * stop, cameraPos.z + dz / len * stop);
            const C3Vector ka = wa * inverse, kb = wb * inverse;
            la[0] = ka.x; la[1] = ka.y; la[2] = ka.z; lb[0] = kb.x; lb[1] = kb.y; lb[2] = kb.z;
            lradius = fade.radius / scale; ltriMax = fade.triMax / scale;
        }
    }
    uint32_t fadedTris = 0;
    static std::vector<uint8_t> hitMask;
    g_theGxDevicePtr->RsPush();
    C44Matrix cameraRelative = placement;
    cameraRelative.d0 -= cameraPos.x;
    cameraRelative.d1 -= cameraPos.y;
    cameraRelative.d2 -= cameraPos.z;
    GxXformPush(GxXform_World, cameraRelative);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_VertexShader, static_cast<void*>(nullptr));
    GxRsSet(GxRs_PixelShader, static_cast<void*>(nullptr));
    GxRsSet(GxRs_DepthTest, 1);
    GxRsSet(GxRs_DepthWrite, 1);
    uint32_t drawn = 0, skipped = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (Group& g : s.groups) {
            GxPrimVertexPtr(static_cast<uint32_t>(g.pos.size()), g.pos.data(), 12, nullptr, 0, g.lit.data(), 4, g.uv.data(), 8, nullptr, 0);
            GxPrimIndexPtr(static_cast<uint32_t>(g.idx.size()), g.idx.data());
            for (const Batch& b : g.batches) {
                if (b.material >= s.materials.size()) continue;
                const Material& m = s.materials[b.material];
                if ((m.blend == 0 || m.blend == 1) != (pass == 0)) continue;
                CGxTex* tex = m.texture ? TextureGetGxTex(m.texture, 0, nullptr) : nullptr;
                if (!tex) { skipped++; continue; }
                GxRsSet(GxRs_Culling, static_cast<int32_t>((m.flags & 0x4) == 0));
                GxRsSet(GxRs_BlendingMode, static_cast<int32_t>(m.blend));
                GxRsSet(GxRs_AlphaRef, static_cast<int32_t>(CGxDevice::s_alphaRef[m.blend]));
                GxRsSet(GxRs_DepthWrite, static_cast<int32_t>(m.blend < 2));
                GxTexSetWrap(tex, static_cast<EGxTexWrapMode>((m.flags & 0x40) == 0), static_cast<EGxTexWrapMode>((m.flags & 0x80) == 0));
                g_theGxDevicePtr->RsSet(GxRs_Texture0, tex);
                auto emit = [&](uint32_t start, uint32_t count) {
                    CGxBatch draw;
                    draw.m_primType = GxPrim_Triangles;
                    draw.m_start = start;
                    draw.m_count = count;
                    draw.m_minIndex = b.vmin;
                    draw.m_maxIndex = b.vmax;
                    g_theGxDevicePtr->Draw(&draw, 1);
                };
                bool anyHit = false;
                const uint32_t tris = b.count / 3;
                if (cut) {
                    if (g.info.size() != g.batches.size()) g.info.resize(g.batches.size());
                    const size_t bi = static_cast<size_t>(&b - g.batches.data());
                    if (!g.info[bi].ready) BuildInfo(g, bi);
                    const BatchInfo& in = g.info[bi];
                    if (in.tri.size() >= static_cast<size_t>(tris) * 4 && SegBox(la, lb, in.mn, in.mx, lradius)) {
                        hitMask.assign(tris, 0);
                        for (uint32_t t = 0; t < tris; t++) {
                            const float* tc = &in.tri[static_cast<size_t>(t) * 4];
                            const float r = tc[3];
                            if (r < 0.0f) continue;
                            bool hit = false;
                            if (r <= ltriMax) { const float lim = lradius + r; hit = SegPointDist2(la, lb, tc) < lim * lim; }
                            else {
                                const size_t at = static_cast<size_t>(b.start) + static_cast<size_t>(t) * 3;
                                const C3Vector& p0 = g.pos[g.idx[at]]; const C3Vector& p1 = g.pos[g.idx[at + 1]]; const C3Vector& p2 = g.pos[g.idx[at + 2]];
                                const float q0[3] = { p0.x, p0.y, p0.z }, q1[3] = { p1.x, p1.y, p1.z }, q2[3] = { p2.x, p2.y, p2.z };
                                hit = SegTri(la, lb, q0, q1, q2);
                            }
                            if (hit) { hitMask[t] = 1; anyHit = true; fadedTris++; }
                        }
                    }
                }
                if (!anyHit) emit(b.start, b.count);
                else {
                    // draw the runs of triangles that are not on the line
                    uint32_t t = 0;
                    while (t < tris) {
                        while (t < tris && hitMask[t]) t++;
                        const uint32_t from = t;
                        while (t < tris && !hitMask[t]) t++;
                        if (t > from) emit(b.start + from * 3, (t - from) * 3);
                    }
                }
                drawn++;
            }
        }
    }
    GxXformPop(GxXform_World);
    g_theGxDevicePtr->RsPop();
    s.drawnBatches = drawn;
    s.skippedBatches = skipped;
    s.fadedTris = fadedTris;
}

// Forget everything without touching the scene models (the select scene frees them with itself).
inline void Forget() {
    State& s = S();
    for (Material& m : s.materials) if (m.texture) HandleClose(reinterpret_cast<HOBJECT>(m.texture));
    s = State();
}

}
