// Client-owned collision wire (WSB1 ops 48/49/50), docs/SKATE_HELPER_PROTOCOL.md. Engine-independent.
#pragma once
#include "SkateTransport.h"
#include <algorithm>

namespace skatecollision {
constexpr uint32_t MaxTriangles = 49152;
// world_collision.rs HALF: the helper's own gather box, half extents in yards.
constexpr float HalfXY = 24.f, HalfZ = 45.f, MinHalfXY = 6.f;
// Next horizontal half extent after a TRUNCATED gather (24 -> 12 -> 6), 0 once the minimum was tried.
inline float Shrink(float half) { return half > MinHalfXY ? (std::max)(MinHalfXY, half * .5f) : 0.f; }
// Refresh once the board has used a third of the box it was given (8 yd for the full 24 yd half extent).
inline float RefreshDistance(float half) { return half / 3.f; }
// Converted coordinates must stay within 10,000 m of the origin; keep a margin in yards.
constexpr float MaxOffset = 10000.f / .9144f - 100.f;

// Whether the helper accepts this triangle: finite, near the origin and with nonzero engine area.
inline bool Keep(const float* v, const float origin[3]) {
    for (int i = 0; i < 9; ++i)
        if (!std::isfinite(v[i]) || fabsf(v[i]) > 1000000.f || fabsf(v[i] - origin[i % 3]) > MaxOffset) return false;
    const float a[3]{v[3]-v[0],v[4]-v[1],v[5]-v[2]}, b[3]{v[6]-v[0],v[7]-v[1],v[8]-v[2]};
    const double c[3]{double(a[1])*b[2]-double(a[2])*b[1], double(a[2])*b[0]-double(a[0])*b[2], double(a[0])*b[1]-double(a[1])*b[0]};
    // Engine metres scale the cross product by 0.9144^2, its square by 0.9144^4 (~0.699); helper needs > 1e-12.
    return (c[0]*c[0]+c[1]*c[1]+c[2]*c[2]) * 0.699 > 2e-12;
}

// Op 48 (init) or 49 (refresh) payload: 44-byte prefix, then count*9 floats. Heading is init only.
inline bool Packet(std::vector<unsigned char>& bytes, bool init, uint32_t map, uint32_t generation,
    const float origin[3], const float centre[3], float heading, const std::vector<float>& tris) {
    bytes.clear();
    const size_t count = tris.size() / 9;
    if (!count || count > MaxTriangles || tris.size() % 9 || !generation || (init && generation != 1)
        || (init && (!std::isfinite(heading) || fabsf(heading) > 7.f))) return false;
    for (int i = 0; init && i < 3; ++i) if (origin[i] != centre[i]) return false;
    bytes.reserve(44 + tris.size() * 4);
    skatetransport::Word(bytes, map); skatetransport::Word(bytes, generation);
    for (int i = 0; i < 3; ++i) skatetransport::Float(bytes, origin[i]);
    for (int i = 0; i < 3; ++i) skatetransport::Float(bytes, centre[i]);
    if (init) skatetransport::Float(bytes, heading); else skatetransport::Word(bytes, 0);
    skatetransport::Word(bytes, 0); // gather status: only complete snapshots are sent
    skatetransport::Word(bytes, static_cast<uint32_t>(count));
    for (float v : tris) skatetransport::Float(bytes, v);
    return true;
}

// Worker-side generation bookkeeping: strictly increasing, only the latest accepted one is awaited.
struct Generations {
    uint32_t accepted = 0, installed = 0, awaiting = 0;
    void Initialized() { accepted = installed = 1; awaiting = 0; }
    uint32_t Next() const { return accepted + 1; }
    // Op 49 acknowledged: an older awaited generation is superseded and no longer waited on.
    void Queued(uint32_t generation) { accepted = generation; awaiting = generation; }
    // Op 50 reply (map, queued, installed, lastFailed). 1 installed, -1 failed, 0 pending, -2 malformed.
    int Status(const std::vector<unsigned char>& reply) {
        if (reply.size() != 16) return -2;
        const uint32_t done = skatetransport::Get(reply.data() + 8), failed = skatetransport::Get(reply.data() + 12);
        if (done > installed) installed = done;
        if (!awaiting) return 0;
        if (installed >= awaiting) { awaiting = 0; return 1; }
        if (failed == awaiting) { awaiting = 0; return -1; }
        return 0;
    }
};
}
