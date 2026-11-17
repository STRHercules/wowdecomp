#pragma once
#include <cstdint>
namespace localplayer {
// Build12340 player GUIDs have high type/entry32 bits zero. World/creature GUIDs
// are never accepted, and the actual logged-in GUID must be nonzero.
inline bool Valid(uint64_t guid){return guid!=0 && (guid>>32)==0;}
inline bool Owns(uint64_t candidate,uint64_t current){return Valid(current) && candidate==current;}
}
