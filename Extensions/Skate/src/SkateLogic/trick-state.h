#pragma once
#include "skate-pipe.h"
namespace trickstate {
struct State {
    bool valid=false;
    uint64_t tick=0;
    uint32_t source=0,landed=0;
    float points=0;
    double total=0;
    char live[97]{},last[97]{};
};
inline bool Decode(const std::vector<unsigned char>& bytes,uint64_t tick,State& out) {
    out={};
    if (bytes.size()<36 || bytes.size()>228) return false;
    memcpy(&out.tick,bytes.data(),8);
    out.source=skatewire::Get(bytes.data()+8); out.points=skatewire::GetFloat(bytes.data()+12);
    out.landed=skatewire::Get(bytes.data()+16); memcpy(&out.total,bytes.data()+20,8);
    if (out.tick!=tick || !std::isfinite(out.points) || out.points<0 || out.points>1e8f
        || !std::isfinite(out.total) || out.total<0 || out.total>1e12) return false;
    size_t offset=28;
    for (char* label:{out.live,out.last}) {
        if (offset+4>bytes.size()) return false;
        const unsigned size=skatewire::Get(bytes.data()+offset); offset+=4;
        if (size>96 || offset+size>bytes.size()) return false;
        for (unsigned i=0;i<size;++i) {
            const unsigned char c=bytes[offset+i];
            if (!((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c==' ' || c=='-')) return false;
            label[i]=static_cast<char>(c);
        }
        offset+=size;
    }
    out.valid=offset==bytes.size(); return out.valid;
}
}
