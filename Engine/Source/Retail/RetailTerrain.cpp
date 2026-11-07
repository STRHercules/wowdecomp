#include "Engine/Source/Retail/RetailTerrain.h"
#include "Engine/Source/Retail/IffChunks.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace Retail {
namespace {

using namespace Iff;

constexpr uint32_t kVersion = 18;
constexpr size_t kTiles = 4096, kMcnk = 256, kMcnkHeader = 128, kDoodad = 36, kMapObj = 64, kLayer = 16;
constexpr uint32_t kHighResHoles = 0x10000, kFineHoles = 0x20000, kHasMcsh = 0x1, kHasMccv = 0x40, kLegacyLiquid = 0x3C;

constexpr size_t kAlphaSide = 64, kAlphaTexels = kAlphaSide * kAlphaSide, kMaxLayers = 4;
constexpr uint32_t kUseAlpha = 0x100, kCompressedAlpha = 0x200;
constexpr int kBorderFadeTexels = 8, kBorderRingTexels = 4, kBorderRingScore = 8;

// One texture layer of a chunk: weight per texel out of 255. The terrain shaders blend additively (HdTerrain's
// t0 * (1 - sum of alphas) + ...), so the base's weight is whatever the other layers leave.
struct PaintLayer {
    uint32_t texture, flags, effect, id; // id: stands for the texture across tiles, whose MDID indices differ
    std::vector<uint8_t> weight;
};

struct ChunkPaint {
    std::vector<PaintLayer> layers;
    bool rewrite = false;
    std::vector<uint8_t> mcly, mcal;
    uint8_t predominant[16] = {};
};

bool DecodeAlpha(const std::vector<uint8_t>& mcal, size_t at, bool compressed, std::vector<uint8_t>& out) {
    out.assign(kAlphaTexels, 0);
    if (!compressed) {
        if (at > mcal.size() || mcal.size() - at < kAlphaTexels) return false;
        std::memcpy(out.data(), &mcal[at], kAlphaTexels);
        return true;
    }
    for (size_t done = 0; done < kAlphaTexels;) {
        if (at >= mcal.size()) return false;
        const uint8_t control = mcal[at++];
        const size_t count = std::min<size_t>(control & 0x7F, kAlphaTexels - done);
        if (control & 0x80) {
            if (at >= mcal.size()) return false;
            std::memset(&out[done], mcal[at++], count);
        } else {
            if (mcal.size() - at < (control & 0x7Fu)) return false;
            std::memcpy(&out[done], &mcal[at], count);
            at += control & 0x7F;
        }
        done += count;
    }
    return true;
}

// Reads a chunk's layers, keeping the four that show most. Border texels count extra: a texture that reaches a chunk's
// edge is what shows as a seam if it goes. Layers keep their source order; the first kept one is the new base.
ChunkPaint ReadPaint(const std::vector<uint8_t>& mcly, const std::vector<uint8_t>& mcal, const std::vector<std::string>& textures) {
    ChunkPaint paint;
    const size_t count = mcly.size() / kLayer;
    std::vector<int> base(kAlphaTexels, 255);
    paint.layers.resize(count);
    for (size_t l = 0; l < count; ++l) {
        const uint8_t* p = &mcly[l * kLayer];
        PaintLayer& layer = paint.layers[l];
        layer.texture = Get<uint32_t>(p);
        layer.id = layer.texture < textures.size() ? uint32_t(std::hash<std::string>{}(textures[layer.texture])) | 1u : 0u;
        layer.flags = Get<uint32_t>(p + 4);
        layer.effect = Get<uint32_t>(p + 12);
        layer.weight.assign(kAlphaTexels, 0);
        // A map that runs off MCAL's end counts as no paint: stripping only its alpha would cover everything below.
        if (l && (layer.flags & kUseAlpha))
            if (!DecodeAlpha(mcal, Get<uint32_t>(p + 8), (layer.flags & kCompressedAlpha) != 0, layer.weight))
                std::fill(layer.weight.begin(), layer.weight.end(), uint8_t(0));
        if (l)
            for (size_t t = 0; t < kAlphaTexels; ++t) base[t] -= layer.weight[t];
    }
    if (count)
        for (size_t t = 0; t < kAlphaTexels; ++t) paint.layers[0].weight[t] = uint8_t(std::max(base[t], 0));
    if (count <= kMaxLayers) return paint;

    paint.rewrite = true;
    std::vector<std::pair<uint64_t, size_t>> score(count);
    for (size_t l = 0; l < count; ++l) {
        uint64_t sum = 0;
        for (size_t t = 0; t < kAlphaTexels; ++t) {
            const size_t x = t % kAlphaSide, y = t / kAlphaSide, last = kAlphaSide - 1 - kBorderRingTexels;
            const bool ring = x < kBorderRingTexels || x > last || y < kBorderRingTexels || y > last;
            sum += uint64_t(paint.layers[l].weight[t]) * (ring ? kBorderRingScore : 1);
        }
        score[l] = {sum, l};
    }
    std::stable_sort(score.begin(), score.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    score.resize(kMaxLayers);
    std::sort(score.begin(), score.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    std::vector<PaintLayer> kept;
    for (const auto& s : score) kept.push_back(std::move(paint.layers[s.second]));
    paint.layers = std::move(kept);
    return paint;
}

// A texture a neighbouring chunk does not have would end in a hard line at the shared border, so each one fades out as
// it nears that border and the rest of the chunk's paint takes over. Both sides then agree on the edge itself.
// neighbours: left, right, up, down (null where there is no terrain).
void FadeAndEncode(ChunkPaint& paint, const KeptTextures* const neighbours[4]) {
    const size_t count = paint.layers.size();
    if (!count) return;
    std::vector<std::vector<float>> weights(count, std::vector<float>(kAlphaTexels));
    for (size_t l = 0; l < count; ++l) {
        bool missing[4] = {};
        for (int e = 0; e < 4; ++e) {
            if (!neighbours[e]) continue;
            missing[e] = std::find(neighbours[e]->begin(), neighbours[e]->end(), paint.layers[l].id) == neighbours[e]->end();
        }
        for (size_t t = 0; t < kAlphaTexels; ++t) {
            const int x = int(t % kAlphaSide), y = int(t / kAlphaSide), last = int(kAlphaSide) - 1;
            const int distance[4] = {x, last - x, y, last - y};
            float keep = 1.0f;
            for (int e = 0; e < 4; ++e) {
                if (!missing[e] || distance[e] >= kBorderFadeTexels) continue;
                const float f = float(distance[e]) / kBorderFadeTexels;
                keep *= f * f * (3.0f - 2.0f * f);
            }
            if (keep < 1.0f && paint.layers[l].weight[t]) paint.rewrite = true;
            weights[l][t] = paint.layers[l].weight[t] * keep;
        }
    }
    if (!paint.rewrite) return;

    paint.mcly.assign(count * kLayer, 0);
    paint.mcal.clear();
    for (size_t l = 0; l < count; ++l) {
        const PaintLayer& layer = paint.layers[l];
        uint8_t* p = &paint.mcly[l * kLayer];
        Put(p, layer.texture);
        Put(p + 4, l ? (layer.flags & ~kCompressedAlpha) | kUseAlpha : layer.flags & ~(kUseAlpha | kCompressedAlpha));
        Put(p + 8, l ? uint32_t((l - 1) * kAlphaTexels) : 0u);
        Put(p + 12, layer.effect);
    }
    paint.mcal.resize((count - 1) * kAlphaTexels);
    for (size_t t = 0; t < kAlphaTexels; ++t) {
        float total = 0.0f;
        for (size_t l = 0; l < count; ++l) total += weights[l][t];
        // Nothing left here (every texture faded): the original proportions; a seam beats a hole.
        const bool faded = total <= 0.0f;
        if (faded)
            for (size_t l = 0; l < count; ++l) total += paint.layers[l].weight[t];
        if (total <= 0.0f) continue; // only dropped layers painted this texel: all base
        for (size_t l = 1; l < count; ++l) {
            const float w = faded ? float(paint.layers[l].weight[t]) : weights[l][t];
            paint.mcal[(l - 1) * kAlphaTexels + t] = uint8_t(w * 255.0f / total); // floor: alphas never sum past 255
        }
    }
    // The 8x8 low-quality map (2 bits a cell, header 0x40) names layers by index: which ground clutter grows there.
    for (size_t cell = 0; cell < 64; ++cell) {
        const size_t cx = cell % 8, cy = cell / 8;
        const size_t texel = (cy * 8 + 4) * kAlphaSide + cx * 8 + 4;
        int rest = 255, best = 0, bestWeight = 0;
        for (size_t l = 1; l < count; ++l) {
            const int w = paint.mcal[(l - 1) * kAlphaTexels + texel];
            rest -= w;
            if (w > bestWeight) { best = int(l); bestWeight = w; }
        }
        if (rest > bestWeight) best = 0;
        paint.predominant[cell / 4] |= uint8_t(best << (cell % 4 * 2));
    }
}

std::vector<std::string> Split(const std::vector<uint8_t>& table);

// Textures: MDID FileDataIDs (8.1+) or a legacy MTEX table.
std::vector<std::string> TextureNames(const Chunks& tex) {
    if (tex.Find(Tag("MTEX"))) return Split(tex.Bytes(Tag("MTEX")));
    std::vector<std::string> textures;
    const auto mdid = tex.Bytes(Tag("MDID"));
    for (size_t o = 0; o + 4 <= mdid.size(); o += 4) {
        const uint32_t id = Get<uint32_t>(&mdid[o]);
        textures.push_back(id ? RetailPath(id, ".blp") : "");
    }
    return textures;
}

KeptTextures KeptOf(const ChunkPaint& paint) {
    KeptTextures kept{};
    for (size_t l = 0; l < paint.layers.size() && l < kept.size(); ++l) kept[l] = paint.layers[l].id;
    return kept;
}

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

// MH2O since Warlords stores a LiquidObject id (42 and up) where 3.3.5 expects the instance's vertex format (LVF):
// 0 height+depth (5 bytes per vertex), 1 height+UV (8), 2 depth (1), 3 height+UV+depth (9). Each instance is decoded
// in its retail format and rewritten in the one 3.3.5 uses for its kind of liquid (wotlkconv adt/mh2o.py), with retail
// liquid types mapped onto the stock ones so the client's own water draws them: 1 water, 2 ocean, 3 magma, 4 slime.
// shortcut: a retail type above 4 is water unless its data carries UVs (then magma); read LiquidType.db2's SoundBank
// if slime or lava rivers show up as water.
bool ConvertLiquid(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    constexpr size_t kHeader = 12, kInstance = 24, kAttributes = 16;
    static const uint32_t kBytes[4] = {5, 8, 1, 9};
    out.clear();
    if (in.size() < kMcnk * kHeader) return false;
    // Where every block starts, so a vertex block's length can be read off the next start (blocks are packed).
    std::vector<uint32_t> starts{uint32_t(in.size())};
    for (size_t c = 0; c < kMcnk; ++c) {
        const uint32_t at = Get<uint32_t>(&in[c * kHeader]), layers = Get<uint32_t>(&in[c * kHeader + 4]);
        const uint32_t attributes = Get<uint32_t>(&in[c * kHeader + 8]);
        if (layers && (at > in.size() || size_t(layers) * kInstance > in.size() - at)) return false;
        if (layers) starts.push_back(at);
        if (attributes) starts.push_back(attributes);
        for (uint32_t l = 0; l < layers; ++l) {
            const uint8_t* inst = &in[at + l * kInstance];
            for (size_t f : {16u, 20u}) if (Get<uint32_t>(inst + f)) starts.push_back(Get<uint32_t>(inst + f));
        }
    }
    std::sort(starts.begin(), starts.end());
    const auto room = [&](uint32_t offset) {
        const auto next = std::upper_bound(starts.begin(), starts.end(), offset);
        return next == starts.end() ? 0u : *next - offset;
    };

    out.resize(kMcnk * kHeader);
    for (size_t c = 0; c < kMcnk; ++c) {
        const uint32_t at = Get<uint32_t>(&in[c * kHeader]), layers = Get<uint32_t>(&in[c * kHeader + 4]);
        const uint32_t attributes = Get<uint32_t>(&in[c * kHeader + 8]);
        if (!layers) continue;
        const size_t table = out.size();
        out.resize(table + layers * kInstance);
        Put(&out[c * kHeader], uint32_t(table));
        Put(&out[c * kHeader + 4], layers);
        if (attributes && attributes + kAttributes <= in.size()) {
            Put(&out[c * kHeader + 8], uint32_t(out.size()));
            out.insert(out.end(), in.begin() + attributes, in.begin() + attributes + kAttributes);
        }
        for (uint32_t l = 0; l < layers; ++l) {
            const uint8_t* src = &in[at + l * kInstance];
            uint16_t type = Get<uint16_t>(src);
            const uint16_t field = Get<uint16_t>(src + 2);
            const float minimum = Get<float>(src + 4), maximum = Get<float>(src + 8);
            const uint32_t x = src[12], y = src[13], w = src[14], h = src[15];
            const uint32_t bitmapAt = Get<uint32_t>(src + 16), verticesAt = Get<uint32_t>(src + 20);
            if (x + w > 8 || y + h > 8) return false;
            const uint32_t n = (w + 1) * (h + 1);
            int format = field < 42 ? (field < 4 ? int(field) : -1) : type == 2 ? 2 : -1;
            if (verticesAt && (format < 0 || kBytes[format] * n > room(verticesAt))) {
                format = -1; // the only format whose block fills the space up to the next block (up to 3 pad bytes)
                for (int f = 0; f < 4; ++f)
                    if (room(verticesAt) >= kBytes[f] * n && room(verticesAt) - kBytes[f] * n < 4) format = format == -1 ? f : -2;
            }
            if (verticesAt && format < 0) return false;
            const uint8_t* v = verticesAt ? &in[verticesAt] : nullptr;
            const bool hasHeights = v && format != 2, hasUvs = v && (format == 1 || format == 3);
            const bool hasDepths = v && format != 1;
            if (type > 4) type = hasUvs ? 3 : 1;
            const uint16_t lvf = type == 3 || type == 4 ? 1 : type == 2 && minimum == 0 && maximum == 0 ? 2 : 0;

            uint8_t* dst = &out[table + l * kInstance];
            Put(dst, type);
            Put(dst + 2, lvf);
            std::memcpy(dst + 4, src + 4, 12); // min, max, x, y, w, h
            Put(dst + 16, 0u);
            Put(dst + 20, 0u);
            if (bitmapAt) {
                const uint32_t bits = (w * h + 7) / 8;
                if (bitmapAt + bits > in.size()) return false;
                Put(&out[table + l * kInstance + 16], uint32_t(out.size()));
                out.insert(out.end(), in.begin() + bitmapAt, in.begin() + bitmapAt + bits);
            }
            std::vector<uint8_t> block;
            const auto height = [&](uint32_t i) { return hasHeights ? Get<float>(v + i * 4) : minimum; };
            const uint8_t* depths = hasDepths ? v + (format == 2 ? 0 : format == 3 ? 8 * n : 4 * n) : nullptr;
            if (lvf != 2) for (uint32_t i = 0; i < n; ++i) { block.resize(block.size() + 4); Put(&block[block.size() - 4], height(i)); }
            if (lvf == 1)
                for (uint32_t i = 0; i < n; ++i) {
                    uint16_t uv[2] = {uint16_t((x + i % (w + 1)) * 255 / 4), uint16_t((y + i / (w + 1)) * 255 / 4)};
                    if (hasUvs) std::memcpy(uv, v + 4 * n + i * 4, 4);
                    block.insert(block.end(), reinterpret_cast<uint8_t*>(uv), reinterpret_cast<uint8_t*>(uv) + 4);
                }
            else
                for (uint32_t i = 0; i < n; ++i) block.push_back(depths ? depths[i] : 0xFF);
            Put(&out[table + l * kInstance + 20], uint32_t(out.size()));
            out.insert(out.end(), block.begin(), block.end());
        }
    }
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
        t.minimap = Get<uint32_t>(e + 28);
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

bool ReadTileTextures(const std::vector<uint8_t>& texBytes, TileTextures& out) {
    out.chunks.clear();
    Chunks tex;
    if (!tex.Parse(texBytes.data(), texBytes.size())) return false;
    const std::vector<std::string> textures = TextureNames(tex);
    for (const auto& ch : tex.list) {
        if (ch.tag != Tag("MCNK")) continue;
        Chunks ts;
        if (!ts.Parse(ch.data, ch.size)) return false;
        out.chunks.push_back(KeptOf(ReadPaint(ts.Bytes(Tag("MCLY")), ts.Bytes(Tag("MCAL")), textures)));
    }
    return out.chunks.size() == kMcnk;
}

bool PrepareAdt(const std::vector<uint8_t>& rootBytes, const std::vector<uint8_t>& objBytes,
    const std::vector<uint8_t>& texBytes, std::vector<uint8_t>& out, std::string& error, const TileTextures* const* adjoining) {
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

    // MTXF 0x1 stops 3.3.5 asking for <name>_s.blp, which has no retail\<fdid> equivalent; the diffuse is loaded as named.
    const std::vector<std::string> textures = TextureNames(tex);
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

    // Retail paints up to eight layers on a chunk and 3.3.5 draws four (over half the chunks of a Founder's Point tile).
    std::vector<ChunkPaint> paints(kMcnk);
    for (size_t i = 0; i < kMcnk && i < texChunks.size(); ++i) {
        Chunks ts;
        if (!ts.Parse(texChunks[i]->data, texChunks[i]->size)) return Fail(error, "Retail ADT MCNK has a truncated sub-chunk");
        paints[i] = ReadPaint(ts.Bytes(Tag("MCLY")), ts.Bytes(Tag("MCAL")), textures);
    }
    std::vector<KeptTextures> kept(kMcnk);
    for (size_t i = 0; i < kMcnk; ++i) kept[i] = KeptOf(paints[i]);
    const auto beyond = [&](int side, size_t chunk) -> const KeptTextures* {
        return adjoining && adjoining[side] && adjoining[side]->chunks.size() == kMcnk ? &adjoining[side]->chunks[chunk] : nullptr;
    };
    for (size_t i = 0; i < kMcnk; ++i) {
        const size_t x = i % 16, y = i / 16;
        const KeptTextures* const neighbours[4] = {x ? &kept[i - 1] : beyond(0, y * 16 + 15), x < 15 ? &kept[i + 1] : beyond(1, y * 16),
                                                   y ? &kept[i - 16] : beyond(2, 240 + x), y < 15 ? &kept[i + 16] : beyond(3, x)};
        FadeAndEncode(paints[i], neighbours);
    }

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
        uint8_t fineHoles[8] = {};
        if (flags & kHighResHoles) {
            // A split root walks its sub-chunks, so the MCVT/MCNR offset words at 0x14 hold the 8x8 mask instead.
            // Kept as it is for CMapChunk (HoleAt): folded alone, a 4 yd hole becomes 8 yd of open ground beside what
            // fills it. The fold still answers "has any hole" and the low-detail terrain.
            Put(&header[0x3C], FoldHoles(&header[0x14]));
            std::memcpy(fineHoles, &header[0x14], sizeof fineHoles);
            flags = (flags & ~kHighResHoles) | kFineHoles;
        }
        const ChunkPaint& paint = paints[i];
        const auto mcly = paint.rewrite ? paint.mcly : ts.Bytes(Tag("MCLY"));
        const auto mcal = paint.rewrite ? paint.mcal : ts.Bytes(Tag("MCAL"));
        if (paint.rewrite) std::memcpy(&header[0x40], paint.predominant, sizeof paint.predominant);
        auto mcrf = merge(os.Bytes(Tag("MCRD")), extraDoodads[i]);
        const auto mcrw = merge(os.Bytes(Tag("MCRW")), extraObjects[i]);
        const uint32_t doodadRefs = uint32_t(mcrf.size() / 4), objectRefs = uint32_t(mcrw.size() / 4);
        mcrf.insert(mcrf.end(), mcrw.begin(), mcrw.end());

        struct Piece { uint32_t tag; std::vector<uint8_t> data; };
        const Piece pieces[] = {
            {Tag("MCVT"), rs.Bytes(Tag("MCVT"))}, {Tag("MCCV"), rs.Bytes(Tag("MCCV"))}, {Tag("MCNR"), rs.Bytes(Tag("MCNR"))},
            {Tag("MCLY"), mcly}, {Tag("MCRF"), mcrf}, {Tag("MCSH"), ts.Bytes(Tag("MCSH"))}, {Tag("MCAL"), mcal},
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
        std::memcpy(&header[0x78], fineHoles, sizeof fineHoles);
        flags = offsetOf[5] ? flags | kHasMcsh : flags & ~kHasMcsh;
        flags = offsetOf[1] ? flags | kHasMccv : flags & ~kHasMccv;
        Put(&header[0], flags & ~kLegacyLiquid & (0xFFFFu | kFineHoles));
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
    std::vector<uint8_t> mh2o;
    if (root.Find(Tag("MH2O")) && !ConvertLiquid(root.Bytes(Tag("MH2O")), mh2o))
        return Fail(error, "Retail ADT MH2O cannot be read");
    if (!mh2o.empty()) place(9, Tag("MH2O"), mh2o);
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
