#pragma once

// IFF chunk reading and writing shared by the in-memory retail WMO and terrain adapters.
// Tags are compared as the little-endian word the file stores, so "MVER" is spelled forwards.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Retail::Iff {

constexpr uint32_t Tag(const char (&s)[5]) {
    return uint32_t(uint8_t(s[0])) << 24 | uint32_t(uint8_t(s[1])) << 16 | uint32_t(uint8_t(s[2])) << 8 | uint8_t(s[3]);
}

template <class T> T Get(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }
template <class T> void Put(uint8_t* p, T v) { std::memcpy(p, &v, sizeof v); }
inline bool Fail(std::string& error, const char* message) { error = message; return false; }

struct Chunk { uint32_t tag; const uint8_t* data; uint32_t size; };

struct Chunks {
    std::vector<Chunk> list;
    bool Parse(const uint8_t* p, size_t size) {
        for (size_t o = 0; o < size;) {
            if (size - o < 8) return false;
            const uint32_t tag = Get<uint32_t>(p + o), length = Get<uint32_t>(p + o + 4);
            if (length > size - o - 8) return false;
            list.push_back({tag, p + o + 8, length});
            o += 8 + size_t(length);
        }
        return true;
    }
    const Chunk* Find(uint32_t tag) const {
        for (const auto& c : list) if (c.tag == tag) return &c;
        return nullptr;
    }
    std::vector<uint8_t> Bytes(uint32_t tag) const {
        const auto* c = Find(tag);
        return c ? std::vector<uint8_t>(c->data, c->data + c->size) : std::vector<uint8_t>();
    }
    std::vector<std::vector<uint8_t>> All(uint32_t tag) const {
        std::vector<std::vector<uint8_t>> all;
        for (const auto& c : list) if (c.tag == tag) all.emplace_back(c.data, c.data + c.size);
        return all;
    }
};

inline void Add(std::vector<uint8_t>& out, uint32_t tag, const std::vector<uint8_t>& data) {
    const size_t at = out.size();
    out.resize(at + 8 + data.size());
    Put(&out[at], tag);
    Put(&out[at + 4], uint32_t(data.size()));
    if (!data.empty()) std::memcpy(&out[at + 8], data.data(), data.size());
}

inline std::vector<uint8_t> Word(uint32_t value) { std::vector<uint8_t> v(4); Put(v.data(), value); return v; }

// MOTX/MODN/MTEX/MMDX/MWMO string table: zero-terminated names addressed by byte offset. Pad keeps each entry on a
// 4-byte boundary, as Blizzard's WMO tables do; ADT tables are unpadded.
struct Names {
    explicit Names(bool pad = true) : pad(pad) {}
    bool pad;
    std::vector<uint8_t> blob;
    std::vector<std::pair<std::string, uint32_t>> entries;
    uint32_t Add(const std::string& name) {
        for (const auto& e : entries) if (e.first == name) return e.second;
        const auto offset = uint32_t(blob.size());
        blob.insert(blob.end(), name.begin(), name.end());
        do blob.push_back(0); while (pad && blob.size() % 4);
        entries.emplace_back(name, offset);
        return offset;
    }
    std::vector<uint8_t> Value() const { return blob.empty() && pad ? std::vector<uint8_t>(4) : blob; }
};

inline std::string RetailPath(uint32_t id, const char* extension) { return "retail\\" + std::to_string(id) + extension; }

}
