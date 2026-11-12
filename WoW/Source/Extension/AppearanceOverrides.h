#pragma once
#include <cstdint>
#include "WoW/Source/Object/ObjectClient/Unit_C.h"

namespace ClientAppearance {
// Rendering inputs only. The player's update fields and real inventory stay untouched.
struct Item {
    uint32_t entry = 0, display = 0;
    CGUnitVirtualItem weapon{};
};
using ItemResolver = const Item* (*)(const CGUnit_C&, int32_t slot);
inline ItemResolver itemResolver = nullptr;
inline const Item* Resolve(const CGUnit_C& player, int32_t slot) {
    return itemResolver ? itemResolver(player, slot) : nullptr;
}
}
