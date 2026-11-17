#pragma once
#include <cstdint>
#include <algorithm>
#include <iterator>
#include "world-map-catalog.h"
namespace worldmap {
inline bool Supported(uint32_t map) { return std::binary_search(std::begin(KnownMaps),std::end(KnownMaps),map); }
inline bool Changed(uint32_t before,uint32_t after) { return before!=after; }
// Entry into a different copy of the same dungeon still owns a fresh epoch.
// This tracks ordinary world-loading events, not movement pauses or bails.
struct EntrySerial {
    bool seen=false;uint32_t serial=0;
    bool Observe(bool valid,uint32_t next){
        if(!valid)return false;
        const bool changed=seen && serial!=next;
        seen=true;serial=next;return changed;
    }
};
}
