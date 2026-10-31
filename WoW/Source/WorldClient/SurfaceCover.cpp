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
#include "Engine/Source/Tempest/CMath.h"
#include "WoW/Source/WorldClient/CChunkLiquid.h"
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
enum Field { kDepth, kMaxSlope, kSlopeFade, kDrift, kBreakup, kRim, kRelax, kTint, kZOffset, kMoistureMin, kMeltKept, kMeltGone, kFieldCount };
constexpr uint32_t kColumn[kFieldCount] = { 3, 4, 5, 6, 7, 8, 9, 11, 15, 20, 18, 19 };

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
    float meltKept = 0.6f;       // SurfaceCover MeltKeptShare: painted snow left at 15 degC (the donor's default where unset)
    float meltGone = 25.0f;      // MeltGoneTemperature: degC at which none is left
    int32_t meltClass = -1;      // index into s_meltClasses for snow covers (a zone keeps one melt state per class)
    uint32_t tintColor = 0;
    uint32_t flags = 0;
    uint32_t coverMaterial = 0;
    int32_t texture = 0;
    bool snow = false;
    bool snowable = false;       // fallen snow may lie on this layer (bare, or already snow); never on sand and ash
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
    if (set[kMeltKept]) c.meltKept = std::min(value[kMeltKept], 1.0f);
    if (set[kMeltGone]) c.meltGone = std::max(value[kMeltGone], 1.0f);
    c.meltClass = -1;
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

// The look of fallen snow (slope limits, drifts, rim, texture, tint): the snow material's cover, here or everywhere.
// False without a snow material in the tables.
bool FallenLook(uint32_t area, int32_t map, Cover& out) {
    uint32_t material = Select(area, map, std::string(), 0, kSnowTerrainType);
    if (!material) material = Select(0, -1, std::string(), 0, kSnowTerrainType);
    if (!material) return false;
    out = ForMaterial(material);
    out.snow = true;
    return true;
}

// Painted snow's melting numbers come from its SurfaceCover row: a zone keeps one melt state per distinct pair.
struct MeltClass {
    float kept, gone;
};
std::vector<MeltClass> s_meltClasses;

int32_t MeltClassOf(float kept, float gone) {
    for (size_t i = 0; i < s_meltClasses.size(); i++) {
        if (std::fabs(s_meltClasses[i].kept - kept) < 1e-4f && std::fabs(s_meltClasses[i].gone - gone) < 1e-4f) return static_cast<int32_t>(i);
    }
    s_meltClasses.push_back({ kept, gone });
    return static_cast<int32_t>(s_meltClasses.size()) - 1;
}

// ---- options ------------------------------------------------------------------------------------------------------

CVar* s_enabled = nullptr;
CVar* s_fallen = nullptr;
CVar* s_fallenRate = nullptr;
CVar* s_melt = nullptr;
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
    float fshare[kChunkVerts] = {};   // the share of each vertex's ground fallen snow can lie on (far mesh; slope and water applied)
    float pfull[kChunkVerts] = {};    // ...the painted cover's full depth there (yd, capped), and its share of the ground
    float pshare[kChunkVerts] = {};
    bool any = false;
    std::vector<uint8_t> share, slope, depth;  // per node: share and slope factor (0..255), depth in cm (slope applied)
    std::vector<uint8_t> fall;                 // per node: how much of it fallen snow can lie on (bare or snowy layers, slope applied)
    float drift = 0.0f, rim = 0.0f, relax = 30.0f, stiffness = 0.0f, breakup = 0.0f;
    float tint[3] = { 1.0f, 1.0f, 1.0f };
    CTexture* texture = nullptr;
    bool snow = false;
    int32_t meltClass = -1;
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

// 1 on dry ground, falling to 0 as the ground goes 0.3 yd below the surface of a terrain liquid (rivers, lakes, sea; not
// WMO water) at (x, y), where the terrain is at z.
float Dry(const CMapChunk* chunk, float x, float y, float z) {
    if (!const_cast<CMapChunk*>(chunk)->m_liquidChunkLinkList.Head()) return 1.0f;
    float surface = 0.0f;
    const C2Vector tile(-(y - 17066.666f) * 0.24f, -(x - 17066.666f) * 0.24f);
    if (!chunk->GetLiquidHeight(tile, &surface)) return 1.0f;
    return Clamp((z - surface) / 0.3f + 1.0f, 0.0f, 1.0f);
}

// ---- heat: magma melts snow around it ------------------------------------------------------------------------------

bool IsMagma(int32_t type) {
    const LiquidTypeRec* rec = g_liquidTypeDB.GetRecord(type);
    return rec && rec->m_soundBank == 2;  // LiquidType column 3 is its category: 2 = magma
}

// The magma surface over the terrain tile at (x, y), if there is any (CMapChunk::GetLiquidHeight, magma liquids only).
bool MagmaHeight(const CMapChunk* chunk, float x, float y, float* height) {
    const C2Vector pos(-(y - 17066.666f) * 0.24f, -(x - 17066.666f) * 0.24f);
    const int32_t tx = CMath::fint_(pos.x - 0.5f), ty = CMath::fint_(pos.y - 0.5f);
    C2iVector tile;
    tile.x = tx & 7;
    tile.y = ty & 7;
    const C2Vector frac(pos.x - tx, pos.y - ty);
    CMapChunk* mc = const_cast<CMapChunk*>(chunk);
    for (CChunkLiquid* liquid = mc->m_liquidChunkLinkList.Head(); liquid; liquid = mc->m_liquidChunkLinkList.Next(liquid)) {
        if (IsMagma(liquid->m_type) && liquid->TileExists(tile.x, tile.y) && liquid->GetHeight(frac, tile, *height)) return true;
    }
    return false;
}

// Heat 0..1 on the chunk's cover nodes (kNodes x kNodes): full within 1 yd of magma, gone by 12 yd (the donor's reach).
// Empty where the chunk has no magma. shortcut: only the chunk's own magma counts, not a neighbour's pool across the
// border; hot terrain materials (GroundMaterial.Temperature) are not read yet.
void ComputeHeat(const CMapChunk* chunk, const ChunkRec& rec, std::vector<float>& out) {
    out.clear();
    CMapChunk* mc = const_cast<CMapChunk*>(chunk);
    bool magma = false;
    for (CChunkLiquid* liquid = mc->m_liquidChunkLinkList.Head(); liquid && !magma; liquid = mc->m_liquidChunkLinkList.Next(liquid)) magma = IsMagma(liquid->m_type);
    if (!magma) return;
    const float node = kChunkSize / (kNodes - 1);
    std::vector<float> d(kNodes * kNodes, 1e9f);
    bool found = false;
    for (int32_t a = 0; a < kNodes; a++) {
        for (int32_t b = 0; b < kNodes; b++) {
            const float x = ChunkTop(chunk) - a * node, y = ChunkLeft(chunk) - b * node;
            float surface;
            if (MagmaHeight(chunk, x, y, &surface) && surface >= OverTriangles(chunk, rec.terrain, x, y) - 0.3f) {
                d[a * kNodes + b] = 0.0f;
                found = true;
            }
        }
    }
    if (!found) return;
    // a two-pass chamfer distance, in nodes
    for (int32_t a = 0; a < kNodes; a++) {
        for (int32_t b = 0; b < kNodes; b++) {
            float& v = d[a * kNodes + b];
            if (a > 0) v = std::min(v, d[(a - 1) * kNodes + b] + 1.0f);
            if (b > 0) v = std::min(v, d[a * kNodes + b - 1] + 1.0f);
            if (a > 0 && b > 0) v = std::min(v, d[(a - 1) * kNodes + b - 1] + 1.4142f);
            if (a > 0 && b < kNodes - 1) v = std::min(v, d[(a - 1) * kNodes + b + 1] + 1.4142f);
        }
    }
    for (int32_t a = kNodes - 1; a >= 0; a--) {
        for (int32_t b = kNodes - 1; b >= 0; b--) {
            float& v = d[a * kNodes + b];
            if (a < kNodes - 1) v = std::min(v, d[(a + 1) * kNodes + b] + 1.0f);
            if (b < kNodes - 1) v = std::min(v, d[a * kNodes + b + 1] + 1.0f);
            if (a < kNodes - 1 && b < kNodes - 1) v = std::min(v, d[(a + 1) * kNodes + b + 1] + 1.4142f);
            if (a < kNodes - 1 && b > 0) v = std::min(v, d[(a + 1) * kNodes + b - 1] + 1.4142f);
        }
    }
    out.resize(kNodes * kNodes);
    for (int32_t i = 0; i < kNodes * kNodes; i++) {
        const float t = Clamp((d[i] * node - 1.0f) / 11.0f, 0.0f, 1.0f);
        out[i] = 1.0f - t * t * (3.0f - 2.0f * t);
    }
}

// The heat at world (x, y) from a chunk's heat nodes (nearest node); 0 without any.
float HeatAt(const CMapChunk* chunk, const std::vector<float>& heat, float x, float y) {
    if (heat.empty()) return 0.0f;
    const float node = kChunkSize / (kNodes - 1);
    const int32_t a = ClampI(static_cast<int32_t>(std::lround((ChunkTop(chunk) - x) / node)), 0, kNodes - 1);
    const int32_t b = ClampI(static_cast<int32_t>(std::lround((ChunkLeft(chunk) - y) / node)), 0, kNodes - 1);
    return heat[a * kNodes + b];
}

// What a layer's cover keeps of `dry`: SurfaceCover Flags 0x1 prevents it underwater, 0x2 keeps it to the water's floor.
float LayerDry(const Cover& c, bool fall, float dry) {
    if (fall || (c.flags & 0x1)) return dry;
    return (c.flags & 0x2) ? 1.0f - dry : 1.0f;
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
    float fallLayer[4] = {};   // 1 where fallen snow may lie (scaled by its depth at draw time)
    int32_t snowClass[4] = { -1, -1, -1, -1 };   // the melt class of a painted snow layer (its share follows the melting at draw time)
    // The vertices as built (without fallen snow's lift) and each one's fallen-snow share, so the mesh can be lifted again
    // as the fallen depth changes without building the chunk again.
    std::vector<CGxVertexPNC> base;
    std::vector<float> fshare, pfull, pshare;  // per vertex: fallen snow's share; painted cover's depth and share
    std::vector<float> open, fallFrac;         // per vertex: 1 in the open, 0 under a roof; the fallen share of its cover
    int32_t paintClass = -1;                   // the melt class of the chunk's painted snow
    float fallApplied = 0.0f;                  // the lifts the slot's vertices carry now
    float keepApplied = 1.0f;
    bool roofDirty = false;                    // `open` changed since they were written
    uint32_t roofCursor = 0;                   // the next vertex whose roof test is still to be done
    ULONGLONG roofTime = 0;                    // when the last pass over them finished
    bool painted = false;      // some layer has painted cover; else the mesh is only there for fallen snow
    bool waitFall = false;     // built without fallen snow's part because none lay then: built again once it does
    float breakup = 0.0f;
    CTexture* texture = nullptr;
};
std::unordered_map<CMapRenderChunk*, Entry> s_entries;
constexpr float kFarFall = 0.6f;  // yd: the most fallen snow lifts the far mesh (= the deepest it gets, so far and near agree)
float s_farFall = 0.0f;           // fallen snow's lift of the far mesh now, in 2 cm steps
int32_t s_refreshBudget = 0;      // far chunks that may be lifted again this frame
int32_t s_roofBudget = 0;         // far vertices whose roof test may be read this frame
int32_t s_waitBudget = 0;         // chunks built again this frame because fallen snow has started

// How far fallen snow lifts the far mesh where `share` of the ground can hold it: none below a third (no lip where the
// per-pixel edge is thinner than the vertices can follow), the full lift from near all of it.
float FarFall(float lift, float share) {
    const float t = Clamp((share - 0.35f) / 0.65f, 0.0f, 1.0f);
    return lift * t * t * (3.0f - 2.0f * t);
}

constexpr float kFarPainted = 0.6f;  // yd: the most a painted cover lifts the far mesh
float KeepNow(int32_t meltClass);

// Painted snow's melted share in 5% steps (the far mesh follows it that coarsely).
float QuantizedKeep(float keep) { return std::floor(keep / 0.05f + 0.5f) * 0.05f; }

// The far mesh's vertices as the entry built them, lifted by the fallen snow (`fall` yd at most) and the painted cover
// (kept share `keep`), each where its share of the ground is high enough, and with the fallen share of the cover thinned
// under roofs.
void LiftVertices(const Entry& e, CGxVertexPNC* out, float fall, float keep) {
    for (size_t i = 0; i < e.base.size(); i++) {
        out[i] = e.base[i];
        const float open = e.open[i];
        const float lift = std::max(FarFall(fall * open, e.fshare[i]), FarFall(e.pfull[i] * keep, e.pshare[i]));
        out[i].m_position.z += lift;
        if (open < 1.0f) {
            const float a = out[i].m_color.a * (1.0f - e.fallFrac[i] * (1.0f - open));
            out[i].m_color.a = static_cast<uint8_t>(a + 0.5f);
        }
    }
}
// What the cover costs, per status window (surfaceCoverInfo): calls, milliseconds in all of them, and the worst one.
struct Perf {
    double ms = 0.0, worst = 0.0;
    uint32_t n = 0;
};
Perf s_perfBuild, s_perfApply, s_perfRoof, s_perfUpdate, s_perfNear;

double NowMs() {
    static const double s_scale = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return 1000.0 / static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) * s_scale;
}

struct PerfScope {
    Perf& perf;
    double start;
    explicit PerfScope(Perf& p) : perf(p), start(NowMs()) {}
    ~PerfScope() {
        const double d = NowMs() - start;
        perf.ms += d;
        perf.n++;
        if (d > perf.worst) perf.worst = d;
    }
};
float s_fallenDepth = 0.0f;          // fallen snow in the player's zone, as of this frame
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
        c.meltClass = c.snow ? MeltClassOf(c.meltKept, c.meltGone) : -1;
        c.snowable = c.snow || c.depth <= 0.0f;
        // shortcut: covers that need ground moisture (mud) wait for a moisture model; nothing else is gated yet
        if (c.moistureMin <= 1.0f || (!c.snow && !others)) c.depth = 0.0f;
        covers[i] = c;
        any = any || c.depth > 0.0f;
    }
    // Layers with no material at all are bare ground: snowable too.
    Cover fallenLook;
    const bool fallenAllowed = OptionCVars::Bool(s_fallen, true) && FallenLook(areaId, mapId, fallenLook);
    // Bare chunks get a mesh and a record only while fallen snow lies (a zone's snow is one depth): else every chunk in
    // view would build one for snow that is not there, and spinning the camera would build hundreds a second. They are
    // built when the snow starts (e.waitFall, a few a frame).
    const bool fallenOn = fallenAllowed && s_fallenDepth > 0.0f;
    bool snowableAny = false, snowableRaw = false;
    for (uint32_t i = 0; i < layerCount; i++) {
        if (!covers[i].snow && covers[i].depth <= 0.0f && covers[i].coverMaterial == 0) covers[i].snowable = true;
        snowableRaw = snowableRaw || (fallenAllowed && covers[i].snowable);
        snowableAny = snowableAny || (fallenOn && covers[i].snowable);
    }
    e.waitFall = snowableRaw && !snowableAny;

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
    if (!any && !snowableAny) return false;

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

    // Magma's heat melts the cover around it (none where the chunk has no magma).
    std::vector<float> heat[2];
    for (uint32_t k = 0; k < chunks; k++) ComputeHeat(rc->m_mapChunks[k], *recs[k], heat[k]);

    // The cover's nodes, for the near patch.
    const float node = kChunkSize / (kNodes - 1);
    for (uint32_t k = 0; k < chunks; k++) {
        ChunkRec& rec = *recs[k];
        const CMapChunk* mc = rc->m_mapChunks[k];
        rec.share.assign(kNodes * kNodes, 0);
        rec.slope.assign(kNodes * kNodes, 0);
        rec.depth.assign(kNodes * kNodes, 0);
        rec.fall.assign(snowableAny ? kNodes * kNodes : 0, 0);
        for (int32_t a = 0; a < kNodes; a++) {
            for (int32_t b = 0; b < kNodes; b++) {
                float w[4];
                weights(ChunkTop(mc) - a * node, ChunkLeft(mc) - b * node, w);
                const int32_t r = (a + 4) / 8, c = (b + 4) / 8;
                const C3Vector& n = vertices[k * kChunkVerts + r * 17 + c].m_normal;
                const float nx = ChunkTop(mc) - a * node, ny = ChunkLeft(mc) - b * node;
                const float dry = Dry(mc, nx, ny, OverTriangles(mc, rec.terrain, nx, ny));
                const float warm = 1.0f - (heat[k].empty() ? 0.0f : heat[k][a * kNodes + b]);
                float share = 0.0f, slope = 0.0f, depth = 0.0f, fall = 0.0f;
                for (uint32_t l = 0; l < layerCount; l++) {
                    if (fallenOn && covers[l].snowable) fall += w[l] * dry * warm * SlopeFactor(fallenLook, n);
                    if (covers[l].depth <= 0.0f || w[l] <= 0.0f) continue;
                    const float s = SlopeFactor(covers[l], n);
                    const float wl = w[l] * LayerDry(covers[l], false, dry) * (covers[l].snow ? warm : 1.0f);
                    share += wl;
                    slope += wl * s;
                    depth += wl * s * covers[l].depth;
                }
                const int32_t index = a * kNodes + b;
                rec.share[index] = static_cast<uint8_t>(Clamp(share, 0.0f, 1.0f) * 255.0f + 0.5f);
                rec.slope[index] = static_cast<uint8_t>(Clamp(share > 0.0f ? slope / share : 0.0f, 0.0f, 1.0f) * 255.0f + 0.5f);
                rec.depth[index] = static_cast<uint8_t>(Clamp(depth * 100.0f, 0.0f, 255.0f) + 0.5f);
                if (snowableAny) rec.fall[index] = static_cast<uint8_t>(Clamp(fall, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        }
    }

    if (!any) {
        // Bare ground: no painted cover; its record takes the fallen snow's look.
        for (uint32_t k = 0; k < chunks; k++) {
            ChunkRec& rec = *recs[k];
            rec.any = true;
            rec.drift = fallenLook.drift;
            rec.rim = fallenLook.rim;
            rec.relax = std::max(fallenLook.relax, 0.5f);
            rec.stiffness = Clamp(fallenLook.stiffness, 0.0f, 1.0f);
            rec.breakup = fallenLook.breakup;
            rec.tint[0] = 1.0f + (((fallenLook.tintColor >> 16) & 255) / 255.0f - 1.0f) * fallenLook.tintStrength;
            rec.tint[1] = 1.0f + (((fallenLook.tintColor >> 8) & 255) / 255.0f - 1.0f) * fallenLook.tintStrength;
            rec.tint[2] = 1.0f + ((fallenLook.tintColor & 255) / 255.0f - 1.0f) * fallenLook.tintStrength;
            rec.texture = CoverTexture(fallenLook.texture);
            rec.snow = true;
        }
    }

    // The far mesh: each vertex lifted by its cover.
    float fullLift[2 * kChunkVerts] = {};
    float vPaintedFull[2 * kChunkVerts] = {}, vPaintedShare[2 * kChunkVerts] = {}, vFallFrac[2 * kChunkVerts] = {};
    const float depthScale = OptionCVars::Float(s_depth, 0.0f, 4.0f, 1.0f);
    const bool rgba = g_theGxDevicePtr->Caps().m_colorFormat == GxCF_rgba;
    for (uint32_t i = 0; i < count; i++) {
        CGxVertexPNC& v = vertices[i];
        float w[4];
        weights(v.m_position.x, v.m_position.y, w);
        const float drift = Drift(v.m_position.x, v.m_position.y);
        float share = 0.0f, slopeShare = 0.0f, depth = 0.0f, zOffset = 0.0f, covered = 0.0f, fallShare = 0.0f, paintedShare = 0.0f;
        float tint[3] = { 0.0f, 0.0f, 0.0f };
        const float dry = Dry(rc->m_mapChunks[i / kChunkVerts], v.m_position.x, v.m_position.y, v.m_position.z);
        const float warm = 1.0f - HeatAt(rc->m_mapChunks[i / kChunkVerts], heat[i / kChunkVerts], v.m_position.x, v.m_position.y);
        for (uint32_t l = 0; l < layerCount; l++) {
            const Cover& c = covers[l];
            // Bare layers carry fallen snow: shaped by its look, no painted depth (the pixel shader's cover share for
            // them follows the fallen depth at draw time).
            const bool fall = fallenOn && c.snowable && c.depth <= 0.0f;
            if ((c.depth <= 0.0f && !fall) || w[l] <= 0.0f) continue;
            const Cover& look = fall ? fallenLook : c;
            const float slope = SlopeFactor(look, v.m_normal);
            // the layer's share that stays dry (and, for snow, cool) enough for its cover
            const float wl = w[l] * LayerDry(c, fall, dry) * (fall || c.snow ? warm : 1.0f);
            covered += w[l];
            share += wl;
            slopeShare += wl * slope;
            if (!fall) {
                paintedShare += wl * slope;
                depth += wl * slope * c.depth * std::max(0.0f, 1.0f + c.drift * drift);
                zOffset += wl * slope * c.zOffset;
            } else {
                fallShare += wl * slope;
            }
            const float t = look.tintStrength;
            tint[0] += wl * (1.0f + (((look.tintColor >> 16) & 255) / 255.0f - 1.0f) * t);
            tint[1] += wl * (1.0f + (((look.tintColor >> 8) & 255) / 255.0f - 1.0f) * t);
            tint[2] += wl * (1.0f + ((look.tintColor & 255) / 255.0f - 1.0f) * t);
        }
        const float slope = share > 0.0f ? slopeShare / share : 0.0f;
        // Far away the cover's edges follow the alpha maps per pixel, much finer than these vertices 4 yd apart, so a
        // cover of its full depth would end in a lip hanging over the ground: the mesh is lifted only where most of the
        // ground is covered (FarLift tapers it), by the cover's depth as it is now (melting, fallen snow). What is built
        // here is a floor of 2 cm under any cover, so it never fights the terrain for depth, and the numbers the lift
        // is made of (LiftVertices).
        const float full = share > 0.0f ? std::max(0.0f, depth * depthScale + zOffset) : 0.0f;
        const float lift = share > 0.0f ? 0.02f * std::min(1.0f, share * 4.0f) : 0.0f;
        v.m_position.z += lift;
        ChunkRec& vrec = *recs[i / kChunkVerts];
        vrec.lift[i % kChunkVerts] = lift;
        vrec.fshare[i % kChunkVerts] = Clamp(fallShare, 0.0f, 1.0f);
        vrec.pfull[i % kChunkVerts] = std::min(full, kFarPainted);
        vrec.pshare[i % kChunkVerts] = Clamp(paintedShare, 0.0f, 1.0f);
        vPaintedFull[i] = std::min(full, kFarPainted);
        vPaintedShare[i] = Clamp(paintedShare, 0.0f, 1.0f);
        vFallFrac[i] = share > 0.0f ? Clamp(fallShare / share, 0.0f, 1.0f) : 0.0f;
        fullLift[i] = full;
        CImVector color;
        for (int32_t c = 0; c < 3; c++) tint[c] = share > 0.0f ? tint[c] / share : 1.0f;
        color.r = static_cast<uint8_t>(std::min(tint[0], 1.0f) * 255.0f + 0.5f);
        color.g = static_cast<uint8_t>(std::min(tint[1], 1.0f) * 255.0f + 0.5f);
        color.b = static_cast<uint8_t>(std::min(tint[2], 1.0f) * 255.0f + 0.5f);
        // the vertex alpha is the slope factor, times what the water leaves of the cover (the shader's share scales by it)
        color.a = static_cast<uint8_t>(slope * (covered > 0.0f ? share / covered : 1.0f) * 255.0f + 0.5f);
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
    e.painted = any;
    for (uint32_t l = 0; l < 4; l++) e.snowClass[l] = l < layerCount && covers[l].depth > 0.0f && covers[l].snow ? covers[l].meltClass : -1;
    for (uint32_t l = 0; l < 4; l++) e.fallLayer[l] = l < layerCount && fallenOn && covers[l].snowable && covers[l].depth <= 0.0f ? 1.0f : 0.0f;
    if (!any) {
        e.snow = true;
        e.breakup = fallenLook.breakup;
        e.texture = CoverTexture(fallenLook.texture);
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
            rec.meltClass = c.meltClass;
        }
    }

    if (e.slot < 0) e.slot = TakeSlot(rc, now);
    if (e.slot < 0) {
        s_starved++;
        return false;
    }
    // The built vertices and what their lift is made of, kept so the mesh can be lifted again as the depth changes. Built
    // lifted by the current depths, so a chunk streaming in is never lower than its neighbours.
    e.base.assign(vertices, vertices + count);
    e.fshare.resize(count);
    e.pfull.assign(vPaintedFull, vPaintedFull + count);
    e.pshare.assign(vPaintedShare, vPaintedShare + count);
    e.fallFrac.assign(vFallFrac, vFallFrac + count);
    e.open.assign(count, 1.0f);
    for (uint32_t i = 0; i < count; i++) e.fshare[i] = recs[i / kChunkVerts]->fshare[i % kChunkVerts];
    e.paintClass = -1;
    for (uint32_t l = 0; l < layerCount; l++) {
        if (covers[l].depth > 0.0f && covers[l].snow) e.paintClass = covers[l].meltClass;
    }
    e.roofDirty = false;
    e.roofCursor = 0;
    e.roofTime = 0;
    e.fallApplied = s_farFall;
    e.keepApplied = QuantizedKeep(KeepNow(e.paintClass));
    LiftVertices(e, vertices, e.fallApplied, e.keepApplied);
    CGxBuf* buf = s_slots[e.slot].buf;
    char* dst = g_theGxDevicePtr->BufLock(buf);
    if (!dst) return false;
    std::memcpy(dst, vertices, count * sizeof(CGxVertexPNC));
    g_theGxDevicePtr->BufUnlock(buf, 0);
    buf->m_hasData = 1;
    return true;
}

// The slot's vertices lifted again by the current fallen depth and painted share.
void ApplyFar(Entry& e, float fall, float keep) {
    if (e.base.empty() || e.slot < 0) return;
    static std::vector<CGxVertexPNC> s_scratch;
    s_scratch.resize(e.base.size());
    LiftVertices(e, s_scratch.data(), fall, keep);
    CGxBuf* buf = s_slots[e.slot].buf;
    char* dst = g_theGxDevicePtr->BufLock(buf);
    if (!dst) return;
    std::memcpy(dst, s_scratch.data(), s_scratch.size() * sizeof(CGxVertexPNC));
    g_theGxDevicePtr->BufUnlock(buf, 0);
    buf->m_hasData = 1;
    e.fallApplied = fall;
    e.keepApplied = keep;
    e.roofDirty = false;
}

bool Fresh(const Entry& e, CMapRenderChunk* rc) {
    return e.generation == s_generation && e.chunks[0] == rc->m_mapChunks[0] && e.chunks[1] == rc->m_mapChunks[1] &&
           e.header == (rc->m_mapChunks[0] ? rc->m_mapChunks[0]->m_header : nullptr);
}

// ---- near: a fine patch around the player, with trenches ---------------------------------------------------------

float kCell = 0.25f;                     // yards per cell: surfaceCoverNearRange / kCells (0.25 = 48 yd across)
CVar* s_nearRange = nullptr;
constexpr int32_t kCells = 192;          // 48 yd across
constexpr int32_t kVerts = kCells + 1;
constexpr int32_t kRecentre = 8;         // cells the player may move from the centre before it follows
constexpr int32_t kBlend = 48;           // cells along the border that blend into the far mesh (deep snow meets the far skin)

struct Cell {
    float terrain = 0.0f;
    float coarse = 0.0f;   // the far mesh here (the border blends into it), with fallen snow's lift as of this frame
    float coarseBase = 0.0f;  // ...without it
    float farShare = 0.0f; // the far mesh's fallen-snow share here
    float farPFull = 0.0f, farPShare = 0.0f;  // ...and its painted cover's depth and share
    float lift = 0.0f;     // the cover's depth with drifts and lumps, untouched
    float share = 0.0f;    // how much of the ground here is covered, slope included (0..1)
    float pShare = 0.0f;   // painted cover: coverage before its edge, and what Edge needs to redo it as it melts
    float pNoise = 0.0f, pBreakup = 0.0f, pDepth = 0.0f;
    int32_t meltClass = -1;  // the painted cover is snow of this melt class (it thins with the weather); -1 not snow
    float fAlpha = 0.0f;   // fallen snow: the edge's alpha here, if the zone has some
    float fRise = 0.0f;    // fallen snow: the share of its depth standing here (edges slope to the ground)
    float fMul = 1.0f;     // fallen snow: drifts and lumps as a multiple of its depth
    float eShare = 0.0f;   // this frame: share with fallen snow
    float eLift = 0.0f;    // this frame: the cover's height with fallen snow
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

// The cover's edge as the chunks' pixel shader draws it: breakup b (0..1) and noise n (0..1) move it, a soft band; and how
// far the cover has risen to its depth there (it slopes down to the ground through the band).
void Edge(float coverage, float b, float n, float& alpha, float& rise) {
    const float soft = 0.35f + (0.08f - 0.35f) * b;
    const float e = coverage * (1.0f + b) - b * n;
    alpha = Clamp((e - 0.5f) / soft + 0.5f, 0.0f, 1.0f);
    rise = Clamp((e - 0.5f + 0.5f * soft) / (0.5f + 0.5f * soft), 0.0f, 1.0f);
    rise = rise * rise * (3.0f - 2.0f * rise);
}

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
    cell.coarse = cell.coarseBase = cell.terrain + OverTriangles(chunk, rec.lift, x, y);
    cell.farShare = OverTriangles(chunk, rec.fshare, x, y);
    cell.farPFull = OverTriangles(chunk, rec.pfull, x, y);
    cell.farPShare = OverTriangles(chunk, rec.pshare, x, y);
    if (!rec.any || rec.share.empty()) return;

    // The edge as the chunks' pixel shader draws it (breakup noise moving it, a soft band), and the cover sloping down
    // to the ground through that band, so no edge hangs over the ground.
    const float b = std::min(1.0f, rec.breakup * OptionCVars::Float(s_breakup, 0.0f, 3.0f, 1.0f));
    const float n = 0.5f + 0.5f * (Noise(x, y, 0.35f) * 0.65f + Noise(x + 17.0f, y + 17.0f, 0.945f) * 0.35f);
    auto edge = [&](float coverage, float& alpha, float& rise) { Edge(coverage, b, n, alpha, rise); };

    const float node = kChunkSize / (kNodes - 1);
    const float fa = Clamp((ChunkTop(chunk) - x) / node, 0.0f, kNodes - 1.0f), fb = Clamp((ChunkLeft(chunk) - y) / node, 0.0f, kNodes - 1.0f);
    const int32_t a0 = std::min(static_cast<int32_t>(fa), kNodes - 2), b0 = std::min(static_cast<int32_t>(fb), kNodes - 2);
    const float ta = fa - a0, tb = fb - b0;
    auto at = [&](const std::vector<uint8_t>& v) {
        const float p = v[a0 * kNodes + b0] + (v[a0 * kNodes + b0 + 1] - v[a0 * kNodes + b0]) * tb;
        const float q = v[(a0 + 1) * kNodes + b0] + (v[(a0 + 1) * kNodes + b0 + 1] - v[(a0 + 1) * kNodes + b0]) * tb;
        return p + (q - p) * ta;
    };
    cell.relax = rec.relax;
    cell.stiffness = rec.stiffness;
    cell.rimShare = rec.rim;
    for (int32_t c = 0; c < 3; c++) cell.tint[c] = static_cast<uint8_t>(Clamp(rec.tint[c], 0.0f, 1.0f) * 255.0f + 0.5f);
    if (!rec.fall.empty()) {
        const float fall = at(rec.fall) / 255.0f;
        if (fall > 0.002f) {
            edge(fall, cell.fAlpha, cell.fRise);
            cell.fMul = std::max(0.0f, 1.0f + rec.drift * Drift(x, y) + 0.08f * Noise(x, y, 0.55f));
        }
    }
    const float share = at(rec.share) / 255.0f * at(rec.slope) / 255.0f;
    if (share <= 0.002f) return;
    float alpha, rise;
    edge(share, alpha, rise);
    if (alpha <= 0.0f) return;
    const float depth = at(rec.depth) / 100.0f * OptionCVars::Float(s_depth, 0.0f, 4.0f, 1.0f) * std::max(0.0f, 1.0f + rec.drift * Drift(x, y));
    // Soft swells a few centimetres high, two yards or so across. Nothing finer: Lumenveil's lamps light the snow by
    // the depth buffer's slope from pixel to pixel, so small lumps would show every triangle.
    const float lumps = depth * 0.08f * Noise(x, y, 0.55f);
    cell.share = alpha;
    cell.lift = std::max(0.0f, depth + lumps) * rise + 0.02f * alpha;
    cell.pShare = share;
    cell.pNoise = n;
    cell.pBreakup = b;
    cell.pDepth = std::max(0.0f, depth + lumps);
    cell.meltClass = rec.snow ? rec.meltClass : -1;
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
    if (under.eShare <= 0.0f || pos.z > under.terrain + under.eLift + 0.6f || pos.z < under.terrain - 1.0f) return;
    for (int32_t i = i0; i <= i1; i++) {
        for (int32_t j = j0; j <= j1; j++) {
            Cell& cell = s_cells[i * kVerts + j];
            const float dx = (s_oi + i) * kCell - pos.x, dy = (s_oj + j) * kCell - pos.y;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d >= reach) continue;
            const float t = Clamp((radius - d) / (0.7f * radius), 0.0f, 1.0f);
            cell.press = std::max(cell.press, t * t * (3.0f - 2.0f * t));
            if (d > 0.8f * radius) cell.rim = std::max(cell.rim, std::sin(3.14159265f * (d - 0.8f * radius) / reach) * (1.0f - cell.press));
        }
    }
}

float s_trenchDeep = 1.0f;  // trench width multiplier from the fallen snow's depth (1 up to 25 cm, 2.5 at 62 cm)

struct Stamper {
    C3Vector pos;
    float radius;
};
std::vector<Stamper> s_stampers;
CGObject_C* s_self = nullptr;

float StampRadius(CGUnit_C* unit) {
    float radius = (unit->m_unit ? unit->m_unit->m_unitFieldBoundingradius : 0.4f) * OptionCVars::Float(s_trenchScale, 0.2f, 4.0f, 1.0f);
    if (unit->IsMounted()) radius *= 1.6f;
    // Deep snow: a trench as wide as the snow is deep, so the walls don't stand over the unit's legs.
    return Clamp(radius * s_trenchDeep, 0.25f, 4.0f);
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

// ---- fallen snow --------------------------------------------------------------------------------------------------
// Rates and rules of seyris-living-azeroth's env/Snow.hpp (GPL-3.0): it builds up while it snows on ground below 0 degC,
// melts by degree-days above 0 degC, and sublimates while it isn't snowing. Kept per zone (the top of the area chain).
// The player's zone sees the real weather; zones left behind evolve on their own climate (no snowfall: the weather there
// is unknown). Entering a zone not seen for 10 minutes fast-forwards it. A zone's snow is one depth for the whole zone.

constexpr float kSnowfallPerSecond = 0.2f / 3600.0f;   // yd per second at full intensity
constexpr float kMaxFallen = 0.6f;                      // yd
constexpr float kMeltPerDegreeSecond = 0.0164f / 86400.0f;
constexpr float kSublimationHalfLife = 8.0f * 3600.0f;  // seconds, calm average-humidity night

// Painted snow's melting (env/Snow.hpp's MeltCap and StepKeep): its share `keep` follows a cap that falls as the air
// warms (all of it at or below 0 degC, down to kDefaultKeptShare at 15 degC, gone at kDefaultGoneTemperature) by
// degree-days, and grows back in the cold, faster while it snows.
// The cap's two numbers come from the cover's SurfaceCover row (MeltKeptShare, MeltGoneTemperature; the donor's 0.6 and
// 25 degC where unset). Magma's heat melts the cover around it at once (ComputeHeat), not through a state.
constexpr float kRegrowPerSecond = 0.05f / 3600.0f, kRegrowSnowingBoost = 4.0f, kTypicalPaintedDepth = 0.35f;
constexpr float kMildEnd = 15.0f;

float MeltCap(float t, float kept, float gone) {
    if (t <= 0.0f) return 1.0f;
    if (t >= gone) return 0.0f;
    const float mildEnd = std::min(gone, kMildEnd);
    if (t < mildEnd) return 1.0f + (kept - 1.0f) * t / kMildEnd;
    const float atMildEnd = 1.0f + (kept - 1.0f) * mildEnd / kMildEnd;
    return atMildEnd * (gone - t) / (gone - mildEnd);
}

struct Zone {
    float depth = 0.0f;    // fallen snow, yd
    std::vector<float> keep;        // painted snow's share left, per melt class
    std::vector<uint8_t> started;   // ...and whether that has taken its first value
    double lastSeen = 0.0; // seconds of play the player was last in it
    float catchUp = 0.0f;  // seconds left of running its rules fast, after entering it
};
std::map<uint64_t, Zone> s_zones;    // by map and zone
std::vector<float> s_keepNow;        // the player's zone's painted share per melt class, as of this frame
float KeepNow(int32_t meltClass) { return meltClass >= 0 && meltClass < static_cast<int32_t>(s_keepNow.size()) ? s_keepNow[meltClass] : 1.0f; }
float s_fallenClock = 0.0f;
uint32_t s_pressed = 0;              // cells pressed by a trench (status line)
float s_fallenTemperature = 0.0f;

// One zone over `step` seconds of its own time: fallen snow builds up while it snows below 0 degC, sublimates while it
// doesn't, melts above 0 degC; painted snow's share follows its melt cap.
void StepZone(Zone& zone, float step, float air, float humidity, float wind, float snowing) {
    float& depth = zone.depth;
    if (OptionCVars::Bool(s_fallen, true)) {
        if (snowing > 0.0f && air <= 0.0f) {
            depth = std::min(kMaxFallen, depth + kSnowfallPerSecond * snowing * step);
        } else if (snowing <= 0.0f && depth > 0.0f) {
            const float speed = (1.0f + 2.0f * wind) * (1.3f - 0.6f * humidity) * 1.5f;
            depth *= std::exp(-0.6931472f / kSublimationHalfLife * speed * step);
        }
        if (air > 0.0f && depth > 0.0f) depth -= std::min(depth, kMeltPerDegreeSecond * air * step);
        if (depth < 0.001f) depth = 0.0f;
    }
    zone.keep.resize(s_meltClasses.size(), 1.0f);
    zone.started.resize(s_meltClasses.size(), 0);
    for (size_t c = 0; c < s_meltClasses.size(); c++) {
        float& keep = zone.keep[c];
        if (!OptionCVars::Bool(s_melt, true)) {
            keep = 1.0f;
            continue;
        }
        const float cap = MeltCap(air, s_meltClasses[c].kept, s_meltClasses[c].gone);
        if (!zone.started[c]) {
            keep = cap;  // a zone first seen: as if the season had been running
            zone.started[c] = 1;
        } else if (keep > cap) {
            keep = std::max(cap, keep - kMeltPerDegreeSecond * std::max(air, 0.0f) / kTypicalPaintedDepth * step);
        } else {
            keep = std::min(cap, keep + kRegrowPerSecond * (1.0f + kRegrowSnowingBoost * snowing) / kTypicalPaintedDepth * step);
        }
    }
}

constexpr double kStaleSeconds = 600.0;      // a zone not visited for this long is fast-forwarded when entered
constexpr float kCatchUpSeconds = 30.0f, kCatchUpSpeed = 60.0f;  // ...for 30 s at 60 times (half an hour of its weather)
double s_clock = 0.0;                        // seconds of play
float s_otherClock = 0.0f;
uint64_t s_playerKey = 0;

void StepFallen(float dt, const CMapChunk* chunk) {
    if (!chunk) {
        s_fallenDepth = 0.0f;
        s_keepNow.clear();
        return;
    }
    s_clock += dt;
    uint32_t chain[8];
    const int32_t n = LivingWind::AreaChain(static_cast<uint32_t>(chunk->m_areaId), chain, 8);
    const uint64_t map = static_cast<uint64_t>(static_cast<uint32_t>(s_mapId)) << 32;
    const uint64_t key = map | (n > 0 ? chain[n - 1] : 0u);
    auto found = s_zones.find(key);
    if (key != s_playerKey) {
        // entering: a zone never seen, or not seen for a while, runs its rules fast for a moment
        if (found == s_zones.end() || s_clock - found->second.lastSeen > kStaleSeconds) s_zones[key].catchUp = kCatchUpSeconds;
        s_playerKey = key;
        s_fallenClock = 0.0f;
    }
    Zone& zone = s_zones[key];
    zone.lastSeen = s_clock;
    s_fallenClock += dt;
    s_otherClock += dt;
    const float rate = OptionCVars::Float(s_fallenRate, 0.0f, 1000.0f, 1.0f);
    if (s_fallenClock >= 0.25f) {  // the climate query costs a few dozen lookups
        const float elapsed = s_fallenClock;
        s_fallenClock = 0.0f;
        LivingWind::GroundState g;
        if (LivingWind::GroundConditions(g)) {
            s_fallenTemperature = g.temperature;
            const bool catching = zone.catchUp > 0.0f;
            // the air's temperature: a zone's snow doesn't melt away because the player stands near lava
            StepZone(zone, elapsed * rate * (catching ? kCatchUpSpeed : 1.0f), g.air, g.humidity, g.wind, g.weatherType == 2 ? g.weather : 0.0f);
            zone.catchUp = std::max(0.0f, zone.catchUp - elapsed);
        }
    }
    // The zones left behind keep going on their own climate: no weather known there, so no snowfall.
    if (s_otherClock >= 1.0f) {
        const float elapsed = s_otherClock;
        s_otherClock = 0.0f;
        for (auto& entry : s_zones) {
            if (entry.first == key || (entry.first & ~0xFFFFFFFFull) != map) continue;
            float air, humidity;
            if (LivingWind::ZoneAir(static_cast<uint32_t>(entry.first), air, humidity)) StepZone(entry.second, elapsed * rate, air, humidity, 0.0f, 0.0f);
        }
    }
    s_fallenDepth = OptionCVars::Bool(s_fallen, true) ? zone.depth : 0.0f;
    s_keepNow = zone.keep;
}

// Roofs: LivingWind's roof test (a ray 40 yd up, cached per 4-yard cell) on a 4-yard grid over the near patch, blended
// between its nodes so a roof's shadow on the snow has no blocky edge. Refreshed every frame from the cache.
// shortcut: near patch only; the far mesh ignores roofs. Upgrade: bake the roof test into the chunk records.
constexpr float kRoofGrid = 4.0f;
std::vector<float> s_open;
int32_t s_openI0 = 0, s_openJ0 = 0, s_openN = 0;  // world 4-yard index of the first node, nodes per side

void UpdateOpen() {
    s_openI0 = static_cast<int32_t>(std::floor(s_oi * kCell / kRoofGrid));
    s_openJ0 = static_cast<int32_t>(std::floor(s_oj * kCell / kRoofGrid));
    s_openN = static_cast<int32_t>(std::ceil(kCells * kCell / kRoofGrid)) + 2;
    if (static_cast<int32_t>(s_open.size()) != s_openN * s_openN) s_open.assign(s_openN * s_openN, 1.0f);
    for (int32_t a = 0; a < s_openN; a++) {
        for (int32_t b = 0; b < s_openN; b++) {
            const float x = (s_openI0 + a) * kRoofGrid, y = (s_openJ0 + b) * kRoofGrid;
            const int32_t ci = ClampI(static_cast<int32_t>(std::lround(x / kCell)) - s_oi, 0, kVerts - 1);
            const int32_t cj = ClampI(static_cast<int32_t>(std::lround(y / kCell)) - s_oj, 0, kVerts - 1);
            const Cell& under = s_cells[ci * kVerts + cj];
            const float pos[3] = { x, y, under.state == 1 ? under.terrain : s_nearZ };
            const float open = LivingWind::RoofOpenness(pos);
            if (open >= 0.0f) s_open[a * s_openN + b] = open;  // negative: this frame's rays are spent, keep the last
        }
    }
}

float OpenAt(float x, float y) {
    if (s_open.empty()) return 1.0f;
    const float fx = Clamp(x / kRoofGrid - s_openI0, 0.0f, s_openN - 1.001f), fy = Clamp(y / kRoofGrid - s_openJ0, 0.0f, s_openN - 1.001f);
    const int32_t a = static_cast<int32_t>(fx), b = static_cast<int32_t>(fy);
    const float ta = fx - a, tb = fy - b;
    const float p = s_open[a * s_openN + b] + (s_open[a * s_openN + b + 1] - s_open[a * s_openN + b]) * tb;
    const float q = s_open[(a + 1) * s_openN + b] + (s_open[(a + 1) * s_openN + b + 1] - s_open[(a + 1) * s_openN + b]) * tb;
    return p + (q - p) * ta;
}

void ShowInfo() {
    static ULONGLONG s_last = 0;
    const ULONGLONG now = GetTickCount64();
    if (!s_info || now - s_last < 500) return;
    s_last = now;
    char text[1300];
    std::snprintf(text, sizeof text, "COST in 0.5 s (calls/ms/worst): build %u/%.1f/%.1f, lift %u/%.1f/%.1f, roof %u/%.1f/%.1f, update %u/%.1f/%.1f, near %u/%.1f/%.1f; shaders: %s; %u cover draws, %u chunk(s) found no slot (%u slots); near patch %s (%.0f yd across, %u cells pressed); fallen snow %.1f cm at %.1f C, painted snow %.0f%% kept; %s",
                  s_perfBuild.n, s_perfBuild.ms, s_perfBuild.worst, s_perfApply.n, s_perfApply.ms, s_perfApply.worst, s_perfRoof.n, s_perfRoof.ms, s_perfRoof.worst,
                  s_perfUpdate.n, s_perfUpdate.ms, s_perfUpdate.worst, s_perfNear.n, s_perfNear.ms, s_perfNear.worst,
                  HD::SurfaceCoverShaderInfo(), s_draws, s_starved, static_cast<uint32_t>(s_slots.size()),
                  !s_haveNear ? "off" : s_nearAny ? "covered" : "bare", kCell * kCells, s_pressed, s_fallenDepth * 91.44f, s_fallenTemperature, KeepNow(0) * 100.0f,
                  s_loaded ? s_tableStatus : "tables not loaded yet");
    s_draws = 0;
    s_starved = 0;
    s_perfBuild = s_perfApply = s_perfRoof = s_perfUpdate = s_perfNear = Perf();
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
    s_nearRange = reg("surfaceCoverNearRange", "Surface cover: yards across of the fine patch (48 = 0.25 yd cells; wider is coarser and costs more)", "48", false);
    s_trenches = reg("surfaceCoverTrenches", "Surface cover: you and the units near you press trenches into it", "1", false);
    s_trenchScale = reg("surfaceCoverTrenchScale", "Surface cover: trench width as a multiple of each unit's size", "1", false);
    s_fallen = reg("surfaceCoverFallen", "Surface cover: snow falls and lies on bare ground in snowstorms, and melts (near patch)", "1", true);
    s_fallenRate = reg("surfaceCoverFallenRate", "Surface cover: fallen snow builds up and melts this many times faster (1 = 0.2 yd an hour)", "1", false);
    s_melt = reg("surfaceCoverMelt", "Surface cover: painted snow thins in warm weather and grows back in the cold", "1", false);
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
    PerfScope timing(s_perfUpdate);
    s_frame++;
    ShowInfo();
    C3Vector player;
    CGObject_C* self = nullptr;
    if (!OptionCVars::Bool(s_enabled, true) || !OptionCVars::Bool(s_near, true) || !CameraTarget(&player, &self)) {
        s_haveNear = false;
        return;
    }
    s_nearZ = player.z;
    // A new range changes the cell size: the grid is sampled again.
    const float cellSize = OptionCVars::Float(s_nearRange, 24.0f, 192.0f, 48.0f) / kCells;
    if (std::fabs(cellSize - kCell) > 1e-4f) {
        kCell = cellSize;
        s_haveNear = false;
    }
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
    StepFallen(dt, chunk);
    // Fallen snow shows from 2 cm; its edge fades in over the first 2 cm so it never pops.
    const float fallen = s_fallenDepth * OptionCVars::Float(s_depth, 0.0f, 4.0f, 1.0f);
    const float presence = Clamp(s_fallenDepth / 0.02f, 0.0f, 1.0f);
    if (presence > 0.0f) UpdateOpen();
    s_trenchDeep = Clamp(fallen / 0.25f, 1.0f, 2.5f);
    s_farFall = std::floor(std::min(fallen, kFarFall) / 0.02f + 0.5f) * 0.02f;
    s_refreshBudget = 96;
    s_roofBudget = 400;
    s_waitBudget = 24;
    s_nearAny = false;
    for (int32_t index = 0; index < kVerts * kVerts; index++) {
        Cell& cell = s_cells[index];
        if (cell.state == 1) {
            const float pk = QuantizedKeep(cell.meltClass >= 0 ? KeepNow(cell.meltClass) : 1.0f);
            cell.coarse = cell.coarseBase + std::max(FarFall(s_farFall, cell.farShare), FarFall(cell.farPFull * pk, cell.farPShare));
        }
        // none under roofs and overhangs (fallen snow only: painted snow is authored)
        const float fall = presence > 0.0f && cell.fAlpha > 0.0f ? presence * OpenAt((s_oi + index / kVerts) * kCell, (s_oj + index % kVerts) * kCell) : 0.0f;
        // painted snow thins and retreats as it melts: its edge is worked out again from its share kept
        float painted = cell.share, paintedLift = cell.lift;
        const float keep = cell.meltClass >= 0 ? KeepNow(cell.meltClass) : 1.0f;
        if (cell.share > 0.0f && keep < 0.999f) {
            float rise;
            Edge(cell.pShare * keep, cell.pBreakup, cell.pNoise, painted, rise);
            paintedLift = painted > 0.0f ? cell.pDepth * keep * rise + 0.02f * painted : 0.0f;
        }
        cell.eShare = std::max(painted, cell.fAlpha * fall);
        cell.eLift = std::max(painted > 0.0f ? paintedLift : 0.0f, fall > 0.0f ? fallen * cell.fMul * cell.fRise * (fall / presence) + 0.02f * cell.fAlpha * fall : 0.0f);
        s_nearAny = s_nearAny || cell.eShare > 0.0f;
        if (cell.press > 0.0f) cell.press = std::max(0.0f, cell.press - dt / cell.relax);
        if (cell.rim > 0.0f) cell.rim = std::max(0.0f, cell.rim - dt / cell.relax);
    }
    if (!s_nearAny || !OptionCVars::Bool(s_trenches, true)) return;

    s_stampers.clear();
    s_self = self;
    if (self->m_obj->m_type & TYPEMASK_UNIT) s_stampers.push_back(Stamper{ player, StampRadius(static_cast<CGUnit_C*>(self)) });
    ClntObjMgrEnumVisibleObjects(CollectUnit, nullptr);
    for (const Stamper& s : s_stampers) Stamp(s.pos, s.radius);
    s_pressed = 0;
    for (const Cell& cell : s_cells) s_pressed += cell.press > 0.0f;
}

// The top of the near patch's cover at (x, y) at rest (trenches and the blur ignored), blended between its cells; false
// where it has none.
static bool CoverTop(float x, float y, float& top) {
    if (!s_haveNear || !s_nearAny || !OptionCVars::Bool(s_enabled, true)) return false;
    const float fi = x / kCell - s_oi, fj = y / kCell - s_oj;
    if (fi < 0.0f || fj < 0.0f || fi >= kCells || fj >= kCells) return false;
    const int32_t i = static_cast<int32_t>(fi), j = static_cast<int32_t>(fj);
    const float ti = fi - i, tj = fj - j;
    float value = 0.0f;
    for (int32_t k = 0; k < 4; k++) {
        const Cell& cell = s_cells[(i + (k & 1)) * kVerts + j + (k >> 1)];
        const float weight = ((k & 1) ? ti : 1.0f - ti) * ((k >> 1) ? tj : 1.0f - tj);
        if (cell.state != 1) return false;
        value += weight * (cell.terrain + (cell.eShare > 0.0f ? cell.eLift : 0.0f));
    }
    top = value;
    return true;
}

float SurfaceCover::LimitCameraDistance(const float pivot[3], const float forward[3], float distance) {
    constexpr float kClear = 0.3f;  // yd the eye keeps above the cover
    constexpr float kStep = 0.25f;
    if (distance <= 0.01f) return distance;
    float top;
    if (CoverTop(pivot[0], pivot[1], top) && pivot[2] < top + kClear) return distance;  // already under it: no free spot
    auto blocked = [&](float d) {
        float z;
        return CoverTop(pivot[0] - forward[0] * d, pivot[1] - forward[1] * d, z) && pivot[2] - forward[2] * d < z + kClear;
    };
    float free = 0.0f;
    float stop = -1.0f;
    for (float d = kStep; d < distance + kStep; d += kStep) {
        const float at = std::min(d, distance);
        if (blocked(at)) {
            stop = at;
            break;
        }
        free = at;
    }
    if (stop < 0.0f) return distance;
    for (int32_t k = 0; k < 8; k++) {
        const float mid = 0.5f * (free + stop);
        (blocked(mid) ? stop : free) = mid;
    }
    return free;
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
    if (!stale && e.waitFall && s_fallenDepth > 0.0f && s_waitBudget > 0) {
        s_waitBudget--;
        stale = true;
    }
    if (stale) {
        e.chunks[0] = rc->m_mapChunks[0];
        e.chunks[1] = rc->m_mapChunks[1];
        e.header = rc->m_mapChunks[0]->m_header;
        e.generation = s_generation;
        {
            PerfScope timing(s_perfBuild);
            e.cover = Build(rc, e, now);
        }
        if (!e.cover) FreeSlot(e);
    }
    if (!e.cover) return;
    s_slots[e.slot].lastUsed = now;
    // Fallen snow on the bare layers grows in over its first 2 cm; none lying: nothing to draw where nothing is painted.
    const float presence = Clamp(s_fallenDepth / 0.02f, 0.0f, 1.0f);
    if (!e.painted && presence <= 0.0f) return;
    // Roofs: a few of the chunk's vertices a frame get their roof test, from LivingWind's cache of it (a ray 40 yd up per
    // 4-yard cell); the fallen snow thins under them. Looked at again every 30 seconds, as buildings stream in.
    if (presence > 0.0f && !e.fallFrac.empty()) {
        PerfScope timing(s_perfRoof);
        if (e.roofCursor >= e.base.size() && now - e.roofTime > 30000) e.roofCursor = 0;
        while (e.roofCursor < e.base.size() && s_roofBudget > 0) {
            const CGxVertexPNC& v = e.base[e.roofCursor];
            if (e.fallFrac[e.roofCursor] > 0.0f) {
                const float pos[3] = { v.m_position.x, v.m_position.y, v.m_position.z };
                const float open = LivingWind::RoofOpenness(pos);
                if (open < 0.0f) {  // this frame's rays are spent: later
                    s_roofBudget = 0;
                    break;
                }
                s_roofBudget--;
                if (std::fabs(open - e.open[e.roofCursor]) > 0.01f) {
                    e.open[e.roofCursor] = open;
                    e.roofDirty = true;
                }
            }
            if (++e.roofCursor >= e.base.size()) e.roofTime = now;
        }
    }
    // The mesh follows the fallen snow's depth, the painted snow's melting and the roofs: a few chunks a frame.
    const float keepQ = QuantizedKeep(KeepNow(e.paintClass));
    if (!e.base.empty() && s_refreshBudget > 0 &&
        (std::fabs(e.fallApplied - s_farFall) > 0.001f || std::fabs(e.keepApplied - keepQ) > 0.001f || e.roofDirty)) {
        s_refreshBudget--;
        PerfScope timing(s_perfApply);
        ApplyFar(e, s_farFall, keepQ);
    }
    float layerCover[4];
    for (int32_t l = 0; l < 4; l++) layerCover[l] = std::max(e.layerCover[l] * KeepNow(e.snowClass[l]), e.fallLayer[l] * presence);

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
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 33, layerCover, 1);
    LookConstants(e.breakup, (CMap::header.m_flags & 0x4) != 0, false, e.snow);

    g_theGxDevicePtr->Draw(&rc->m_batch, 1);
    g_theGxDevicePtr->RsPop();
    s_draws++;
}

void SurfaceCover::DrawNear() {
    PerfScope timing(s_perfNear);
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
                // the diagonal alternates from quad to quad, so no grid of like triangles shows in the shading
                const uint16_t a[6] = { v00, v10, v11, v00, v11, v01 };
                const uint16_t b[6] = { v00, v10, v01, v10, v11, v01 };
                const uint16_t* quad = (i + j) & 1 ? b : a;
                std::memcpy(index, quad, 6 * sizeof(uint16_t));
                index += 6;
            }
        }
        g_theGxDevicePtr->BufUnlock(s_nearIdx, 0);
        s_nearIdx->m_hasData = 1;
    }

    // Heights: the cover at rest smoothed over about a yard (snow settles: no step at its edges), then pressed by its
    // trenches and raised by their rims (their own profile is round already), blending into the far mesh at the
    // border.
    static std::vector<float> s_height, s_blur;
    s_height.resize(kVerts * kVerts);
    s_blur.resize(kVerts * kVerts);
    for (int32_t i = 0; i < kVerts; i++) {
        for (int32_t j = 0; j < kVerts; j++) {
            const Cell& cell = s_cells[i * kVerts + j];
            const float edge = Clamp((std::min(std::min(i, kCells - i), std::min(j, kCells - j)) - 1.0f) / (kBlend - 1.0f), 0.0f, 1.0f);
            s_height[i * kVerts + j] = cell.eShare > 0.0f ? cell.eLift : 0.0f;
        }
    }
    // two passes of a 5-cell box, each along both axes: close to a gaussian of about 0.7 yd
    for (int32_t pass = 0; pass < 4; pass++) {
        const bool alongJ = pass & 1;
        const std::vector<float>& src = pass & 1 ? s_blur : s_height;
        std::vector<float>& dst = pass & 1 ? s_height : s_blur;
        for (int32_t i = 0; i < kVerts; i++) {
            for (int32_t j = 0; j < kVerts; j++) {
                float sum = 0.0f;
                for (int32_t k = -2; k <= 2; k++) {
                    const int32_t ii = alongJ ? i : ClampI(i + k, 0, kCells), jj = alongJ ? ClampI(j + k, 0, kCells) : j;
                    sum += src[ii * kVerts + jj];
                }
                dst[i * kVerts + j] = sum * 0.2f;
            }
        }
    }
    for (int32_t i = 0; i < kVerts; i++) {
        for (int32_t j = 0; j < kVerts; j++) {
            const Cell& cell = s_cells[i * kVerts + j];
            float edge = Clamp((std::min(std::min(i, kCells - i), std::min(j, kCells - j)) - 1.0f) / (kBlend - 1.0f), 0.0f, 1.0f);
            edge = edge * edge * (3.0f - 2.0f * edge);
            float above = s_height[i * kVerts + j];
            if (cell.eShare > 0.0f) {
                const float press = cell.press * edge * (1.0f - cell.stiffness);
                above = above * (1.0f - press) + cell.rim * edge * cell.rimShare * above;
            }
            const float fine = cell.terrain + above;
            s_height[i * kVerts + j] = cell.coarse + (fine - cell.coarse) * edge;
        }
    }

    // Trenches and rims are sharper than the resting snow: two more light passes (3-cell box, both axes) round them so
    // the lamps' depth-buffer normals don't pick out single cells.
    for (int32_t pass = 0; pass < 4; pass++) {
        const bool alongJ = pass & 1;
        const std::vector<float>& src = alongJ ? s_blur : s_height;
        std::vector<float>& dst = alongJ ? s_height : s_blur;
        for (int32_t i = 0; i < kVerts; i++) {
            for (int32_t j = 0; j < kVerts; j++) {
                float sum = 0.0f;
                for (int32_t k = -1; k <= 1; k++) {
                    const int32_t ii = alongJ ? i : ClampI(i + k, 0, kCells), jj = alongJ ? ClampI(j + k, 0, kCells) : j;
                    sum += src[ii * kVerts + jj];
                }
                dst[i * kVerts + j] = sum * (1.0f / 3.0f);
            }
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
            // The far mesh carries fallen snow too (and is clipped inside the patch), so the patch keeps its full alpha to
            // the border. Thin fallen snow is only a few centimetres: a trench through it is no visible dip, so it shows
            // the ground.
            const float border = Clamp((std::min(std::min(i, kCells - i), std::min(j, kCells - j)) - 1.0f) / (kBlend - 1.0f), 0.0f, 1.0f);
            float alpha = cell.eShare;
            if (cell.share <= 0.0f) alpha *= 1.0f - 0.95f * cell.press * border * (1.0f - cell.stiffness);
            color.a = static_cast<uint8_t>(Clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
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
