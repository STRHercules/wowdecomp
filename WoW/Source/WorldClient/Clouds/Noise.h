// Cloud noise from the Manastorm VolumetricClouds share pack (2026-11-15).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

namespace CloudNoise {
inline float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
uint32_t Hash(int32_t x, int32_t y, int32_t z, uint32_t seed) {
    uint32_t h = seed ^ (static_cast<uint32_t>(x) * 0x8da6b343u) ^ (static_cast<uint32_t>(y) * 0xd8163841u) ^ (static_cast<uint32_t>(z) * 0xcb1ab31fu);
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

float Unit(uint32_t h) {
    return static_cast<float>(h & 0xFFFFFFu) / 16777216.0f;
}

int32_t Wrap(int32_t v, int32_t period) {
    return ((v % period) + period) % period;
}

float Fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// One of the twelve cube-edge gradients, dotted with the offset.
float Grad(uint32_t h, float x, float y, float z) {
    switch (h % 12) {
        case 0: return x + y;
        case 1: return -x + y;
        case 2: return x - y;
        case 3: return -x - y;
        case 4: return x + z;
        case 5: return -x + z;
        case 6: return x - z;
        case 7: return -x - z;
        case 8: return y + z;
        case 9: return -y + z;
        case 10: return y - z;
        default: return -y - z;
    }
}

// Gradient noise at (x, y, z) in lattice units, tiling every `period` cells; about -1..1.
float Perlin(float x, float y, float z, int32_t period, uint32_t seed) {
    int32_t x0 = static_cast<int32_t>(std::floor(x));
    int32_t y0 = static_cast<int32_t>(std::floor(y));
    int32_t z0 = static_cast<int32_t>(std::floor(z));
    float fx = x - x0;
    float fy = y - y0;
    float fz = z - z0;
    float corner[8];

    for (int32_t i = 0; i < 8; i++) {
        int32_t dx = i & 1;
        int32_t dy = (i >> 1) & 1;
        int32_t dz = (i >> 2) & 1;
        uint32_t h = Hash(Wrap(x0 + dx, period), Wrap(y0 + dy, period), Wrap(z0 + dz, period), seed);
        corner[i] = Grad(h, fx - dx, fy - dy, fz - dz);
    }

    float u = Fade(fx);
    float v = Fade(fy);
    float w = Fade(fz);
    float x00 = Lerp(corner[0], corner[1], u);
    float x10 = Lerp(corner[2], corner[3], u);
    float x01 = Lerp(corner[4], corner[5], u);
    float x11 = Lerp(corner[6], corner[7], u);
    return Lerp(Lerp(x00, x10, v), Lerp(x01, x11, v), w);
}

// Distance (in cells) from (x, y, z) in 0..1 to the nearest of one random point per cell, `cells` cells a side, tiling.
float Worley(float x, float y, float z, int32_t cells, uint32_t seed) {
    float px = x * cells;
    float py = y * cells;
    float pz = z * cells;
    int32_t ix = static_cast<int32_t>(std::floor(px));
    int32_t iy = static_cast<int32_t>(std::floor(py));
    int32_t iz = static_cast<int32_t>(std::floor(pz));
    float best = 1e9f;

    for (int32_t dz = -1; dz <= 1; dz++) {
        for (int32_t dy = -1; dy <= 1; dy++) {
            for (int32_t dx = -1; dx <= 1; dx++) {
                int32_t cx = ix + dx;
                int32_t cy = iy + dy;
                int32_t cz = iz + dz;
                uint32_t h = Hash(Wrap(cx, cells), Wrap(cy, cells), Wrap(cz, cells), seed);
                float fx = cx + Unit(h) - px;
                float fy = cy + Unit(h * 0x9E3779B9u + 0x7F4A7C15u) - py;
                float fz = cz + Unit(h * 0x85EBCA6Bu + 0xC2B2AE35u) - pz;
                best = std::min(best, fx * fx + fy * fy + fz * fz);
            }
        }
    }

    return std::min(std::sqrt(best), 1.0f);
}

// Three octaves of Worley, inverted: high inside the blobs, 0..1.
float WorleyFbm(float x, float y, float z, int32_t cells, uint32_t seed) {
    float w = Worley(x, y, z, cells, seed) * 0.625f + Worley(x, y, z, cells * 2, seed + 1) * 0.25f + Worley(x, y, z, cells * 4, seed + 2) * 0.125f;
    return Clamp01(1.0f - w);
}

float PerlinFbm(float x, float y, float z, int32_t period, uint32_t seed) {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float total = 0.0f;

    for (int32_t octave = 0; octave < 4; octave++) {
        int32_t p = period << octave;
        sum += amplitude * Perlin(x * p, y * p, z * p, p, seed + octave);
        total += amplitude;
        amplitude *= 0.5f;
    }

    return Clamp01(sum / total * 0.5f + 0.5f);
}

uint8_t Byte(float v) {
    return static_cast<uint8_t>(Clamp01(v) * 255.0f + 0.5f);
}

// Rank-equalises one channel: each value becomes its rank / (n - 1), an even spread over 0..1, so a threshold in the
// shader cuts away a known share of the layer (raw Worley and Perlin-Worley bunch up mid-range, and a cut there turned
// the whole sky to cloud or none).
void Equalise(std::vector<float>& values) {
    std::vector<uint32_t> order(values.size());
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(), [&values](uint32_t a, uint32_t b) { return values[a] < values[b]; });

    const float scale = 1.0f / static_cast<float>(values.size() - 1);
    std::vector<float> ranked(values.size());

    for (size_t i = 0; i < order.size(); i++) {
        ranked[order[i]] = static_cast<float>(i) * scale;
    }

    values.swap(ranked);
}

// The shape: r Perlin-Worley, g b a Worley at 8, 16 and 32 cells. The detail: r g b Worley at 4, 8 and 16 cells, a 1.
// Every noise channel is equalised before it is packed.
std::vector<uint8_t> MakeNoise(int32_t size, bool shape) {
    const size_t count = static_cast<size_t>(size) * size * size;
    std::vector<float> channel[4];

    for (std::vector<float>& c : channel) {
        c.resize(count);
    }

    size_t at = 0;

    for (int32_t z = 0; z < size; z++) {
        for (int32_t y = 0; y < size; y++) {
            for (int32_t x = 0; x < size; x++) {
                float u = (x + 0.5f) / size;
                float v = (y + 0.5f) / size;
                float w = (z + 0.5f) / size;

                if (shape) {
                    float worley = WorleyFbm(u, v, w, 4, 11);
                    float perlin = PerlinFbm(u, v, w, 4, 3);
                    channel[0][at] = (perlin + 1.0f - worley) / (2.0f - worley);
                    channel[1][at] = WorleyFbm(u, v, w, 8, 23);
                    channel[2][at] = WorleyFbm(u, v, w, 16, 37);
                    channel[3][at] = WorleyFbm(u, v, w, 32, 51);
                } else {
                    channel[0][at] = WorleyFbm(u, v, w, 4, 67);
                    channel[1][at] = WorleyFbm(u, v, w, 8, 79);
                    channel[2][at] = WorleyFbm(u, v, w, 16, 97);
                    channel[3][at] = 1.0f;
                }

                at++;
            }
        }
    }

    for (int32_t c = 0; c < (shape ? 4 : 3); c++) {
        Equalise(channel[c]);
    }

    std::vector<uint8_t> texels(count * 4);

    for (size_t i = 0; i < count; i++) {
        for (int32_t c = 0; c < 4; c++) {
            texels[i * 4 + c] = Byte(channel[c][i]);
        }
    }

    return texels;
}

// A box-filtered mip chain of a cubic RGBA8 volume, level 0 first: the CPU half of a volume, shared by both devices.
std::vector<std::vector<uint8_t>> VolumeLevels(int32_t size, bool shape) {
    std::vector<std::vector<uint8_t>> levels;
    levels.push_back(MakeNoise(size, shape));

    for (int32_t s = size; s > 1; s /= 2) {
        const std::vector<uint8_t>& src = levels.back();
        int32_t half = s / 2;
        std::vector<uint8_t> dst(static_cast<size_t>(half) * half * half * 4);

        for (int32_t z = 0; z < half; z++) {
            for (int32_t y = 0; y < half; y++) {
                for (int32_t x = 0; x < half; x++) {
                    for (int32_t c = 0; c < 4; c++) {
                        uint32_t sum = 0;

                        for (int32_t i = 0; i < 8; i++) {
                            int32_t sx = x * 2 + (i & 1);
                            int32_t sy = y * 2 + ((i >> 1) & 1);
                            int32_t sz = z * 2 + ((i >> 2) & 1);
                            sum += src[((static_cast<size_t>(sz) * s + sy) * s + sx) * 4 + c];
                        }

                        dst[((static_cast<size_t>(z) * half + y) * half + x) * 4 + c] = static_cast<uint8_t>((sum + 4) / 8);
                    }
                }
            }
        }

        levels.push_back(std::move(dst));
    }

    return levels;
}


}
