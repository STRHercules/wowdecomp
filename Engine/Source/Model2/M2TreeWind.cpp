// Not in the original binary. See M2TreeWind.h. The wind field, filters and defaults follow wxl-treewind 0.4.0
// (GPL-3.0-or-later, WarcraftXL); the donor detoured AnimateMT and rewrote the placement, this rewrites only the
// world-view product, so no pristine-placement cache is needed.
#include "Engine/Source/Model2/M2TreeWind.h"

#include <cmath>
#include <cstdint>

#include "Engine/Source/Model2/M2Data.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Model2/M2Shared.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/Tempest/C44Matrix.h"
#include "WoW/Source/Ui/OptionCVars.h"
#include "WoW/Source/WorldClient/LivingWind.h"

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;
constexpr float kDeg2Rad = kPi / 180.0f;
// CM2Scene::CreateModel flag for map-placed doodads (Map.cpp, MapDoodadDef.cpp); units, weapons and spell effects never carry it.
constexpr uint32_t kDoodadFlag = 0x20;

struct Wind {
    float direction, speed, amplitude, wavelength, lean, crossAmplitude, crossWavelength, crossAngle, variance, gust, distanceFade;
};

struct FloatOption {
    const char* name;
    const char* help;
    const char* def;
    float low;
    float high;
    float Wind::*field;
    CVar* cvar;
};

// One row per wind: the trees' and the bushes' are independent.
#define WIND_OPTIONS(prefix, what, v0, v1, v2, v3, v4, v5, v6, v7, v8, v9)                                      \
    { prefix "Direction", what " wind heading in the world, degrees", v0, 0.0f, 360.0f, &Wind::direction, nullptr },           \
    { prefix "Speed", what " wind wave speed, yards per second", v1, 0.0f, 12.0f, &Wind::speed, nullptr },                   \
    { prefix "Amplitude", what " sway at the crown, degrees", v2, 0.0f, 6.0f, &Wind::amplitude, nullptr },                     \
    { prefix "Wavelength", what " wind wave length, yards", v3, 2.0f, 80.0f, &Wind::wavelength, nullptr },                     \
    { prefix "Lean", what " constant downwind lean, degrees", v4, 0.0f, 4.0f, &Wind::lean, nullptr },                         \
    { prefix "CrossAmplitude", what " cross-swell sway, degrees", v5, 0.0f, 3.0f, &Wind::crossAmplitude, nullptr },           \
    { prefix "CrossWavelength", what " cross-swell length, yards", v6, 2.0f, 40.0f, &Wind::crossWavelength, nullptr },         \
    { prefix "CrossAngle", what " cross-swell heading from the wind, degrees", v7, 0.0f, 180.0f, &Wind::crossAngle, nullptr }, \
    { prefix "Variance", what " sway spread between models, 0..1", v8, 0.0f, 1.0f, &Wind::variance, nullptr },                 \
    { prefix "Gust", what " slow gust depth, 0..1", v9, 0.0f, 1.0f, &Wind::gust, nullptr },                                   \
    { prefix "DistanceFade", what " sway lost per yard of distance, 0 = off", "0", 0.0f, 0.05f, &Wind::distanceFade, nullptr }

FloatOption s_treeWind[] = { WIND_OPTIONS("treeWind", "Tree", "35", "3", "0.6", "26", "0.25", "0.22", "9", "40", "0.5", "0.35") };
FloatOption s_bushWind[] = { WIND_OPTIONS("bushWind", "Bush", "35", "2", "0.5", "9", "0.2", "0.25", "5", "40", "0.6", "0.4") };
#undef WIND_OPTIONS

CVar* s_tree = nullptr;
CVar* s_treeDoodadsOnly = nullptr;
CVar* s_treeMatchNames = nullptr;
CVar* s_treeMatchTallThin = nullptr;
CVar* s_treeExcludeDead = nullptr;
CVar* s_treeMinHeight = nullptr;
CVar* s_treeMinAspect = nullptr;
CVar* s_treeMaxHeight = nullptr;
CVar* s_treeMaxDistance = nullptr;
CVar* s_bush = nullptr;
CVar* s_bushDoodadsOnly = nullptr;
CVar* s_bushExcludeDead = nullptr;
CVar* s_bushMaxDistance = nullptr;
CVar* s_followWorld = nullptr;
CVar* s_worldScale = nullptr;

Wind ReadWind(const FloatOption (&options)[11]) {
    Wind wind{};
    for (const FloatOption& option : options) {
        wind.*(option.field) = OptionCVars::Float(option.cvar, option.low, option.high, static_cast<float>(std::atof(option.def)));
    }
    return wind;
}

// Case-insensitive substring test.
bool Contains(const char* hay, const char* needle) {
    for (int i = 0; hay[i] && i < 260; ++i) {
        int n = 0;
        while (hay[i + n] && needle[n]) {
            char a = hay[i + n];
            char b = needle[n];
            if (a >= 'A' && a <= 'Z') {
                a = static_cast<char>(a - 'A' + 'a');
            }
            if (a != b) {
                break;
            }
            ++n;
        }
        if (!needle[n]) {
            return true;
        }
    }
    return false;
}

template <size_t N>
bool ContainsAny(const char* path, const char* const (&words)[N]) {
    for (const char* word : words) {
        if (Contains(path, word)) {
            return true;
        }
    }
    return false;
}

// Bare "fir" would match "fire"; bushes are their own row.
const char* const kTreeWords[] = {
    "tree", "pines", "pine", "spruce", "cedar", "oak", "willow", "birch", "aspen", "palm", "canopy", "conifer", "maple",
    "eucalyptus", "redwood", "cypress", "larch", "juniper", "mangrove", "olive", "baobab", "treant",
};
const char* const kBushWords[] = {
    "bush", "shrub", "fern", "hedge", "thicket", "bramble", "undergrowth", "topiary", "bracken", "sapling",
};
// Dead, burnt and felled wood and loose tree parts: checked first so "DeadTree" and "Treestump" stay still.
const char* const kDeadWords[] = {
    "dead", "deadwood", "burnt", "burned", "charred", "scorch", "wither", "wilted", "blight", "corrupt", "cursed", "bare",
    "leafless", "defoliated", "skeletal", "petrified", "stump", "snag", "log", "driftwood", "uprooted", "felled", "fallen",
    "rotten", "hollow",
};

struct PathClass {
    const CM2Shared* shared = nullptr;
    uint32_t hash = 0;
    bool tree = false;
    bool bush = false;
    bool dead = false;
};

// The animate pass runs on several threads, so each keeps its own memo: the keyword scans happen once per model, not
// once per instance per frame. The name hash guards against a freed model's address being reused.
const PathClass& Classify(const CM2Shared* shared) {
    thread_local PathClass memo[256];
    PathClass& slot = memo[(reinterpret_cast<uintptr_t>(shared) >> 4) & 255];
    if (slot.shared != shared || slot.hash != shared->m_fileNameHash) {
        const char* path = shared->m_fileName;
        slot.shared = shared;
        slot.hash = shared->m_fileNameHash;
        slot.dead = ContainsAny(path, kDeadWords);
        slot.bush = ContainsAny(path, kBushWords);
        slot.tree = ContainsAny(path, kTreeWords);
    }
    return slot;
}

// Deterministic 0..1 hash of a world position: neighbouring trees get their own phase and amplitude.
float Hash01(float x, float y) {
    const uint32_t ix = static_cast<uint32_t>(static_cast<int32_t>(x * 8.0f));
    const uint32_t iy = static_cast<uint32_t>(static_cast<int32_t>(y * 8.0f));
    uint32_t h = ix * 73856093u ^ iy * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

}  // namespace

void TreeWind::RegisterCVars() {
    s_tree = OptionCVars::Register("treeWind", "Placed trees sway in the wind", "1");
    for (FloatOption& option : s_treeWind) {
        option.cvar = OptionCVars::Register(option.name, option.help, option.def);
    }
    s_treeDoodadsOnly = OptionCVars::Register("treeWindDoodadsOnly", "Tree wind: only map-placed doodads sway", "1");
    s_treeMatchNames = OptionCVars::Register("treeWindMatchNames", "Tree wind: the model's name must name a tree", "1");
    s_treeMatchTallThin = OptionCVars::Register("treeWindMatchTallThin", "Tree wind: the model must be tall and narrow", "0");
    s_treeExcludeDead = OptionCVars::Register("treeWindExcludeDead", "Tree wind: dead, bare, burnt and felled trees stay still", "1");
    s_treeMinHeight = OptionCVars::Register("treeWindMinHeight", "Tree wind: tall-and-narrow test, least model height in yards", "6");
    s_treeMinAspect = OptionCVars::Register("treeWindMinAspect", "Tree wind: tall-and-narrow test, least height / width", "1.8");
    s_treeMaxHeight = OptionCVars::Register("treeWindMaxHeight", "Tree wind: trees taller than this (yards) stay still, 0 = no limit", "200");
    s_treeMaxDistance = OptionCVars::Register("treeWindMaxDistance", "Tree wind: trees further than this (yards) stay still, 0 = no limit", "0");

    s_bush = OptionCVars::Register("bushWind", "Placed bushes sway in the wind", "1");
    for (FloatOption& option : s_bushWind) {
        option.cvar = OptionCVars::Register(option.name, option.help, option.def);
    }
    s_bushDoodadsOnly = OptionCVars::Register("bushWindDoodadsOnly", "Bush wind: only map-placed doodads sway", "1");
    s_bushExcludeDead = OptionCVars::Register("bushWindExcludeDead", "Bush wind: dead, bare and burnt bushes stay still", "1");
    s_bushMaxDistance = OptionCVars::Register("bushWindMaxDistance", "Bush wind: bushes further than this (yards) stay still, 0 = no limit", "0");

    s_followWorld = OptionCVars::Register("treeWindFollowWorld", "Tree and bush wind follow the world wind (WindProfile.cdbc) where it blows", "1");
    s_worldScale = OptionCVars::Register("treeWindWorldScale", "Tree and bush wind: sway per unit of world wind (2 = the sliders' sway at half strength)", "2");
}

void TreeWind::Apply(CM2Model* model, const C44Matrix& view) {
    if (!s_tree || model->m_attachParent || !model->m_shared || !model->m_shared->m_data) {
        return;
    }

    const PathClass& kind = Classify(model->m_shared);
    // A bush is claimed by name alone and never meets the tree size tests; with bush wind off it may still sway as a tree.
    const bool bush = kind.bush && OptionCVars::Bool(s_bush, true);
    const bool doodad = (model->m_flags & kDoodadFlag) != 0;
    const C44Matrix& world = model->m_worldTransform;
    const CAaBox& extent = model->m_shared->m_data->m_bounds.m_extent;
    const float height = extent.t.z - extent.b.z;
    float maxDistance;

    if (bush) {
        if ((OptionCVars::Bool(s_bushDoodadsOnly, true) && !doodad) || (OptionCVars::Bool(s_bushExcludeDead, true) && kind.dead)) {
            return;
        }
        maxDistance = OptionCVars::Float(s_bushMaxDistance, 0.0f, 500.0f, 0.0f);
    } else {
        if (!OptionCVars::Bool(s_tree, true) || (OptionCVars::Bool(s_treeDoodadsOnly, true) && !doodad) ||
            (OptionCVars::Bool(s_treeExcludeDead, true) && kind.dead)) {
            return;
        }
        // Every enabled test must pass; with both off nothing sways.
        const bool names = OptionCVars::Bool(s_treeMatchNames, true);
        const bool tallThin = OptionCVars::Bool(s_treeMatchTallThin, false);
        if ((!names && !tallThin) || (names && !kind.tree)) {
            return;
        }
        if (tallThin) {
            const float width = std::fmax(extent.t.x - extent.b.x, extent.t.y - extent.b.y);
            if (!(height >= OptionCVars::Float(s_treeMinHeight, 0.0f, 40.0f, 6.0f)) ||
                (width >= 0.01f && height / width < OptionCVars::Float(s_treeMinAspect, 0.5f, 6.0f, 1.8f))) {
                return;
            }
        }
        // Giant world trees keep their pose rather than lean a canopy across the zone. Compared squared: no sqrt per tree.
        const float maxHeight = OptionCVars::Float(s_treeMaxHeight, 0.0f, 300.0f, 200.0f);
        if (maxHeight > 0.0f && height > 0.0f) {
            const float scaleSq = std::fmax(world.a0 * world.a0 + world.a1 * world.a1 + world.a2 * world.a2,
                                  std::fmax(world.b0 * world.b0 + world.b1 * world.b1 + world.b2 * world.b2,
                                            world.c0 * world.c0 + world.c1 * world.c1 + world.c2 * world.c2));
            if (height * height * scaleSq > maxHeight * maxHeight) {
                return;
            }
        }
        maxDistance = OptionCVars::Float(s_treeMaxDistance, 0.0f, 500.0f, 0.0f);
    }

    // The translation row of the world-view matrix is the model's base relative to the camera.
    const C44Matrix& worldView = model->m_matrixF4;
    const float distSq = worldView.d0 * worldView.d0 + worldView.d1 * worldView.d1 + worldView.d2 * worldView.d2;
    if (maxDistance > 0.0f && distSq > maxDistance * maxDistance) {
        return;
    }

    Wind w = bush ? ReadWind(s_bushWind) : ReadWind(s_treeWind);
    const float px = world.d0;
    const float py = world.d1;

    // The world wind sets the heading, and its strength (with the gusts passing here) scales the sway: calm places
    // stand still, storms bend the crowns. Its bearing is a compass heading (0 = +X, 90 = -Y); ours turns from +X to +Y.
    const bool followWorld = OptionCVars::Bool(s_followWorld, true) && LivingWind::Active();
    if (followWorld) {
        const float scale = LivingWind::Strength() * OptionCVars::Float(s_worldScale, 0.0f, 10.0f, 2.0f) *
                            (1.0f + LivingWind::GustStrength() * LivingWind::GustAt(px, py));
        w.direction = LivingWind::HeadingDegrees();
        w.amplitude *= scale;
        w.crossAmplitude *= scale;
        w.lean *= scale;
    }
    const float t = static_cast<float>(model->m_scene->m_time % 3600000u) * 0.001f;

    const float a1 = w.direction * kDeg2Rad;
    const float a2 = (w.direction + w.crossAngle) * kDeg2Rad;
    const float d1x = std::cos(a1), d1y = std::sin(a1);
    const float d2x = std::cos(a2), d2y = std::sin(a2);
    const float k1 = kTwoPi / w.wavelength;
    const float k2 = kTwoPi / w.crossWavelength;

    // Distance along each wave's heading. The world wind turns all the time, so its waves are laid out in its wind
    // frame (turning about the player): in world x, y a tenth of a degree would move every wave far from the origin.
    float along1 = px * d1x + py * d1y;
    float along2 = px * d2x + py * d2y;
    if (followWorld) {
        float u, v;
        LivingWind::WindFrame(px, py, u, v);
        along1 = u;
        along2 = u * std::cos(w.crossAngle * kDeg2Rad) + v * std::sin(w.crossAngle * kDeg2Rad);
    }

    const float seed = Hash01(px, py) * kTwoPi;
    const float ampScale = 1.0f + (Hash01(py, px) - 0.5f) * 2.0f * w.variance;
    const float fade = w.distanceFade > 0.0f ? 1.0f / (1.0f + w.distanceFade * std::sqrt(distSq)) : 1.0f;
    const float gust = 1.0f + w.gust * std::sin(t * 0.37f + seed * 0.5f);
    const float s1 = std::sin(k1 * along1 - k1 * w.speed * t + seed);
    const float s2 = std::sin(k2 * along2 - k2 * w.speed * 1.37f * t + seed * 0.6f);

    const float deg1 = (w.amplitude * s1 * gust + w.lean) * ampScale * fade;
    const float deg2 = w.crossAmplitude * s2 * gust * ampScale * fade;
    const float tx = (deg1 * d1x + deg2 * d2x) * kDeg2Rad;
    const float ty = (deg1 * d1y + deg2 * d2y) * kDeg2Rad;
    const float tilt = std::sqrt(tx * tx + ty * ty);
    if (tilt < 1.0e-6f) {
        return;
    }

    // Tilting up toward (tx, ty) about the model's own base: rotate the axes, keep the translation.
    C44Matrix swayed = world * C44Matrix::Rotation(tilt, C3Vector(-ty, tx, 0.0f), false);
    swayed.d0 = world.d0;
    swayed.d1 = world.d1;
    swayed.d2 = world.d2;
    model->m_matrixF4 = swayed * view;
}
