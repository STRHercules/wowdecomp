// SPDX-License-Identifier: GPL-3.0-or-later
// Azeroth Remastered's HdTime.cpp, gated on visuals 2; the cvars and their defaults (the donor pack's) are the donor's.
#include "WoW/Source/WorldClient/HD/HdTime.h"

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "WoW/Source/Ui/OptionCVars.h"
#include "WoW/Source/Ui/Visuals.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"

namespace {
    CVar* s_hdTime = nullptr;
    CVar* s_hdSunPath = nullptr;
    CVar* s_hdShadowSun = nullptr;
    CVar* s_hdShadowBillboard = nullptr;

    bool On() {
        return Visuals::Current() == Visuals::AzerothRemastered;
    }

    // Forced minutes since midnight (0 to 1439), or -1 when the override is off.
    int32_t ForcedMinutes() {
        if (!On() || !s_hdTime) return -1;
        float hours = s_hdTime->m_floatValue;
        if (hours < 0.0f) return -1;
        if (hours >= 24.0f) hours = 0.0f;
        int32_t minutes = static_cast<int32_t>(hours * 60.0f + 0.5f);
        return minutes >= 1440 ? 0 : minutes;
    }
}

void HD::RegisterTimeCVars() {
    if (s_hdTime) return;
    s_hdTime = OptionCVars::Register("hdTime", "HD: -1 = real time, 0 to 24 = force this hour on this screen only", "-1");
    s_hdSunPath = OptionCVars::Register("hdSunPath", "HD: 0 = original sun, 1 = the sun sits where the world light comes from (like WoW Forever)", "1");
    s_hdShadowSun = OptionCVars::Register("hdShadowSun", "HD: 0 = original short shadows, 1 = shadows cast at the real sun height (long at dawn and dusk, like WoW Forever)", "1");
    s_hdShadowBillboard = OptionCVars::Register("hdShadowBillboard", "HD: 0 = original tree shadows (turn with the camera), 1 = tree leaf cards face the sun in the shadows (still), 2 = test: leaf cards cast no shadow", "1");
}

void HD::ApplyTimeOverride(DayNight::DNInfo* info) {
    int32_t minutes = ForcedMinutes();
    if (minutes < 0 || !info) return;
    info->m_timeOfDay = static_cast<uint32_t>(minutes);
    info->m_dayProgression = minutes * (1.0f / 1440.0f);
}

int32_t HD::OverrideMinutes(int32_t realMinutes) {
    int32_t minutes = ForcedMinutes();
    return minutes < 0 ? realMinutes : minutes;
}

bool HD::SunFollowsLight() {
    return On() && OptionCVars::Bool(s_hdSunPath, false);
}

float HD::ShadowHeightScale() {
    return On() && OptionCVars::Bool(s_hdShadowSun, false) ? 1.0f : 5.0f;
}

bool HD::ShadowFullCasters() {
    return ShadowHeightScale() < 5.0f;
}

int32_t HD::ShadowBillboardMode() {
    return On() ? OptionCVars::Int(s_hdShadowBillboard, 0, 2, 0) : 0;
}
