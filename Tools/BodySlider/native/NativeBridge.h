#pragma once
#include <cstdint>

namespace Bridge {
// Verified compiler alignment padding inside stock .text; see evidence-lua-bridge.txt.
inline constexpr uintptr_t Slots[6] = {0x84DBE1, 0x84DF17, 0x84DF55, 0x84DFD1, 0x84E022, 0x84E064};
inline constexpr const char* Names[6] = {
    "BodySlider_Set", "BodySlider_Clear", "BodySlider_Supported",
    "BodySlider_Names", "BodySlider_HasSlider", "BodySlider_Units"
};
}
