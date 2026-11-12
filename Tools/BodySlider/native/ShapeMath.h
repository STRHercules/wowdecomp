#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Shape {
constexpr size_t SliderCount = 37;
inline constexpr const char* Names[SliderCount] = {
    "Height", "HeadSize", "NeckLength", "ShoulderWidth", "ArmLength", "ArmGirth",
    "ChestSize", "WaistWidth", "TorsoLength", "HipWidth", "LegLength", "LegGirth",
    "BreastSize", "ButtSize", "JawSize", "JawLength", "FaceWidth", "FaceLength",
    "BrowHeight", "BrowDepth", "EyeSize", "EyeSpacing", "EyeHeight", "NoseSize",
    "NoseWidth", "NoseLength", "NoseHeight", "MouthWidth", "LipFullness", "MouthHeight",
    "ChinLength", "ChinDepth", "CheekWidth", "CheekHeight", "JawWidth", "EarSize", "EarHeight"
};
struct Matrix { float m[4][4]; };
struct Transform {
    float scale[3] = {1, 1, 1};
    float move[3] = {};
    bool Active() const {
        for (int a = 0; a < 3; ++a)
            if (scale[a] != 1 || move[a] != 0) return true;
        return false;
    }
};
inline bool ValidValue(double value, int slider = -1) {
    return std::isfinite(value) && value >= (slider == 12 || slider == 13 ? 0 : -1) && value <= 1;
}
inline int Find(const char* name) {
    for (size_t i = 0; i < SliderCount; ++i) if (std::strcmp(name, Names[i]) == 0) return static_cast<int>(i);
    return -1;
}
inline bool Guid(const char* text, size_t length, uint64_t& out) {
    if (length == 18 && text[0] == '0' && text[1] == 'x') { text += 2; length -= 2; }
    if (length != 16) return false;
    uint64_t value = 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        unsigned digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
        if (digit > 15) return false;
        value = (value << 4) | digit;
    }
    if (!value) return false;
    out = value;
    return true;
}
// Scale in bind axes before rotation, keeping the animated bind pivot stationary.
inline Matrix Apply(const Matrix& local, const float pivot[3], const Transform& transform) {
    Matrix shaped = local;
    for (int a = 0; a < 3; ++a) {
        for (int b = 0; b < 3; ++b) {
            shaped.m[a][b] = local.m[a][b] * transform.scale[a];
            shaped.m[3][b] -= pivot[a] * (shaped.m[a][b] - local.m[a][b]);
        }
    }
    for (int a = 0; a < 3; ++a) shaped.m[3][a] += transform.move[a];
    return shaped;
}
inline Matrix Multiply(const Matrix& left, const Matrix& right) {
    Matrix result{};
    for (int a = 0; a < 4; ++a)
        for (int b = 0; b < 4; ++b)
            for (int c = 0; c < 4; ++c) result.m[a][b] += left.m[a][c] * right.m[c][b];
    return result;
}
}
