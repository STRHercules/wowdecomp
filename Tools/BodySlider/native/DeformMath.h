#pragma once
#include "DeformProfiles.h"
#include <cmath>
#include <cstddef>
#include <cstring>

namespace Deform {
struct Vertex {
    float position[3];
    uint8_t weights[4], bones[4];
    float normal[3], uv[4];
};
static_assert(sizeof(Vertex) == 48, "Stock M2Vertex layout");
inline uint64_t Fingerprint(const Vertex* vertices, uint32_t count) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(vertices);
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < size_t(count)*sizeof(Vertex); ++i) hash = (hash^bytes[i])*1099511628211ull;
    return hash;
}
inline const Profile* Find(const char* path, const Vertex* vertices, uint32_t count) {
    if (!path || !vertices || !count || count > 1000000) return nullptr;
    char normalized[260]{};
    size_t length = 0;
    for (; length < 259 && path[length]; ++length) {
        char c = path[length];
        normalized[length] = c == '\\' ? '/' : c >= 'A' && c <= 'Z' ? char(c-'A'+'a') : c;
    }
    if (path[length]) return nullptr;
    if (length >= 4 && std::strcmp(normalized+length-4, ".mdx") == 0) std::memcpy(normalized+length-4, ".m2", 4);
    for (const auto& profile : kProfiles) {
        if (std::strcmp(normalized, profile.path) != 0) continue;
        bool plausible = false;
        for (uint32_t i = 0; i < profile.variantCount; ++i) plausible |= profile.variants[i].count == count;
        if (!plausible) return nullptr;
        uint64_t hash = Fingerprint(vertices, count);
        for (uint32_t i = 0; i < profile.variantCount; ++i)
            if (profile.variants[i].count == count && profile.variants[i].hash == hash) return &profile;
        return nullptr;
    }
    return nullptr;
}
inline bool HasSlider(const Profile& profile, int index) {
    if (index == 12) return profile.female;
    if (index == 13) return true;
    return index >= 14 && index < 37 && (profile.support&(1u<<(index-14))) != 0;
}
inline float Influence(const Vertex& vertex, const Profile& profile, unsigned flag) {
    unsigned sum = 0;
    for (int k = 0; k < 4; ++k)
        if (vertex.bones[k] < profile.boneCount && (profile.boneFlags[vertex.bones[k]]&flag)) sum += vertex.weights[k];
    return sum/255.f;
}
inline float Weight(const float p[3], const Zone& zone) {
    float distance = 0;
    for (int a = 0; a < 3; ++a) {
        float d = (p[a]-zone.center[a])/zone.radius[a];
        distance += d*d;
    }
    if (distance >= 1) return 0;
    float smooth = 1-distance;
    return smooth*smooth*smooth;
}
inline void Offset(float out[3], const float p[3], const Zone& zone, float influence,
        float value, int axes, float scale, float dx, float dy, float dz) {
    if (value == 0 || influence == 0) return;
    float weight = Weight(p, zone)*influence*value;
    const float move[3] = {dx, dy, dz};
    for (int a = 0; a < 3; ++a)
        out[a] += weight*(move[a]+((axes&(1<<a)) ? (p[a]-zone.center[a])*scale : 0));
}
inline void Position(float out[3], const float p[3], const Profile& profile,
        const float* values, float head, float torso) {
    std::memcpy(out, p, 3*sizeof(float));
    const Zone* z = profile.zones;
    float size = profile.height/1.85f;
    for (int side = 0; side < 2; ++side) {
        float sign = side == 0 ? 1.f : -1.f;
        if (profile.female) Offset(out,p,z[side],torso,values[12],6,.22f,.065f*size,0,.006f*size);
        Offset(out,p,z[2+side],torso,values[13],6,.18f,-.055f*size,0,.005f*size);
        Offset(out,p,z[6+side],head,values[18],0,0,0,0,.012f*size);
        Offset(out,p,z[6+side],head,values[19],0,0,.010f*size,0,0);
        Offset(out,p,z[8+side],head,values[20],7,.25f,0,0,0);
        Offset(out,p,z[8+side],head,values[21],0,0,0,sign*.010f*size,0);
        Offset(out,p,z[8+side],head,values[22],0,0,0,0,.008f*size);
        Offset(out,p,z[13+side],head,values[32],0,0,0,sign*.008f*size,0);
        Offset(out,p,z[13+side],head,values[33],0,0,0,0,.008f*size);
        Offset(out,p,z[15+side],head,values[34],0,0,0,sign*.012f*size,0);
        Offset(out,p,z[17+side],head,values[35],7,.25f,0,0,0);
        Offset(out,p,z[17+side],head,values[36],0,0,0,0,.010f*size);
    }
    Offset(out,p,z[5],head,values[14],7,.15f,0,0,0);
    Offset(out,p,z[5],head,values[15],0,0,0,0,-.03f*size);
    Offset(out,p,z[4],head,values[16],2,.12f,0,0,0);
    Offset(out,p,z[4],head,values[17],4,.12f,0,0,0);
    Offset(out,p,z[10],head,values[23],7,.25f,0,0,0);
    Offset(out,p,z[10],head,values[24],2,.35f,0,0,0);
    Offset(out,p,z[10],head,values[25],0,0,.012f*size,0,0);
    Offset(out,p,z[10],head,values[26],0,0,0,0,.008f*size);
    Offset(out,p,z[11],head,values[27],2,.25f,0,0,0);
    Offset(out,p,z[11],head,values[28],4,.3f,.003f*size,0,0);
    Offset(out,p,z[11],head,values[29],0,0,0,0,.010f*size);
    Offset(out,p,z[12],head,values[30],0,0,0,0,-.012f*size);
    Offset(out,p,z[12],head,values[31],0,0,.010f*size,0,0);
}
inline bool Apply(Vertex& out, const Vertex& bind, const Profile& profile, const float* values, uint32_t index) {
    (void)index;
    out = bind;
    bool active = false;
    for (int slider = 12; slider < 37; ++slider) {
        if (!std::isfinite(values[slider]) || values[slider] < -1 || values[slider] > 1 ||
                (slider < 14 && values[slider] < 0)) return false;
        active |= values[slider] != 0 && HasSlider(profile, slider);
    }
    if (!active) return false;
    float head = Influence(bind, profile, 1), torso = Influence(bind, profile, 2);
    Position(out.position, bind.position, profile, values, head, torso);
    if (std::memcmp(out.position, bind.position, sizeof(bind.position)) == 0) return false;
    // Inverse-transpose Jacobian preserves lighting under the smooth local warp.
    float jacobian[3][3]{};
    float epsilon = .0001f*profile.height;
    for (int a = 0; a < 3; ++a) {
        float left[3], right[3], plus[3], minus[3];
        std::memcpy(left, bind.position, sizeof(left)); std::memcpy(right, bind.position, sizeof(right));
        left[a] -= epsilon; right[a] += epsilon;
        Position(minus,left,profile,values,head,torso); Position(plus,right,profile,values,head,torso);
        for (int b = 0; b < 3; ++b) jacobian[a][b] = (plus[b]-minus[b])/(2*epsilon);
    }
    float cross[3][3];
    for (int a = 0; a < 3; ++a) {
        int b = (a+1)%3, c = (a+2)%3;
        cross[a][0] = jacobian[b][1]*jacobian[c][2]-jacobian[b][2]*jacobian[c][1];
        cross[a][1] = jacobian[b][2]*jacobian[c][0]-jacobian[b][0]*jacobian[c][2];
        cross[a][2] = jacobian[b][0]*jacobian[c][1]-jacobian[b][1]*jacobian[c][0];
    }
    float normal[3]{};
    for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) normal[a] += bind.normal[b]*cross[b][a];
    float length = std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
    if (!std::isfinite(length) || length < 1e-8f) return true;
    for (int a = 0; a < 3; ++a) out.normal[a] = normal[a]/length;
    return true;
}
} // namespace Deform
