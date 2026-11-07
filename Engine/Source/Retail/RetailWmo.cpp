#include "Engine/Source/Retail/RetailWmo.h"
#include "Engine/Source/Retail/IffChunks.h"

#include <algorithm>
#include <climits>
#include <cstring>

namespace Retail {
namespace {

using namespace Iff;

constexpr uint32_t kVersion = 17;
constexpr size_t kHeader = 64, kMaterial = 64, kGroupInfo = 32, kDoodad = 40, kLight = 48, kAmbient = 48;
constexpr size_t kGroupHeader = 68, kBatch = 24;

std::string Path(uint32_t id, const char* extension) { return RetailPath(id, extension); }

}

bool IsWmoGroup(const std::vector<uint8_t>& bytes) {
    Chunks top;
    return top.Parse(bytes.data(), bytes.size()) && top.Find(Tag("MOGP")) && !top.Find(Tag("MOHD"));
}

bool PrepareWmoRoot(const std::vector<uint8_t>& in, std::vector<uint8_t>& out,
    std::vector<uint32_t>& groups, std::string& error) {
    out.clear();
    groups.clear();
    Chunks c;
    if (!c.Parse(in.data(), in.size())) return Fail(error, "Retail WMO root has a truncated chunk");
    const auto* version = c.Find(Tag("MVER"));
    if (!version || version->size < 4 || Get<uint32_t>(version->data) != kVersion)
        return Fail(error, "Retail WMO root is not version 17");
    const auto* header = c.Find(Tag("MOHD"));
    if (!header || header->size < kHeader) return Fail(error, "Retail WMO root has no MOHD header");
    std::vector<uint8_t> mohd(header->data, header->data + kHeader);
    const uint32_t groupCount = Get<uint32_t>(&mohd[4]);
    if (c.Bytes(Tag("MOGI")).size() != size_t(groupCount) * kGroupInfo)
        return Fail(error, "Retail WMO MOGI does not match the MOHD group count");

    // Materials: since 8.1 the texture fields hold FileDataIDs and MOTX is gone.
    auto momt = c.Bytes(Tag("MOMT"));
    if (momt.size() % kMaterial) return Fail(error, "Retail WMO MOMT is not a whole number of materials");
    const bool fileIds = !c.Find(Tag("MOTX"));
    Names textures;
    uint32_t empty = UINT32_MAX;
    for (size_t i = 0; i < momt.size() / kMaterial; ++i) {
        uint8_t* m = &momt[i * kMaterial];
        uint32_t shader = Get<uint32_t>(m + 4), blend = Get<uint32_t>(m + 8);
        const bool newerShader = shader > 6;
        Put(m, Get<uint32_t>(m) & 0x1FFu);
        Put(m + 4, newerShader ? 0u : shader);
        Put(m + 8, blend > 6 ? 2u : blend);
        if (!fileIds) continue;
        for (size_t field : {12, 24, 36}) {
            const uint32_t id = Get<uint32_t>(m + field);
            if (!id && empty == UINT32_MAX) empty = textures.Add("");
            Put(m + field, id ? textures.Add(Path(id, ".blp")) : empty);
        }
        if (newerShader) {
            // Newer shaders may keep the diffuse in texture 2; the diffuse fallback reads texture 1. Shader 23 (the
            // four-layer blend) always does, with an environment map such as dr_shine.blp in texture 1 (12hu_human_house02).
            if ((shader == 23 || Get<uint32_t>(m + 12) == empty) && Get<uint32_t>(m + 24) != empty)
                Put(m + 12, Get<uint32_t>(m + 24));
            std::memset(m + 40, 0, kMaterial - 40);
        }
    }
    const auto motx = fileIds ? textures.Value() : c.Bytes(Tag("MOTX"));

    // Doodads: MODI (FileDataIDs) replaced MODN; MODD's name index became an index into it.
    auto modd = c.Bytes(Tag("MODD"));
    if (modd.size() % kDoodad) return Fail(error, "Retail WMO MODD is not a whole number of doodads");
    std::vector<uint8_t> modn = c.Bytes(Tag("MODN"));
    if (!c.Find(Tag("MODN"))) {
        const auto modi = c.Bytes(Tag("MODI"));
        Names names;
        for (size_t i = 0; i < modd.size() / kDoodad; ++i) {
            const uint32_t word = Get<uint32_t>(&modd[i * kDoodad]), index = word & 0xFFFFFF;
            const uint32_t id = size_t(index) * 4 + 4 <= modi.size() ? Get<uint32_t>(&modi[size_t(index) * 4]) : 0;
            Put(&modd[i * kDoodad], (names.Add(id ? Path(id, ".m2") : "") & 0xFFFFFF) | (word & 0xFF000000));
        }
        modn = names.Value();
        Put(&mohd[16], uint32_t(names.entries.size()));
    }

    std::vector<uint8_t> mosb = c.Bytes(Tag("MOSB"));
    if (!c.Find(Tag("MOSB"))) {
        const auto mosi = c.Bytes(Tag("MOSI"));
        const uint32_t id = mosi.size() >= 4 ? Get<uint32_t>(mosi.data()) : 0;
        Names sky;
        if (id) sky.Add(Path(id, ".m2"));
        mosb = sky.Value();
    }

    // Legion moved the ambient colour into MAVG; 3.3.5 reads only MOHD's.
    if (!(Get<uint32_t>(&mohd[0x1C]) & 0xFFFFFF)) {
        const auto mavg = c.Bytes(Tag("MAVG"));
        const uint8_t* chosen = nullptr;
        for (size_t o = 0; o + kAmbient <= mavg.size(); o += kAmbient) {
            if (!(Get<uint32_t>(&mavg[o + 0x14]) & 0xFFFFFF)) continue;
            if (!chosen || Get<uint16_t>(&mavg[o + 0x24]) == 0) chosen = &mavg[o];
            if (Get<uint16_t>(&mavg[o + 0x24]) == 0) break;
        }
        if (chosen) Put(&mohd[0x1C], Get<uint32_t>(chosen + 0x14));
    }

    Put(&mohd[0], uint32_t(momt.size() / kMaterial));
    Put(&mohd[12], uint32_t(c.Bytes(Tag("MOLT")).size() / kLight));
    Put(&mohd[20], uint32_t(modd.size() / kDoodad));
    Put(&mohd[60], uint32_t(Get<uint16_t>(&mohd[60]) & 0xF)); // clears the post-Wrath flags and numLod

    const auto gfid = c.Bytes(Tag("GFID"));
    for (uint32_t i = 0; i < groupCount; ++i)
        groups.push_back(size_t(i) * 4 + 4 <= gfid.size() ? Get<uint32_t>(&gfid[size_t(i) * 4]) : 0);

    auto mogn = c.Bytes(Tag("MOGN"));
    if (mogn.empty()) mogn.resize(4);
    Add(out, Tag("MVER"), Word(kVersion));
    Add(out, Tag("MOHD"), mohd);
    Add(out, Tag("MOTX"), motx);
    Add(out, Tag("MOMT"), momt);
    Add(out, Tag("MOGN"), mogn);
    // Group info flags get the same mask as each group's MOGP header (PrepareWmoGroup).
    auto mogi = c.Bytes(Tag("MOGI"));
    for (size_t i = 0; i < mogi.size() / kGroupInfo; ++i)
        Put(&mogi[i * kGroupInfo], Get<uint32_t>(&mogi[i * kGroupInfo]) & 0x07FFFFFFu & ~(0x400u | 0x20000u));
    Add(out, Tag("MOGI"), mogi);
    Add(out, Tag("MOSB"), mosb);
    for (const uint32_t tag : {Tag("MOPV"), Tag("MOPT"), Tag("MOPR"), Tag("MOVV"), Tag("MOVB"), Tag("MOLT"), Tag("MODS")})
        Add(out, tag, c.Bytes(tag));
    Add(out, Tag("MODN"), modn.empty() ? std::vector<uint8_t>(4) : modn);
    Add(out, Tag("MODD"), modd);
    Add(out, Tag("MFOG"), c.Bytes(Tag("MFOG")));
    if (c.Find(Tag("MCVP"))) Add(out, Tag("MCVP"), c.Bytes(Tag("MCVP")));
    return true;
}

bool PrepareWmoGroup(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, std::string& error) {
    out.clear();
    Chunks top;
    if (!top.Parse(in.data(), in.size())) return Fail(error, "Retail WMO group has a truncated chunk");
    const auto* version = top.Find(Tag("MVER"));
    if (!version || version->size < 4 || Get<uint32_t>(version->data) != kVersion)
        return Fail(error, "Retail WMO group is not version 17");
    const auto* mogp = top.Find(Tag("MOGP"));
    if (!mogp || mogp->size < kGroupHeader) return Fail(error, "Retail WMO group has no MOGP header");
    std::vector<uint8_t> header(mogp->data, mogp->data + kGroupHeader);
    Chunks c;
    if (!c.Parse(mogp->data + kGroupHeader, mogp->size - kGroupHeader))
        return Fail(error, "Retail WMO group has a truncated sub-chunk");

    std::vector<uint16_t> indices;
    if (const auto* movi = c.Find(Tag("MOVI"))) {
        indices.resize(movi->size / 2);
        if (!indices.empty()) std::memcpy(indices.data(), movi->data, indices.size() * 2);
    } else if (const auto* movx = c.Find(Tag("MOVX"))) {
        for (size_t o = 0; o + 4 <= movx->size; o += 4) {
            const uint32_t index = Get<uint32_t>(movx->data + o);
            // shortcut: no group splitting, so groups past 16-bit indices are refused; port wotlkconv's split.py when one shows up.
            if (index > 0xFFFF) return Fail(error, "Retail WMO group needs 32-bit indices (group splitting is not implemented)");
            indices.push_back(uint16_t(index));
        }
    }
    const size_t triangles = indices.size() / 3;

    std::vector<uint8_t> polys = c.Bytes(Tag("MOPY"));
    const bool widePolys = !c.Find(Tag("MOPY")) && c.Find(Tag("MPY2"));
    if (widePolys) {
        const auto mpy2 = c.Bytes(Tag("MPY2"));
        if (triangles && mpy2.size() == triangles * 2) polys = mpy2;
        else if (triangles && mpy2.size() == triangles * 4) {
            polys.resize(triangles * 2);
            for (size_t i = 0; i < triangles; ++i) {
                const uint16_t material = Get<uint16_t>(&mpy2[i * 4 + 2]);
                polys[i * 2] = uint8_t(Get<uint16_t>(&mpy2[i * 4]));
                polys[i * 2 + 1] = material > 0xFF ? 0xFF : uint8_t(material); // 0xFF: collision only
            }
        } else if (triangles) return Fail(error, "Retail WMO MPY2 does not match the triangle count");
    }

    const auto vertices = c.Bytes(Tag("MOVT"));
    const size_t vertexCount = vertices.size() / 12;
    if (vertexCount > 0x10000) return Fail(error, "Retail WMO group has more vertices than 16-bit indices reach");
    for (const auto index : indices)
        if (index >= vertexCount) return Fail(error, "Retail WMO group references a vertex it does not have");

    auto batches = c.Bytes(Tag("MOBA"));
    if (batches.size() % kBatch) return Fail(error, "Retail WMO MOBA is not a whole number of batches");
    for (size_t b = 0; b < batches.size() / kBatch; ++b) {
        uint8_t* batch = &batches[b * kBatch];
        const bool wideMaterial = batch[22] & 0x2;
        if (wideMaterial) {
            // Shadowlands keeps a 16-bit material id at 0x0A, inside the old bounding box.
            const uint16_t material = Get<uint16_t>(batch + 0x0A);
            batch[23] = material > 0xFF ? 0xFF : uint8_t(material);
            batch[22] &= ~0x2;
        }
        if (!widePolys && !wideMaterial) continue;
        const uint32_t start = Get<uint32_t>(batch + 12), count = Get<uint16_t>(batch + 16);
        int lo[3] = {INT_MAX, INT_MAX, INT_MAX}, hi[3] = {INT_MIN, INT_MIN, INT_MIN};
        bool seen = false;
        for (size_t n = start; n < size_t(start) + count && n < indices.size(); ++n) {
            const uint8_t* v = &vertices[size_t(indices[n]) * 12];
            for (int axis = 0; axis < 3; ++axis) {
                const int value = int(Get<float>(v + axis * 4));
                lo[axis] = std::min(lo[axis], value - 1);
                hi[axis] = std::max(hi[axis], value + 1);
            }
            seen = true;
        }
        if (!seen) continue;
        for (int axis = 0; axis < 3; ++axis) {
            Put(batch + axis * 2, int16_t(std::clamp(lo[axis], -32768, 32767)));
            Put(batch + 6 + axis * 2, int16_t(std::clamp(hi[axis], -32768, 32767)));
        }
    }

    auto uvs = c.All(Tag("MOTV"));
    auto colours = c.All(Tag("MOCV"));
    if (uvs.size() > 2) uvs.resize(2);
    if (colours.size() > 2) colours.resize(2);
    // 3.3.5 adds MOCV to the lit colour as baked light (CMapObjGroup::FillVertexVB uses 0 when there is none).
    // Retail groups whose MOCV is white on every vertex use only its alpha, as shader-23 layer weights: as light it
    // turns walls pure white (12hu_human_house02), so it goes. Near-white counts: the Founder's Point brewery has
    // 1,021 F7F7F7 vertices among 52,638 FFFFFF ones.
    const auto white = [](const std::vector<uint8_t>& mocv) {
        constexpr uint8_t kNearWhite = 0xE0;
        for (size_t i = 0; i + 4 <= mocv.size(); i += 4)
            if (mocv[i] < kNearWhite || mocv[i + 1] < kNearWhite || mocv[i + 2] < kNearWhite) return false;
        return true;
    };
    if (!colours.empty() && white(colours[0])) colours.clear();
    const auto mobn = c.Bytes(Tag("MOBN")), mobr = c.Bytes(Tag("MOBR"));
    const auto molr = c.Bytes(Tag("MOLR")), modr = c.Bytes(Tag("MODR")), mliq = c.Bytes(Tag("MLIQ"));

    // CreateOptionalDataPointers reads an optional chunk wherever its flag is set, without checking the
    // name, so every flag must describe exactly what is written below.
    uint32_t flags = Get<uint32_t>(&header[8]) & 0x07FFFFFF;
    const auto set = [&flags](uint32_t bit, bool on) { flags = on ? flags | bit : flags & ~bit; };
    set(0x1, !mobn.empty()); // shortcut: a group without MOBN gets no collision; port wotlkconv's bsp.py if one shows up.
    set(0x4, !colours.empty());
    set(0x200, !molr.empty());
    set(0x400, false);
    set(0x800, !modr.empty());
    set(0x1000, !mliq.empty());
    set(0x20000, false);
    set(0x1000000, colours.size() >= 2);
    set(0x2000000, uvs.size() >= 2);
    Put(&header[8], flags);
    std::memset(&header[60], 0, 8); // Legion split-group indices; 3.3.5 reads padding there

    std::vector<uint8_t> inner;
    std::vector<uint8_t> movi(indices.size() * 2);
    if (!indices.empty()) std::memcpy(movi.data(), indices.data(), movi.size());
    Add(inner, Tag("MOPY"), polys);
    Add(inner, Tag("MOVI"), movi);
    Add(inner, Tag("MOVT"), vertices);
    Add(inner, Tag("MONR"), c.Bytes(Tag("MONR")));
    Add(inner, Tag("MOTV"), uvs.empty() ? std::vector<uint8_t>(vertexCount * 8) : uvs[0]);
    Add(inner, Tag("MOBA"), batches);
    if (!molr.empty()) Add(inner, Tag("MOLR"), molr);
    if (!modr.empty()) Add(inner, Tag("MODR"), modr);
    if (!mobn.empty()) { Add(inner, Tag("MOBN"), mobn); Add(inner, Tag("MOBR"), mobr); }
    if (!colours.empty()) Add(inner, Tag("MOCV"), colours[0]);
    if (!mliq.empty()) Add(inner, Tag("MLIQ"), mliq);
    if (uvs.size() >= 2) Add(inner, Tag("MOTV"), uvs[1]);
    if (colours.size() >= 2) Add(inner, Tag("MOCV"), colours[1]);

    header.insert(header.end(), inner.begin(), inner.end());
    Add(out, Tag("MVER"), Word(kVersion));
    Add(out, Tag("MOGP"), header);
    return true;
}

}
