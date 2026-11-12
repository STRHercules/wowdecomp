#include "ASECM3/BufferMaterial.h"
#include <algorithm>
#include <string>
#include <unordered_set>

namespace ASECM3 {
namespace {
struct Span { uint64_t begin, end; };
class Reader {
    const std::vector<uint8_t>& bytes;
public:
    explicit Reader(const std::vector<uint8_t>& source):bytes(source) {}
    void Range(uint64_t at,uint64_t size,uint64_t begin,uint64_t end) const {
        if (at<begin || at>end || size>end-at || end>bytes.size())
            throw Error("MADD parameter buffer range exceeds its containing block: at="+std::to_string(at)+
                " size="+std::to_string(size)+" block=["+std::to_string(begin)+","+std::to_string(end)+
                ") bytes="+std::to_string(bytes.size()));
    }
    uint64_t UInt(uint64_t at,unsigned size) const {
        Range(at,size,0,bytes.size());
        uint64_t value=0;
        for(unsigned i=0;i<size;++i)value|=uint64_t(bytes[size_t(at+i)])<<(i*8);
        return value;
    }
    void Claim(std::vector<Span>& spans,uint64_t at,uint64_t size,uint64_t begin,uint64_t end) const {
        Range(at,size,begin,end);
        if(size)spans.push_back({at,at+size});
    }
    void Disjoint(std::vector<Span>& spans) const {
        std::sort(spans.begin(),spans.end(),[](const auto& a,const auto& b){return a.begin<b.begin;});
        for(size_t i=1;i<spans.size();++i)if(spans[i].begin<spans[i-1].end)
            throw Error("MADD parameter buffer contains overlapping tables or values");
    }
};
}
BufferMaterial DecodeBufferMaterial(const std::vector<uint8_t>& bytes,
                                   const std::vector<uint32_t>& expectedGroupIds) {
    if(bytes.size()<20 || bytes.size()>32ull*1024*1024)
        throw Error("MADD parameter buffer size is outside its budget");
    Reader read(bytes);
    const uint64_t count=read.UInt(0,4),ids=read.UInt(4,8),offsets=read.UInt(12,8);
    if(count>4096 || count!=expectedGroupIds.size())
        throw Error("MADD parameter group count disagrees with its identifier list or budget");
    std::vector<Span> rootSpans{{0,20}};
    read.Claim(rootSpans,ids,count*4,0,bytes.size());
    read.Claim(rootSpans,offsets,count*8,0,bytes.size());
    std::vector<uint64_t> starts;
    starts.reserve(size_t(count));
    for(uint64_t i=0;i<count;++i)starts.push_back(read.UInt(offsets+i*8,8));
    auto sorted=starts;
    std::sort(sorted.begin(),sorted.end());
    if(std::adjacent_find(sorted.begin(),sorted.end())!=sorted.end())
        throw Error("MADD parameter groups alias the same block");
    const uint64_t rootEnd=sorted.empty()?bytes.size():sorted.front();
    for(const auto& span:rootSpans)if(span.end>rootEnd)
        throw Error("MADD root tables overlap a parameter group");
    read.Disjoint(rootSpans);
    BufferMaterial result;
    result.groups.reserve(size_t(count));
    uint64_t totalFields=0;
    for(uint64_t i=0;i<count;++i) {
        BufferParameterGroup group;
        group.id=uint32_t(read.UInt(ids+i*4,4));group.sourceOffset=starts[size_t(i)];
        if(group.id!=expectedGroupIds[size_t(i)])
            throw Error("MADD parameter group identifier mismatch");
        const auto next=std::upper_bound(sorted.begin(),sorted.end(),group.sourceOffset);
        const uint64_t end=next==sorted.end()?bytes.size():*next;
        read.Range(group.sourceOffset,36,rootEnd,end);
        const uint64_t fields=read.UInt(group.sourceOffset,4);
        totalFields+=fields;
        if(fields>4096 || totalFields>65536)
            throw Error("MADD parameter count exceeds its budget");
        const uint64_t keys=read.UInt(group.sourceOffset+4,8),widths=read.UInt(group.sourceOffset+12,8),
            flags=read.UInt(group.sourceOffset+20,8),values=read.UInt(group.sourceOffset+28,8);
        if(!fields) {
            if(keys||widths||flags||values)throw Error("Empty MADD group has nonempty table offsets");
        } else {
            std::vector<Span> spans{{group.sourceOffset,group.sourceOffset+36}};
            read.Claim(spans,keys,fields*4,group.sourceOffset,end);
            read.Claim(spans,widths,fields*2,group.sourceOffset,end);
            read.Claim(spans,flags,fields,group.sourceOffset,end);
            read.Claim(spans,values,fields*8,group.sourceOffset,end);
            group.parameters.reserve(size_t(fields));
            std::unordered_set<uint32_t> fieldIds;
            for(uint64_t j=0;j<fields;++j) {
                BufferParameter field;
                field.id=uint32_t(read.UInt(keys+j*4,4));field.byteWidth=uint16_t(read.UInt(widths+j*2,2));
                field.flags=uint8_t(read.UInt(flags+j,1));field.sourceOffset=read.UInt(values+j*8,8);
                if(!field.id&&!field.byteWidth&&!field.flags&&!field.sourceOffset)continue; // absent optional slot, retained in the original payload
                if(!fieldIds.insert(field.id).second)throw Error("Duplicate MADD parameter identifier");
                // HotS stores absent optional inputs as a zero-width/null-offset entry.
                if(field.byteWidth||field.sourceOffset)read.Claim(spans,field.sourceOffset,field.byteWidth,group.sourceOffset,end);
                field.bytes.assign(bytes.begin()+size_t(field.sourceOffset),bytes.begin()+size_t(field.sourceOffset)+field.byteWidth);
                group.parameters.push_back(std::move(field));
            }
            read.Disjoint(spans);
        }
        result.groups.push_back(std::move(group));
    }
    return result;
}
}
