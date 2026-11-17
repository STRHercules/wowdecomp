#pragma once
#include "skate-pipe.h"
namespace onfootpossession {
struct State { unsigned source=0,board=0,hand=0; };
inline bool Decode(const std::vector<unsigned char>& bytes,uint64_t tick,unsigned source,State& out) {
    out={};if(bytes.size()!=20)return false;
    uint64_t paired=0;memcpy(&paired,bytes.data(),8);
    out={skatewire::Get(bytes.data()+8),skatewire::Get(bytes.data()+12),skatewire::Get(bytes.data()+16)};
    return paired==tick && out.source==source && out.board<=5 && out.hand<=2 && (!out.hand || out.board==1);
}
}
