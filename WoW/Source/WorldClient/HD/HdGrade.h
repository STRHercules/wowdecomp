// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDGRADE_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDGRADE_H

// Not in the original binary. Azeroth Remastered's colour grading (hdGrade, HdVulkanGrade.cpp) over GxEffects: the
// finished world through two of WoW Forever's 32x32x32 LUTs (forever_data\grading\<id>.blp), picked by the zone's
// lights and the time of day. Only with visuals 2 (Visuals::AzerothRemastered).
namespace HD::Grade {

void RegisterCVars();
// After the world's last draw, while its target is still bound.
void World();

}  // namespace HD::Grade

#endif
