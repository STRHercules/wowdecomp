// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDGI_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDGI_H

// Not in the original binary. Azeroth Remastered's screen-space global illumination, one colour bounce (HdGI.cpp) over
// GxEffects: the light the opaque world shows on screen, above the client's ambient, bounced once onto what is around
// it (a lit wall tints the ground beside it). Only with visuals 2 (Visuals::AzerothRemastered).
//   hdGI 0 off / 1 on; hdGIRadius reach in yards (0.5..12); hdGIStrength 0..4; hdGIDebug 1 the bounce alone,
//   2 the surface colour estimate; hdGIInfo status
namespace HD {

void RegisterGICVars();
// Right after HD::ContactShadowRenderOpaque, into the open HD::Frame (closed by the caller).
void GIRenderOpaque();

}  // namespace HD

#endif
