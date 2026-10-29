// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDCONTACTSHADOW_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDCONTACTSHADOW_H

// Not in the original binary. Azeroth Remastered's screen-space contact shadows (HdContactShadow.cpp, v4) over
// GxEffects: the small shadows the sun shadow maps are too coarse for, from a short ray marched toward the world light
// in the world depth. Where the client's own sun shadow maps already shadow, nothing is added; characters receive them
// but cast none (hdContactShadowUnits 1: they cast too). Only with visuals 2 (Visuals::AzerothRemastered).
//   hdContactShadow 0 off / 1 on; hdContactShadowLength ray length in yards (0.25..6); hdContactShadowStrength 0..1;
//   hdContactShadowUnits; hdContactShadowDebug 1 the shadow alone, 2 the same without the shadow-map check (red where
//   the shadow maps already shadow); hdContactShadowInfo status
namespace HD {

// A character for the contact shadows: feet position relative to the camera (world axes, yards) and its height.
struct ContactShadowUnit {
    float x, y, z;
    float height;
};

// The visible characters nearest to the camera, at most max of them (HdContactShadowUnits.cpp). How many.
int ContactShadowGatherUnits(ContactShadowUnit* out, int max);

void RegisterContactShadowCVars();
// After the opaque world, before the grass: builds the shadow mask from the depth without the grass.
void ContactShadowPrepare();
// After the grass (CWorldScene::RenderDetailDoodads), before HD::GIRenderOpaque: applies the mask, into the open
// HD::Frame (closed by the caller).
void ContactShadowRenderOpaque();

}  // namespace HD

#endif
