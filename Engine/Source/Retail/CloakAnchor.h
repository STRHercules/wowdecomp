#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace Retail {
struct CloakVertex {
    std::array<float, 3> position;
    std::array<uint16_t, 4> bones;
    std::array<uint8_t, 4> weights;
};
struct CloakAnchor {
    std::array<float, 3> position{};
    uint16_t bone = 0;
    float width = 0;
};

// Use the cloak's upper edge, not its independently animated lower cloth bones.
inline bool FindCloakAnchor(const std::vector<CloakVertex>& vertices, uint32_t boneCount, CloakAnchor& out) {
    if (vertices.empty() || !boneCount) return false;
    float bottom = vertices.front().position[2], top = bottom;
    for (const auto& vertex : vertices) {
        for (float value : vertex.position) if (!std::isfinite(value)) return false;
        bottom = std::min(bottom, vertex.position[2]);
        top = std::max(top, vertex.position[2]);
    }
    const float edge = top - (top - bottom) * 0.1f;
    float left = 1e9f, right = -1e9f;
    std::array<float, 3> sum{};
    std::map<uint16_t, unsigned> votes;
    unsigned count = 0;
    for (const auto& vertex : vertices) {
        if (vertex.position[2] < edge) continue;
        for (unsigned i = 0; i < 4; ++i) {
            if (vertex.weights[i] && vertex.bones[i] >= boneCount) return false;
            if (vertex.weights[i]) votes[vertex.bones[i]] += vertex.weights[i];
        }
        for (unsigned i = 0; i < 3; ++i) sum[i] += vertex.position[i];
        left = std::min(left, vertex.position[1]);
        right = std::max(right, vertex.position[1]);
        ++count;
    }
    if (!count || votes.empty() || right - left < 0.01f) return false;
    for (unsigned i = 0; i < 3; ++i) out.position[i] = sum[i] / count;
    out.bone = std::max_element(votes.begin(), votes.end(), [](const auto& a, const auto& b) {
        return a.second < b.second;
    })->first;
    out.width = right - left;
    return true;
}

// The neck's cloak edge can be inside the torso below it. Measure the whole back piece's footprint.
inline bool FitCloakBack(const std::vector<std::array<float, 3>>& surface, CloakAnchor& anchor) {
    float depth = anchor.position[0];
    bool found = false;
    for (const auto& position : surface) {
        for (float value : position) if (!std::isfinite(value)) return false;
        if (std::fabs(position[1] - anchor.position[1]) > anchor.width * 0.5f ||
            position[2] < anchor.position[2] - anchor.width || position[2] > anchor.position[2] + 0.03f) continue;
        depth = std::min(depth, position[0]);
        found = true;
    }
    if (!found) return false;
    // The accepted trial fit is halfway between the cloak-edge mount and full back-surface clearance.
    anchor.position[0] = (anchor.position[0] - 0.035f + depth - 0.025f) * 0.5f;
    return true;
}
}
