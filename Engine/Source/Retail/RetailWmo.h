#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Retail {

// Retail (Legion and later) WMOs rewritten in memory into the 3.3.5 layout that CMapObj::Load and
// CMapObjGroup::CreateDataPointers walk by position (WoW/Source/WorldClient/MapObjRead.cpp).
// Texture and doodad FileDataIDs become retail\<id>.blp / retail\<id>.m2 names, so they load through
// the same Retail source. Port of wotlkconv's wmo/convert.py and wmo/group.py, without group splitting.

bool IsWmoGroup(const std::vector<uint8_t>& bytes);

// groups receives one FileDataID per MOHD group from GFID (base level of detail), 0 when absent.
bool PrepareWmoRoot(const std::vector<uint8_t>& in, std::vector<uint8_t>& out,
    std::vector<uint32_t>& groups, std::string& error);

bool PrepareWmoGroup(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, std::string& error);

}
