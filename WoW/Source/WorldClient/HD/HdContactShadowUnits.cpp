// SPDX-License-Identifier: GPL-3.0-or-later
// Azeroth Remastered's HdContactShadowUnits.cpp: the characters near the camera, kept out of the contact shadows. Apart
// from HdContactShadow.cpp so that file needs none of the game's object headers.
#include "WoW/Source/WorldClient/HD/HdContactShadow.h"

#include "Engine/Source/Tempest/C3Vector.h"
#include "WoW/Source/Object/ObjectClient/Object_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Ui/WorldFrame.h"

namespace {
    struct Gather {
        HD::ContactShadowUnit* out;
        float dist2[64];
        int count;
        int max;
        C3Vector camera;
    };

    // Keeps the max units nearest to the camera.
    int32_t EnumUnit(WOWGUID guid, void* param) {
        Gather& g = *static_cast<Gather*>(param);
        CGObject_C* object = ClntObjMgrObjectPtr(guid, TYPEMASK_UNIT, __FILE__, __LINE__);
        if (!object) return 1;
        C3Vector pos = object->GetPosition();
        float height = object->m_height * object->m_scale;
        if (!(height > 0.1f) || height > 40.0f) height = 2.0f;  // model not loaded yet: a person's size
        float x = pos.x - g.camera.x, y = pos.y - g.camera.y, z = pos.z - g.camera.z;
        float d2 = x * x + y * y + z * z;
        int slot = g.count;
        if (g.count >= g.max) {
            slot = 0;
            for (int i = 1; i < g.count; i++) {
                if (g.dist2[i] > g.dist2[slot]) slot = i;
            }
            if (g.dist2[slot] <= d2) return 1;
        } else {
            g.count++;
        }
        g.out[slot].x = x;
        g.out[slot].y = y;
        g.out[slot].z = z;
        g.out[slot].height = height;
        g.dist2[slot] = d2;
        return 1;
    }
}

int HD::ContactShadowGatherUnits(HD::ContactShadowUnit* out, int max) {
    if (max > 64) max = 64;
    if (max <= 0 || !ClntObjMgrGetCurrent() || !CGWorldFrame::s_currentWorldFrame || !CGWorldFrame::s_currentWorldFrame->m_camera) return 0;
    Gather g;
    g.out = out;
    g.count = 0;
    g.max = max;
    CGWorldFrame::GetCameraPosition(&g.camera);
    ClntObjMgrEnumVisibleObjects(&EnumUnit, &g);
    return g.count;
}
