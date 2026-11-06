#include "Engine/Source/Retail/RetailTerrain.h"
#include "Engine/Source/Retail/IffChunks.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Retail {
namespace {

using namespace Iff;

constexpr uint32_t kVersion = 18;
constexpr size_t kTiles = 4096, kMcnk = 256, kMcnkHeader = 128, kDoodad = 36, kMapObj = 64, kLayer = 16;
constexpr uint32_t kHighResHoles = 0x10000, kHasMcsh = 0x1, kHasMccv = 0x40, kLegacyLiquid = 0x3C;

bool Version(const Chunks& c, std::string& error, const char* what) {
    const auto* v = c.Find(Tag("MVER"));
    if (!v || v->size < 4 || Get<uint32_t>(v->data) != kVersion) {
        error = std::string("Retail ") + what + " is not version 18";
        return false;
    }
    return true;
}

// Cataclysm's 8x8 hole mask (one byte per row) folded into Wrath's 4x4 one: a 2x2 block with any hole is a hole.
uint16_t FoldHoles(const uint8_t* high) {
    uint16_t low = 0;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            const int rows = (high[y * 2] | high[y * 2 + 1]) >> (x * 2);
            if (rows & 0x3) low |= uint16_t(1u << (y * 4 + x));
        }
    return low;
}

std::vector<std::string> Split(const std::vector<uint8_t>& table) {
    std::vector<std::string> names;
    for (size_t o = 0; o < table.size();) {
        size_t end = o;
        while (end < table.size() && table[end]) ++end;
        names.emplace_back(reinterpret_cast<const char*>(table.data() + o), end - o);
        o = end + 1;
    }
    return names;
}

std::string NameAt(const std::vector<uint8_t>& table, uint32_t offset) {
    if (offset >= table.size()) return {};
    size_t end = offset;
    while (end < table.size() && table[end]) ++end;
    return std::string(reinterpret_cast<const char*>(table.data() + offset), end - offset);
}

// MDDF/MODF since 8.1 may name their model by FileDataID (flag fdidFlag) instead of through MMDX/MMID or MWMO/MWID.
// Rebuilds the legacy name table and offset array so nameId is always an index into it.
bool Placements(const Chunks& obj, uint32_t defsTag, size_t stride, size_t flagsAt, uint16_t fdidFlag,
    uint32_t namesTag, uint32_t idsTag, const char* extension, std::vector<uint8_t>& defs, std::vector<uint8_t>& names,
    std::vector<uint8_t>& ids, std::string& error) {
    defs = obj.Bytes(defsTag);
    if (defs.size() % stride) return Fail(error, "Retail ADT placement chunk is not a whole number of entries");
    const auto oldNames = obj.Bytes(namesTag), oldIds = obj.Bytes(idsTag);
    Names table(false);
    std::vector<uint32_t> offsets;
    std::vector<std::pair<std::string, uint32_t>> index; // name -> MMID/MWID slot
    for (size_t i = 0; i < defs.size() / stride; ++i) {
        uint8_t* d = &defs[i * stride];
        const uint32_t nameId = Get<uint32_t>(d);
        const uint16_t flags = Get<uint16_t>(d + flagsAt);
        std::string name;
        if (flags & fdidFlag) name = RetailPath(nameId, extension);
        else if (size_t(nameId) * 4 + 4 <= oldIds.size()) name = NameAt(oldNames, Get<uint32_t>(&oldIds[size_t(nameId) * 4]));
        uint32_t slot = UINT32_MAX;
        for (const auto& e : index) if (e.first == name) { slot = e.second; break; }
        if (slot == UINT32_MAX) {
            slot = uint32_t(offsets.size());
            offsets.push_back(table.Add(name));
            index.emplace_back(name, slot);
        }
        Put(d, slot);
        Put(d + flagsAt, uint16_t(flags & ~fdidFlag));
    }
    names = table.Value();
    ids.resize(offsets.size() * 4);
    if (!offsets.empty()) std::memcpy(ids.data(), offsets.data(), ids.size());
    return true;
}

}

bool PrepareWdt(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, std::vector<WdtTile>& tiles, std::string& error) {
    out.clear();
    tiles.clear();
    Chunks c;
    if (!c.Parse(in.data(), in.size())) return Fail(error, "Retail WDT has a truncated chunk");
    if (!Version(c, error, "WDT")) return false;
    const auto* mphd = c.Find(Tag("MPHD"));
    const auto* main = c.Find(Tag("MAIN"));
    const auto* maid = c.Find(Tag("MAID"));
    if (!mphd || mphd->size < 32 || !main || main->size != kTiles * 8) return Fail(error, "Retail WDT has no MPHD/MAIN");
    const uint32_t flags = Get<uint32_t>(mphd->data);
    // shortcut: WMO-only WDTs (MPHD 0x1, MODF by FileDataID) are refused; housing maps are all terrain.
    if (flags & 0x1) return Fail(error, "Retail WMO-only WDT is not supported yet");
    if (!maid || maid->size < kTiles * 32) return Fail(error, "Retail WDT has no MAID file list");
    for (size_t i = 0; i < kTiles; ++i) {
        if (!(Get<uint32_t>(main->data + i * 8) & 1)) continue;
        const uint8_t* e = maid->data + i * 32; // root, obj0, obj1, tex0, lod, map texture, normals, minimap
        WdtTile t{uint32_t(i % 64), uint32_t(i / 64), Get<uint32_t>(e), Get<uint32_t>(e + 4), Get<uint32_t>(e + 12)};
        if (t.root) tiles.push_back(t);
    }
    std::vector<uint8_t> header(32);
    // Keep vertex colours and sorted doodads. Split tiles always carry 8-bit (or compressed) MCAL, so 3.3.5's big-alpha
    // flag 0x4 must be set whatever retail says: without it the 4-bit unpacker reads past each layer (crash).
    Put(header.data(), (flags & 0xAu) | 0x4u);
    Add(out, Tag("MVER"), Word(kVersion));
    Add(out, Tag("MPHD"), header);
    Add(out, Tag("MAIN"), c.Bytes(Tag("MAIN")));
    return true;
}

bool PrepareAdt(const std::vector<uint8_t>& rootBytes, const std::vector<uint8_t>& objBytes,
    const std::vector<uint8_t>& texBytes, std::vector<uint8_t>& out, std::string& error) {
    out.clear();
    Chunks root, obj, tex;
    if (!root.Parse(rootBytes.data(), rootBytes.size()) || !obj.Parse(objBytes.data(), objBytes.size())
        || !tex.Parse(texBytes.data(), texBytes.size()))
        return Fail(error, "Retail ADT has a truncated chunk");
    if (!Version(root, error, "ADT") || !Version(obj, error, "ADT _obj0") || !Version(tex, error, "ADT _tex0")) return false;
    const auto* mhdr = root.Find(Tag("MHDR"));
    if (!mhdr || mhdr->size < 64) return Fail(error, "Retail ADT has no MHDR");

    std::vector<const Chunk*> rootChunks, objChunks, texChunks;
    for (const auto& ch : root.list) if (ch.tag == Tag("MCNK")) rootChunks.push_back(&ch);
    for (const auto& ch : obj.list) if (ch.tag == Tag("MCNK")) objChunks.push_back(&ch);
    for (const auto& ch : tex.list) if (ch.tag == Tag("MCNK")) texChunks.push_back(&ch);
    if (rootChunks.size() != kMcnk) return Fail(error, "Retail ADT does not have 256 MCNK chunks");

    // Textures: MDID FileDataIDs (8.1+) or a legacy MTEX table. MTXF 0x1 stops 3.3.5 asking for <name>_s.blp,
    // which has no retail\<fdid> equivalent; the diffuse is loaded as named.
    std::vector<std::string> textures;
    if (tex.Find(Tag("MTEX"))) textures = Split(tex.Bytes(Tag("MTEX")));
    else {
        const auto mdid = tex.Bytes(Tag("MDID"));
        for (size_t o = 0; o + 4 <= mdid.size(); o += 4) {
            const uint32_t id = Get<uint32_t>(&mdid[o]);
            textures.push_back(id ? RetailPath(id, ".blp") : "");
        }
    }
    std::vector<uint8_t> mtex;
    for (const auto& t : textures) { mtex.insert(mtex.end(), t.begin(), t.end()); mtex.push_back(0); }
    std::vector<uint8_t> mtxf(textures.size() * 4);
    for (size_t i = 0; i < textures.size(); ++i) Put(&mtxf[i * 4], 1u);

    std::vector<uint8_t> mddf, mmdx, mmid, modf, mwmo, mwid;
    if (!Placements(obj, Tag("MDDF"), kDoodad, 0x22, 0x40, Tag("MMDX"), Tag("MMID"), ".m2", mddf, mmdx, mmid, error)
        || !Placements(obj, Tag("MODF"), kMapObj, 0x38, 0x8, Tag("MWMO"), Tag("MWID"), ".wmo", modf, mwmo, mwid, error))
        return false;
    for (size_t i = 0; i < modf.size() / kMapObj; ++i) {
        uint8_t* d = &modf[i * kMapObj];
        Put(d + 0x38, uint16_t(Get<uint16_t>(d + 0x38) & 0x1)); // 3.3.5 knows only "destroyable"
        Put(d + 0x3E, uint16_t(0)); // retail scale: Wrath reads padding there and cannot scale world objects
    }

    // 3.3.5 draws a placement only while a chunk whose MCRF lists it is visible. Retail lists objects in fewer chunks
    // than they cover (a 31 x 50 yd WMO in two), so they popped in and out with the camera. Every chunk an object's
    // bounds overlap lists it: MODF's extents; doodads have none, so a radius grown with their scale.
    // Placement x/z are tile-space; chunk 0's header holds the tile's world corner (x = 17066.66 - z0, y = 17066.66 - x0).
    constexpr float kMapHalf = 17066.666f, kChunkSize = 100.0f / 3.0f, kDoodadRadius = 12.0f;
    const float originX = kMapHalf - Get<float>(rootChunks[0]->data + 0x6C), originZ = kMapHalf - Get<float>(rootChunks[0]->data + 0x68);
    std::vector<std::vector<uint32_t>> extraDoodads(kMcnk), extraObjects(kMcnk);
    const auto cover = [&](float minX, float minZ, float maxX, float maxZ, uint32_t index, std::vector<std::vector<uint32_t>>& refs) {
        const auto cell = [&](float v, float origin) { return int(std::floor((v - origin) / kChunkSize)); };
        for (int cy = std::max(0, cell(minZ, originZ)); cy <= std::min(15, cell(maxZ, originZ)); ++cy)
            for (int cx = std::max(0, cell(minX, originX)); cx <= std::min(15, cell(maxX, originX)); ++cx)
                refs[size_t(cy) * 16 + size_t(cx)].push_back(index);
    };
    for (uint32_t i = 0; i < mddf.size() / kDoodad; ++i) {
        const uint8_t* d = &mddf[i * kDoodad];
        const float r = kDoodadRadius * std::max(1.0f, Get<uint16_t>(d + 0x20) / 1024.0f);
        cover(Get<float>(d + 8) - r, Get<float>(d + 16) - r, Get<float>(d + 8) + r, Get<float>(d + 16) + r, i, extraDoodads);
    }
    for (uint32_t i = 0; i < modf.size() / kMapObj; ++i) {
        const uint8_t* d = &modf[i * kMapObj];
        cover(Get<float>(d + 0x20), Get<float>(d + 0x28), Get<float>(d + 0x2C), Get<float>(d + 0x34), i, extraObjects);
    }
    const auto merge = [](std::vector<uint8_t> refs, const std::vector<uint32_t>& extra) {
        std::vector<uint32_t> all(refs.size() / 4);
        if (!all.empty()) std::memcpy(all.data(), refs.data(), refs.size());
        all.insert(all.end(), extra.begin(), extra.end());
        std::sort(all.begin(), all.end());
        all.erase(std::unique(all.begin(), all.end()), all.end());
        std::vector<uint8_t> out(all.size() * 4);
        if (!all.empty()) std::memcpy(out.data(), all.data(), out.size());
        return out;
    };

    std::vector<std::vector<uint8_t>> mcnks;
    for (size_t i = 0; i < kMcnk; ++i) {
        const Chunk& r = *rootChunks[i];
        if (r.size < kMcnkHeader) return Fail(error, "Retail ADT MCNK is shorter than its header");
        Chunks rs, ts, os;
        if (!rs.Parse(r.data + kMcnkHeader, r.size - kMcnkHeader)
            || (i < texChunks.size() && !ts.Parse(texChunks[i]->data, texChunks[i]->size))
            || (i < objChunks.size() && !os.Parse(objChunks[i]->data, objChunks[i]->size)))
            return Fail(error, "Retail ADT MCNK has a truncated sub-chunk");

        std::vector<uint8_t> header(r.data, r.data + kMcnkHeader);
        uint32_t flags = Get<uint32_t>(&header[0]);
        if (flags & kHighResHoles) {
            // A split root walks its sub-chunks, so the MCVT/MCNR offset words at 0x14 hold the 8x8 mask instead.
            Put(&header[0x3C], FoldHoles(&header[0x14]));
            flags &= ~kHighResHoles;
        }
        auto mcly = ts.Bytes(Tag("MCLY"));
        // shortcut: more than four layers is cut to the first four; port wotlkconv's limit_layers when a tile needs it.
        if (mcly.size() > 4 * kLayer) mcly.resize(4 * kLayer);
        auto mcrf = merge(os.Bytes(Tag("MCRD")), extraDoodads[i]);
        const auto mcrw = merge(os.Bytes(Tag("MCRW")), extraObjects[i]);
        const uint32_t doodadRefs = uint32_t(mcrf.size() / 4), objectRefs = uint32_t(mcrw.size() / 4);
        mcrf.insert(mcrf.end(), mcrw.begin(), mcrw.end());

        struct Piece { uint32_t tag; std::vector<uint8_t> data; };
        const Piece pieces[] = {
            {Tag("MCVT"), rs.Bytes(Tag("MCVT"))}, {Tag("MCCV"), rs.Bytes(Tag("MCCV"))}, {Tag("MCNR"), rs.Bytes(Tag("MCNR"))},
            {Tag("MCLY"), mcly}, {Tag("MCRF"), mcrf}, {Tag("MCSH"), ts.Bytes(Tag("MCSH"))}, {Tag("MCAL"), ts.Bytes(Tag("MCAL"))},
        };
        std::vector<uint8_t> body;
        uint32_t offsetOf[7] = {}, sizeOf[7] = {};
        for (size_t p = 0; p < 7; ++p) {
            if (pieces[p].data.empty() && pieces[p].tag != Tag("MCRF")) continue;
            offsetOf[p] = uint32_t(8 + kMcnkHeader + body.size()); // from the MCNK chunk's own tag
            sizeOf[p] = uint32_t(pieces[p].data.size() + 8);
            Add(body, pieces[p].tag, pieces[p].data);
        }
        Put(&header[0x0C], uint32_t(mcly.size() / kLayer));
        Put(&header[0x10], doodadRefs);
        Put(&header[0x14], offsetOf[0]);
        Put(&header[0x18], offsetOf[2]);
        Put(&header[0x1C], offsetOf[3]);
        Put(&header[0x20], offsetOf[4]);
        Put(&header[0x24], offsetOf[6]);
        Put(&header[0x28], sizeOf[6]); // CMapChunk::ProcessIffChunks trusts this over MCAL's own size
        Put(&header[0x2C], offsetOf[5]);
        Put(&header[0x30], sizeOf[5]);
        Put(&header[0x34], 0u);        // retail AreaTable IDs mean nothing to 3.3.5
        Put(&header[0x38], objectRefs);
        Put(&header[0x58], 0u);        // MCSE sound emitters name retail SoundEntries; dropped
        Put(&header[0x5C], 0u);
        Put(&header[0x60], 0u);        // no MCLQ: a chunk without liquid declares the empty chunk's 8 bytes
        Put(&header[0x64], 8u);
        Put(&header[0x74], offsetOf[1]);
        Put(&header[0x78], 0u);
        Put(&header[0x7C], 0u);
        flags = offsetOf[5] ? flags | kHasMcsh : flags & ~kHasMcsh;
        flags = offsetOf[1] ? flags | kHasMccv : flags & ~kHasMccv;
        Put(&header[0], flags & ~kLegacyLiquid & 0xFFFFu);
        header.insert(header.end(), body.begin(), body.end());
        mcnks.push_back(std::move(header));
    }

    // Layout: MVER, MHDR, MCIN, the tables MHDR points at (always present: 3.3.5 dereferences each offset), MCNKs,
    // MFBO, MTXF. MHDR offsets count from MHDR's payload; MCIN offsets from the file start.
    Add(out, Tag("MVER"), Word(kVersion));
    const size_t mhdrData = out.size() + 8;
    std::vector<uint8_t> header(64);
    Add(out, Tag("MHDR"), header);
    uint32_t offsets[11] = {}; // mcin mtex mmdx mmid mwmo mwid mddf modf mfbo mh2o mtxf
    const auto place = [&](int slot, uint32_t tag, const std::vector<uint8_t>& data) {
        offsets[slot] = uint32_t(out.size() - mhdrData);
        Add(out, tag, data);
    };
    place(0, Tag("MCIN"), std::vector<uint8_t>(kMcnk * 16));
    const size_t mcinData = out.size() - kMcnk * 16;
    place(1, Tag("MTEX"), mtex);
    place(2, Tag("MMDX"), mmdx);
    place(3, Tag("MMID"), mmid);
    place(4, Tag("MWMO"), mwmo);
    place(5, Tag("MWID"), mwid);
    place(6, Tag("MDDF"), mddf);
    place(7, Tag("MODF"), modf);
    // shortcut: MH2O is left out (retail liquid types and vertex formats differ); port wotlkconv's mh2o.py for water.
    for (size_t i = 0; i < kMcnk; ++i) {
        Put(&out[mcinData + i * 16], uint32_t(out.size()));
        Put(&out[mcinData + i * 16 + 4], uint32_t(mcnks[i].size() + 8));
        Add(out, Tag("MCNK"), mcnks[i]);
    }
    const auto mfbo = root.Bytes(Tag("MFBO"));
    if (!mfbo.empty()) place(8, Tag("MFBO"), mfbo);
    place(10, Tag("MTXF"), mtxf);
    uint8_t* h = &out[mhdrData];
    Put(h, uint32_t(mfbo.empty() ? 0 : 1));
    for (int i = 0; i < 11; ++i) Put(h + 4 + i * 4, offsets[i]);
    const auto mamp = tex.Bytes(Tag("MAMP"));
    h[0x30] = mamp.empty() ? 0 : mamp[0];
    return true;
}

}
