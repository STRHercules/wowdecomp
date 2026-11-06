// RetailTerrainTests <wdt.bin> [<root.bin> <obj0.bin> <tex0.bin>]...: prepares a retail WDT and split tiles and checks
// the result is laid out the way CMap::LoadWdt and CMapArea/CMapChunk::ProcessIffChunks read it
// (WoW/Source/WorldClient/Map.cpp, MapArea.cpp, MapChunk.cpp). Inputs are originals saved with RetailAssetProbe --extract.
#include "Engine/Source/Retail/RetailTerrain.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
int failures = 0;
void Check(bool ok, const std::string& what) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; } }
uint32_t Tag(const char* s) { return uint32_t(uint8_t(s[0])) << 24 | uint32_t(uint8_t(s[1])) << 16 | uint32_t(uint8_t(s[2])) << 8 | uint8_t(s[3]); }
uint32_t U32(const std::vector<uint8_t>& b, size_t at) { uint32_t v = 0; if (at + 4 <= b.size()) std::memcpy(&v, &b[at], 4); return v; }
std::vector<uint8_t> Load(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void CheckWdt(const std::vector<uint8_t>& w) {
    Check(U32(w, 0) == Tag("MVER") && U32(w, 12) == Tag("MPHD") && U32(w, 16) == 32, "WDT: MVER then 32-byte MPHD");
    Check(U32(w, 52) == Tag("MAIN") && U32(w, 56) == 32768 && w.size() == 60 + 32768, "WDT: MAIN and nothing after");
    Check(!(U32(w, 20) & ~0xEu), "WDT: only 3.3.5 MPHD flags");
    Check(U32(w, 20) & 0x4u, "WDT: big alpha set, split tiles carry 8-bit MCAL");
}

void CheckAdt(const std::vector<uint8_t>& a, const std::string& name) {
    const size_t h = 20; // MHDR payload: offsets count from here
    Check(U32(a, 0) == Tag("MVER") && U32(a, 12) == Tag("MHDR"), name + ": MVER, MHDR");
    const char* tables[] = {"MCIN", "MTEX", "MMDX", "MMID", "MWMO", "MWID", "MDDF", "MODF"};
    for (int i = 0; i < 8; ++i)
        Check(U32(a, h + U32(a, h + 4 + i * 4)) == Tag(tables[i]), name + ": MHDR points at " + tables[i]);
    Check(!(U32(a, h) & 1) || U32(a, h + U32(a, h + 36)) == Tag("MFBO"), name + ": MFBO flag matches");
    Check(U32(a, h + U32(a, h + 44)) == Tag("MTXF"), name + ": MTXF");
    const auto table = [&](int i) { return h + U32(a, h + 4 + i * 4); };
    const uint32_t mmdxSize = U32(a, table(2) + 4), mmidCount = U32(a, table(3) + 4) / 4;
    const uint32_t mwmoSize = U32(a, table(4) + 4), mwidCount = U32(a, table(5) + 4) / 4;
    for (uint32_t i = 0; i < mmidCount; ++i) Check(U32(a, table(3) + 8 + i * 4) < mmdxSize, name + ": MMID inside MMDX");
    for (uint32_t i = 0; i < mwidCount; ++i) Check(U32(a, table(5) + 8 + i * 4) < mwmoSize, name + ": MWID inside MWMO");
    const uint32_t doodads = U32(a, table(6) + 4) / 36, objects = U32(a, table(7) + 4) / 64;
    for (uint32_t i = 0; i < doodads; ++i) Check(U32(a, table(6) + 8 + i * 36) < mmidCount, name + ": MDDF names an MMID slot");
    for (uint32_t i = 0; i < objects; ++i) Check(U32(a, table(7) + 8 + i * 64) < mwidCount, name + ": MODF names an MWID slot");
    const size_t mcin = table(0) + 8;
    for (int i = 0; i < 256; ++i) {
        const size_t at = U32(a, mcin + i * 16), size = U32(a, mcin + i * 16 + 4);
        if (U32(a, at) != Tag("MCNK") || U32(a, at + 4) + 8 != size) { Check(false, name + ": MCIN entry " + std::to_string(i)); continue; }
        const size_t hdr = at + 8, end = hdr + U32(a, at + 4);
        const uint32_t layers = U32(a, hdr + 0x0C), refs = U32(a, hdr + 0x10) + U32(a, hdr + 0x38), alpha = U32(a, hdr + 0x28);
        bool ok = !(U32(a, hdr) & 0x10000);
        size_t mcly = 0;
        for (size_t p = hdr + 128; ok && p < end;) {
            const uint32_t tag = U32(a, p), n = U32(a, p + 4);
            if (tag == Tag("MCLY")) { ok = n == layers * 16 && layers <= 4; mcly = p + 8; }
            if (tag == Tag("MCAL") && mcly) // every layer with an alpha map (0x100) starts inside MCAL
                for (uint32_t l = 1; ok && l < layers; ++l)
                    ok = !(U32(a, mcly + l * 16 + 4) & 0x100) || U32(a, mcly + l * 16 + 8) < n;
            if (tag == Tag("MCRF")) ok = n == refs * 4;
            if (tag == Tag("MCAL")) ok = n + 8 == alpha;
            if (tag == Tag("MCNR")) ok = n == 448;
            p += 8 + n;
            ok = ok && p <= end;
        }
        Check(ok, name + ": MCNK " + std::to_string(i) + " walks cleanly");
    }
}
}

int main(int argc, char** argv) {
    if (argc < 2 || (argc - 2) % 3) {
        std::fprintf(stderr, "Usage: RetailTerrainTests <wdt.bin> [<root.bin> <obj0.bin> <tex0.bin>]...\n");
        return 2;
    }
    std::string error;
    std::vector<uint8_t> out;
    std::vector<Retail::WdtTile> tiles;
    Check(Retail::PrepareWdt(Load(argv[1]), out, tiles, error), error);
    CheckWdt(out);
    std::printf("%s: %zu tiles\n", argv[1], tiles.size());
    Check(!Retail::PrepareWdt(std::vector<uint8_t>(out.begin(), out.begin() + 10), out, tiles, error), "truncated WDT is refused");
    for (int i = 2; i + 2 < argc; i += 3) {
        const auto root = Load(argv[i]);
        const bool ok = Retail::PrepareAdt(root, Load(argv[i + 1]), Load(argv[i + 2]), out, error);
        Check(ok, std::string(argv[i]) + ": " + error);
        if (ok) CheckAdt(out, argv[i]);
        std::printf("%s: %zu -> %zu bytes\n", argv[i], root.size(), out.size());
    }
    if (failures) return 1;
    std::puts("RetailTerrainTests passed");
    return 0;
}
