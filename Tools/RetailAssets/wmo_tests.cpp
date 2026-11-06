// RetailWmoTests [root.bin [group.bin...]]: checks that prepared WMOs are laid out the way the stock
// positional readers walk them (CMapObj::Load, CMapObjGroup::CreateDataPointers/CreateOptionalDataPointers
// in WoW/Source/WorldClient/MapObjRead.cpp). Without arguments it runs on a synthetic retail WMO.
#include "Engine/Source/Retail/RetailWmo.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
int failures = 0;
void Check(bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; } }

uint32_t Tag(const char* s) { return uint32_t(uint8_t(s[0])) << 24 | uint32_t(uint8_t(s[1])) << 16 | uint32_t(uint8_t(s[2])) << 8 | uint8_t(s[3]); }
template <class T> T Get(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }
void Add(std::vector<uint8_t>& out, const char* tag, const std::vector<uint8_t>& data) {
    const size_t at = out.size();
    out.resize(at + 8 + data.size());
    const uint32_t t = Tag(tag), n = uint32_t(data.size());
    std::memcpy(&out[at], &t, 4);
    std::memcpy(&out[at + 4], &n, 4);
    if (!data.empty()) std::memcpy(&out[at + 8], data.data(), data.size());
}
template <class T> void Push(std::vector<uint8_t>& v, T value) { const auto* p = reinterpret_cast<const uint8_t*>(&value); v.insert(v.end(), p, p + sizeof value); }

struct Walk {
    const std::vector<uint8_t>& b;
    size_t o;
    // Returns the payload of the chunk at the current position if it carries the expected tag.
    const uint8_t* Next(const char* tag, uint32_t* size = nullptr) {
        if (o + 8 > b.size() || Get<uint32_t>(&b[o]) != Tag(tag)) { std::fprintf(stderr, "  expected %s at %zu\n", tag, o); return nullptr; }
        const uint32_t n = Get<uint32_t>(&b[o + 4]);
        const uint8_t* data = &b[o + 8];
        if (size) *size = n;
        o += 8 + n;
        return o <= b.size() ? data : nullptr;
    }
};

bool LegacyRoot(const std::vector<uint8_t>& b, std::string& motx) {
    Walk w{b, 0};
    uint32_t size = 0;
    if (!w.Next("MVER") || !w.Next("MOHD")) return false;
    const uint8_t* names = w.Next("MOTX", &size);
    if (!names) return false;
    motx.assign(reinterpret_cast<const char*>(names), size);
    for (const char* tag : {"MOMT", "MOGN", "MOGI", "MOSB", "MOPV", "MOPT", "MOPR", "MOVV", "MOVB", "MOLT", "MODS", "MODN", "MODD", "MFOG"})
        if (!w.Next(tag)) return false;
    return w.o == b.size() || (w.o + 4 <= b.size() && Get<uint32_t>(&b[w.o]) == Tag("MCVP"));
}

bool LegacyGroup(const std::vector<uint8_t>& b) {
    Walk outer{b, 0};
    uint32_t size = 0;
    if (!outer.Next("MVER") || !outer.Next("MOGP", &size) || outer.o != b.size()) return false;
    const uint32_t flags = Get<uint32_t>(&b[20 + 8]);
    Walk w{b, 20 + 68};
    for (const char* tag : {"MOPY", "MOVI", "MOVT", "MONR", "MOTV", "MOBA"}) if (!w.Next(tag)) return false;
    if ((flags & 0x200) && !w.Next("MOLR")) return false;
    if ((flags & 0x800) && !w.Next("MODR")) return false;
    if ((flags & 0x1) && (!w.Next("MOBN") || !w.Next("MOBR"))) return false;
    if (flags & 0x400) return false;
    if ((flags & 0x4) && !w.Next("MOCV")) return false;
    if ((flags & 0x1000) && !w.Next("MLIQ")) return false;
    if (flags & 0x20000) return false;
    if ((flags & 0x2000000) && !w.Next("MOTV")) return false;
    if ((flags & 0x1000000) && !w.Next("MOCV")) return false;
    return w.o == b.size();
}

std::vector<uint8_t> SyntheticRoot() {
    std::vector<uint8_t> mohd(64), momt(64), mogi(32), modi, modd(40), gfid, mavg(48);
    mohd[0] = 1; mohd[4] = 1; mohd[60] = 0x1F; mohd[62] = 4; // flags 0x41F, numLod 4
    const uint32_t shader = 23, tex2 = 7225255;
    std::memcpy(&momt[4], &shader, 4);       // newer shader, diffuse in texture 2
    std::memcpy(&momt[24], &tex2, 4);
    Push<uint32_t>(modi, 6008935);
    Push<uint32_t>(gfid, 6432330); Push<uint32_t>(gfid, 0); // group 0, then a LOD entry
    const uint32_t colour = 0x00405060;
    std::memcpy(&mavg[0x14], &colour, 4);
    std::vector<uint8_t> out;
    Add(out, "MVER", {17, 0, 0, 0});
    Add(out, "MOHD", mohd);
    Add(out, "MOMT", momt);
    Add(out, "MOMX", std::vector<uint8_t>(64));
    Add(out, "MOGN", {'g', 0, 0, 0});
    Add(out, "MOGI", mogi);
    Add(out, "MGI2", std::vector<uint8_t>(16));
    for (const char* tag : {"MOPV", "MOPT", "MOPR", "MOPE", "MOLT"}) Add(out, tag, {});
    Add(out, "MODS", std::vector<uint8_t>(32));
    Add(out, "MODI", modi);
    Add(out, "MODD", modd);
    Add(out, "MFOG", std::vector<uint8_t>(48));
    Add(out, "MAVG", mavg);
    Add(out, "GFID", gfid);
    return out;
}

std::vector<uint8_t> SyntheticGroup(uint8_t mocvRgb = 0) { // mocvRgb: 0 no MOCV, else every vertex this grey

    std::vector<uint8_t> header(68), mpy2, movi, movt, monr, motv, moba(24);
    const uint32_t flags = 0x80000009 | 0x400;
    std::memcpy(&header[8], &flags, 4);
    header[60] = 7; // split-group index
    Push<uint16_t>(mpy2, 0x20); Push<uint16_t>(mpy2, 300); // one triangle: flags 0x20, material 300
    for (uint16_t i : {0, 1, 2}) Push<uint16_t>(movi, i);
    for (float v : {0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 0.f, 10.f, 5.f}) Push<float>(movt, v);
    monr = movt;
    motv.resize(24);
    const uint16_t count = 3, wideMaterial = 2;
    std::memcpy(&moba[16], &count, 2);
    moba[22] = 0x2;
    std::memcpy(&moba[10], &wideMaterial, 2);
    std::vector<uint8_t> inner;
    Add(inner, "MOGX", {0, 0, 0, 0});
    Add(inner, "MPY2", mpy2);
    Add(inner, "MOVI", movi);
    Add(inner, "MOVT", movt);
    Add(inner, "MONR", monr);
    for (int i = 0; i < 4; ++i) Add(inner, "MOTV", motv);
    Add(inner, "MOBA", moba);
    Add(inner, "MOBS", std::vector<uint8_t>(24));
    Add(inner, "MOBN", std::vector<uint8_t>(16));
    Add(inner, "MOBR", {0, 0});
    if (mocvRgb) Add(inner, "MOCV", {mocvRgb, mocvRgb, mocvRgb, 0xFF, mocvRgb, mocvRgb, mocvRgb, 0x80, mocvRgb, mocvRgb, mocvRgb, 0xFF});
    Add(inner, "MOC2", std::vector<uint8_t>(12));
    header.insert(header.end(), inner.begin(), inner.end());
    std::vector<uint8_t> out;
    Add(out, "MVER", {17, 0, 0, 0});
    Add(out, "MOGP", header);
    return out;
}

std::vector<uint8_t> Load(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
}

int main(int argc, char** argv) {
    std::string error, motx;
    std::vector<uint8_t> out;
    std::vector<uint32_t> groups;

    const auto root = SyntheticRoot();
    Check(!Retail::IsWmoGroup(root), "root is not a group");
    Check(Retail::PrepareWmoRoot(root, out, groups, error), error.c_str());
    Check(LegacyRoot(out, motx), "synthetic root walks in stock order");
    Check(motx.find("retail\\7225255.blp") != std::string::npos, "texture FDID becomes a retail name");
    Check(groups.size() == 1 && groups[0] == 6432330, "GFID gives group 0");
    if (out.size() > 20 + 64) {
        const uint8_t* mohd = &out[20];
        Check(Get<uint32_t>(mohd + 60) == 0xF, "header flags masked and numLod cleared");
        Check(Get<uint32_t>(mohd + 0x1C) == 0x00405060, "ambient colour taken from MAVG");
        const uint8_t* m = &out[20 + 64 + 8 + Get<uint32_t>(&out[20 + 64 + 4]) + 8]; // first MOMT entry
        Check(Get<uint32_t>(m + 4) == 0, "newer shader falls back to diffuse");
        Check(motx.compare(Get<uint32_t>(m + 12), 18, "retail\\7225255.blp") == 0, "diffuse promoted to texture 1");
    }
    // Shader 23 with an environment map in texture 1 (dr_shine.blp on 12hu_human_house02) still draws texture 2.
    auto shine = root;
    const uint32_t envMap = 1111;
    std::memcpy(&shine[92 + 12], &envMap, 4); // MVER 12 + MOHD 72 + MOMT header 8, texture 1 at +12
    Check(Retail::PrepareWmoRoot(shine, out, groups, error) && LegacyRoot(out, motx), "shader-23 root prepares");
    if (out.size() > 20 + 64) {
        const uint8_t* m = &out[20 + 64 + 8 + Get<uint32_t>(&out[20 + 64 + 4]) + 8];
        Check(motx.compare(Get<uint32_t>(m + 12), 18, "retail\\7225255.blp") == 0, "shader 23 draws texture 2 over an environment map");
    }
    std::vector<uint8_t> broken(root.begin(), root.end() - 3);
    Check(!Retail::PrepareWmoRoot(broken, out, groups, error), "truncated root is refused");

    const auto group = SyntheticGroup();
    Check(Retail::IsWmoGroup(group), "group is a group");
    Check(Retail::PrepareWmoGroup(group, out, error), error.c_str());
    Check(LegacyGroup(out), "synthetic group walks in stock order");
    if (out.size() > 20 + 68 + 8 + 2) {
        const uint32_t flags = Get<uint32_t>(&out[28]);
        Check(flags == (0x9 | 0x2000000), "group flags describe what was written");
        Check(out[20 + 60] == 0, "split-group index cleared");
        Check(out[20 + 68 + 8] == 0x20 && out[20 + 68 + 9] == 0xFF, "MPY2 to MOPY, wide material marked collision-only");
    }
    Check(!Retail::PrepareWmoGroup(std::vector<uint8_t>(group.begin(), group.begin() + 30), out, error), "truncated group is refused");
    // An all-white MOCV is shader-23 weights, not light: dropped. A real one stays.
    Check(Retail::PrepareWmoGroup(SyntheticGroup(0xFF), out, error) && LegacyGroup(out) && !(Get<uint32_t>(&out[28]) & 0x4),
        "white MOCV is dropped");
    Check(Retail::PrepareWmoGroup(SyntheticGroup(0x40), out, error) && LegacyGroup(out) && (Get<uint32_t>(&out[28]) & 0x4),
        "lit MOCV is kept");

    for (int i = 1; i < argc; ++i) {
        const auto bytes = Load(argv[i]);
        const bool isGroup = Retail::IsWmoGroup(bytes);
        const bool ok = isGroup ? Retail::PrepareWmoGroup(bytes, out, error) : Retail::PrepareWmoRoot(bytes, out, groups, error);
        Check(ok, error.c_str());
        Check(ok && (isGroup ? LegacyGroup(out) : LegacyRoot(out, motx)), argv[i]);
        std::printf("%s: %s %zu -> %zu bytes\n", argv[i], isGroup ? "group" : "root", bytes.size(), out.size());
    }
    if (failures) return 1;
    std::puts("RetailWmoTests passed");
    return 0;
}
