#include "WoW/Source/WorldClient/Clouds/Noise.h"
#include <cstdlib>
#include <iostream>
#include <set>

namespace {
#include "WoW/Source/WorldClient/Clouds/Spirv.inc"

void Check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

// Check the compiled modules against GxEffects' descriptor layout, including the storage image format.
template <size_t N>
void Shader(const uint32_t (&words)[N], const std::set<uint32_t>& expected, bool compute) {
    Check(words[0] == 0x07230203, "SPIR-V header");
    std::set<uint32_t> bindings;
    std::set<uint32_t> offsets;
    bool rgba32f = false, groups = false;
    for (size_t i = 5; i < N;) {
        uint32_t count = words[i] >> 16, op = words[i] & 0xFFFF;
        Check(count && i + count <= N, "SPIR-V instruction length");
        if (op == 71 && count == 4 && words[i + 2] == 33) bindings.insert(words[i + 3]);
        if (op == 71 && count == 4 && words[i + 2] == 34) Check(words[i + 3] == 0, "descriptor set must be zero");
        if (op == 72 && count == 5 && words[i + 3] == 35) offsets.insert(words[i + 4]);
        if (op == 25 && count == 9 && words[i + 7] == 2) rgba32f = words[i + 8] == 1;
        if (op == 16 && count == 6 && words[i + 2] == 17)
            groups = words[i + 3] == 8 && words[i + 4] == 8 && words[i + 5] == 1;
        i += count;
    }
    Check(bindings == expected, "shader bindings must match GxEffects inputs");
    if (bindings.count(16)) {
        // glslang removes unused members but preserves the byte offsets supplied by the CPU.
        const std::set<uint32_t> expectedOffsets = compute
            ? std::set<uint32_t>({0,16,32,48,64,80,96,224,448,464,480})
            : std::set<uint32_t>({80,192,256,320});
        Check(offsets == expectedOffsets, "CPU and shader constant offsets");
    }
    if (compute) Check(rgba32f && groups, "panorama storage format or dispatch size");
}
}

int main() {
    using namespace CloudNoise;
    Check(std::fabs(Perlin(-0.2f, 1.3f, 2.4f, 4, 3) - Perlin(3.8f, 1.3f, 2.4f, 4, 3)) < 1e-5f, "Perlin must tile across negative coordinates");
    Check(std::fabs(Worley(-0.2f, 0.3f, 0.4f, 4, 11) - Worley(0.8f, 0.3f, 0.4f, 4, 11)) < 1e-5f, "Worley must tile");
    std::vector<float> ranked = {3, 1, 2};
    Equalise(ranked);
    Check(ranked == std::vector<float>({1, 0, 0.5f}), "noise ranks must span the coverage range");
    for (bool shape : {false, true}) {
        const auto levels = VolumeLevels(8, shape);
        Check(levels.size() == 4 && levels[0] == MakeNoise(8, shape), "noise must be deterministic with a complete mip chain");
        int size = 8;
        for (size_t level = 0; level < levels.size(); ++level, size /= 2) {
            Check(levels[level].size() == size * size * size * 4, "cubic mip dimensions");
            if (!shape) for (size_t i = 3; i < levels[level].size(); i += 4)
                Check(levels[level][i] == 255, "detail alpha must stay opaque");
            if (!level) continue;
            const int prev = size * 2;
            for (int z = 0; z < size; ++z) for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) for (int c = 0; c < 4; ++c) {
                unsigned sum = 0;
                for (int n = 0; n < 8; ++n)
                    sum += levels[level - 1][(((z * 2 + (n >> 2)) * prev + y * 2 + ((n >> 1) & 1)) * prev + x * 2 + (n & 1)) * 4 + c];
                Check(levels[level][((z * size + y) * size + x) * 4 + c] == (sum + 4) / 8, "volume mip box filter");
            }
        }
    }
    Shader(kCloudPanorama, {0, 1, 2, 16, 17}, true);
    Shader(kCloudScatter, {}, false);
    Shader(kCloudDraw, {3, 4, 16}, false);
    std::cout << "Cloud noise, mip chain and compiled shader contracts passed\n";
}
