#include <cstdint>
#include <cstdlib>
#include "Engine/Source/Base/SIMD.h"
#include "Engine/Source/Storm/H/storm.h"

// VERIFY: aaf6c69b auto
// OFFSET: 0x0083DE50
void* SIMD::AllocA16(uint32_t size, const char* fileName, int32_t lineNumber) {
    // Not in the original binary: a block this large is a full-detail model (Model2 wide skins, tens of MB). Storm's
    // allocator ends the game when it fails; here the caller gets null instead and the model is left out.
    uint8_t* ptr = size >= (16u << 20) ? static_cast<uint8_t*>(std::malloc(size + 16))
        : static_cast<uint8_t*>(SMemAlloc(size + 16, fileName, lineNumber, 0));

    if (!ptr) {
        return nullptr;
    }

    uint8_t pad = static_cast<uint8_t>(16 - (reinterpret_cast<uintptr_t>(ptr) & 0xF));
    ptr += pad;
    ptr[-1] = pad;

    return ptr;
}

// VERIFY: 9b3a75f2 manual
// OFFSET: 0x0083DE90
void SIMD::FreeA16(void* ptr) {
    if (ptr) {
        uint8_t* base = static_cast<uint8_t*>(ptr) - static_cast<uint8_t*>(ptr)[-1];
        delete[] base;
    }
}
