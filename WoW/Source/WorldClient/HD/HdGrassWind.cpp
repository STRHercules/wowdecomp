// SPDX-License-Identifier: GPL-3.0-or-later
// Not in the original binary. Azeroth Remastered (Waystone HD): grass wind and grass parting around the player.
// Algorithm and default values adapted from wxl-grasswind (WarcraftXL, by iThorgrim), GPLv3:
// this program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
// License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later
// version. It is distributed WITHOUT ANY WARRANTY. See <https://www.gnu.org/licenses/>.
#include "WoW/Source/WorldClient/HD/HdGrassWind.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>
#include <windows.h>
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Object/ObjectClient/Object_C.h"
#include "WoW/Source/UIUtil/Camera.h"
#include "WoW/Source/Ui/WorldFrame.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/WorldClient/CustomTable.h"
#include "WoW/Source/WorldClient/LivingWind.h"
#include "WoW/Source/WorldClient/World.h"

namespace {
    const double kTwoPi = 6.283185307179586;

    // WarcraftXL's defaults (GrassWind.hpp)
    const float kAmplitude = 0.06f;        // wave 1 sway at the tip, yards
    const float kWavelength = 18.0f;       // wave 1 length, yards
    const float kCrossAmplitude = 0.02f;   // wave 2 sway, yards
    const float kCrossWavelength = 6.5f;   // wave 2 length, yards
    const float kCrossAngle = 35.0f;       // wave 2 heading, degrees from wave 1
    const float kCrossSpeed = 1.37f;       // wave 2 speed / wave 1 speed
    const float kLean = 0.35f;             // downwind lean, fraction of wave 1
    const float kVariance = 0.6f;          // per-blade amplitude spread (0.7 .. 1.3)
    const float kDistanceFade = 0.015f;    // sway / (1 + depth * this)
    const float kAnchor = 0.3f;            // foot part of the blade that never moves
    const float kJitter = 1.7f;            // per-blade phase spread, radians
    const float kPushCentre = 0.5f;        // lean at the player, yards (0 at the radius)
    const float kConeHeight = 0.5f;        // height above the player's feet where the lean is full
    const float kMinHeight = 0.3f;         // lower than this above the player's feet: no lean

    CVar* s_wind = nullptr;
    CVar* s_speed = nullptr;
    CVar* s_direction = nullptr;
    CVar* s_strength = nullptr;
    CVar* s_react = nullptr;
    CVar* s_radius = nullptr;
    CVar* s_followWorld = nullptr;
    CVar* s_reactUnits = nullptr;
    CVar* s_unitScale = nullptr;
    CVar* s_mountedScale = nullptr;

    // Not in the donor: grass parts around the units near the player too (seyris-living-azeroth's Actors), each sized by
    // its own bounding radius, wider while mounted. The player is actor 0 (hdGrassReactRadius); 7 nearest units after it.
    constexpr int kActors = 8;
    constexpr float kActorRange = 40.0f;       // yards from the player a unit still parts grass
    constexpr float kMinUnitRadius = 0.5f;
    struct Actor {
        C3Vector pos;
        float radius;
        float distSq;
    };
    Actor s_actors[kActors];
    int s_actorCount = 0;

    // GroundEffectDoodadWind.cdbc (ID, Flags, Stiffness, RootV, TipV): per-doodad overrides of the bend. Doodads with a
    // row bake into batches of their own (CDetailDoodadInst::AddDoodad), whose c68 says how they bend.
    struct WindClass {
        uint32_t flags;      // 0x1 no wind, 0x2 no push, 0x10 root and tip swapped
        float stiffness;     // 0 flexible .. 1 rigid: scales wind and push
        float rootV, tipV;   // texture V of the foot and the tip; -1 = the default (foot at V 1, tip at V 0)
    };
    std::vector<WindClass> s_classes;               // index = class - 1
    std::unordered_map<uint32_t, uint16_t> s_classOf;
    bool s_classesLoaded = false;

    void LoadClasses() {
        s_classesLoaded = true;
        CustomTable::Table table;
        char status[160];
        if (!CustomTable::LoadTable("DBFilesClient\\GroundEffectDoodadWind.cdbc", 5, table, status, sizeof status)) return;
        for (uint32_t r = 0; r < table.Rows() && s_classes.size() < 0xFFFE; ++r) {
            s_classes.push_back(WindClass{ table.U(r, 1), table.F(r, 2), table.F(r, 3), table.F(r, 4) });
            s_classOf[table.U(r, 0)] = static_cast<uint16_t>(s_classes.size());
        }
    }

    int32_t CollectUnit(WOWGUID guid, void* param) {
        const CGObject_C* self = static_cast<const CGObject_C*>(param);
        CGObject_C* object = ClntObjMgrObjectPtr(guid, TYPEMASK_UNIT, __FILE__, __LINE__);
        if (!object || object == self) return 1;
        CGUnit_C* unit = static_cast<CGUnit_C*>(object);
        const C3Vector pos = unit->GetPosition();
        const float dx = pos.x - s_actors[0].pos.x, dy = pos.y - s_actors[0].pos.y, dz = pos.z - s_actors[0].pos.z;
        const float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > kActorRange * kActorRange) return 1;
        float radius = std::max(kMinUnitRadius, (unit->m_unit ? unit->m_unit->m_unitFieldBoundingradius : 0.5f) * (s_unitScale ? s_unitScale->m_floatValue : 2.0f));
        if (unit->IsMounted()) radius *= s_mountedScale ? s_mountedScale->m_floatValue : 2.0f;
        // keep the nearest: when full, replace the farthest unit (never the player in slot 0)
        int slot = s_actorCount < kActors ? s_actorCount++ : -1;
        if (slot < 0) {
            slot = 1;
            for (int i = 2; i < kActors; i++) {
                if (s_actors[i].distSq > s_actors[slot].distSq) slot = i;
            }
            if (s_actors[slot].distSq <= distSq) return 1;
        }
        s_actors[slot] = Actor{ pos, std::min(radius, 20.0f), distSq };
        return 1;
    }
    bool s_world = false;                  // this frame follows the world wind

    // The world wind's strength on an 8 x 8 grid around the player (shelter under roofs, lee behind walls and gusts already
    // in it), 8 yards apart, sampled per blade by the grass vertex shader (c100..c163, x = the sample; c69 per chunk).
    constexpr int kGrid = 8;
    constexpr float kGridSpacing = 8.0f;
    float s_grid[kGrid * kGrid * 4];
    float s_gridOrigin[2] = { 0.0f, 0.0f };
    bool s_gridOn = false;

    // this frame (GrassWindPrepare)
    ULONGLONG s_lastTick = 0;
    double s_phase1 = 0.0;                 // wave phases, advanced each frame (a speed change does not jump)
    double s_phase2 = 0.0;
    float s_dir1[2] = { 0.0f, 0.0f };      // wave directions * 2 pi / wavelength
    float s_dir2[2] = { 0.0f, 0.0f };
    float s_amp1[2] = { 0.0f, 0.0f };      // wave directions * amplitude
    float s_amp2[2] = { 0.0f, 0.0f };
    float s_on = 0.0f;
    bool s_havePlayer = false;
    C3Vector s_player;
    float s_push = 0.0f;
    float s_radiusValue = 2.0f;

    float Clamp(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // same object as ShadowQuery.cpp's GetViewerPosition: what the camera follows (the player, or the player's vehicle)
    bool PlayerPosition(C3Vector* position) {
        CGCamera* camera = CGWorldFrame::GetActiveCamera();
        if (!camera) return false;
        CGObject_C* object = ClntObjMgrObjectPtr(camera->m_targetGUID, TYPEMASK_OBJECT, __FILE__, __LINE__);
        if (!object) return false;
        *position = object->GetPosition();
        return true;
    }
}

void HD::GrassWindRegister() {
    static bool registered = false;
    if (registered) return;
    registered = true;
    s_wind = CVar::Register("hdGrassWind", "HD: 1 = wind in the grass, 0 = off", 1, "1", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    s_speed = CVar::Register("hdGrassWindSpeed", "HD: grass wind wave speed, yards per second", 1, "3", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    s_direction = CVar::Register("hdGrassWindDirection", "HD: grass wind heading, degrees", 1, "45", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    s_strength = CVar::Register("hdGrassWindStrength", "HD: grass wind sway multiplier (1 = default)", 1, "1", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    s_react = CVar::Register("hdGrassReact", "HD: 1 = grass parts around the player, 0 = off", 1, "1", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    s_radius = CVar::Register("hdGrassReactRadius", "HD: radius of the grass parting, yards", 1, "2", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    s_followWorld = CVar::Register("hdGrassWindFollowWorld", "Grass wind follows the world wind (WindProfile.cdbc) where it blows", 1, "1", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    s_reactUnits = CVar::Register("hdGrassReactUnits", "Grass parts around the creatures and players near you too", 1, "1", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    s_unitScale = CVar::Register("hdGrassReactUnitScale", "Grass parting around a unit: radius as a multiple of its bounding radius", 1, "2", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    s_mountedScale = CVar::Register("hdGrassReactMountedScale", "Grass parting: how much wider it is around a mounted unit (you too)", 1, "2", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
}

bool HD::GrassWindWanted() {
    HD::GrassWindRegister();
    return (s_wind && s_wind->m_intValue != 0) || (s_react && s_react->m_intValue != 0);
}

void HD::GrassWindPrepare() {
    HD::GrassWindRegister();
    ULONGLONG now = GetTickCount64();
    double dt = s_lastTick ? (now - s_lastTick) * 0.001 : 0.0;
    s_lastTick = now;
    if (dt > 0.25) dt = 0.25; // a hitch or a loading screen does not make the grass jump

    bool wind = s_wind ? s_wind->m_intValue != 0 : true;
    float speed = s_speed ? s_speed->m_floatValue : 3.0f;
    float heading = s_direction ? s_direction->m_floatValue : 45.0f;
    float strength = s_strength ? Clamp(s_strength->m_floatValue, 0.0f, 50.0f) : 1.0f;
    if (!wind) strength = 0.0f;
    // Not in the donor: the world wind (LivingWind.h) sets the heading, and its strength scales the sway; half strength
    // gives the slider's sway, calm places stand still. The gust passing over the player swells all of it at once (a
    // gust per chunk would show the chunk seams).
    s_world = wind && (!s_followWorld || s_followWorld->m_intValue != 0) && LivingWind::Active();
    s_gridOn = false;
    if (s_world) {
        heading = LivingWind::HeadingDegrees();
        if (PlayerPosition(&s_player)) {
            // Each blade takes the wind of its own place: the grid's samples (StrengthAt: steady wind, gusts, roofs, lee)
            // where the player is near, the slider's sway times two for the whole grid. Beyond the grid's edge the last
            // sample holds. shortcut: sampled at the player's height, not the ground's.
            s_gridOrigin[0] = std::floor(s_player.x / kGridSpacing) * kGridSpacing - 0.5f * (kGrid - 1) * kGridSpacing;
            s_gridOrigin[1] = std::floor(s_player.y / kGridSpacing) * kGridSpacing - 0.5f * (kGrid - 1) * kGridSpacing;
            for (int b = 0; b < kGrid; b++) {
                for (int a = 0; a < kGrid; a++) {
                    const float pos[3] = { s_gridOrigin[0] + a * kGridSpacing, s_gridOrigin[1] + b * kGridSpacing, s_player.z };
                    float* sample = s_grid + 4 * (b * kGrid + a);
                    sample[0] = LivingWind::StrengthAt(pos);
                    sample[1] = sample[2] = sample[3] = 0.0f;
                }
            }
            s_gridOn = true;
            strength *= 2.0f;
        } else {
            strength *= 2.0f * LivingWind::Strength();
        }
    }

    float a1 = static_cast<float>(heading * (kTwoPi / 360.0));
    float a2 = static_cast<float>((heading + kCrossAngle) * (kTwoPi / 360.0));
    float k1 = static_cast<float>(kTwoPi / kWavelength);
    float k2 = static_cast<float>(kTwoPi / kCrossWavelength);
    s_dir1[0] = std::cos(a1) * k1;
    s_dir1[1] = std::sin(a1) * k1;
    s_dir2[0] = std::cos(a2) * k2;
    s_dir2[1] = std::sin(a2) * k2;
    s_amp1[0] = std::cos(a1) * kAmplitude * strength;
    s_amp1[1] = std::sin(a1) * kAmplitude * strength;
    s_amp2[0] = std::cos(a2) * kCrossAmplitude * strength;
    s_amp2[1] = std::sin(a2) * kCrossAmplitude * strength;

    // the waves travel downwind: sin(k . p - k v t)
    s_phase1 = std::fmod(s_phase1 - k1 * speed * dt, kTwoPi);
    s_phase2 = std::fmod(s_phase2 - k2 * speed * kCrossSpeed * dt, kTwoPi);

    bool react = s_react ? s_react->m_intValue != 0 : true;
    s_radiusValue = s_radius ? Clamp(s_radius->m_floatValue, 0.1f, 20.0f) : 2.0f;
    s_havePlayer = react && PlayerPosition(&s_player);
    s_push = s_havePlayer ? kPushCentre : 0.0f;
    s_actorCount = 0;
    if (s_havePlayer) {
        CGCamera* camera = CGWorldFrame::GetActiveCamera();
        CGObject_C* self = camera ? ClntObjMgrObjectPtr(camera->m_targetGUID, TYPEMASK_OBJECT, __FILE__, __LINE__) : nullptr;
        float radius = s_radiusValue;
        if (self && (self->m_obj->m_type & TYPEMASK_UNIT) && static_cast<CGUnit_C*>(self)->IsMounted()) {
            radius *= s_mountedScale ? Clamp(s_mountedScale->m_floatValue, 1.0f, 5.0f) : 2.0f;
        }
        s_actors[0] = Actor{ s_player, radius, 0.0f };
        s_actorCount = 1;
        if (!s_reactUnits || s_reactUnits->m_intValue != 0) {
            ClntObjMgrEnumVisibleObjects(CollectUnit, self);
        }
    }
    s_on = (strength > 0.0f || s_push > 0.0f) ? 1.0f : 0.0f;
}

void HD::GrassWindConstants(float chunkOffsetX, float chunkOffsetY, float chunkOffsetZ, float* out) {
    // the chunk's corner in world coordinates (the blade positions are relative to it)
    double ox = static_cast<double>(chunkOffsetX) + CWorldScene::s_activeWorldView.x;
    double oy = static_cast<double>(chunkOffsetY) + CWorldScene::s_activeWorldView.y;
    double oz = static_cast<double>(chunkOffsetZ) + CWorldScene::s_activeWorldView.z;

    // world coordinates go to 17000 yards: the waves' phase at the chunk corner is worked out here in double and
    // wrapped, the shader only adds k . (position in the chunk), so the sway stays smooth and seamless from chunk
    // to chunk (exact same field as k . world position)
    float phase1 = static_cast<float>(std::fmod(ox * s_dir1[0] + oy * s_dir1[1] + s_phase1, kTwoPi));
    float phase2 = static_cast<float>(std::fmod(ox * s_dir2[0] + oy * s_dir2[1] + s_phase2, kTwoPi));
    if (s_world) {
        // The world wind turns all the time: measured from the map's origin a tenth of a degree would move every wave,
        // so its waves are laid out in its wind frame, which turns about the player (the same field the shader extends
        // with k . position in the chunk, since the frame is the world turned to the heading).
        float u, v;
        LivingWind::WindFrame(static_cast<float>(ox), static_cast<float>(oy), u, v);
        const double cross = kCrossAngle * (kTwoPi / 360.0);
        phase1 = static_cast<float>(std::fmod(u * (kTwoPi / kWavelength) + s_phase1, kTwoPi));
        phase2 = static_cast<float>(std::fmod((u * std::cos(cross) + v * std::sin(cross)) * (kTwoPi / kCrossWavelength) + s_phase2, kTwoPi));
    }

    float* c = out;
    // c53: xy = chunk corner wrapped to 64 yards (per-blade random seed only), z unused, w = wave 1 phase here
    c[0] = static_cast<float>(std::fmod(ox, 64.0));
    c[1] = static_cast<float>(std::fmod(oy, 64.0));
    c[2] = 0.0f;
    c[3] = phase1;
    // c54: xyz = player relative to the chunk corner, w = lean at the player (0 = no parting)
    c[4] = s_havePlayer ? static_cast<float>(s_player.x - ox) : 0.0f;
    c[5] = s_havePlayer ? static_cast<float>(s_player.y - oy) : 0.0f;
    c[6] = s_havePlayer ? static_cast<float>(s_player.z - oz) : 0.0f;
    c[7] = s_push;
    // c55: x = 1 / radius, y = radius, z = 1 / cone height, w = min height
    c[8] = 1.0f / s_radiusValue;
    c[9] = s_radiusValue;
    c[10] = 1.0f / kConeHeight;
    c[11] = kMinHeight;
    // c56: wave 1: xy = direction * amplitude, zw = direction * 2 pi / wavelength
    c[12] = s_amp1[0];
    c[13] = s_amp1[1];
    c[14] = s_dir1[0];
    c[15] = s_dir1[1];
    // c57: wave 2, same
    c[16] = s_amp2[0];
    c[17] = s_amp2[1];
    c[18] = s_dir2[0];
    c[19] = s_dir2[1];
    // c58: x = anchor, y = 1 / (1 - anchor), z = variance, w = distance fade per yard
    c[20] = kAnchor;
    c[21] = 1.0f / (1.0f - kAnchor);
    c[22] = kVariance;
    c[23] = kDistanceFade;
    // c59: x = wave 2 phase here, y = per-blade phase spread, z = lean, w = 1 when wind or parting is on
    c[24] = phase2;
    c[25] = kJitter;
    c[26] = kLean;
    c[27] = s_on;
    // c60..c67: the units the grass parts around, relative to the chunk corner; w = 1 / radius (huge = no unit)
    for (int a = 0; a < kActors; a++) {
        float* u = c + 28 + 4 * a;
        if (a < s_actorCount) {
            u[0] = static_cast<float>(s_actors[a].pos.x - ox);
            u[1] = static_cast<float>(s_actors[a].pos.y - oy);
            u[2] = static_cast<float>(s_actors[a].pos.z - oz);
            u[3] = 1.0f / s_actors[a].radius;
        } else {
            u[0] = u[1] = u[2] = 0.0f;
            u[3] = 1.0e6f;
        }
    }
}

const float* HD::GrassWindGrid(int* count) {
    *count = kGrid * kGrid;
    return s_grid;
}

void HD::GrassWindGridInfo(float chunkOffsetX, float chunkOffsetY, float* out) {
    const double ox = static_cast<double>(chunkOffsetX) + CWorldScene::s_activeWorldView.x;
    const double oy = static_cast<double>(chunkOffsetY) + CWorldScene::s_activeWorldView.y;
    out[0] = static_cast<float>(ox - s_gridOrigin[0]);
    out[1] = static_cast<float>(oy - s_gridOrigin[1]);
    out[2] = 1.0f / kGridSpacing;
    out[3] = s_gridOn ? 1.0f : 0.0f;
}

uint16_t HD::GrassWindClassOf(uint32_t doodadId, bool modelMapped, float modelRootV, float modelTipV) {
    if (!s_classesLoaded) LoadClasses();
    auto it = s_classOf.find(doodadId);
    // A mapping from the model's geometry that is not the default one, where the table leaves root and tip unset: a class
    // of its own (a copy of the row's, or a plain one), so the batch of this plant bends from its own V range.
    const bool usable = modelMapped && std::fabs(modelTipV - modelRootV) > 0.01f && (std::fabs(modelRootV - 1.0f) > 0.03f || std::fabs(modelTipV) > 0.03f);
    if (!usable || s_classes.size() >= 0xFFFE) return it == s_classOf.end() ? 0 : it->second;
    WindClass k = it == s_classOf.end() ? WindClass{ 0, 0.0f, -1.0f, -1.0f } : s_classes[it->second - 1];
    if (k.rootV >= 0.0f && k.tipV >= 0.0f) return it->second;  // the table says it
    k.rootV = modelRootV;
    k.tipV = modelTipV;
    s_classes.push_back(k);
    return static_cast<uint16_t>(s_classes.size());
}

void HD::GrassWindBatchConstants(uint16_t windClass, float* out) {
    // The bend from V: none at `root`, full at the tip. The default is the donor's: the foot at V 1, the tip at V 0, the
    // bottom kAnchor of the blade never moving.
    float wind = 1.0f, push = 1.0f, foot = 1.0f, tip = 0.0f;
    if (windClass && windClass <= s_classes.size()) {
        const WindClass& k = s_classes[windClass - 1];
        const float stiff = Clamp(k.stiffness, 0.0f, 1.0f);
        wind = (k.flags & 0x1) ? 0.0f : 1.0f - stiff;
        push = (k.flags & 0x2) ? 0.0f : 1.0f - stiff;
        if (k.rootV >= 0.0f && k.tipV >= 0.0f && k.rootV != k.tipV) {
            foot = k.rootV;
            tip = k.tipV;
        }
        if (k.flags & 0x10) std::swap(foot, tip);
    }
    const float root = foot + kAnchor * (tip - foot);
    out[0] = wind;
    out[1] = push;
    out[2] = root;
    out[3] = 1.0f / (tip - root);
}
