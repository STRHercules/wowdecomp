#pragma once
#include <cmath>
#include <cstdint>

namespace characterrig {
constexpr unsigned MaxBones=256,MaxKeys=128;
struct Point { float x,y,z; };
struct Bone {
    int32_t key=-1,parent=-1;
    uint32_t flags=0,runtimeFlags=0;
    Point pivot{};
    float palette[16]{};
    bool matrixOverride=false;
};
struct Snapshot {
    uintptr_t model=0,shared=0,header=0,matrices=0;
    uint32_t version=0,count=0,keyCount=0;
    uint16_t keys[MaxKeys]{};
    Bone bones[MaxBones]{};
    char name[128]{};
};
inline bool Finite(Point p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z)
        && fabsf(p.x)<=20 && fabsf(p.y)<=20 && fabsf(p.z)<=20;
}
inline bool Valid(const Snapshot& snapshot) {
    if (snapshot.version!=264 || !snapshot.count || snapshot.count>MaxBones
        || !snapshot.keyCount || snapshot.keyCount>MaxKeys) return false;
    for (unsigned i=0;i<snapshot.keyCount;++i)
        if (snapshot.keys[i]!=0xffff && snapshot.keys[i]>=snapshot.count) return false;
    for (unsigned i=0;i<snapshot.count;++i) {
        const auto& bone=snapshot.bones[i];
        if (!Finite(bone.pivot) || bone.parent<-1 || bone.parent>=static_cast<int32_t>(snapshot.count)
            || bone.parent==static_cast<int32_t>(i) || bone.key<-1 || bone.key>127) return false;
        int32_t parent=bone.parent;
        for (unsigned depth=0;parent>=0;++depth) {
            if (depth>=snapshot.count) return false;
            parent=snapshot.bones[parent].parent;
            if (parent<-1 || parent>=static_cast<int32_t>(snapshot.count)) return false;
        }
        for (float value:bone.palette) if (!std::isfinite(value) || fabsf(value)>20000) return false;
        if (fabsf(bone.palette[3])>.01f || fabsf(bone.palette[7])>.01f
            || fabsf(bone.palette[11])>.01f || fabsf(bone.palette[15]-1)>.01f) return false;
    }
    return true;
}
inline Point PosedPivot(const Bone& bone) {
    const auto& m=bone.palette; const auto p=bone.pivot;
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],
        m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
}
