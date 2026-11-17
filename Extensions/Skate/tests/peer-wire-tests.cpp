// Phase 5 peer message codec (not yet carried by any opcode; docs/SKATE_INTEGRATION.md Phase 5 "Peers").
#include "SkatePeerWire.h"
#include <cassert>

static bool Near(float a, float b, float e) { return fabsf(a - b) <= e; }

int main() {
    using namespace skatepeer;
    State s;
    s.flags = Active | Airborne; s.sequence = 65534; s.offset[0] = -1.25f; s.offset[1] = .5f; s.offset[2] = .03f; s.speed = 9.3f;
    const float h = sqrtf(.5f); s.rotation[0] = h; s.rotation[1] = 0; s.rotation[2] = 0; s.rotation[3] = -h; // a negative w
    uint8_t wire[Size];
    assert(Encode(s, wire) && wire[0] == Version);
    State back;
    assert(Decode(wire, Size, back));
    assert(back.flags == s.flags && back.sequence == 65534);
    for (int i = 0; i < 3; ++i) assert(Near(back.offset[i], s.offset[i], OffsetStep));
    assert(Near(back.speed, 9.25f, .01f));
    // q and -q are one rotation: it travels with w >= 0.
    assert(back.rotation[3] > 0 && Near(back.rotation[0], -h, 1e-3f) && Near(back.rotation[3], h, 1e-3f));
    // Refusals: bad size/version/flags, NaN, out-of-range offset, non-unit quaternion.
    assert(!Decode(wire, Size - 1, back));
    uint8_t bad[Size]; memcpy(bad, wire, Size);
    bad[0] = 2; assert(!Decode(bad, Size, back));
    memcpy(bad, wire, Size); bad[1] = 0x40; assert(!Decode(bad, Size, back));
    memcpy(bad, wire, Size); memset(bad + 10, 0, 8); assert(!Decode(bad, Size, back)); // zero quaternion
    State e = s; e.offset[1] = 70.f; assert(!Encode(e, wire));
    e = s; e.offset[1] = NAN; assert(!Encode(e, wire));
    e = s; e.rotation[0] = 3.f; assert(!Encode(e, wire));
    e = s; e.flags = 0x20; assert(!Encode(e, wire));
    // Speed clamps instead of wrapping.
    e = s; e.speed = 500.f; assert(Encode(e, wire) && wire[18] == 255);
    e.speed = -3.f; assert(Encode(e, wire) && wire[18] == 0);
    // Sequence order wraps.
    assert(Newer(1, 65535) && !Newer(65535, 1) && !Newer(5, 5) && Newer(6, 5));
    return 0;
}
