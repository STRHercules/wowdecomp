#pragma once

#include <cstdint>

// CoA64: security fix (not in the 32-bit client). MSG_BATTLEGROUND_PLAYER_POSITIONS writes one entry per server-sent
// count into fixed static arrays; unchecked counts are the write primitive of the well-known 3.3.5a remote code
// execution (with .zdata RWX in the 32-bit image). A packet whose counts exceed the arrays is dropped.
namespace BattlefieldPositionsGuard {
    constexpr uint32_t kMaxPlayerPositions = 40;   // CGBattlefieldInfo::m_playerPositionGuids / m_playerPositions
    constexpr int32_t kMaxFlags = 2;               // CGBattlefieldInfo::m_flagCarriers / m_flagPositions

    constexpr bool PlayerCountValid(uint32_t count) {
        return count <= kMaxPlayerPositions;
    }

    constexpr bool FlagCountValid(int32_t count) {
        return count >= 0 && count <= kMaxFlags;
    }
}
