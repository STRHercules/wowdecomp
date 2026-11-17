#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "SkateInput.h"
#include "SkateTransport.h"
#include <cassert>
int main() {
    skate::InputGate gate;
    xboxinput::Pad p{};
    p.buttons = 0x1000;
    assert(!gate.Step(true,true,true,p).active);
    p = {}; assert(gate.Step(true,true,true,p).active);
    p.buttons=0x1000; p.lx=12345;
    auto c=gate.Step(true,true,true,p); assert(c.active && c.push && c.steer==12345);
    c=gate.Step(false,true,true,p); assert(!c.active && !c.push && !c.steer);
    assert(!gate.Step(true,true,true,p).active);
    p={}; assert(gate.Step(true,true,false,p).active);
    assert(!gate.Step(true,false,false,p).active);
    std::vector<unsigned char> wire;
    skatetransport::InputStep(wire,1,true,false,true,false,12345);
    assert(wire.size()==16 && skatetransport::Get(wire.data()+4)==0x1000);
    assert(static_cast<int32_t>(skatetransport::Get(wire.data()+8))==-12345);
    skatetransport::Pose pose;
    assert(!skatetransport::Decode(wire,pose));
    wire.assign(60,0); assert(!skatetransport::Decode(wire,pose));
}
