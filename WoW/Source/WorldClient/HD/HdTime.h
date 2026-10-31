// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDTIME_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDTIME_H

// Not in the original binary. Azeroth Remastered's time of day and sun (HdTime.cpp); hdTime works in every visual set,
// the others only with visuals 2 (Visuals::AzerothRemastered), the original behaviour otherwise.
//   hdTime    -1 = real server time (default); 0 to 24 = force this hour on this screen only, e.g. 18.5 = 18:30
//   hdSunPath 0 = original sun (north-west, under the horizon before 5:30 and after 21:30)
//             1 = by day (4:50 to 22:10) the sun sits where the world light comes from (north-west, 20 to 37
//                 degrees up), like WoW Forever's sun; the sun rays and the fog glow then match the lighting
//   hdShadowSun 0 = original shadows: the client casts them 5x steeper than the light (always short)
//               1 = shadows cast at the light's real height (20 to 37 degrees: long, like WoW Forever)
//   hdShadowBillboard 0 = original tree shadows (the leaf cards face the camera, so their shadow turns with it)
//                     1 = the leaf cards face the sun during the shadow pass (the shadow stays still); 2 = test:
//                     the leaf cards cast no shadow

#include <cstdint>

namespace DayNight {
struct DNInfo;
}

namespace HD {

void RegisterTimeCVars();

// From CGWorldFrame::UpdateDayNight once the real time is in info.
void ApplyTimeOverride(DayNight::DNInfo* info);
// Minutes since midnight for the skybox animations: the override if active, else realMinutes.
int32_t OverrideMinutes(int32_t realMinutes);
// hdSunPath: put the sun where the world light comes from (DayNight::SetPlanets).
bool SunFollowsLight();
// hdShadowSun: how much CShadowQuery::Update steepens the light for the shadow maps (5 = original, 1 = real).
float ShadowHeightScale();
// hdShadowBillboard: 0 original, 1 leaf cards face the sun in the shadow pass, 2 they cast none.
int32_t ShadowBillboardMode();
// While hdShadowSun casts long shadows: draw every caster of a band into the shadow maps, not only those the camera's
// frustum reaches (the donor's FullCasters), or a fern beside or behind the camera loses its long shadow in view.
bool ShadowFullCasters();

}  // namespace HD

#endif
