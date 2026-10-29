// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDSHADERSWAP_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDSHADERSWAP_H

#include <cstddef>
#include <cstdint>

class CGxShader;

// Not in the original binary. What the Azeroth Remastered materials (terrain, ...) need to put their own DX9 shader code
// into the client's CGxShader objects, as the donor's HdGxShader.cpp: the Vulkan backend draws DX9 code only through a
// module translated offline and listed by the md5 of the code (Tools/VulkanShaders), so code the device does not know
// keeps Blizzard's shader. Such code is written once to <shader folder>\hd-pending\<name>.bin, where
// Tools/HdMaterials/install_hd_shaders.py translates it into the client's manifests (then restart the game).
namespace HD {

// The device draws this code. When it does not, the code is written to hd-pending (once per name per run).
bool ShaderUsable(const uint8_t* code, size_t size, const char* name);

// New code for this shader; the backend makes its program again from it at the next bind.
void ShaderSetCode(CGxShader* shader, const uint8_t* code, uint32_t size);

}  // namespace HD

#endif
