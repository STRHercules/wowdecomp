// Not in the original binary. See SurfaceCover.h. Table rules follow seyris-living-azeroth's
// features/GroundMaterialTable.cpp and features/SurfaceCoverTable.cpp (GPL-3.0): a terrain layer's material is the most
// specific selector row (the chunk's area chain, its map, everywhere; within a place its texture, then its ground
// effect, then its TerrainType, then any), a material's cover each field from the first row along its Parent chain that
// sets it.
#include "WoW/Source/WorldClient/SurfaceCover.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/IConsole.h"
#include "Engine/Source/Gx/CGxBuf.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/ShaderEffect/ShaderEffect.h"
#include "Engine/Source/ShaderEffect/ShadowCache.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/GroundEffectTextureRec.h"
#include "WoW/Source/Object/ObjectClient/Object_C.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/UIUtil/Camera.h"
#include "WoW/Source/Ui/WorldFrame.h"
#include "WoW/Source/Ui/OptionCVars.h"
#include "WoW/Source/Ui/Visuals.h"
#include "WoW/Source/WorldClient/CMapBaseObjLink.h"
#include "WoW/Source/WorldClient/CustomTable.h"
#include "WoW/Source/WorldClient/HD/HdSurfaceCover.h"
#include "WoW/Source/WorldClient/LivingWind.h"
#include "WoW/Source/WorldClient/Map.h"
#include "WoW/Source/WorldClient/MapArea.h"
#include "WoW/Source/WorldClient/MapChunk.h"
#include "WoW/Source/WorldClient/MapRenderChunk.h"
#include "WoW/Source/WorldClient/Types.h"
#include "WoW/Source/WorldClient/World.h"

namespace {

enum Scope : uint32_t { kGlobal = 0, kMap = 1, kArea = 2 };
constexpr int32_t kSnowTerrainType = 3;  // TerrainType.dbc: Snow

// ---- tables -------------------------------------------------------------------------------------------------------

struct Selector {
    std::string texture;   // normalized; empty = any
    uint32_t effect = 0;   // 0 = any
    int32_t terrain = -1;  // -1 = any
    uint32_t material = 0; // 0 = no material here
};

// SurfaceCover.cdbc fields that inherit along the Parent chain (a negative value takes the parent's). TintStrength
// carries TintColor, Depth carries Flags.
enum Field { kDepth, kMaxSlope, kSlopeFade, kDrift, kBreakup, kRim, kRelax, kTint, kZOffset, kMoistureMin, kFieldCount };
constexpr uint32_t kColumn[kFieldCount] = { 3, 4, 5, 6, 7, 8, 9, 11, 15, 20 };

struct Row {
    float value[kFieldCount];
    uint32_t tintColor = 0;
    uint32_t flags = 0;
    uint32_t coverMaterial = 0;
    int32_t texture = -1;  // -1 = from the parent's row, 0 = none, 1.. = s_texturePaths index + 1
};

struct Cover {
    float depth = 0.0f;
    float maxSlope = 45.0f;
    float slopeFade = 15.0f;
    float drift = 0.35f;
    float breakup = 0.5f;
    float rim = 0.3f;            // trench rim height, share of the depth
    float relax = 30.0f;         // seconds a trench takes to fill back in
    float stiffness = 0.0f;      // the cover material's: 0 feet press it to the ground .. 1 rigid
    float tintStrength = 0.0f;
    float zOffset = 0.0f;
    float moistureMin = 2.0f;
    uint32_t tintColor = 0;
    uint32_t flags = 0;
    uint32_t coverMaterial = 0;
    int32_t texture = 0;
    bool snow = false;
};

std::unordered_map<uint32_t, uint32_t> s_parent;  // GroundMaterial.cdbc: ID -> Parent
std::unordered_map<uint32_t, float> s_stiffness;  // GroundMaterial.cdbc: ID -> Stiffness (negative = the parent's)
std::map<std::pair<uint32_t, uint32_t>, std::vector<Selector>> s_selectors;
std::unordered_map<uint32_t, Row> s_rows;          // SurfaceCover.cdbc by MaterialID
std::vector<std::string> s_texturePaths;
std::unordered_map<uint32_t, Cover> s_materialCache;
std::unordered_map<std::string, uint32_t> s_selectCache;
bool s_loaded = false;
char s_tableStatus[400] = "";

std::string Normalize(const char* path) {
    std::string s = path ? path : "";
    for (char& c : s) {
        if (c == '/') c = '\\';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

bool IsSet(int32_t f, float v) {
    if (f == kZOffset) return v != -1.0f && v > -1000.0f;  // signed: -1000 and below (and exactly -1) inherit
    return v >= 0.0f;
}

void Load() {
    s_loaded = true;
    char materials[160], selectors[160], covers[160];
    CustomTable::Table table;
    // GroundMaterial: ID, Name, Parent, ...
    if (CustomTable::LoadTable("DBFilesClient\\GroundMaterial.cdbc", 3, table, materials, sizeof materials)) {
        for (uint32_t r = 0; r < table.Rows(); ++r) {
            const uint32_t parent = table.U(r, 2);
            s_parent[table.U(r, 0)] = parent == 0xFFFFFFFFu ? 0 : parent;
            if (table.fields > 4) s_stiffness[table.U(r, 0)] = table.F(r, 4);
        }
    }
    // GroundMaterialSelector: ID, ScopeType, ScopeID, TexturePath, GroundEffectID, TerrainType, LiquidType, MaterialID
    if (CustomTable::LoadTable("DBFilesClient\\GroundMaterialSelector.cdbc", 8, table, selectors, sizeof selectors)) {
        for (uint32_t r = 0; r < table.Rows(); ++r) {
            if (table.U(r, 6)) continue;  // liquid rows: not terrain (later phases)
            const uint32_t scope = table.U(r, 1);
            Selector s;
            s.texture = Normalize(table.S(r, 3));
            s.effect = table.U(r, 4);
            s.terrain = static_cast<int32_t>(table.U(r, 5));
            s.material = table.U(r, 7);
            s_selectors[{ scope, scope == kGlobal ? 0 : table.U(r, 2) }].push_back(s);
        }
    }
    // SurfaceCover: ID, MaterialID, CoverMaterial, Depth, MaxSlope, SlopeFade, DriftNoise, EdgeBreakup, Rim,
    // RelaxSeconds, TintColor, TintStrength, CoverTexture, Opacity, Flatten, ZOffset, Wetness, Flags, MeltKeptShare,
    // MeltGoneTemperature, MoistureMin (the last three appended: older files still load)
    if (CustomTable::LoadTable("DBFilesClient\\SurfaceCover.cdbc", 18, table, covers, sizeof covers)) {
        for (uint32_t r = 0; r < table.Rows(); ++r) {
            Row row;
            for (int32_t f = 0; f < kFieldCount; ++f) {
                row.value[f] = kColumn[f] < table.fields ? table.F(r, kColumn[f]) : (f == kZOffset ? -1000.0f : -1.0f);
            }
            row.tintColor = table.U(r, 10);
            row.flags = table.U(r, 17);
            row.coverMaterial = table.U(r, 2) == 0xFFFFFFFFu ? 0 : table.U(r, 2);
            // CoverTexture: empty = from the parent material, "-" = none, else a path (one ID per distinct path)
            const char* texture = table.S(r, 12);
            if (!std::strcmp(texture, "-")) {
                row.texture = 0;
            } else if (texture[0]) {
                const std::string key = Normalize(texture);
                auto it = std::find(s_texturePaths.begin(), s_texturePaths.end(), key);
                if (it == s_texturePaths.end()) it = s_texturePaths.insert(s_texturePaths.end(), key);
                row.texture = static_cast<int32_t>(it - s_texturePaths.begin()) + 1;
            }
            s_rows[table.U(r, 1)] = row;
        }
    }
    std::snprintf(s_tableStatus, sizeof s_tableStatus, "GroundMaterial %s, GroundMaterialSelector %s, SurfaceCover %s",
                  materials, selectors, covers);
    char line[460];
    std::snprintf(line, sizeof line, "Surface cover: %s", s_tableStatus);
    ConsoleWrite(line, DEFAULT_COLOR);
}

// The material of a terrain layer: the first matching selector row, places most specific first. A row with MaterialID
// 0 still ends the search ("no material here").
uint32_t Select(uint32_t area, int32_t map, const std::string& texture, uint32_t effect, int32_t terrain) {
    char key[64];
    std::snprintf(key, sizeof key, "%u|%d|%u|%d|", area, map, effect, terrain);
    const std::string cacheKey = key + texture;
    auto cached = s_selectCache.find(cacheKey);
    if (cached != s_selectCache.end()) return cached->second;

    std::vector<std::pair<uint32_t, uint32_t>> places;
    uint32_t chain[8];
    const int n = LivingWind::AreaChain(area, chain, 8);
    for (int i = 0; i < n; ++i) places.push_back({ kArea, chain[i] });
    if (map >= 0) places.push_back({ kMap, static_cast<uint32_t>(map) });
    places.push_back({ kGlobal, 0 });

    uint32_t material = 0;
    bool found = false;
    for (const auto& place : places) {
        auto rows = s_selectors.find(place);
        if (rows == s_selectors.end()) continue;
        for (int level = 0; level < 4 && !found; ++level) {
            for (const Selector& s : rows->second) {
                bool match = false;
                switch (level) {
                    case 0: match = !s.texture.empty() && s.texture == texture; break;
                    case 1: match = s.texture.empty() && s.effect && effect && s.effect == effect; break;
                    case 2: match = s.texture.empty() && !s.effect && s.terrain >= 0 && s.terrain == terrain; break;
                    default: match = s.texture.empty() && !s.effect && s.terrain < 0; break;
                }
                if (match) {
                    material = s.material;
                    found = true;
                    break;
                }
            }
        }
        if (found) break;
    }
    if (s_selectCache.size() > 20000) s_selectCache.clear();
    s_selectCache.emplace(cacheKey, material);
    return material;
}

template <typename Fn>
void ForChain(uint32_t material, Fn fn) {
    for (int step = 0; material && step < 16; ++step) {
        if (!fn(material)) return;
        auto it = s_parent.find(material);
        material = it == s_parent.end() || it->second == material ? 0 : it->second;
    }
}

Cover ForMaterial(uint32_t material) {
    auto cached = s_materialCache.find(material);
    if (cached != s_materialCache.end()) return cached->second;
    float value[kFieldCount] = {};
    bool set[kFieldCount] = {};
    uint32_t tint = 0, flags = 0, coverMaterial = 0;
    int32_t texture = -1;
    ForChain(material, [&](uint32_t id) {
        auto it = s_rows.find(id);
        if (it == s_rows.end()) return true;
        const Row& r = it->second;
        if (texture < 0 && r.texture >= 0) texture = r.texture;
        if (!coverMaterial && r.coverMaterial) coverMaterial = r.coverMaterial;
        for (int32_t f = 0; f < kFieldCount; ++f) {
            if (set[f] || !IsSet(f, r.value[f])) continue;
            set[f] = true;
            value[f] = r.value[f];
            if (f == kTint) tint = r.tintColor;
            if (f == kDepth) flags = r.flags;
        }
        return true;
    });
    Cover c;
    if (set[kDepth]) { c.depth = value[kDepth]; c.flags = flags; }
    if (set[kMaxSlope]) c.maxSlope = value[kMaxSlope];
    if (set[kSlopeFade]) c.slopeFade = value[kSlopeFade];
    if (set[kDrift]) c.drift = value[kDrift];
    if (set[kBreakup]) c.breakup = value[kBreakup];
    if (set[kRim]) c.rim = value[kRim];
    if (set[kRelax]) c.relax = value[kRelax];
    if (set[kTint]) { c.tintStrength = std::min(value[kTint], 1.0f); c.tintColor = tint; }
    if (set[kZOffset]) c.zOffset = value[kZOffset];
    if (set[kMoistureMin]) c.moistureMin = value[kMoistureMin];
    c.texture = texture > 0 ? texture : 0;
    c.coverMaterial = coverMaterial ? coverMaterial : material;
    ForChain(c.coverMaterial, [&](uint32_t id) {
        auto it = s_stiffness.find(id);
        if (it == s_stiffness.end() || it->second < 0.0f) return true;
        c.stiffness = std::min(it->second, 1.0f);
        return false;
    });
    s_materialCache.emplace(material, c);
    return c;
}

// Snow: the cover's material is the snow material (what the selectors give TerrainType 3, here or everywhere) or a
// child of it.
bool IsSnow(uint32_t coverMaterial, uint32_t area, int32_t map) {
    const uint32_t global = Select(0, -1, std::string(), 0, kSnowTerrainType);
    const uint32_t local = Select(area, map, std::string(), 0, kSnowTerrainType);
    if (!global && !local) return false;
    bool snow = false;
    ForChain(coverMaterial, [&](uint32_t id) {
        if (id == global || id == local) {
            snow = true;
            return false;
        }
        return true;
    });
    return snow;
}

// ---- options ------------------------------------------------------------------------------------------------------

CVar* s_enabled = nullptr;
CVar* s_depth = nullptr;
CVar* s_others = nullptr;
CVar* s_brightness = nullptr;
CVar* s_breakup = nullptr;
CVar* s_farLift = nullptr;
CVar* s_info = nullptr;
uint32_t s_generation = 1;  // bumped when what the covers are made from changes: every chunk is made again

bool Rebuild(CVar*, const char*, const char*, void*) {
    ++s_generation;
    return true;
}

CVar* s_near = nullptr;
CVar* s_trenches = nullptr;
CVar* s_trenchScale = nullptr;

float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
int32_t ClampI(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

float Hash(int32_t x, int32_t y) {
    uint32_t h = static_cast<uint32_t>(x) * 0x8DA6B343u ^ static_cast<uint32_t>(y) * 0xD8163841u;
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    h ^= h >> 15;
    return (h & 0xFFFF) / 65535.0f;
}

// Smooth value noise, -1..1, about `scale` features per yard.
float Noise(float x, float y, float scale) {
    x *= scale;
    y *= scale;
    const float fx = std::floor(x), fy = std::floor(y);
    const int32_t ix = static_cast<int32_t>(fx), iy = static_cast<int32_t>(fy);
    float tx = x - fx, ty = y - fy;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    const float a = Hash(ix, iy) + (Hash(ix + 1, iy) - Hash(ix, iy)) * tx;
    const float b = Hash(ix, iy + 1) + (Hash(ix + 1, iy + 1) - Hash(ix, iy + 1)) * tx;
    return (a + (b - a) * ty) * 2.0f - 1.0f;
}

// Drifts: the depth's slow variation (the far mesh and the near patch use the same).
float Drift(float x, float y) { return Noise(x, y, 0.15f) * 0.7f + Noise(x, y, 0.45f) * 0.3f; }

// Each layer's share (0..1) at alpha texture coordinate (u, v) of the chunk's blend texture as CreateShaderTexture
// left it in CMapRenderChunk::s_defaultTex: channels r, g, b = layers 1, 2, 3 (4444 texels, or 8888 with big alpha
// maps), the terrain shader's blend rule for layer 0.
void LayerWeights(int32_t width, int32_t height, bool big, float u, float v, float* w) {
    const float x = Clamp(u * width - 0.5f, 0.0f, width - 1.0f);
    const float y = Clamp(v * height - 0.5f, 0.0f, height - 1.0f);
    const int32_t x0 = static_cast<int32_t>(x), y0 = static_cast<int32_t>(y);
    const int32_t x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
    const float fx = x - x0, fy = y - y0;
    float a[3] = {};
    const int32_t xs[2] = { x0, x1 }, ys[2] = { y0, y1 };
    for (int32_t j = 0; j < 2; j++) {
        for (int32_t i = 0; i < 2; i++) {
            const float k = (i ? fx : 1.0f - fx) * (j ? fy : 1.0f - fy);
            const int32_t index = ys[j] * width + xs[i];
            if (big) {
                const CImVector c = reinterpret_cast<const CImVector*>(CMapRenderChunk::s_defaultTex)[index];
                a[0] += k * c.r / 255.0f;
                a[1] += k * c.g / 255.0f;
                a[2] += k * c.b / 255.0f;
            } else {
                const uint16_t t = reinterpret_cast<const uint16_t*>(CMapRenderChunk::s_defaultTex)[index];
                a[0] += k * ((t >> 8) & 15) / 15.0f;
                a[1] += k * ((t >> 4) & 15) / 15.0f;
                a[2] += k * (t & 15) / 15.0f;
            }
        }
    }
    if (big) {
        w[0] = 1.0f - std::min(a[0] + a[1] + a[2], 1.0f);
        w[1] = a[0];
        w[2] = a[1];
        w[3] = a[2];
    } else {
        w[3] = a[2];
        w[2] = a[1] * (1.0f - a[2]);
        w[1] = a[0] * (1.0f - a[1]) * (1.0f - a[2]);
        w[0] = (1.0f - a[0]) * (1.0f - a[1]) * (1.0f - a[2]);
    }
}

std::unordered_map<std::string, CTexture*> s_textures;  // cover textures, kept for the session
CTexture* s_white = nullptr;

CTexture* CoverTexture(int32_t id) {
    if (id <= 0 || id > static_cast<int32_t>(s_texturePaths.size())) return nullptr;
    const std::string& path = s_texturePaths[id - 1];
    auto it = s_textures.find(path);
    if (it != s_textures.end()) return it->second;
    CTexture* texture = CMap::LoadTexture(path.c_str());
    s_textures.emplace(path, texture);
    return texture;
}

CGxTex* White() {
    if (!s_white) {
        CImVector white;
        white.r = white.g = white.b = white.a = 0xFF;
        s_white = TextureCreateSolid(white);
    }
    return TextureGetGxTex(s_white, 1, nullptr);
}

// ---- a map chunk as the near patch reads it ----------------------------------------------------------------------

constexpr uint32_t kChunkVerts = 145;
constexpr float kChunkSize = 33.333332f;
constexpr int32_t kNodes = 65;  // cover nodes per side: the alpha maps' resolution
constexpr float kFarDepth = 0.05f;  // yd: the most the far mesh is lifted by its cover (see Build)

// Its terrain and the far mesh's lift at its 145 vertices, and the cover on a grid of nodes.
struct ChunkRec {
    float terrain[kChunkVerts] = {};
    float lift[kChunkVerts] = {};
    bool any = false;
    std::vector<uint8_t> share, slope, depth;  // per node: share and slope factor (0..255), depth in cm (slope applied)
    float drift = 0.0f, rim = 0.0f, relax = 30.0f, stiffness = 0.0f, breakup = 0.0f;
    float tint[3] = { 1.0f, 1.0f, 1.0f };
    CTexture* texture = nullptr;
    bool snow = false;
};
std::unordered_map<CMapChunk*, ChunkRec> s_recs;
uint32_t s_recVersion = 0;  // bumped whenever a record is made: the near patch looks again where it had none

float ChunkTop(const CMapChunk* c) { return 17066.666f - c->m_cOffset.y * kChunkSize; }   // world x of vertex row 0
float ChunkLeft(const CMapChunk* c) { return 17066.666f - c->m_cOffset.x * kChunkSize; }  // world y of vertex column 0

float Tri(float px, float py, float ax, float ay, float va, float bx, float by, float vb, float cx, float cy, float vc) {
    const float det = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
    const float wa = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / det;
    const float wb = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / det;
    return wa * va + wb * vb + (1.0f - wa - wb) * vc;
}

// A value given at the chunk's 145 vertices, linear over its triangles (four per cell around the centre vertex), as
// the terrain mesh draws it.
float OverTriangles(const CMapChunk* chunk, const float* v, float x, float y) {
    const float step = kChunkSize / 8.0f;
    const float fr = (ChunkTop(chunk) - x) / step, fc = (ChunkLeft(chunk) - y) / step;
    const int32_t r = static_cast<int32_t>(Clamp(std::floor(fr), 0.0f, 7.0f));
    const int32_t c = static_cast<int32_t>(Clamp(std::floor(fc), 0.0f, 7.0f));
    const float dr = Clamp(fr - r, 0.0f, 1.0f), dc = Clamp(fc - c, 0.0f, 1.0f);
    const float a = v[r * 17 + c], b = v[r * 17 + c + 1], d = v[(r + 1) * 17 + c], e = v[(r + 1) * 17 + c + 1];
    const float m = v[r * 17 + 9 + c];
    if (dr <= dc && dr <= 1.0f - dc) return Tri(dr, dc, 0, 0, a, 0, 1, b, 0.5f, 0.5f, m);
    if (dr >= dc && dr >= 1.0f - dc) return Tri(dr, dc, 1, 0, d, 1, 1, e, 0.5f, 0.5f, m);
    if (dc < dr && dc < 1.0f - dr) return Tri(dr, dc, 0, 0, a, 1, 0, d, 0.5f, 0.5f, m);
    return Tri(dr, dc, 0, 1, b, 1, 1, e, 0.5f, 0.5f, m);
}

// ---- far: one mesh per covered render chunk ----------------------------------------------------------------------

constexpr int32_t kSlotsPerPool = 256;  // 8 KB each
constexpr int32_t kMaxSlots = 4096;

struct Slot {
    CGxBuf* buf = nullptr;
    CMapRenderChunk* owner = nullptr;
    ULONGLONG lastUsed = 0;
};
std::vector<Slot> s_slots;

struct Entry {
    CMapChunk* chunks[2] = {};
    SMChunk* header = nullptr;
    uint32_t generation = 0;
    bool cover = false;
    bool snow = false;
    int32_t slot = -1;
    float layerCover[4] = {};
    float breakup = 0.0f;
    CTexture* texture = nullptr;
};
std::unordered_map<CMapRenderChunk*, Entry> s_entries;
uint32_t s_draws = 0;     // cover draws since the status line was last shown
uint32_t s_starved = 0;   // since then, chunks that wanted a slot and found none

void FreeSlot(Entry& e) {
    if (e.slot >= 0 && e.slot < static_cast<int32_t>(s_slots.size())) s_slots[e.slot].owner = nullptr;
    e.slot = -1;
}

// A slot for this render chunk: a free one, else a new pool of them (up to kMaxSlots), else the one unused the longest
// (not drawn this second).
int32_t TakeSlot(CMapRenderChunk* owner, ULONGLONG now) {
    int32_t best = -1;
    for (int32_t i = 0; i < static_cast<int32_t>(s_slots.size()); i++) {
        if (!s_slots[i].owner) {
            best = i;
            break;
        }
        if (now - s_slots[i].lastUsed > 1000 && (best < 0 || s_slots[i].lastUsed < s_slots[best].lastUsed)) best = i;
    }
    if ((best < 0 || s_slots[best].owner) && static_cast<int32_t>(s_slots.size()) < kMaxSlots) {
        const uint32_t bytes = 2 * kChunkVerts * sizeof(CGxVertexPNC);
        CGxPool* pool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Vertex, GxPoolUsage_Dynamic, kSlotsPerPool * bytes, GxPoolHintBit_Unk3, "SurfaceCover_vtx");
        best = static_cast<int32_t>(s_slots.size());
        s_slots.resize(s_slots.size() + kSlotsPerPool);
        for (int32_t i = 0; i < kSlotsPerPool; i++) {
            s_slots[best + i].buf = g_theGxDevicePtr->BufCreate(pool, sizeof(CGxVertexPNC), 2 * kChunkVerts, i * bytes);
        }
    }
    if (best < 0) return -1;
    if (CMapRenderChunk* old = s_slots[best].owner) {
        auto it = s_entries.find(old);
        if (it != s_entries.end()) it->second.slot = -1;
    }
    s_slots[best].owner = owner;
    s_slots[best].lastUsed = now;
    return best;
}

float SlopeFactor(const Cover& c, const C3Vector& n) {
    const float nz = n.z / std::max(1e-4f, std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z));
    const float deg = std::acos(Clamp(nz, -1.0f, 1.0f)) * 57.29578f;
    return Clamp((c.maxSlope - deg) / std::max(c.slopeFade, 0.1f), 0.0f, 1.0f);
}

void Normalize(C3Vector& n) {
    const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    n = C3Vector(n.x / len, n.y / len, n.z / len);
}

// What covers this render chunk's layers, its map chunks' records (ChunkRec), and its lifted vertices into a slot.
// False: no cover here.
bool Build(CMapRenderChunk* rc, Entry& e, ULONGLONG now) {
    CMapChunk* chunk = rc->m_mapChunks[0];
    if (!chunk || !chunk->m_header || !chunk->m_layers || !chunk->m_parentLinkList.Head()) return false;
    CMapArea* area = static_cast<CMapArea*>(chunk->m_parentLinkList.Head()->m_ref);
    if (!area || !area->m_header) return false;
    if (!s_loaded) Load();

    const uint32_t areaId = static_cast<uint32_t>(chunk->m_areaId);
    const int32_t mapId = s_mapId;
    const bool others = OptionCVars::Bool(s_others, true);
    Cover covers[4];
    bool any = false;
    const uint32_t layerCount = std::min<uint32_t>(rc->m_layersCount, 4);
    for (uint32_t i = 0; i < layerCount; i++) {
        const uint32_t textureId = rc->m_layers[i].m_textureId;
        if (textureId >= area->m_textures.Count()) continue;
        uint32_t effect = 0;
        for (uint32_t k = 0; k < chunk->m_header->m_nLayers && k < 4; k++) {
            if (chunk->m_layers[k].m_textureId == textureId) effect = chunk->m_layers[k].m_offectId;
        }
        const GroundEffectTextureRec* record = effect ? g_groundEffectTextureDB.GetRecord(effect) : nullptr;
        const int32_t terrain = record ? record->m_sound : -1;
        const uint32_t material = Select(areaId, mapId, Normalize(area->m_textures.m_data[textureId].m_textureName), effect, terrain);
        if (!material) continue;
        Cover c = ForMaterial(material);
        c.snow = IsSnow(c.coverMaterial, areaId, mapId);
        // shortcut: covers that need ground moisture (mud) wait for a moisture model; nothing else is gated yet
        if (c.moistureMin <= 1.0f || (!c.snow && !others)) c.depth = 0.0f;
        covers[i] = c;
        any = any || c.depth > 0.0f;
    }

    CGxVertexPNC vertices[2 * kChunkVerts];
    const uint32_t chunks = rc->m_mapChunks[1] ? 2 : 1;
    const uint32_t count = chunks * kChunkVerts;
    ChunkRec* recs[2] = {};
    for (uint32_t k = 0; k < chunks; k++) {
        rc->m_mapChunks[k]->CreateVerticesWorld(vertices + k * kChunkVerts, C3Vector());
        ChunkRec& rec = s_recs[rc->m_mapChunks[k]];
        rec = ChunkRec();
        for (uint32_t i = 0; i < kChunkVerts; i++) rec.terrain[i] = vertices[k * kChunkVerts + i].m_position.z;
        recs[k] = &rec;
    }
    s_recVersion++;
    if (!any) return false;

    // Each layer's share, read back from the chunk's blend texture as the client builds it.
    rc->CreateShaderTexture();
    const bool big = (CMap::header.m_flags & 0x4) != 0;
    int32_t size = 64 >> area->m_header->m_mampValue;
    if (rc->m_renderFlags & 0x8) size >>= 1;
    const int32_t width = size * (rc->m_unkFlags & 0x2 ? 2 : 1);
    const int32_t height = size * (rc->m_unkFlags & 0x1 ? 2 : 1);
    const float alphaX = -CMapChunk::s_geoToTex * 0.125f * (rc->m_unkFlags & 0x1 ? 0.5f : 1.0f);
    const float alphaY = -CMapChunk::s_geoToTex * 0.125f * (rc->m_unkFlags & 0x2 ? 0.5f : 1.0f);
    auto weights = [&](float x, float y, float* w) {
        LayerWeights(width, height, big, (y - rc->m_vec1.y) * alphaY, (x - rc->m_vec1.x) * alphaX, w);
    };

    // The cover's nodes, for the near patch.
    const float node = kChunkSize / (kNodes - 1);
    for (uint32_t k = 0; k < chunks; k++) {
        ChunkRec& rec = *recs[k];
        const CMapChunk* mc = rc->m_mapChunks[k];
        rec.share.assign(kNodes * kNodes, 0);
        rec.slope.assign(kNodes * kNodes, 0);
        rec.depth.assign(kNodes * kNodes, 0);
        for (int32_t a = 0; a < kNodes; a++) {
            for (int32_t b = 0; b < kNodes; b++) {
                float w[4];
                weights(ChunkTop(mc) - a * node, ChunkLeft(mc) - b * node, w);
                const int32_t r = (a + 4) / 8, c = (b + 4) / 8;
                const C3Vector& n = vertices[k * kChunkVerts + r * 17 + c].m_normal;
                float share = 0.0f, slope = 0.0f, depth = 0.0f;
                for (uint32_t l = 0; l < layerCount; l++) {
                    if (covers[l].depth <= 0.0f || w[l] <= 0.0f) continue;
                    const float s = SlopeFactor(covers[l], n);
                    share += w[l];
                    slope += w[l] * s;
                    depth += w[l] * s * covers[l].depth;
                }
                const int32_t index = a * kNodes + b;
                rec.share[index] = static_cast<uint8_t>(Clamp(share, 0.0f, 1.0f) * 255.0f + 0.5f);
                rec.slope[index] = static_cast<uint8_t>(Clamp(share > 0.0f ? slope / share : 0.0f, 0.0f, 1.0f) * 255.0f + 0.5f);
                rec.depth[index] = static_cast<uint8_t>(Clamp(depth * 100.0f, 0.0f, 255.0f) + 0.5f);
            }
        }
    }

    // The far mesh: each vertex lifted by its cover.
    float fullLift[2 * kChunkVerts] = {};
    const float depthScale = OptionCVars::Float(s_depth, 0.0f, 4.0f, 1.0f);
    const bool rgba = g_theGxDevicePtr->Caps().m_colorFormat == GxCF_rgba;
    for (uint32_t i = 0; i < count; i++) {
        CGxVertexPNC& v = vertices[i];
        float w[4];
        weights(v.m_position.x, v.m_position.y, w);
        const float drift = Drift(v.m_position.x, v.m_position.y);
        float share = 0.0f, slopeShare = 0.0f, depth = 0.0f, zOffset = 0.0f;
        float tint[3] = { 0.0f, 0.0f, 0.0f };
        for (uint32_t l = 0; l < layerCount; l++) {
            const Cover& c = covers[l];
            if (c.depth <= 0.0f || w[l] <= 0.0f) continue;
            const float slope = SlopeFactor(c, v.m_normal);
            share += w[l];
            slopeShare += w[l] * slope;
            depth += w[l] * slope * c.depth * std::max(0.0f, 1.0f + c.drift * drift);
            zOffset += w[l] * slope * c.zOffset;
            const float t = c.tintStrength;
            tint[0] += w[l] * (1.0f + (((c.tintColor >> 16) & 255) / 255.0f - 1.0f) * t);
            tint[1] += w[l] * (1.0f + (((c.tintColor >> 8) & 255) / 255.0f - 1.0f) * t);
            tint[2] += w[l] * (1.0f + ((c.tintColor & 255) / 255.0f - 1.0f) * t);
        }
        const float slope = share > 0.0f ? slopeShare / share : 0.0f;
        // Far away the cover is a skin: its edges follow the alpha maps per pixel, much finer than these vertices 4 yd
        // apart, so a cover of its full depth would end in a lip hanging over the ground. The full depth only shades it
        // (normals below); the near patch, whose edges slope down to the ground, has the volume. A floor of 2 cm under
        // any cover, so it never fights the terrain for depth.
        const float full = share > 0.0f ? std::max(0.0f, depth * depthScale + zOffset) : 0.0f;
        const float lift = share > 0.0f ? std::min(full, kFarDepth) + 0.02f * std::min(1.0f, share * 4.0f) : 0.0f;
        v.m_position.z += lift;
        recs[i / kChunkVerts]->lift[i % kChunkVerts] = lift;
        fullLift[i] = full;
        CImVector color;
        for (int32_t c = 0; c < 3; c++) tint[c] = share > 0.0f ? tint[c] / share : 1.0f;
        color.r = static_cast<uint8_t>(std::min(tint[0], 1.0f) * 255.0f + 0.5f);
        color.g = static_cast<uint8_t>(std::min(tint[1], 1.0f) * 255.0f + 0.5f);
        color.b = static_cast<uint8_t>(std::min(tint[2], 1.0f) * 255.0f + 0.5f);
        color.a = static_cast<uint8_t>(slope * 255.0f + 0.5f);
        if (rgba) std::swap(color.r, color.b);
        v.m_color = color;
    }

    // Its normals: the terrain's, tilted by the lift's slope (drifts catch the light). Rows run toward -x, columns
    // toward -y.
    const float step = kChunkSize / 8.0f;
    for (uint32_t k = 0; k < chunks; k++) {
        const float* L = fullLift + k * kChunkVerts;
        auto outer = [&](int32_t r, int32_t c) { return L[ClampI(r, 0, 8) * 17 + ClampI(c, 0, 8)]; };
        for (int32_t r = 0; r <= 8; r++) {
            for (int32_t c = 0; c <= 8; c++) {
                const int32_t r0 = std::max(r - 1, 0), r1 = std::min(r + 1, 8), c0 = std::max(c - 1, 0), c1 = std::min(c + 1, 8);
                const float gx = -(outer(r1, c) - outer(r0, c)) / ((r1 - r0) * step);
                const float gy = -(outer(r, c1) - outer(r, c0)) / ((c1 - c0) * step);
                C3Vector& n = vertices[k * kChunkVerts + r * 17 + c].m_normal;
                n = C3Vector(n.x - gx, n.y - gy, n.z);
                Normalize(n);
            }
            if (r == 8) break;
            for (int32_t c = 0; c < 8; c++) {
                const float gx = -((outer(r + 1, c) + outer(r + 1, c + 1)) - (outer(r, c) + outer(r, c + 1))) / (2.0f * step);
                const float gy = -((outer(r, c + 1) + outer(r + 1, c + 1)) - (outer(r, c) + outer(r + 1, c))) / (2.0f * step);
                C3Vector& n = vertices[k * kChunkVerts + r * 17 + 9 + c].m_normal;
                n = C3Vector(n.x - gx, n.y - gy, n.z);
                Normalize(n);
            }
        }
    }

    // What the records say of the cover as a whole: the rules of its most present covered layer.
    int32_t main = -1;
    float best = 0.0f;
    for (uint32_t l = 0; l < layerCount; l++) {
        if (covers[l].depth <= 0.0f) continue;
        float w = 0.001f;
        for (int32_t q = 0; q < 9; q++) {
            float s[4];
            weights(rc->m_vec1.x - (q / 3 + 0.5f) * kChunkSize / 3.0f, rc->m_vec1.y - (q % 3 + 0.5f) * kChunkSize / 3.0f, s);
            w += s[l];
        }
        if (w > best) {
            best = w;
            main = static_cast<int32_t>(l);
        }
    }
    e.texture = nullptr;
    e.breakup = 0.0f;
    e.snow = false;
    for (uint32_t l = 0; l < 4; l++) {
        e.layerCover[l] = l < layerCount && covers[l].depth > 0.0f ? 1.0f : 0.0f;
        if (!e.layerCover[l]) continue;
        e.breakup = std::max(e.breakup, covers[l].breakup);
        e.snow = e.snow || covers[l].snow;
        if (!e.texture) e.texture = CoverTexture(covers[l].texture);
    }
    if (main >= 0) {
        const Cover& c = covers[main];
        for (uint32_t k = 0; k < chunks; k++) {
            ChunkRec& rec = *recs[k];
            rec.any = true;
            rec.drift = c.drift;
            rec.rim = c.rim;
            rec.relax = std::max(c.relax, 0.5f);
            rec.stiffness = Clamp(c.stiffness, 0.0f, 1.0f);
            rec.breakup = e.breakup;
            rec.tint[0] = 1.0f + (((c.tintColor >> 16) & 255) / 255.0f - 1.0f) * c.tintStrength;
            rec.tint[1] = 1.0f + (((c.tintColor >> 8) & 255) / 255.0f - 1.0f) * c.tintStrength;
            rec.tint[2] = 1.0f + ((c.tintColor & 255) / 255.0f - 1.0f) * c.tintStrength;
            rec.texture = CoverTexture(c.texture);
            rec.snow = c.snow;
        }
    }

    if (e.slot < 0) e.slot = TakeSlot(rc, now);
    if (e.slot < 0) {
        s_starved++;
        return false;
    }
    CGxBuf* buf = s_slots[e.slot].buf;
    char* dst = g_theGxDevicePtr->BufLock(buf);
    if (!dst) return false;
    std::memcpy(dst, vertices, count * sizeof(CGxVertexPNC));
    g_theGxDevicePtr->BufUnlock(buf, 0);
    buf->m_hasData = 1;
    return true;
}

bool Fresh(const Entry& e, CMapRenderChunk* rc) {
    return e.generation == s_generation && e.chunks[0] == rc->m_mapChunks[0] && e.chunks[1] == rc->m_mapChunks[1] &&
           e.header == (rc->m_mapChunks[0] ? rc->m_mapChunks[0]->m_header : nullptr);
}

// ---- near: a fine patch around the player, with trenches ---------------------------------------------------------

constexpr float kCell = 0.25f;
constexpr int32_t kCells = 192;          // 48 yd across
constexpr int32_t kVerts = kCells + 1;
constexpr int32_t kRecentre = 8;         // cells the player may move from the centre before it follows
constexpr int32_t kBlend = 16;           // cells along the border that blend into the far mesh

struct Cell {
    float terrain = 0.0f;
    float coarse = 0.0f;   // the far mesh here (the border blends into it)
    float lift = 0.0f;     // the cover's depth with drifts and lumps, untouched
    float share = 0.0f;    // how much of the ground here is covered, slope included (0..1)
    float press = 0.0f;    // trench: how far pressed down (0..1)
    float rim = 0.0f;      // trench rim (0..1)
    float rimShare = 0.0f, relax = 30.0f, stiffness = 0.0f;
    uint8_t tint[3] = { 255, 255, 255 };
    uint8_t state = 0;     // 0 not sampled, 1 sampled, 2 no record here yet (looked at again later)
};

std::vector<Cell> s_cells;
int32_t s_oi = 0, s_oj = 0;      // world cell of the patch's first vertex
bool s_haveNear = false;
bool s_nearAny = false;          // some cell has cover
uint32_t s_nearGeneration = 0;
uint32_t s_nearRecVersion = 0;
float s_nearZ = 0.0f;            // the player's height (for cells without terrain)
CTexture* s_nearTexture = nullptr;
bool s_nearSnow = true;
float s_nearBreakup = 0.5f;
CGxBuf* s_nearVtx[2] = {};
CGxBuf* s_nearIdx = nullptr;
int32_t s_nearFlip = 0;
uint32_t s_frame = 0;            // counts world updates
uint32_t s_terrainFrame = 0;     // the last update a terrain chunk was drawn after

void SampleCell(Cell& cell, float x, float y) {
    const float press = cell.press, rim = cell.rim;
    cell = Cell();
    cell.press = press;
    cell.rim = rim;
    C3Vector pos(x, y, s_nearZ);
    float ground = s_nearZ;
    CMapChunk* chunk = nullptr;
    const bool terrain = CMap::LinkStaticEntityGetChunk(pos, &ground, &chunk) && chunk;
    cell.terrain = cell.coarse = terrain ? ground : s_nearZ;
    auto it = terrain ? s_recs.find(chunk) : s_recs.end();
    if (it == s_recs.end()) {
        cell.state = 2;
        return;
    }
    const ChunkRec& rec = it->second;
    cell.state = 1;
    cell.terrain = OverTriangles(chunk, rec.terrain, x, y);
    cell.coarse = cell.terrain + OverTriangles(chunk, rec.lift, x, y);
    if (!rec.any || rec.share.empty()) return;

    const float node = kChunkSize / (kNodes - 1);
    const float fa = Clamp((ChunkTop(chunk) - x) / node, 0.0f, kNodes - 1.0f), fb = Clamp((ChunkLeft(chunk) - y) / node, 0.0f, kNodes - 1.0f);
    const int32_t a0 = std::min(static_cast<int32_t>(fa), kNodes - 2), b0 = std::min(static_cast<int32_t>(fb), kNodes - 2);
    const float ta = fa - a0, tb = fb - b0;
    auto at = [&](const std::vector<uint8_t>& v) {
        const float p = v[a0 * kNodes + b0] + (v[a0 * kNodes + b0 + 1] - v[a0 * kNodes + b0]) * tb;
        const float q = v[(a0 + 1) * kNodes + b0] + (v[(a0 + 1) * kNodes + b0 + 1] - v[(a0 + 1) * kNodes + b0]) * tb;
        return p + (q - p) * ta;
    };
    const float share = at(rec.share) / 255.0f * at(rec.slope) / 255.0f;
    if (share <= 0.002f) return;
    // The edge as the chunks' pixel shader draws it (breakup noise moving it, a soft band), and the cover sloping down
    // to the ground through that band, so no edge hangs over the ground.
    const float b = std::min(1.0f, rec.breakup * OptionCVars::Float(s_breakup, 0.0f, 3.0f, 1.0f));
    const float n = 0.5f + 0.5f * (Noise(x, y, 0.35f) * 0.65f + Noise(x + 17.0f, y + 17.0f, 0.945f) * 0.35f);
    const float e = share * (1.0f + b) - b * n;
    const float soft = 0.35f + (0.08f - 0.35f) * b;
    const float alpha = Clamp((e - 0.5f) / soft + 0.5f, 0.0f, 1.0f);
    if (alpha <= 0.0f) return;
    float rise = Clamp((e - 0.5f + 0.5f * soft) / (0.5f + 0.5f * soft), 0.0f, 1.0f);
    rise = rise * rise * (3.0f - 2.0f * rise);
    const float depth = at(rec.depth) / 100.0f * OptionCVars::Float(s_depth, 0.0f, 4.0f, 1.0f) * std::max(0.0f, 1.0f + rec.drift * Drift(x, y));
    // Fluffy: lumps a few centimetres high, a yard or so across.
    const float lumps = depth * (0.10f * Noise(x, y, 1.1f) + 0.04f * Noise(x, y, 3.3f));
    cell.share = alpha;
    cell.lift = std::max(0.0f, depth + lumps) * rise + 0.02f * alpha;
    cell.rimShare = rec.rim;
    cell.relax = rec.relax;
    cell.stiffness = rec.stiffness;
    for (int32_t c = 0; c < 3; c++) cell.tint[c] = static_cast<uint8_t>(Clamp(rec.tint[c], 0.0f, 1.0f) * 255.0f + 0.5f);
}

void Recentre(int32_t oi, int32_t oj, bool keep) {
    std::vector<Cell> cells(kVerts * kVerts);
    for (int32_t i = 0; i < kVerts; i++) {
        for (int32_t j = 0; j < kVerts; j++) {
            const int32_t pi = oi + i - s_oi, pj = oj + j - s_oj;
            Cell& cell = cells[i * kVerts + j];
            if (keep && pi >= 0 && pi < kVerts && pj >= 0 && pj < kVerts) {
                cell = s_cells[pi * kVerts + pj];
            } else {
                SampleCell(cell, (oi + i) * kCell, (oj + j) * kCell);
            }
        }
    }
    s_cells.swap(cells);
    s_oi = oi;
    s_oj = oj;
    s_haveNear = true;
}

bool CameraTarget(C3Vector* position, CGObject_C** object) {
    CGCamera* camera = CGWorldFrame::GetActiveCamera();
    if (!camera) return false;
    *object = ClntObjMgrObjectPtr(camera->m_targetGUID, TYPEMASK_OBJECT, __FILE__, __LINE__);
    if (!*object) return false;
    *position = (*object)->GetPosition();
    return true;
}

// A unit's feet press a trench into the cover where they stand on it, with a rim of pushed-up cover around it.
void Stamp(const C3Vector& pos, float radius) {
    const float reach = radius * 1.8f;
    const int32_t i0 = std::max(0, static_cast<int32_t>(std::floor((pos.x - reach) / kCell)) - s_oi);
    const int32_t i1 = std::min(kVerts - 1, static_cast<int32_t>(std::ceil((pos.x + reach) / kCell)) - s_oi);
    const int32_t j0 = std::max(0, static_cast<int32_t>(std::floor((pos.y - reach) / kCell)) - s_oj);
    const int32_t j1 = std::min(kVerts - 1, static_cast<int32_t>(std::ceil((pos.y + reach) / kCell)) - s_oj);
    if (i0 > i1 || j0 > j1) return;
    // only on the cover: not while flying, jumping high or standing on a bridge over it
    const int32_t ci = ClampI(static_cast<int32_t>(std::floor(pos.x / kCell)) - s_oi, 0, kVerts - 1);
    const int32_t cj = ClampI(static_cast<int32_t>(std::floor(pos.y / kCell)) - s_oj, 0, kVerts - 1);
    const Cell& under = s_cells[ci * kVerts + cj];
    if (under.share <= 0.0f || pos.z > under.terrain + under.lift + 0.6f || pos.z < under.terrain - 1.0f) return;
    for (int32_t i = i0; i <= i1; i++) {
        for (int32_t j = j0; j <= j1; j++) {
            Cell& cell = s_cells[i * kVerts + j];
            const float dx = (s_oi + i) * kCell - pos.x, dy = (s_oj + j) * kCell - pos.y;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d >= reach) continue;
            const float t = Clamp((radius - d) / (0.4f * radius), 0.0f, 1.0f);
            cell.press = std::max(cell.press, t * t * (3.0f - 2.0f * t));
            if (d > 0.8f * radius) cell.rim = std::max(cell.rim, std::sin(3.14159265f * (d - 0.8f * radius) / reach) * (1.0f - cell.press));
        }
    }
}

struct Stamper {
    C3Vector pos;
    float radius;
};
std::vector<Stamper> s_stampers;
CGObject_C* s_self = nullptr;

float StampRadius(CGUnit_C* unit) {
    float radius = (unit->m_unit ? unit->m_unit->m_unitFieldBoundingradius : 0.4f) * OptionCVars::Float(s_trenchScale, 0.2f, 4.0f, 1.0f);
    if (unit->IsMounted()) radius *= 1.6f;
    return Clamp(radius, 0.25f, 3.0f);
}

int32_t CollectUnit(WOWGUID guid, void*) {
    CGObject_C* object = ClntObjMgrObjectPtr(guid, TYPEMASK_UNIT, __FILE__, __LINE__);
    if (!object || object == s_self || s_stampers.size() >= 48) return 1;
    CGUnit_C* unit = static_cast<CGUnit_C*>(object);
    const C3Vector pos = unit->GetPosition();
    const float half = kCells * kCell * 0.5f + 2.0f;
    const float cx = (s_oi + kCells / 2) * kCell, cy = (s_oj + kCells / 2) * kCell;
    if (std::fabs(pos.x - cx) > half || std::fabs(pos.y - cy) > half) return 1;
    s_stampers.push_back(Stamper{ pos, StampRadius(unit) });
    return 1;
}

void ShowInfo() {
    static ULONGLONG s_last = 0;
    const ULONGLONG now = GetTickCount64();
    if (!s_info || now - s_last < 500) return;
    s_last = now;
    char text[900];
    std::snprintf(text, sizeof text, "shaders: %s; %u cover draws in 0.5 s, %u chunk(s) found no slot (%u slots); near patch %s; %s",
                  HD::SurfaceCoverShaderInfo(), s_draws, s_starved, static_cast<uint32_t>(s_slots.size()),
                  !s_haveNear ? "off" : s_nearAny ? "covered" : "bare", s_loaded ? s_tableStatus : "tables not loaded yet");
    s_draws = 0;
    s_starved = 0;
    s_info->Set(text, true, false, false, false);
}

// The terrain's c49..c53 and pixel c16 hold Azeroth Remastered's values only while its terrain constants are set: else
// 3.3.5 lighting, no HD lights. patch: the near patch takes no chunk's HD lights.
void LightingConstants(bool patch) {
    const bool remastered = Visuals::Current() == Visuals::AzerothRemastered;
    static CVar* s_terrainLight = CVar::Lookup("hdTerrainLight");
    const int32_t mode = s_terrainLight ? s_terrainLight->m_intValue : 1;
    if (!remastered) {
        const C3Vector& camera = CWorldScene::s_activeWorldView;
        const float hd[20] = { 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 0, camera.x, camera.y, camera.z, 0 };
        g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 49, hd, 5);
    }
    if (!remastered || mode == 0 || patch) {
        const float pixel[4] = { remastered && (mode == 1 || mode == 3) ? 1.0f : 0.0f, 0, 0, 0 };
        g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 16, pixel, 1);
    }
}

void LookConstants(float breakup, bool big, bool patch, bool snow) {
    const float bright = OptionCVars::Float(s_brightness, 0.2f, 1.5f, 0.93f);
    const bool rect = s_haveNear && s_nearAny && OptionCVars::Bool(s_near, true);
    const float pixel[8] = {
        bright, bright, bright, snow ? 1.0f : 0.0f,
        std::min(1.0f, breakup * OptionCVars::Float(s_breakup, 0.0f, 3.0f, 1.0f)), big ? 1.0f : 0.0f, 0.35f, patch ? 1.0f : 0.0f,
    };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 34, pixel, 2);
    const float area[4] = {
        rect ? s_oi * kCell : 1e9f, rect ? s_oj * kCell : 1e9f, rect ? (s_oi + kCells) * kCell : -1e9f, rect ? (s_oj + kCells) * kCell : -1e9f,
    };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 36, area, 1);
}

}  // namespace

void SurfaceCover::RegisterCVars() {
    auto reg = [](const char* name, const char* help, const char* value, bool rebuild) {
        return CVar::Register(name, help, 1, value, rebuild ? Rebuild : nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    };
    s_enabled = reg("surfaceCover", "Snow (and sand, ash) lies on the ground as a layer of its own, from SurfaceCover.cdbc", "1", false);
    s_depth = reg("surfaceCoverDepth", "Surface cover: depth multiplier on what SurfaceCover.cdbc says (1 = the table)", "1", true);
    s_others = reg("surfaceCoverOthers", "Surface cover: sand and ash too, not only snow", "1", true);
    s_brightness = reg("surfaceCoverBrightness", "Surface cover: how bright the cover is (0.93 = the donor's snow)", "0.93", false);
    s_breakup = reg("surfaceCoverBreakup", "Surface cover: ragged edges multiplier on the table's EdgeBreakup", "1", false);
    s_farLift = reg("surfaceCoverDistanceLift", "Surface cover: yards it is lifted far away, against the depth buffer's coarseness there", "0.1", false);
    s_near = reg("surfaceCoverNear", "Surface cover: a fine, lumpy patch 48 yards across around you, where trenches form", "1", false);
    s_trenches = reg("surfaceCoverTrenches", "Surface cover: you and the units near you press trenches into it", "1", false);
    s_trenchScale = reg("surfaceCoverTrenchScale", "Surface cover: trench width as a multiple of each unit's size", "1", false);
    s_info = CVar::Register("surfaceCoverInfo", "Surface cover status (read only)", 0, "not started", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    HD::SurfaceCoverShadersStart();
}

void SurfaceCover::Forget(CMapRenderChunk* chunk) {
    for (CMapChunk* mc : chunk->m_mapChunks) {
        if (mc) s_recs.erase(mc);
    }
    auto it = s_entries.find(chunk);
    if (it == s_entries.end()) return;
    FreeSlot(it->second);
    s_entries.erase(it);
}

void SurfaceCover::Update(float elapsedSec) {
    s_frame++;
    ShowInfo();
    C3Vector player;
    CGObject_C* self = nullptr;
    if (!OptionCVars::Bool(s_enabled, true) || !OptionCVars::Bool(s_near, true) || !CameraTarget(&player, &self)) {
        s_haveNear = false;
        return;
    }
    s_nearZ = player.z;
    const int32_t oi = static_cast<int32_t>(std::floor(player.x / kCell)) - kCells / 2;
    const int32_t oj = static_cast<int32_t>(std::floor(player.y / kCell)) - kCells / 2;
    const bool fresh = s_haveNear && s_nearGeneration == s_generation;
    if (!fresh || std::abs(oi - s_oi) > kRecentre || std::abs(oj - s_oj) > kRecentre) {
        Recentre(oi, oj, fresh);
        s_nearGeneration = s_generation;
        s_nearRecVersion = s_recVersion;
    } else if (s_nearRecVersion != s_recVersion) {
        // chunks drawn for the first time since: the cells that had no record look again
        s_nearRecVersion = s_recVersion;
        for (int32_t i = 0; i < kVerts; i++) {
            for (int32_t j = 0; j < kVerts; j++) {
                Cell& cell = s_cells[i * kVerts + j];
                if (cell.state != 1) SampleCell(cell, (s_oi + i) * kCell, (s_oj + j) * kCell);
            }
        }
    }

    // The look of the patch: that of the ground under the player.
    C3Vector pos(player.x, player.y, player.z);
    float ground = 0.0f;
    CMapChunk* chunk = nullptr;
    if (CMap::LinkStaticEntityGetChunk(pos, &ground, &chunk) && chunk) {
        auto it = s_recs.find(chunk);
        if (it != s_recs.end() && it->second.any) {
            s_nearTexture = it->second.texture;
            s_nearSnow = it->second.snow;
            s_nearBreakup = it->second.breakup;
        }
    }

    // Trenches fill back in over the cover's RelaxSeconds.
    const float dt = Clamp(elapsedSec, 0.0f, 0.5f);
    s_nearAny = false;
    for (Cell& cell : s_cells) {
        s_nearAny = s_nearAny || cell.share > 0.0f;
        if (cell.press > 0.0f) cell.press = std::max(0.0f, cell.press - dt / cell.relax);
        if (cell.rim > 0.0f) cell.rim = std::max(0.0f, cell.rim - dt / cell.relax);
    }
    if (!s_nearAny || !OptionCVars::Bool(s_trenches, true)) return;

    s_stampers.clear();
    s_self = self;
    if (self->m_obj->m_type & TYPEMASK_UNIT) s_stampers.push_back(Stamper{ player, StampRadius(static_cast<CGUnit_C*>(self)) });
    ClntObjMgrEnumVisibleObjects(CollectUnit, nullptr);
    for (const Stamper& s : s_stampers) Stamp(s.pos, s.radius);
}

void SurfaceCover::DrawAfter(CMapRenderChunk* rc) {
    s_terrainFrame = s_frame;
    if (!OptionCVars::Bool(s_enabled, true) || !g_theGxDevicePtr || !rc->m_chunkBuf || !rc->m_mapChunks[0] || !rc->m_terrainBlendTexture) return;

    CGxShader* vertexShader = nullptr;
    CGxShader* pixelShader = nullptr;
    if (!HD::SurfaceCoverShaders(static_cast<int32_t>(CShadowCache::GetShadowValue()), CShaderEffect::s_usePcfFiltering != 0, &vertexShader, &pixelShader)) return;

    const ULONGLONG now = GetTickCount64();
    Entry& e = s_entries[rc];
    bool stale = !Fresh(e, rc);
    if (!stale && e.cover && (e.slot < 0 || !s_slots[e.slot].buf->m_hasData || !s_slots[e.slot].buf->m_valid)) stale = true;
    if (stale) {
        e.chunks[0] = rc->m_mapChunks[0];
        e.chunks[1] = rc->m_mapChunks[1];
        e.header = rc->m_mapChunks[0]->m_header;
        e.generation = s_generation;
        e.cover = Build(rc, e, now);
        if (!e.cover) FreeSlot(e);
    }
    if (!e.cover) return;
    s_slots[e.slot].lastUsed = now;

    CGxTex* blend = TextureGetGxTex(rc->m_terrainBlendTexture, 1, nullptr);
    CGxTex* cover = e.texture ? TextureGetGxTex(e.texture, 0, nullptr) : nullptr;
    const bool textured = cover != nullptr;
    if (!cover) cover = White();
    if (!blend || !cover) return;

    g_theGxDevicePtr->RsPush();
    GxPrimVertexPtr(s_slots[e.slot].buf, GxVBF_PNC);
    g_theGxDevicePtr->PrimIndexPtr(rc->m_chunkBuf->m_indexBuf);
    g_theGxDevicePtr->RsSet(GxRs_VertexShader, vertexShader);
    g_theGxDevicePtr->RsSet(GxRs_PixelShader, pixelShader);
    g_theGxDevicePtr->RsSet(GxRs_Texture0, cover);
    GxTexSetWrap(cover, GxTex_Wrap, GxTex_Wrap);
    g_theGxDevicePtr->RsSet(GxRs_Texture1, blend);
    GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
    GxRsSet(GxRs_DepthWrite, 1);

    LightingConstants(false);
    const float vertex[8] = {
        -CMapChunk::s_geoToTex,
        -CMapChunk::s_geoToTex * 0.125f * (rc->m_unkFlags & 0x1 ? 0.5f : 1.0f),
        -CMapChunk::s_geoToTex * 0.125f * (rc->m_unkFlags & 0x2 ? 0.5f : 1.0f),
        0.0f,
        OptionCVars::Float(s_farLift, 0.0f, 5.0f, 0.1f), 1.0f / 600.0f, textured ? 1.0f : 0.0f, 0.0f,
    };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 54, vertex, 2);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 33, e.layerCover, 1);
    LookConstants(e.breakup, (CMap::header.m_flags & 0x4) != 0, false, e.snow);

    g_theGxDevicePtr->Draw(&rc->m_batch, 1);
    g_theGxDevicePtr->RsPop();
    s_draws++;
}

void SurfaceCover::DrawNear() {
    if (s_terrainFrame != s_frame || !s_haveNear || !s_nearAny || !OptionCVars::Bool(s_enabled, true) || !OptionCVars::Bool(s_near, true)) return;
    CGxShader* vertexShader = nullptr;
    CGxShader* pixelShader = nullptr;
    if (!HD::SurfaceCoverShaders(static_cast<int32_t>(CShadowCache::GetShadowValue()), CShaderEffect::s_usePcfFiltering != 0, &vertexShader, &pixelShader)) return;

    if (!s_nearIdx) {
        // Two vertex buffers taken in turn (the one drawn last frame is not locked while that frame may be in flight),
        // one index buffer for the grid.
        const uint32_t bytes = kVerts * kVerts * sizeof(CGxVertexPNC);
        CGxPool* vertexPool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Vertex, GxPoolUsage_Dynamic, 2 * bytes, GxPoolHintBit_Unk3, "SurfaceCoverNear_vtx");
        CGxPool* indexPool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Index, GxPoolUsage_Dynamic, kCells * kCells * 6 * 2, GxPoolHintBit_Unk0, "SurfaceCoverNear_idx");
        s_nearVtx[0] = g_theGxDevicePtr->BufCreate(vertexPool, sizeof(CGxVertexPNC), kVerts * kVerts, 0);
        s_nearVtx[1] = g_theGxDevicePtr->BufCreate(vertexPool, sizeof(CGxVertexPNC), kVerts * kVerts, bytes);
        s_nearIdx = g_theGxDevicePtr->BufCreate(indexPool, 2, kCells * kCells * 6, 0);
    }
    if (!s_nearIdx->m_hasData || !s_nearIdx->m_valid) {
        uint16_t* index = reinterpret_cast<uint16_t*>(g_theGxDevicePtr->BufLock(s_nearIdx));
        if (!index) return;
        for (int32_t i = 0; i < kCells; i++) {
            for (int32_t j = 0; j < kCells; j++) {
                const uint16_t v00 = static_cast<uint16_t>(i * kVerts + j), v01 = static_cast<uint16_t>(v00 + 1);
                const uint16_t v10 = static_cast<uint16_t>(v00 + kVerts), v11 = static_cast<uint16_t>(v10 + 1);
                const uint16_t quad[6] = { v00, v10, v11, v00, v11, v01 };
                std::memcpy(index, quad, sizeof quad);
                index += 6;
            }
        }
        g_theGxDevicePtr->BufUnlock(s_nearIdx, 0);
        s_nearIdx->m_hasData = 1;
    }

    // Heights: the cover pressed by its trenches and raised by their rims, blending into the far mesh at the border.
    static std::vector<float> s_height;
    s_height.resize(kVerts * kVerts);
    for (int32_t i = 0; i < kVerts; i++) {
        for (int32_t j = 0; j < kVerts; j++) {
            const Cell& cell = s_cells[i * kVerts + j];
            const float edge = Clamp((std::min(std::min(i, kCells - i), std::min(j, kCells - j)) - 1.0f) / (kBlend - 1.0f), 0.0f, 1.0f);
            float fine = cell.terrain;
            if (cell.share > 0.0f) {
                const float press = cell.press * edge * (1.0f - cell.stiffness);
                fine += cell.lift * (1.0f - press) + cell.rim * edge * cell.rimShare * cell.lift;
            }
            s_height[i * kVerts + j] = cell.coarse + (fine - cell.coarse) * edge;
        }
    }

    s_nearFlip ^= 1;
    CGxBuf* buf = s_nearVtx[s_nearFlip];
    CGxVertexPNC* v = reinterpret_cast<CGxVertexPNC*>(g_theGxDevicePtr->BufLock(buf));
    if (!v) return;
    const bool rgba = g_theGxDevicePtr->Caps().m_colorFormat == GxCF_rgba;
    for (int32_t i = 0; i < kVerts; i++) {
        for (int32_t j = 0; j < kVerts; j++) {
            const Cell& cell = s_cells[i * kVerts + j];
            const int32_t i0 = std::max(i - 1, 0), i1 = std::min(i + 1, kCells), j0 = std::max(j - 1, 0), j1 = std::min(j + 1, kCells);
            const float gx = (s_height[i1 * kVerts + j] - s_height[i0 * kVerts + j]) / ((i1 - i0) * kCell);
            const float gy = (s_height[i * kVerts + j1] - s_height[i * kVerts + j0]) / ((j1 - j0) * kCell);
            const float len = std::sqrt(gx * gx + gy * gy + 1.0f);
            v->m_position = C3Vector((s_oi + i) * kCell, (s_oj + j) * kCell, s_height[i * kVerts + j]);
            v->m_normal = C3Vector(-gx / len, -gy / len, 1.0f / len);
            CImVector color;
            color.r = cell.tint[0];
            color.g = cell.tint[1];
            color.b = cell.tint[2];
            color.a = static_cast<uint8_t>(Clamp(cell.share, 0.0f, 1.0f) * 255.0f + 0.5f);
            if (rgba) std::swap(color.r, color.b);
            v->m_color = color;
            v++;
        }
    }
    g_theGxDevicePtr->BufUnlock(buf, 0);
    buf->m_hasData = 1;

    CGxTex* cover = s_nearTexture ? TextureGetGxTex(s_nearTexture, 0, nullptr) : nullptr;
    const bool textured = cover != nullptr;
    CGxTex* white = White();
    if (!cover) cover = white;
    if (!cover || !white) return;

    g_theGxDevicePtr->RsPush();
    GxPrimVertexPtr(buf, GxVBF_PNC);
    g_theGxDevicePtr->PrimIndexPtr(s_nearIdx);
    g_theGxDevicePtr->RsSet(GxRs_VertexShader, vertexShader);
    g_theGxDevicePtr->RsSet(GxRs_PixelShader, pixelShader);
    g_theGxDevicePtr->RsSet(GxRs_Texture0, cover);
    GxTexSetWrap(cover, GxTex_Wrap, GxTex_Wrap);
    g_theGxDevicePtr->RsSet(GxRs_Texture1, white);
    GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
    GxRsSet(GxRs_DepthWrite, 1);
    GxRsSet(GxRs_Culling, 0);

    LightingConstants(true);
    // The cover texture tiles from a point on the terrain layers' own grid (every 33.33 / 8 yd), so it lines up with the
    // far mesh's.
    const float tile = kChunkSize / 8.0f;
    const float origin[4] = { std::floor(s_oi * kCell / tile) * tile, std::floor(s_oj * kCell / tile) * tile, 0.0f, 0.0f };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 23, origin, 1);
    const float vertex[8] = {
        -CMapChunk::s_geoToTex, 0.0f, 0.0f, 0.0f,
        OptionCVars::Float(s_farLift, 0.0f, 5.0f, 0.1f), 1.0f / 600.0f, textured ? 1.0f : 0.0f, 0.0f,
    };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 54, vertex, 2);
    const float layers[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 33, layers, 1);
    LookConstants(s_nearBreakup, false, true, s_nearSnow);

    CGxBatch batch;
    batch.m_primType = GxPrim_Triangles;
    batch.m_start = 0;
    batch.m_count = kCells * kCells * 6;
    batch.m_minIndex = 0;
    batch.m_maxIndex = static_cast<uint16_t>(kVerts * kVerts - 1);
    g_theGxDevicePtr->Draw(&batch, 1);
    g_theGxDevicePtr->RsPop();
    s_draws++;
}
