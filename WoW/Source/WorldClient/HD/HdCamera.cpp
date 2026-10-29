// SPDX-License-Identifier: GPL-3.0-or-later
#include "WoW/Source/WorldClient/HD/HdCamera.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Gx/GxEffects.h"
#include "WoW/Source/Ui/Visuals.h"

namespace {
    HD::Camera s_camera;
}

// The viewport in target pixels as the Vulkan backend places it (as Lumenveil's engine::CaptureOpaqueState): the
// engine's viewport is y up, and rows are flipped unless a texture is the target.
void HD::CaptureCamera() {
    GxEffects::Device* device = Visuals::Current() == Visuals::AzerothRemastered ? GxEffects::Get() : nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    const CGxDevice* gx = g_theGxDevicePtr;
    if (!device || !gx || !device->TargetSize(width, height)) {
        s_camera.valid = false;
        return;
    }
    const float W = static_cast<float>(width);
    const float H = static_cast<float>(height);
    const CBoundingBox& v = gx->m_viewport;
    const bool target = gx->m_textureTarget[0].m_texture != nullptr || gx->m_textureTarget[1].m_texture != nullptr;
    const float x0 = std::clamp(std::floor(v.m_x.m_l * W + 0.5f), 0.0f, W);
    const float x1 = std::clamp(std::floor(v.m_x.m_h * W + 0.5f), x0, W);
    const float y0 = std::clamp(target ? std::floor(v.m_y.m_l * H + 0.5f) : std::floor((1.0f - v.m_y.m_h) * H + 0.5f), 0.0f, H);
    const float y1 = std::clamp(target ? std::floor(v.m_y.m_h * H + 0.5f) : std::floor((1.0f - v.m_y.m_l) * H + 0.5f), y0, H);
    s_camera.x = x0;
    s_camera.y = y0;
    s_camera.width = x1 - x0;
    s_camera.height = y1 - y0;
    s_camera.minZ = std::clamp(v.m_z.m_l, 0.0f, 1.0f);
    s_camera.maxZ = std::clamp(v.m_z.m_h, 0.0f, 1.0f);
    std::memcpy(s_camera.projection, &gx->m_projection, sizeof s_camera.projection);
    C44Matrix view;
    GxXformView(view);
    std::memcpy(s_camera.view, &view, sizeof s_camera.view);
    s_camera.valid = s_camera.width > 0 && s_camera.height > 0;
}

const HD::Camera& HD::CurrentCamera() {
    return s_camera;
}
