// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDBLOOM_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDBLOOM_H

// Not in the original binary. Azeroth Remastered's halo around bright parts of the world (hdBloom) and sun rays
// through the sky (hdRays), HdVulkanBloom.cpp over GxEffects. Only with visuals 2 (Visuals::AzerothRemastered).
namespace HD::Bloom {

void RegisterCVars();
// After the world's last draw, while its target is still bound; before HD::Grade::World.
void World();

}  // namespace HD::Bloom

#endif
