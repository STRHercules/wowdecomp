// GxEffects over Vulkan (Effects.cpp): the device GxEffects::Get() hands out while the Vulkan
// device is up. Not in the original binary.
#pragma once

#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Vk.hpp"

namespace gxvk::vk::effects
{
    /// Destroys what the passes made: pipelines, render passes, programs, descriptor pools, the
    /// constant rings, framebuffers. After the device is idle, before frame::Destroy(), the texture
    /// module and memory go. GxEffects::Get() returns null afterwards.
    void Shutdown();

    /// What GxEffects::SetDrawTexture put on a texture stage of the engine's draws.
    struct DrawTexture
    {
        VkImageView view = VK_NULL_HANDLE;
        uint32_t    id   = 0;   ///< the image's number (texture::Image::id)
        uint32_t    key  = 0;   ///< the sampler key (CGxTexFlags filter and wrap bits)
    };

    /// True, and the override, when stage `stage` of the engine's draws has one; stamps the image as used by this frame.
    bool DrawTextureFor(uint32_t stage, DrawTexture& out);
}
