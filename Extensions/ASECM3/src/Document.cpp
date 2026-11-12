#include "ASECM3/Document.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
namespace ASECM3 {
const uint8_t* Document::Data(size_t offset, size_t length) const {
    if (offset > m_bytes.size() || length > m_bytes.size()-offset) throw Error("M3 byte range outside input");
    return m_bytes.data()+offset;
}
uint32_t Document::U32(size_t p) const {
    const auto* b=Data(p,4);
    return uint32_t(b[0]) | uint32_t(b[1])<<8 | uint32_t(b[2])<<16 | uint32_t(b[3])<<24;
}
uint16_t Document::U16(size_t p) const { const auto* b=Data(p,2); return uint16_t(b[0]) | uint16_t(b[1])<<8; }
uint8_t Document::U8(size_t p) const { return *Data(p,1); }
float Document::F32(size_t p) const {
    uint32_t bits=U32(p); float value; std::memcpy(&value,&bits,4);
    if (!std::isfinite(value)) throw Error("M3 contains non-finite float at byte "+std::to_string(p));
    return value;
}
void Document::ValidateReference(const Reference& ref, std::string_view expected) const {
    if (!ref.count) return; // empty reference section/flags may be unspecified
    if (ref.section >= m_sections.size()) throw Error("M3 reference section outside index");
    const auto& section=m_sections[ref.section];
    if (ref.count > section.count) throw Error("M3 reference count exceeds section count: "+section.tag);
    if (!expected.empty() && expected != section.tag) throw Error("M3 reference type mismatch: expected "+std::string(expected)+", found "+section.tag);
}
const Section& Document::Resolve(const Reference& ref, std::string_view expected) const {
    ValidateReference(ref,expected);
    if (!ref.count) throw Error("Cannot dereference empty M3 reference");
    if (!expected.empty() && !m_sections[ref.section].layout)
        throw Error("Unsupported version for decoded M3 section: "+m_sections[ref.section].tag);
    return m_sections[ref.section];
}
Record Document::At(const Reference& ref, uint32_t index) const {
    Resolve(ref);
    if (index >= ref.count) throw Error("M3 record outside reference");
    return Record(*this,ref.section,index);
}
std::string Document::String(const Reference& ref) const {
    if (!ref.count) return {};
    const auto& section=Resolve(ref,"CHAR");
    const char* begin=reinterpret_cast<const char*>(Data(section.offset,ref.count));
    const char* end=static_cast<const char*>(std::memchr(begin,0,ref.count));
    if (!end) throw Error("Unterminated M3 CHAR reference");
    return std::string(begin,end);
}
Document Document::Parse(std::vector<uint8_t> bytes,bool validateReferences) {
    if (bytes.size()<20 || bytes.size()>512ull*1024*1024) throw Error("M3 input size is outside supported limits");
    Document out; out.m_bytes=std::move(bytes);
    const uint32_t magic=out.U32(0);
    out.m_mdVersion=magic==0x4d443334 ? 34 : magic==0x4d443333 ? 33 : 0;
    if (!out.m_mdVersion) throw Error("Input is not MD33/MD34");
    const size_t headerSize=out.m_mdVersion==34 ? 24 : 20;
    out.Data(0,headerSize);
    const uint32_t indexOffset=out.U32(4), count=out.U32(8);
    if (count==0 || count>1000000 || indexOffset<headerSize) throw Error("Invalid M3 index header");
    out.Data(indexOffset,size_t(count)*16);
    out.m_model={out.U32(12),out.U32(16),out.m_mdVersion==34?out.U32(20):0};
    out.m_sections.reserve(count);
    std::vector<size_t> boundaries{out.m_bytes.size(),indexOffset};
    for (uint32_t i=0;i<count;++i) {
        const size_t address=indexOffset+size_t(i)*16;
        Section section;
        for (int c=3;c>=0;--c) { char ch=static_cast<char>(out.U8(address+c)); if (ch) section.tag+=ch; }
        section.offset=out.U32(address+4); section.count=out.U32(address+8); section.version=out.U32(address+12);
        if (section.offset>out.m_bytes.size()) throw Error("M3 section offset outside input: "+section.tag);
        if (section.count && section.offset>=indexOffset && section.offset<indexOffset+size_t(count)*16)
            throw Error("M3 section overlaps index: "+section.tag);
        section.layout=FindLayout(section.tag,section.version,out.m_mdVersion);
        boundaries.push_back(section.offset);
        out.m_sections.push_back(std::move(section));
    }
    std::sort(boundaries.begin(),boundaries.end());
    for (auto& section:out.m_sections) {
        auto end=std::upper_bound(boundaries.begin(),boundaries.end(),section.offset);
        section.span=end==boundaries.end()?0:*end-section.offset;
        // Eleven original HotS mount meshes have one 128-byte v2 record, not the conjectured 136-byte layout.
        if(out.m_mdVersion==34&&section.tag=="PHCL"&&section.version==2&&section.count==1&&section.span==128)
            section.layout=FindLayout("PHC2",2,34);
        if (section.layout) {
            const uint64_t required=uint64_t(section.layout->size)*section.count;
            if (required>section.span) throw Error("Truncated/overlapping M3 section "+section.tag+" v"+std::to_string(section.version));
            out.Data(section.offset,static_cast<size_t>(required));
        }
    }
    out.ValidateReference(out.m_model,"MODL");
    if (out.m_model.count!=1) throw Error("M3 must reference exactly one model record");
    if(validateReferences)out.ValidateReferences();
    return out;
}
void Document::ValidateReferences()const {
    // Validate all schema-known references, including nested animation tracks.
    for (uint32_t i=0;i<m_sections.size();++i) {
        const auto& section=m_sections[i]; if (!section.layout) continue;
        for (uint32_t f=0;f<section.layout->fieldCount;++f) {
            const auto& field=section.layout->fields[f];
            if (std::string_view(field.kind)!="reference") continue;
            for (uint32_t r=0;r<section.count;++r) {
                try { ValidateReference(Record(*this,i,r).Ref(field.name),field.referenceTag); }
                catch (const Error& e) { throw Error(section.tag+"["+std::to_string(r)+"]."+field.name+": "+e.what()); }
            }
        }
    }
}
Record::Record(const Document& doc,uint32_t section,uint32_t record):m_document(&doc),m_section(section),m_record(record) {
    if (section>=doc.m_sections.size() || record>=doc.m_sections[section].count || !doc.m_sections[section].layout)
        throw Error("Unknown or invalid M3 record layout");
}
const Layout& Record::Schema() const { return *m_document->m_sections[m_section].layout; }
bool Record::Has(std::string_view field) const { return Schema().Find(field)!=nullptr; }
size_t Record::Address(const Field& field) const {
    return m_document->m_sections[m_section].offset+size_t(m_record)*Schema().size+field.offset;
}
uint32_t Record::UInt(std::string_view name) const {
    auto* field=Schema().Find(name); if (!field) throw Error("Missing M3 field: "+std::string(name));
    const size_t address=Address(*field);
    if (std::string_view(field->kind)=="uint8") return m_document->U8(address);
    if (std::string_view(field->kind)=="uint16") return m_document->U16(address);
    if (std::string_view(field->kind)=="uint32") return m_document->U32(address);
    throw Error("M3 field is not an unsigned scalar: "+std::string(name));
}
uint64_t Record::UInt64(std::string_view name) const {
    const auto* field=Schema().Find(name);
    if (!field || std::string_view(field->kind)!="uint64") throw Error("M3 field is not uint64");
    const size_t address=Address(*field);
    return uint64_t(m_document->U32(address)) | (uint64_t(m_document->U32(address+4))<<32);
}
int32_t Record::Int(std::string_view name) const {
    auto* field=Schema().Find(name); if (!field) throw Error("Missing M3 field: "+std::string(name));
    if (std::string_view(field->kind)=="int16") return static_cast<int16_t>(m_document->U16(Address(*field)));
    if (std::string_view(field->kind)=="int32") return static_cast<int32_t>(m_document->U32(Address(*field)));
    throw Error("M3 field is not a signed scalar");
}
float Record::Float(std::string_view name) const {
    auto* field=Schema().Find(name);
    if (!field || std::string_view(field->kind)!="float") throw Error("M3 field is not a float: "+std::string(name));
    return m_document->F32(Address(*field));
}
Reference Record::Ref(std::string_view name) const {
    auto* field=Schema().Find(name);
    if (!field || std::string_view(field->kind)!="reference") throw Error("M3 field is not a reference: "+std::string(name));
    const size_t address=Address(*field);
    return {m_document->U32(address),m_document->U32(address+4),field->size==12?m_document->U32(address+8):0};
}
std::string Record::String(std::string_view name) const { return m_document->String(Ref(name)); }
std::vector<uint8_t> Record::Bytes() const {
    const auto& section=m_document->Sections().at(m_section);
    const auto size=Schema().size;
    const auto* begin=m_document->Data(section.offset+size_t(m_record)*size,size);
    return std::vector<uint8_t>(begin,begin+size);
}
}
