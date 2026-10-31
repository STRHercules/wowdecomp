// Not in the original binary. See GrassDensity.h. Row resolution follows seyris-living-azeroth's
// features/GrassDensity.cpp: places most specific first (the chunk's area chain, its map, Global), and within a place
// DoodadID + GroundEffectID, then DoodadID, then GroundEffectID, then every doodad; a negative field takes the next row's.
#include "WoW/Source/WorldClient/GrassDensity.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <tuple>
#include <vector>

#include "Engine/Source/ConsoleBase/IConsole.h"
#include "WoW/Source/Ui/OptionCVars.h"
#include "WoW/Source/WorldClient/CustomTable.h"
#include "WoW/Source/WorldClient/DetailDoodad.h"
#include "WoW/Source/WorldClient/LivingWind.h"

namespace {

enum Scope : uint32_t { kGlobal = 0, kMap = 1, kArea = 2 };

struct Row {
    uint32_t doodad = 0;
    uint32_t effect = 0;
    float value[3] = { -1.0f, -1.0f, -1.0f };   // multiplier, radius, spread
};

CVar* s_enabled = nullptr;
CVar* s_maxMultiplier = nullptr;
CVar* s_maxRadius = nullptr;
CVar* s_maxSpread = nullptr;
CVar* s_thin = nullptr;
CVar* s_thinStart = nullptr;
CVar* s_thinEnd = nullptr;
CVar* s_thinMinShare = nullptr;

bool s_loaded = false;
std::map<std::pair<uint32_t, uint32_t>, std::vector<Row>> s_rows;   // (scope, id) -> rows there
std::map<std::tuple<uint32_t, int32_t, uint32_t, uint32_t>, GrassDensity::Values> s_cache;

void Load() {
    s_loaded = true;
    CustomTable::Table table;
    char status[160];
    // ID, ScopeType, ScopeID, DoodadID, GroundEffectID, Multiplier, Radius, Spread
    if (CustomTable::LoadTable("DBFilesClient\\GroundEffectDoodadDensity.cdbc", 8, table, status, sizeof status)) {
        for (uint32_t r = 0; r < table.Rows(); ++r) {
            const uint32_t scope = table.U(r, 1);
            Row row;
            row.doodad = table.U(r, 3);
            row.effect = table.U(r, 4);
            for (int f = 0; f < 3; ++f) row.value[f] = table.F(r, 5 + f);
            s_rows[{ scope, scope == kGlobal ? 0 : table.U(r, 2) }].push_back(row);
        }
    }
    char line[220];
    std::snprintf(line, sizeof line, "Grass density: GroundEffectDoodadDensity.cdbc %s", status);
    ConsoleWrite(line, DEFAULT_COLOR);
}

const Row* AtLevel(const std::vector<Row>& rows, int level, uint32_t doodad, uint32_t effect) {
    for (const Row& r : rows) {
        switch (level) {
            case 0: if (r.doodad && r.effect && r.doodad == doodad && r.effect == effect) return &r; break;
            case 1: if (r.doodad && !r.effect && r.doodad == doodad) return &r; break;
            case 2: if (!r.doodad && r.effect && effect && r.effect == effect) return &r; break;
            default: if (!r.doodad && !r.effect) return &r; break;
        }
    }
    return nullptr;
}

float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

// The copies share the kept grass pool, which is sized for them: a change rebuilds it (and bakes the grass again).
static bool PoolChanged(CVar*, const char*, const char*, void*) {
    CDetailDoodad::s_bPoolsDirty = 1;
    return true;
}

static CVar* Register(const char* name, const char* help, const char* value, bool resizesPool) {
    return CVar::Register(name, help, 1, value, resizesPool ? PoolChanged : nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
}

void GrassDensity::RegisterCVars() {
    s_enabled = Register("grassDensity", "Denser grass near you, from GroundEffectDoodadDensity.cdbc", "1", true);
    s_maxMultiplier = Register("grassDensityMaxMultiplier", "Grass density: most plants drawn per placed plant (1 = never denser)", "4", true);
    s_maxRadius = Register("grassDensityMaxRadius", "Grass density: most yards around you that grass is made denser", "150", true);
    s_maxSpread = Register("grassDensityMaxSpread", "Grass density: most yards an extra plant moves from its plant", "3", false);
    s_thin = Register("grassThinning", "Far grass draws fewer plants (an even sample)", "1", false);
    s_thinStart = Register("grassThinStart", "Grass thinning: every plant is drawn within this many yards", "150", false);
    s_thinEnd = Register("grassThinEnd", "Grass thinning: yards where it reaches grassThinMinShare", "800", false);
    s_thinMinShare = Register("grassThinMinShare", "Grass thinning: share of plants drawn at grassThinEnd and beyond", "0.25", false);
}

bool GrassDensity::Active() {
    return GrassDensity::MaxMultiplier() > 1.0f || OptionCVars::Bool(s_thin, true);
}

float GrassDensity::ThinShare(float distance) {
    if (!OptionCVars::Bool(s_thin, true)) return 1.0f;
    const float start = OptionCVars::Float(s_thinStart, 0.0f, 10000.0f, 150.0f);
    const float end = std::fmax(start + 1.0f, OptionCVars::Float(s_thinEnd, 0.0f, 10000.0f, 800.0f));
    const float t = Clamp((distance - start) / (end - start), 0.0f, 1.0f);
    const float share = 1.0f - t * (1.0f - OptionCVars::Float(s_thinMinShare, 0.01f, 1.0f, 0.25f));
    return std::ceil(share * 8.0f) / 8.0f;
}

float GrassDensity::MaxMultiplier() {
    return OptionCVars::Bool(s_enabled, true) ? OptionCVars::Float(s_maxMultiplier, 1.0f, 8.0f, 4.0f) : 1.0f;
}

float GrassDensity::MaxRadius() { return OptionCVars::Float(s_maxRadius, 0.0f, 500.0f, 150.0f); }

GrassDensity::Values GrassDensity::Resolve(uint32_t chunkAreaId, int32_t mapId, uint32_t doodadId, uint32_t groundEffectId) {
    if (!s_loaded) Load();
    Values v;
    if (GrassDensity::MaxMultiplier() <= 1.0f) {
        return v;
    }
    const auto key = std::make_tuple(chunkAreaId, mapId, doodadId, groundEffectId);
    auto it = s_cache.find(key);
    if (it == s_cache.end()) {
        std::vector<std::pair<uint32_t, uint32_t>> places;
        uint32_t chain[8];
        const int n = LivingWind::AreaChain(chunkAreaId, chain, 8);
        for (int i = 0; i < n; ++i) places.push_back({ kArea, chain[i] });
        if (mapId >= 0) places.push_back({ kMap, static_cast<uint32_t>(mapId) });
        places.push_back({ kGlobal, 0 });

        float value[3] = { -1.0f, -1.0f, -1.0f };
        for (const auto& place : places) {
            auto rows = s_rows.find(place);
            if (rows == s_rows.end()) continue;
            for (int level = 0; level < 4; ++level) {
                if (const Row* r = AtLevel(rows->second, level, doodadId, groundEffectId)) {
                    for (int f = 0; f < 3; ++f) {
                        if (value[f] < 0.0f && r->value[f] >= 0.0f) value[f] = r->value[f];
                    }
                }
            }
        }
        Values raw;
        if (value[0] >= 0.0f) raw.multiplier = value[0];
        if (value[1] >= 0.0f) raw.radius = value[1];
        if (value[2] >= 0.0f) raw.spread = value[2];
        if (s_cache.size() > 20000) s_cache.clear();
        it = s_cache.emplace(key, raw).first;
    }
    // the player's limits, like a graphics setting (applied after the cache, so they can change live)
    v.multiplier = Clamp(it->second.multiplier, 1.0f, GrassDensity::MaxMultiplier());
    v.radius = Clamp(it->second.radius, 0.0f, GrassDensity::MaxRadius());
    v.spread = Clamp(it->second.spread, 0.0f, OptionCVars::Float(s_maxSpread, 0.0f, 10.0f, 3.0f));
    return v;
}

float GrassDensity::Weight(const Values& values, float distance) {
    if (values.multiplier <= 1.0f || values.radius <= 0.0f) return 0.0f;
    return Clamp((values.radius - distance) / (0.5f * values.radius), 0.0f, 1.0f);
}

uint32_t GrassDensity::Level(float distanceToPlayer) {
    Values cap;
    cap.multiplier = GrassDensity::MaxMultiplier();
    cap.radius = GrassDensity::MaxRadius();
    const uint32_t density = static_cast<uint32_t>(GrassDensity::Weight(cap, distanceToPlayer) * 4.0f + 0.5f);
    const uint32_t thin = 8 - static_cast<uint32_t>(GrassDensity::ThinShare(distanceToPlayer) * 8.0f + 0.5f);
    return density | (thin << 4);
}
