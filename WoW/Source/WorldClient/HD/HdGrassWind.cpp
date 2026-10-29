// SPDX-License-Identifier: GPL-3.0-or-later
// Not in the original binary. Azeroth Remastered (Waystone HD): grass wind and grass parting around the player.
// Algorithm and default values adapted from wxl-grasswind (WarcraftXL, by iThorgrim), GPLv3:
// this program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
// License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later
// version. It is distributed WITHOUT ANY WARRANTY. See <https://www.gnu.org/licenses/>.
#include "WoW/Source/WorldClient/HD/HdGrassWind.h"
#include <cmath>
#include <windows.h>
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Object/ObjectClient/Object_C.h"
#include "WoW/Source/UIUtil/Camera.h"
#include "WoW/Source/Ui/WorldFrame.h"
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
}
