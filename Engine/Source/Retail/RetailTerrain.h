#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Retail {

// Retail (Legion and later) terrain rewritten in memory into the 3.3.5 layout CMap::LoadWdt and
// CMapArea::ProcessIffChunks read (WoW/Source/WorldClient/Map.cpp, MapArea.cpp). Port of wotlkconv's adt/convert.py
// and adt/wdt.py, without MH2O liquid, MCSE sound emitters or layer limiting.

struct WdtTile {
    uint32_t x = 0, y = 0;      // tile column and row, as in <map>_<x>_<y>.adt
    uint32_t root = 0, obj0 = 0, tex0 = 0;
};

// A terrain WDT (MPHD flag 0x200, MAID): keeps MVER/MPHD/MAIN with the flags 3.3.5 understands and lists the tiles.
bool PrepareWdt(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, std::vector<WdtTile>& tiles, std::string& error);

// Merges a split tile (root, _obj0, _tex0) into one ADT with MHDR offsets, MCIN, name tables and MCRF.
// Textures, models and WMOs become retail\<fdid>.blp|.m2|.wmo names.
bool PrepareAdt(const std::vector<uint8_t>& root, const std::vector<uint8_t>& obj0, const std::vector<uint8_t>& tex0,
    std::vector<uint8_t>& out, std::string& error);

}
