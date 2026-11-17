#include "character-rig-schema.h"
#include <cassert>
#include <cstdio>

int main() {
    unsigned checks=0;
    characterrig::Snapshot valid{}; valid.version=264; valid.count=3; valid.keyCount=2;
    valid.keys[0]=0; valid.keys[1]=2;
    for (unsigned i=0;i<valid.count;++i) {
        auto& bone=valid.bones[i]; bone.parent=i?static_cast<int32_t>(i)-1:-1;
        bone.palette[0]=bone.palette[5]=bone.palette[10]=bone.palette[15]=1;
    }
    assert(characterrig::Valid(valid)); ++checks;
    auto bad=valid; bad.count=0; assert(!characterrig::Valid(bad)); ++checks;
    bad=valid; bad.count=257; assert(!characterrig::Valid(bad)); ++checks;
    bad=valid; bad.version=260; assert(!characterrig::Valid(bad)); ++checks;
    bad=valid; bad.keyCount=129; assert(!characterrig::Valid(bad)); ++checks;
    bad=valid; bad.keys[0]=9; assert(!characterrig::Valid(bad)); ++checks;
    bad=valid; bad.bones[0].parent=2; assert(!characterrig::Valid(bad)); ++checks;
    bad=valid; bad.bones[2].parent=2; assert(!characterrig::Valid(bad)); ++checks;
    bad=valid; bad.bones[2].parent=3; assert(!characterrig::Valid(bad)); ++checks;
    bad=valid; bad.bones[0].key=-2; assert(!characterrig::Valid(bad)); ++checks;
    bad=valid; bad.bones[0].pivot.x=NAN; assert(!characterrig::Valid(bad)); ++checks;
    bad=valid; bad.bones[0].palette[2]=NAN; assert(!characterrig::Valid(bad)); ++checks;
    bad=valid; bad.bones[0].palette[15]=0; assert(!characterrig::Valid(bad)); ++checks;
    valid.keys[0]=0xffff; assert(characterrig::Valid(valid)); ++checks;
    auto& bone=valid.bones[1]; bone.pivot={1,2,3}; bone.palette[12]=4; bone.palette[13]=5; bone.palette[14]=6;
    const auto p=characterrig::PosedPivot(bone); assert(p.x==5 && p.y==7 && p.z==9); ++checks;
    printf("{\"passed\":true,\"checks\":%u,\"scope\":\"read-only skeleton validation; no live character posing\"}\n",checks);
}
