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
    // MH2O: what Liquid::CInstanceVertexData reads (WoW/Source/LiquidSystem/LiquidData.cpp).
    if (U32(a, h + 40)) {
        const size_t m = table(9);
        Check(U32(a, m) == Tag("MH2O"), name + ": MHDR points at MH2O");
        const size_t base = m + 8, size = U32(a, m + 4);
        size_t instances = 0;
        bool ok = true;
        for (int c = 0; ok && c < 256; ++c) {
            const uint32_t at = U32(a, base + c * 12), layers = U32(a, base + c * 12 + 4);
            for (uint32_t l = 0; ok && l < layers; ++l, ++instances) {
                const size_t inst = base + at + l * 24;
                const uint32_t type = U32(a, inst) & 0xFFFF, lvf = U32(a, inst) >> 16;
                const uint32_t w = a[inst + 14], hh = a[inst + 15], n = (w + 1) * (hh + 1);
                const uint32_t bitmap = U32(a, inst + 16), vertices = U32(a, inst + 20), per = lvf == 0 ? 5 : lvf == 1 ? 8 : 1;
                ok = type >= 1 && type <= 4 && lvf <= 2 && a[inst + 12] + w <= 8 && a[inst + 13] + hh <= 8
                    && (!bitmap || bitmap + (w * hh + 7) / 8 <= size) && vertices && vertices + n * per <= size;
            }
        }
        Check(ok, name + ": MH2O instances are stock types in 3.3.5 vertex formats");
        std::printf("%s: %zu liquid instances\n", name.c_str(), instances);
    }
    const uint32_t mmdxSize = U32(a, table(2) + 4), mmidCount = U32(a, table(3) + 4) / 4;
    const uint32_t mwmoSize = U32(a, table(4) + 4), mwidCount = U32(a, table(5) + 4) / 4;
    for (uint32_t i = 0; i < mmidCount; ++i) Check(U32(a, table(3) + 8 + i * 4) < mmdxSize, name + ": MMID inside MMDX");
    for (uint32_t i = 0; i < mwidCount; ++i) Check(U32(a, table(5) + 8 + i * 4) < mwmoSize, name + ": MWID inside MWMO");
    const uint32_t doodads = U32(a, table(6) + 4) / 36, objects = U32(a, table(7) + 4) / 64;
    for (uint32_t i = 0; i < doodads; ++i) Check(U32(a, table(6) + 8 + i * 36) < mmidCount, name + ": MDDF names an MMID slot");
    for (uint32_t i = 0; i < objects; ++i) Check(U32(a, table(7) + 8 + i * 64) < mwidCount, name + ": MODF names an MWID slot");
    const size_t mcin = table(0) + 8;
    // Each doodad must be listed by the chunk its position falls in (chunk 0's header gives the tile corner).
    const auto f32 = [&](size_t at) { float v; std::memcpy(&v, &a[at], 4); return v; };
    const size_t chunk0 = U32(a, mcin) + 8;
    const float originX = 17066.666f - f32(chunk0 + 0x6C), originZ = 17066.666f - f32(chunk0 + 0x68);
    size_t listed = 0, refsTotal = 0;
    for (uint32_t d = 0; d < doodads; ++d) {
        const int cx = int((f32(table(6) + 8 + d * 36 + 8) - originX) / (100.0f / 3)), cy = int((f32(table(6) + 8 + d * 36 + 16) - originZ) / (100.0f / 3));
        if (cx < 0 || cx > 15 || cy < 0 || cy > 15) { ++listed; continue; } // placed outside its own tile
        const size_t hdr = U32(a, mcin + (cy * 16 + cx) * 16) + 8, end = hdr + U32(a, hdr - 4);
        for (size_t p = hdr + 128; p < end; p += 8 + U32(a, p + 4))
            if (U32(a, p) == Tag("MCRF"))
                for (uint32_t r = 0; r < U32(a, hdr + 0x10); ++r) listed += U32(a, p + 8 + r * 4) == d;
    }
    for (int i = 0; i < 256; ++i) refsTotal += U32(a, U32(a, mcin + i * 16) + 8 + 0x10);
    Check(listed == doodads, name + ": every doodad listed by its own chunk (" + std::to_string(listed) + "/" + std::to_string(doodads) + ")");
    std::printf("%s: %u doodads, %zu doodad refs, %u WMOs\n", name.c_str(), doodads, refsTotal, objects);
    // Placed WMOs keep their retail scale for CMap::CreateMapObjDef.
    size_t scaled = 0;
    if (objects) {
        const size_t modf = table(7) + 8;
        for (uint32_t o = 0; o < objects; ++o) {
            const uint32_t scale = U32(a, modf + o * 64 + 0x3C) >> 16;
            Check(scale != 0, name + ": WMO placement " + std::to_string(o) + " keeps its scale");
            scaled += scale != 1024;
        }
    }
    std::printf("%s: %zu of %u WMOs not at scale 1.0\n", name.c_str(), scaled, objects);
    int holeChunks = 0;
    for (int i = 0; i < 256; ++i) {
        const size_t at = U32(a, mcin + i * 16), size = U32(a, mcin + i * 16 + 4);
        if (U32(a, at) != Tag("MCNK") || U32(a, at + 4) + 8 != size) { Check(false, name + ": MCIN entry " + std::to_string(i)); continue; }
        const size_t hdr = at + 8, end = hdr + U32(a, at + 4);
        const uint32_t layers = U32(a, hdr + 0x0C), refs = U32(a, hdr + 0x10) + U32(a, hdr + 0x38), alpha = U32(a, hdr + 0x28);
        bool ok = !(U32(a, hdr) & 0x10000);
        // A hole keeps its fine 8x8 mask and the flag that makes CMapChunk::HoleAt read it.
        const uint32_t folded = U32(a, hdr + 0x3C) & 0xFFFF;
        const bool fine = U32(a, hdr + 0x78) || U32(a, hdr + 0x7C);
        Check(!folded || (fine && (U32(a, hdr) & 0x20000)), name + ": MCNK " + std::to_string(i) + " hole keeps its fine mask");
        holeChunks += folded != 0;
        size_t mcly = 0;
        for (size_t p = hdr + 128; ok && p < end;) {
            const uint32_t tag = U32(a, p), n = U32(a, p + 4);
            if (tag == Tag("MCLY")) { ok = n == layers * 16 && layers <= 4; mcly = p + 8; }
            if (tag == Tag("MCAL") && mcly) // every layer with an alpha map (0x100) starts inside MCAL
                for (uint32_t l = 1; ok && l < layers; ++l) {
                    const uint32_t flags = U32(a, mcly + l * 16 + 4), offset = U32(a, mcly + l * 16 + 8);
                    ok = !(flags & 0x100) || offset < n;
                    // an uncompressed map holds all 4096 texels; flag 0x8000 makes the client read it as 8-bit
                    if (ok && (flags & 0x100) && !(flags & 0x200)) ok = n - offset >= 4096 && (U32(a, hdr) & 0x8000);
                    if (flags & 0x200) continue;
                    // the shaders subtract the alphas from the base, which breaks if they sum past 255
                    if (ok && l == layers - 1 && !(U32(a, mcly + 20) & 0x200) && layers > 1)
                        for (uint32_t t = 0; ok && t < 4096; t += 13) {
                            uint32_t sum = 0;
                            for (uint32_t k = 1; k < layers; ++k)
                                if (!(U32(a, mcly + k * 16 + 4) & 0x200)) sum += a[p + 8 + U32(a, mcly + k * 16 + 8) + t];
                            ok = sum <= 255;
                        }
                }
            if (tag == Tag("MCRF")) ok = n == refs * 4;
            if (tag == Tag("MCAL")) ok = n + 8 == alpha;
            if (tag == Tag("MCNR")) ok = n == 448;
            p += 8 + n;
            ok = ok && p <= end;
        }
        Check(ok, name + ": MCNK " + std::to_string(i) + " walks cleanly");
    }
    std::printf("%s: %d chunks with holes\n", name.c_str(), holeChunks);
}

void CheckLeftEdgeFaded(const std::vector<uint8_t>& a, const std::string& name) {
    const size_t h = 20, mcin = h + U32(a, h + 4) + 8;
    for (int y = 0; y < 16; ++y) {
        const size_t at = U32(a, mcin + y * 16 * 16), hdr = at + 8, end = hdr + U32(a, at + 4);
        const uint32_t layers = U32(a, hdr + 0x0C);
        size_t mcly = 0, mcal = 0;
        for (size_t p = hdr + 128; p < end; p += 8 + U32(a, p + 4)) {
            if (U32(a, p) == Tag("MCLY")) mcly = p + 8;
            if (U32(a, p) == Tag("MCAL")) mcal = p + 8;
        }
        bool clean = true;
        if (mcly && mcal && layers > 1 && !(U32(a, mcly + 20) & 0x200)) // a chunk left compressed had nothing to fade
            for (int row = 0; row < 64; ++row) {
                uint32_t sum = 0;
                for (uint32_t l = 1; l < layers; ++l) sum += a[mcal + U32(a, mcly + l * 16 + 8) + row * 64];
                clean = clean && (sum == 0 || sum >= 250); // 250+: no base was painted here, so the proportions stay
            }
        Check(clean || layers < 2, name + ": left edge of row " + std::to_string(y) + " fades to the base");
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
        // A neighbour tile holding only each border chunk's base texture: the left column must lose every other layer.
        Retail::TileTextures own, left;
        Check(Retail::ReadTileTextures(Load(argv[i + 2]), own), std::string(argv[i]) + ": kept textures read");
        left.chunks = own.chunks;
        for (int y = 0; y < 16 && own.chunks.size() == 256; ++y) left.chunks[y * 16 + 15] = {own.chunks[y * 16][0], 0, 0, 0};
        const Retail::TileTextures* adjoining[4] = {&left, nullptr, nullptr, nullptr};
        std::vector<uint8_t> faded;
        if (ok && Retail::PrepareAdt(root, Load(argv[i + 1]), Load(argv[i + 2]), faded, error, adjoining)) CheckLeftEdgeFaded(faded, argv[i]);
        std::printf("%s: %zu -> %zu bytes\n", argv[i], root.size(), out.size());
    }
    if (failures) return 1;
    std::puts("RetailTerrainTests passed");
    return 0;
}
