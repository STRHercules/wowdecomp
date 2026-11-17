#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include "skate-pipe.h"
namespace bailrecovery {
struct Metadata {
    uint64_t tick=0;
    uint32_t cycle=0,checkpoint=0;
    bool active=false;
};
inline bool Decode(const std::vector<unsigned char>& bytes,uint64_t tick,Metadata& out) {
    if (bytes.size()!=20 || skatewire::Get(bytes.data()+16)>1) return false;
    const uint64_t paired=static_cast<uint64_t>(skatewire::Get(bytes.data()))
        | static_cast<uint64_t>(skatewire::Get(bytes.data()+4))<<32;
    if (paired!=tick) return false;
    out={paired,skatewire::Get(bytes.data()+8),skatewire::Get(bytes.data()+12),skatewire::Get(bytes.data()+16)!=0};
    return !out.active || out.cycle!=0;
}
inline bool Rolling(unsigned state) { return state>=100 && state<=103; }
inline bool Recoverable(unsigned state,bool onfoot=false) { return state==300 || state==702 || Rolling(state) || (state>=200 && state<=202) || (onfoot && state>=500 && state<=503); }
// Real Ironforge approaches enter BipedGround500 before WipeoutGround300;
// later elevated impacts still occupy500 after the original250ms allowance.
// Permit a bounded transition, not arbitrary off-board gameplay or a fake bail.
constexpr float EntrySeconds=1.f;
struct Entry {
    uint32_t epoch=0;
    uint64_t tick=0,start=0;
    unsigned previous=0;
    bool active=false,expired=false;
    float stepPeriod=0;
    void Clear(){*this={};}
    bool Observe(uint32_t owner,uint64_t current,float period,unsigned source,bool fresh){
        if(!owner || !current || !fresh || !std::isfinite(period) || period<.001f || period>.05f){Clear();return false;}
        if(epoch!=owner){Clear();epoch=owner;}
        if(current<tick){Clear();return false;}
        if(tick && current-tick>9){active=false;previous=0;}
        if(source==500){
            if(!active && Rolling(previous)){active=true;expired=false;start=current;stepPeriod=period;}
            // Do not let repeated ticks or a changed period renew the budget.
            if(active && (period!=stepPeriod || (current-start)*stepPeriod>EntrySeconds)){active=false;expired=true;}
        }else {active=false;expired=false;}
        previous=source;tick=current;return active;
    }
};
inline bool DetachedBoard(const Metadata& m,unsigned source,bool pendingEntry) {
    // Caller pairs metadata with the decoded pose tick before evaluating this.
    // Typed entry already checks epoch, input freshness, riding predecessor,
    // monotonic ticks and its nonrenewable one-second simulation-time budget.
    return m.tick && ((pendingEntry && source==500)
        || (m.active && m.cycle && (source==300 || source==702)));
}
struct Phase {
    bool active=false;
    uint32_t epoch=0,cycle=0,checkpoint=0;
    uint64_t start=0,tick=0;
    unsigned contactSamples=0;
    void Clear() { *this={}; }
    bool Observe(uint32_t owner,const Metadata& m,float period,unsigned source,bool fresh,
        bool contact,bool flight,bool& resetRoot,bool onfoot=false) {
        resetRoot=false;
        if (!owner || !fresh || !m.tick || !std::isfinite(period) || period<.001f || period>.05f) return false;
        if (epoch!=owner) { Clear(); epoch=owner; }
        if (m.tick<tick || m.cycle<cycle || m.checkpoint<checkpoint) return false;
        const bool distinct=m.tick!=tick; tick=m.tick;
        if (m.cycle!=cycle) {
            if (!m.active || !Recoverable(source,onfoot)) return false;
            active=true;start=m.tick;contactSamples=0;cycle=m.cycle;
        }
        if (m.checkpoint!=checkpoint) {
            if (!active) return false;
            resetRoot=true; checkpoint=m.checkpoint;
        }
        if (!active) return source!=300 && source!=702;
        // Integrated on-foot recovery follows the real engine phase regardless
        // of duration. Legacy diagnostic recovery retains its original budget.
        if (!Recoverable(source,onfoot) || (!onfoot && (m.tick-start)*period>15.f)) return false;
        if ((Rolling(source) || (onfoot && (source==500 || source==502))) && !m.active && contact && !flight) {
            if (distinct && contactSamples<3) ++contactSamples;
            if (contactSamples>=3) active=false;
        } else contactSamples=0;
        return true;
    }
};
}
