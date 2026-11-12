#include "ASECM3/BufferMaterial.h"
#include <iostream>
#include <limits>
using namespace ASECM3;
static void Put(std::vector<uint8_t>& bytes,size_t offset,uint64_t value,unsigned size) {
    for(unsigned i=0;i<size;++i)bytes.at(offset+i)=uint8_t(value>>(i*8));
}
int main() {
    try {
        unsigned cases=0;
        // Deliberately unaligned offsets, as in the source cowl buffer.
        // One root -> one group -> four fields, each a different byte width.
        std::vector<uint8_t> fixture(220);
        Put(fixture,0,1,4);Put(fixture,4,20,8);Put(fixture,12,24,8);Put(fixture,20,0x1234,4);Put(fixture,24,32,8);
        Put(fixture,32,4,4);Put(fixture,36,68,8);Put(fixture,44,84,8);Put(fixture,52,92,8);Put(fixture,60,96,8);
        const unsigned widths[]={4,8,20,32},flags[]={1,0,2,255};size_t cursor=128;
        for(unsigned i=0;i<4;++i) {
            Put(fixture,68+i*4,100+i,4);Put(fixture,84+i*2,widths[i],2);Put(fixture,92+i,flags[i],1);Put(fixture,96+i*8,cursor,8);
            for(unsigned j=0;j<widths[i];++j)fixture[cursor+j]=uint8_t(16*i+j);
            cursor+=widths[i];
        }
        const auto decoded=DecodeBufferMaterial(fixture,{0x1234});
        if(decoded.groups.size()!=1 || decoded.groups[0].parameters.size()!=4)throw Error("Buffer fixture group/field mismatch");
        cursor=128;
        for(unsigned i=0;i<4;++i) {
            const auto& field=decoded.groups[0].parameters[i];
            if(field.id!=100+i || field.byteWidth!=widths[i] || field.flags!=flags[i] || field.sourceOffset!=cursor)throw Error("Buffer fixture metadata mismatch");
            for(unsigned j=0;j<widths[i];++j)if(field.bytes.at(j)!=16*i+j)throw Error("Buffer fixture value mismatch");
            cursor+=widths[i];
        }
        ++cases;
        auto optional=fixture;Put(optional,68,0,4);Put(optional,84,0,2);Put(optional,92,0,1);Put(optional,96,0,8);
        if(DecodeBufferMaterial(optional,{0x1234}).groups[0].parameters.size()!=3)throw Error("Absent optional MADD slot was treated as a value");++cases;
        std::vector<uint8_t> repeated(116);Put(repeated,0,2,4);Put(repeated,4,20,8);Put(repeated,12,28,8);
        Put(repeated,20,0x1234,4);Put(repeated,24,0x1234,4);Put(repeated,28,44,8);Put(repeated,36,80,8);
        if(DecodeBufferMaterial(repeated,{0x1234,0x1234}).groups.size()!=2)throw Error("Separate instances of one shader group were rejected");++cases;
        auto reject=[&](std::vector<uint8_t> bytes,const std::vector<uint32_t>& ids) {
            bool failed=false;try{DecodeBufferMaterial(bytes,ids);}catch(const Error&){failed=true;}
            if(!failed)throw Error("Malformed parameter buffer was accepted");++cases;
        };
        auto mutate=[&](size_t offset,uint64_t value,unsigned size) {auto b=fixture;Put(b,offset,value,size);reject(std::move(b),{0x1234});};
        reject({},{});reject(std::vector<uint8_t>(19),{});
        reject(fixture,{});reject(fixture,{0x9999});
        mutate(0,4097,4);mutate(4,std::numeric_limits<uint64_t>::max(),8);
        mutate(12,std::numeric_limits<uint64_t>::max(),8);mutate(24,219,8);
        mutate(24,0,8);mutate(32,4097,4);mutate(36,219,8);
        mutate(44,219,8);mutate(52,219,8);mutate(60,219,8);
        mutate(96,219,8);mutate(96,68,8);mutate(84,65535,2);
        mutate(72,100,4);mutate(104,128,8);mutate(44,68,8);
        // Empty roots and empty groups are represented without fake values.
        std::vector<uint8_t> empty(20);if(!DecodeBufferMaterial(empty,{}).groups.empty())throw Error("Empty root mismatch");++cases;
        auto group=fixture;group.resize(68);Put(group,32,0,4);for(unsigned offset:{36,44,52,60})Put(group,offset,0,8);
        if(!DecodeBufferMaterial(group,{0x1234}).groups[0].parameters.empty())throw Error("Empty group mismatch");++cases;
        auto badEmpty=group;Put(badEmpty,36,68,8);reject(badEmpty,{0x1234});
        std::cout<<"{\"ok\":true,\"cases\":"<<cases<<",\"scope\":\"MADD indexed parameter structure, exact byte widths/flags and malformed-range rejection; no shader role inference\"}\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
