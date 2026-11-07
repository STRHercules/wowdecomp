#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Retail {

// Retail (Legion and later) terrain rewritten in memory into the 3.3.5 layout CMap::LoadWdt and
// CMapArea::ProcessIffChunks read (WoW/Source/WorldClient/Map.cpp, MapArea.cpp). Port of wotlkconv's adt/convert.py
// and adt/wdt.py (liquid from adt/mh2o.py), without MCSE sound emitters. Chunks past 3.3.5's four texture layers keep the
// four that show most, and textures fade out toward chunks without them (not wotlkconv's limit_layers).

struct WdtTile {
    uint32_t x = 0, y = 0;      // tile column and row, as in <map>_<x>_<y>.adt
    uint32_t root = 0, obj0 = 0, tex0 = 0;
    uint32_t minimap = 0;       // the tile's minimap texture (MAID), 0 for none
};

// What each of a tile's 256 chunks keeps after that cut, so the tile beside it can fade to match. An id is 0 or stands for
// one texture in any tile.
using KeptTextures = std::array<uint32_t, 4>;
struct TileTextures { std::vector<KeptTextures> chunks; };
bool ReadTileTextures(const std::vector<uint8_t>& tex0, TileTextures& out);

// Whether the client's AreaTable has an area. A converted terrain chunk keeps its retail area ID when it does (Blizzard
// never reuses one, so it names the same place), and 0 otherwise. Without a filter every chunk gets 0.
using AreaKnown = bool (*)(uint32_t area);
void SetAreaFilter(AreaKnown known);

// A terrain WDT (MPHD flag 0x200, MAID): keeps MVER/MPHD/MAIN with the flags 3.3.5 understands and lists the tiles.
bool PrepareWdt(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, std::vector<WdtTile>& tiles, std::string& error);

// Merges a split tile (root, _obj0, _tex0) into one ADT with MHDR offsets, MCIN, name tables and MCRF.
// Textures, models and WMOs become retail\<fdid>.blp|.m2|.wmo names. adjoining: the tiles to the left, right, above and
// below (x-1, x+1, y-1, y+1), null entries where there is none; null keeps a seam on every tile edge.
bool PrepareAdt(const std::vector<uint8_t>& root, const std::vector<uint8_t>& obj0, const std::vector<uint8_t>& tex0,
    std::vector<uint8_t>& out, std::string& error, const TileTextures* const* adjoining = nullptr);

}
