// Phase 5 peer message (docs/SKATE_INTEGRATION.md "Peers"): what a skater's client sends so other clients can draw its
// board. Pure codec, no engine dependency. Phase 5 carries it as hex over group addon messages;
// a Phase 6 area-of-interest relay can reuse the binary state later.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace skatepeer {
constexpr uint8_t Version = 1;
constexpr uint8_t Active = 1, Airborne = 2, Free = 4 /* tumbling board (bail) */, Hidden = 8, KnownFlags = 15;
constexpr size_t Size = 19;
constexpr float OffsetStep = 1.f / 512.f; // yards; int16 range is +-64 yd
constexpr uint32_t ExpireMs = 1500;      // a receiver hides the board when no state arrived for this long
struct State {
    uint8_t flags = 0;
    uint16_t sequence = 0;
    float offset[3]{};   // board centre minus the skater's position, world yards
    float rotation[4]{0, 0, 0, 1}; // unit quaternion x, y, z, w: the M2's local-to-world rotation
    float speed = 0;     // ground speed, yd/s (0..63.75)
};
inline void Put16(uint8_t* p, int16_t v) { memcpy(p, &v, 2); }
inline int16_t Get16(const uint8_t* p) { int16_t v; memcpy(&v, p, 2); return v; }
// Layout (little endian): u8 version, u8 flags, u16 sequence, i16 offset[3], i16 quaternion[4] (x32767), u8 speed*4.
inline bool Encode(const State& s, uint8_t out[Size]) {
    if ((s.flags & ~KnownFlags) || !std::isfinite(s.speed)) return false;
    double n = 0;
    for (float q : s.rotation) { if (!std::isfinite(q)) return false; n += double(q) * q; }
    if (n < .9 || n > 1.1) return false;
    out[0] = Version; out[1] = s.flags; memcpy(out + 2, &s.sequence, 2);
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(s.offset[i]) || fabsf(s.offset[i]) > 63.9f) return false;
        Put16(out + 4 + i*2, static_cast<int16_t>(lrintf(s.offset[i] / OffsetStep)));
    }
    const float sign = s.rotation[3] < 0 ? -1.f : 1.f; // q and -q are the same rotation; send w >= 0
    for (int i = 0; i < 4; ++i) Put16(out + 10 + i*2, static_cast<int16_t>(lrintf((std::max)(-1.f, (std::min)(1.f, sign * s.rotation[i] / float(sqrt(n)))) * 32767.f)));
    out[18] = static_cast<uint8_t>((std::max)(0.f, (std::min)(255.f, nearbyintf(s.speed * 4.f))));
    return true;
}
inline bool Decode(const uint8_t* in, size_t size, State& s) {
    if (size != Size || in[0] != Version || (in[1] & ~KnownFlags)) return false;
    s = {};
    s.flags = in[1]; memcpy(&s.sequence, in + 2, 2);
    for (int i = 0; i < 3; ++i) s.offset[i] = Get16(in + 4 + i*2) * OffsetStep;
    double n = 0;
    for (int i = 0; i < 4; ++i) { s.rotation[i] = Get16(in + 10 + i*2) / 32767.f; n += double(s.rotation[i]) * s.rotation[i]; }
    if (n < .9 || n > 1.1) return false;
    for (float& q : s.rotation) q = static_cast<float>(q / sqrt(n));
    s.speed = in[18] / 4.f;
    return true;
}
// Wrapping sequence order: true when `next` is newer than `last` (older or equal packets are dropped).
inline bool Newer(uint16_t next, uint16_t last) { return next != last && static_cast<uint16_t>(next - last) < 32768; }
}
