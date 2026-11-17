#pragma once
#include "SkateDrive.h"

namespace skatepeerspeed {
// Only the viewer's existing ground extrapolator speed is leased; positions, packets and server state are untouched.
class Lease {
    skatedrive::Lease lease_;
public:
    bool Held() const { return lease_.held; }
    bool Apply(float current,float reported,bool eligible,float& out) {
        if(!eligible || !std::isfinite(current) || current<0 || !std::isfinite(reported) || reported<0) return false;
        const float server=lease_.held && fabsf(current-lease_.applied)<1e-4f?lease_.server:current;
        if(reported>server+.1251f || server>55) return false;
        lease_.Observe(current);
        out=(std::min)(reported,server);
        lease_.Applied(out); return true;
    }
    bool Release(float current,float& out) {
        if(!lease_.held) return false;
        const bool own=std::isfinite(current) && fabsf(current-lease_.applied)<1e-4f;
        out=lease_.Release(); return own && std::isfinite(out);
    }
};
inline bool PackedGuid(const uint8_t* bytes,size_t size,uint64_t& guid) {
    if(!bytes || !size) return false;
    const uint8_t mask=*bytes++; --size; guid=0;
    for(unsigned i=0;i<8;++i) if(mask&(1u<<i)) {
        if(!size) return false;
        guid|=uint64_t(*bytes++)<<(8*i); --size;
    }
    return true;
}
}
