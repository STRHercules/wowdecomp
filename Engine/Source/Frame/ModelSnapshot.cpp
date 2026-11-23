#include "Engine/Source/Frame/ModelSnapshot.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Engine/Source/Base/Handle.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxTex.h"
#include "Engine/Source/Gx/CGxTexFlags.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Services/Texture.h"
#include <zlib.h>

namespace ModelSnapshot {

namespace {

namespace fs = std::filesystem;

constexpr const char* kDirectory = "Cache/Snapshots";
constexpr uint32_t kMagic = 0x31504E53;  // "SNP1"
constexpr size_t kPixels = static_cast<size_t>(kWidth) * kHeight;
// shortcut: the folder is pruned by file count, oldest first; upgrade to a byte budget if the pictures grow
constexpr size_t kMaxFiles = 4000;
// The picture must differ from its own corner by at least this much (per channel) in at least this many pixels.
constexpr int kContentDelta = 6;
constexpr size_t kMinContentPixels = 150;
// A picture of an item must differ from the bare character in at least this many pixels, by at least kReferenceDelta.
constexpr int kReferenceDelta = 16;
constexpr size_t kMinReferencePixels = 120;
// The low two bits of every channel are dropped before compressing: invisible on a card, and long runs of equal
// pixels deflate far better.
constexpr uint32_t kQuantMask = 0xFCFCFCFC;

struct Header {
    uint32_t magic;
    uint16_t width;
    uint16_t height;
    uint32_t rawSize;
};

// pictures stored and valid; a key is looked up on disk once and remembered
std::unordered_set<std::string> s_valid;
// textures alive for pictures somebody shows
std::unordered_map<std::string, CTexture*> s_resident;
// one working target per drawing frame
std::unordered_map<const void*, CTexture*> s_scratch;
// stable addresses for the texture callback to find its key by
std::unordered_set<std::string> s_keys;
std::vector<uint32_t> s_latch;
// Decoded pictures of the ones shown, so a texture is filled from memory and never depends on the file being there
// at that moment. Nodes keep their address; entries go when a picture is stored again or the map grows large.
std::unordered_map<std::string, std::vector<uint32_t>> s_pixels;
std::vector<uint32_t> s_reference;
// draws logged per picture name since its texture was last made (TraceDraw)
std::unordered_map<std::string, int> s_draws;
std::unordered_map<std::string, int> s_states;

// screen pixels read just after a card was drawn, to be read again when the frame ends (FrameEnd)
struct PendingSample {
    std::string name;
    uint32_t width, x0, y0, x1, y1;
    std::vector<uint32_t> before;  // the box of the target before the draw
    std::vector<uint32_t> after;   // and right after it
};
std::vector<PendingSample> s_pending;
std::vector<uint32_t> s_before;  // the whole target before the draw being traced (TraceBefore)
size_t s_files = 0;
bool s_filesKnown = false;
unsigned s_saved = 0;
unsigned s_rejected = 0;
int s_failed = 0;

// Logs/snapshot.log: what failed to be stored or shown, for when a card stays blank (cleared at the first line of a run).
void Log(const char* format, const char* key, int value) {
    static bool started = false;
    FILE* file = fopen("Logs/snapshot.log", started ? "ab" : "wb");

    if (!file) {
        return;
    }

    started = true;
    fprintf(file, format, key ? key : "", value);
    fputc('\n', file);
    fclose(file);
}

std::string PathFor(const char* key) {
    std::string path = std::string(kDirectory) + "/";

    for (const char* c = key; *c; ++c) {
        path += std::isalnum(static_cast<unsigned char>(*c)) ? *c : '_';
    }

    return path + ".snp";
}

bool Decode(const char* key, std::vector<uint32_t>& out) {
    FILE* file = fopen(PathFor(key).c_str(), "rb");

    if (!file) {
        return false;
    }

    Header header;
    std::vector<uint8_t> packed;
    bool ok = fread(&header, sizeof(header), 1, file) == 1 && header.magic == kMagic && header.width == kWidth && header.height == kHeight
        && header.rawSize == kPixels * 4;

    if (ok) {
        fseek(file, 0, SEEK_END);
        long size = ftell(file) - static_cast<long>(sizeof(header));
        fseek(file, static_cast<long>(sizeof(header)), SEEK_SET);
        ok = size > 0 && size < 4 * 1024 * 1024;

        if (ok) {
            packed.resize(static_cast<size_t>(size));
            ok = fread(packed.data(), 1, packed.size(), file) == packed.size();
        }
    }

    fclose(file);

    if (!ok) {
        return false;
    }

    out.resize(kPixels);
    uLongf rawSize = static_cast<uLongf>(kPixels * 4);
    return uncompress(reinterpret_cast<Bytef*>(out.data()), &rawSize, packed.data(), static_cast<uLong>(packed.size())) == Z_OK
        && rawSize == kPixels * 4;
}

bool HasContent(const std::vector<uint32_t>& pixels, size_t minPixels) {
    const uint32_t background = pixels[0];
    size_t count = 0;

    for (uint32_t pixel : pixels) {
        for (int shift = 0; shift < 32; shift += 8) {
            int delta = static_cast<int>((pixel >> shift) & 0xFF) - static_cast<int>((background >> shift) & 0xFF);

            if (delta >= kContentDelta || delta <= -kContentDelta) {
                ++count;
                break;
            }
        }
    }

    return count >= minPixels;
}

// Pixels of a (about to be quantized) picture that differ visibly from a stored one.
size_t CountDifferences(const std::vector<uint32_t>& pixels, const std::vector<uint32_t>& reference) {
    size_t count = 0;

    for (size_t i = 0; i < pixels.size() && i < reference.size(); ++i) {
        const uint32_t a = pixels[i] & kQuantMask;
        const uint32_t b = reference[i];

        for (int shift = 0; shift < 32; shift += 8) {
            int delta = static_cast<int>((a >> shift) & 0xFF) - static_cast<int>((b >> shift) & 0xFF);

            if (delta >= kReferenceDelta || delta <= -kReferenceDelta) {
                ++count;
                break;
            }
        }
    }

    return count;
}

void PruneFolder() {
    std::error_code ec;
    std::vector<std::pair<fs::file_time_type, fs::path>> files;

    for (const auto& entry : fs::directory_iterator(kDirectory, ec)) {
        if (entry.path().extension() == ".snp") {
            files.emplace_back(entry.last_write_time(ec), entry.path());
        }
    }

    s_files = files.size();

    if (s_files <= kMaxFiles) {
        return;
    }

    std::sort(files.begin(), files.end());
    size_t drop = s_files - kMaxFiles + kMaxFiles / 10;

    for (size_t i = 0; i < drop && i < files.size(); ++i) {
        fs::remove(files[i].second, ec);
        --s_files;
    }

    // what was removed is looked up on disk again
    s_valid.clear();
}

bool Store(const char* key, std::vector<uint32_t>& pixels) {
    // opaque: a card's picture is never see-through, whatever alpha the target was left with
    for (uint32_t& pixel : pixels) {
        pixel = (pixel & kQuantMask) | 0xFF000000;
    }

    uLongf packedSize = compressBound(static_cast<uLong>(kPixels * 4));
    std::vector<uint8_t> packed(packedSize);

    if (compress2(packed.data(), &packedSize, reinterpret_cast<const Bytef*>(pixels.data()), static_cast<uLong>(kPixels * 4), 6) != Z_OK) {
        return false;
    }

    std::error_code ec;
    fs::create_directories(kDirectory, ec);

    // written beside the real name and moved into place, so a crash leaves no half-written picture to be trusted
    std::string path = PathFor(key);
    std::string temp = path + ".tmp";
    FILE* file = fopen(temp.c_str(), "wb");

    if (!file) {
        return false;
    }

    Header header = { kMagic, static_cast<uint16_t>(kWidth), static_cast<uint16_t>(kHeight), static_cast<uint32_t>(kPixels * 4) };
    bool ok = fwrite(&header, sizeof(header), 1, file) == 1 && fwrite(packed.data(), 1, packedSize, file) == packedSize;
    ok = fclose(file) == 0 && ok;

    if (ok) {
        fs::remove(path, ec);
        fs::rename(temp, path, ec);
        ok = !ec;
    }

    if (!ok) {
        fs::remove(temp, ec);
    }

    return ok;
}

// Fills the texture of a stored picture whenever the device asks for its texels, now or after a texture is reloaded.
void LatchPicture(EGxTexCommand command, uint32_t width, uint32_t height, uint32_t face, uint32_t level, void* userArg, uint32_t& texelStrideInBytes, const void*& texels) {
    if (command != GxTex_Latch || !userArg) {
        return;
    }

    const std::string* key = static_cast<const std::string*>(userArg);
    auto it = s_pixels.find(*key);
    Log("latch: %s level %d", key->c_str(), static_cast<int>(level));

    if (it != s_pixels.end() && it->second.size() == kPixels) {
        texelStrideInBytes = 4 * width;
        texels = it->second.data();
    } else if (Decode(key->c_str(), s_latch)) {
        texelStrideInBytes = 4 * width;
        texels = s_latch.data();
    } else {
        Log("latch: no pixels for %s (%d)", key->c_str(), 0);
    }
}

CTexture* CreateTexture(const char* name, bool renderTarget, void* userArg, GxTexUserFunc userFunc) {
    EGxTexFormat format = GxTex_Argb8888;

    if (renderTarget && !g_theGxDevicePtr->Caps().m_rttFormat[GxTex_Argb8888]) {
        if (!g_theGxDevicePtr->Caps().m_rttFormat[GxTex_Abgr8888]) {
            s_failed = 1;
            return nullptr;
        }

        format = GxTex_Abgr8888;
    }

    CGxTexFlags flags(GxTex_Linear, 0, 0, 0, 0, renderTarget ? 1 : 0, 1, 0, 0, 0);
    CTexture* texture = TextureCreate(GxTex_2d, kWidth, kHeight, 0, format, format, flags, userArg, userFunc, name, 0);

    if (!texture || !TextureGetGxTex(texture, 1, nullptr)) {
        if (texture) {
            HandleClose(reinterpret_cast<HOBJECT>(texture));
        }

        s_failed = 2;
        return nullptr;
    }

    return texture;
}

void ReleaseScratch() {
    for (auto& entry : s_scratch) {
        HandleClose(reinterpret_cast<HOBJECT>(entry.second));
    }

    s_scratch.clear();
}

}  // namespace

bool Has(const char* key) {
    if (!key || !*key) {
        return false;
    }

    if (s_valid.count(key)) {
        return true;
    }

    std::error_code ec;

    if (!fs::exists(PathFor(key), ec)) {
        return false;
    }

    s_valid.insert(key);
    return true;
}

CGxTex* Scratch(const void* owner) {
    if (!owner || !g_theGxDevicePtr) {
        return nullptr;
    }

    auto it = s_scratch.find(owner);

    if (it != s_scratch.end()) {
        return TextureGetGxTex(it->second, 1, nullptr);
    }

    char name[64];
    snprintf(name, sizeof(name), "SnapshotScratch:%p", owner);
    CTexture* texture = CreateTexture(name, true, nullptr, GxTexNullUserFunc);

    if (!texture) {
        return nullptr;
    }

    s_scratch[owner] = texture;
    return TextureGetGxTex(texture, 1, nullptr);
}

bool Commit(const char* key, CGxTex* target, int force, const char* refKey) {
    if (!key || !*key || !target || !g_theGxDevicePtr) {
        return false;
    }

    std::vector<uint32_t> pixels(kPixels);

    if (!g_theGxDevicePtr->DeviceReadTexture(target, kWidth, kHeight, pixels.data())) {
        s_failed = 3;
        Log("commit: %s could not be read or stored (%d)", key, s_failed);
        return false;
    }

    // forcing only lowers the bar: a picture with nothing at all in it is never stored, it is a failed read
    if (!HasContent(pixels, force >= 2 ? 0 : (force ? 1 : kMinContentPixels))) {
        s_failed = 4;
        Log("commit: %s is empty (force %d)", key, force);
        ++s_rejected;
        return false;
    }

    // the picture must show something the bare character does not: an item still loading is photographed as the
    // bare character, which is a dark figure on black and reads as an empty card
    if (!force && refKey && *refKey && Has(refKey) && Decode(refKey, s_reference) && CountDifferences(pixels, s_reference) < kMinReferencePixels) {
        s_failed = 5;
        Log("commit: %s equals the bare character (force %d)", key, force);
        ++s_rejected;
        return false;
    }

    if (!s_filesKnown) {
        s_filesKnown = true;
        PruneFolder();
    }

    if (!Store(key, pixels)) {
        s_failed = 3;
        Log("commit: %s could not be read or stored (%d)", key, s_failed);
        return false;
    }

    // a texture still resident under this name holds an older picture of it; cards load the file that was just stored
    auto old = s_resident.find(key);

    if (old != s_resident.end()) {
        HandleClose(reinterpret_cast<HOBJECT>(old->second));
        s_resident.erase(old);
    }

    s_pixels.erase(key);

    s_valid.insert(key);
    ++s_saved;
    Log("commit: stored %s (force %d)", key, force);
    s_failed = 0;

    if (++s_files > kMaxFiles) {
        PruneFolder();
    }

    return true;
}

CTexture* Acquire(const char* key) {
    if (!Has(key)) {
        Log("acquire: %s has no stored picture (%d)", key, 0);
        return nullptr;
    }

    auto it = s_resident.find(key);

    if (it == s_resident.end()) {
        // a picture that cannot be read is removed, so it is made again rather than shown blank for ever
        std::vector<uint32_t>& pixels = s_pixels[key];

        if (pixels.size() != kPixels && !Decode(key, pixels)) {
            s_pixels.erase(key);
            std::error_code ec;
            fs::remove(PathFor(key), ec);
            s_valid.erase(key);
            Log("acquire: %s unreadable, removed (%d)", key, 0);
            return nullptr;
        }

        const std::string* owner = &*s_keys.insert(key).first;
        char name[96];
        snprintf(name, sizeof(name), "%s%s", kPrefix, key);
        CTexture* texture = CreateTexture(name, false, const_cast<std::string*>(owner), LatchPicture);

        if (!texture) {
            Log("acquire: no texture for %s (reason %d)", key, s_failed);
            return nullptr;
        }

        s_draws.erase(name);
        s_states.erase(name);
        GxTexUpdate(TextureGetGxTex(texture, 1, nullptr), 0, 0, kWidth, kHeight, 1);
        it = s_resident.emplace(key, texture).first;
    }

    Log("acquire: shown %s (resident %d)", key, static_cast<int>(s_resident.size()));
    return reinterpret_cast<CTexture*>(HandleDuplicate(reinterpret_cast<HOBJECT>(it->second)));
}

void TraceQueue(CTexture* texture, CGxTex* gxTex) {
    if (!texture || strncmp(texture->m_filename, kPrefix, kPrefixLength) != 0) {
        return;
    }

    // one number: needs-update x100 + needs-creation x10 + has-d3d-texture
    const int state = gxTex ? gxTex->m_needsUpdate * 100 + gxTex->m_needsCreation * 10 + (gxTex->m_apiSpecificData ? 1 : 0) : -1;
    Log("queue: %s (update/create/api %d)", texture->m_filename + kPrefixLength, state);

    // what the graphics card's copy of the picture holds right now: pixels that are not black, -1 when unreadable
    std::vector<uint32_t> pixels(kPixels);
    int lit = -1;

    if (gxTex && g_theGxDevicePtr && g_theGxDevicePtr->DeviceReadTexture(gxTex, kWidth, kHeight, pixels.data())) {
        lit = 0;

        for (uint32_t pixel : pixels) {
            lit += (pixel & 0x00FFFFFF) != 0;
        }
    }

    Log("queue: %s holds %d lit pixels", texture->m_filename + kPrefixLength, lit);
}

void TraceDraw(CTexture* texture, CGxTex* gxTex, const char* meshInfo) {
    if (!texture || strncmp(texture->m_filename, kPrefix, kPrefixLength) != 0) {
        return;
    }

    int& draws = s_draws[texture->m_filename];

    if (++draws > 3) {
        return;
    }

    std::vector<uint32_t> pixels(kPixels);
    int lit = -1;

    if (gxTex && g_theGxDevicePtr && g_theGxDevicePtr->DeviceReadTexture(gxTex, kWidth, kHeight, pixels.data())) {
        lit = 0;

        for (uint32_t pixel : pixels) {
            lit += (pixel & 0x00FFFFFF) != 0;
        }
    }

    const int state = gxTex ? gxTex->m_needsUpdate * 100 + gxTex->m_needsCreation * 10 + (gxTex->m_apiSpecificData ? 1 : 0) : -1;
    char text[200];
    snprintf(text, sizeof(text), "draw #%d: %%s state %d lit %d", draws, state, lit);
    Log(text, texture->m_filename + kPrefixLength, 0);
    Log("    mesh %s", meshInfo, 0);
}

void Note(const char* text) {
    Log("%s", text, 0);
}

void TraceState(CTexture* texture, float left, float top, float right, float bottom) {
    if (!texture || strncmp(texture->m_filename, kPrefix, kPrefixLength) != 0 || !g_theGxDevicePtr) {
        return;
    }

    const int shots = s_states[texture->m_filename];

    {
        CGxTex* gxTex = TextureGetGxTex(texture, 1, nullptr);
        char state[1100];
        g_theGxDevicePtr->DeviceDumpState(state, sizeof(state));
        char text[100];
        snprintf(text, sizeof(text), "    d3d after draw %d, texture meant: %p; %%s", shots, gxTex ? gxTex->m_apiSpecificData : nullptr);
        Log(text, state, 0);
    }

    // what this draw changed on the target: the whole target before against after, so no guess about where the quad lands
    std::vector<uint32_t> after;
    uint32_t width = 0, height = 0;

    if (!s_before.empty() && g_theGxDevicePtr->DeviceCaptureTarget(after, width, height) && after.size() == s_before.size()) {
        uint32_t x0 = width, y0 = height, x1 = 0, y1 = 0;
        unsigned changed = 0;

        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                if (after[y * width + x] != s_before[y * width + x]) {
                    changed++;
                    x0 = x < x0 ? x : x0;
                    y0 = y < y0 ? y : y0;
                    x1 = x > x1 ? x : x1;
                    y1 = y > y1 ? y : y1;
                }
            }
        }

        char text[300];
        snprintf(text, sizeof(text), "    draw %d of %%s: changed %u pixels, box (%u,%u)-(%u,%u) of %ux%u; quad was (%.3f,%.3f)-(%.3f,%.3f)", shots, changed, changed ? x0 : 0, changed ? y0 : 0, x1, y1,
            width, height, left, top, right, bottom);
        Log(text, texture->m_filename + kPrefixLength, 0);

        if (changed) {
            PendingSample sample;
            sample.name = texture->m_filename + kPrefixLength;
            sample.width = width;
            sample.x0 = x0;
            sample.y0 = y0;
            sample.x1 = x1;
            sample.y1 = y1;

            for (uint32_t y = y0; y <= y1; y++) {
                sample.before.insert(sample.before.end(), s_before.begin() + y * width + x0, s_before.begin() + y * width + x1 + 1);
                sample.after.insert(sample.after.end(), after.begin() + y * width + x0, after.begin() + y * width + x1 + 1);
            }

            s_pending.push_back(std::move(sample));
        }
    } else {
        char text[120];
        snprintf(text, sizeof(text), "    draw %d of %%s: target could not be captured", shots);
        Log(text, texture->m_filename + kPrefixLength, 0);
    }

    s_before.clear();
}

bool TraceBefore(CTexture* texture) {
    if (!texture || strncmp(texture->m_filename, kPrefix, kPrefixLength) != 0 || !g_theGxDevicePtr) {
        return false;
    }

    // the first two draws of a picture are traced, then one in 240 (every few seconds)
    const int next = ++s_states[texture->m_filename];

    if (next > 2 && next % 240 != 0) {
        return false;
    }

    uint32_t width = 0, height = 0;
    return g_theGxDevicePtr->DeviceCaptureTarget(s_before, width, height);
}

void FrameEnd() {
    if (s_pending.empty() || !g_theGxDevicePtr) {
        return;
    }

    // the target just before the frame is presented: of the pixels each draw changed, how many still hold what it drew
    std::vector<uint32_t> final;
    uint32_t width = 0, height = 0;
    const bool captured = g_theGxDevicePtr->DeviceCaptureTarget(final, width, height);

    for (const PendingSample& sample : s_pending) {
        unsigned drawn = 0, kept = 0, firstAfter = 0, firstFinal = 0;
        const uint32_t boxWidth = sample.x1 - sample.x0 + 1;

        for (size_t i = 0; captured && width == sample.width && i < sample.after.size(); i++) {
            if (sample.after[i] == sample.before[i]) {
                continue;
            }

            const uint32_t now = final[(sample.y0 + i / boxWidth) * width + sample.x0 + i % boxWidth];
            drawn++;

            if (now == sample.after[i]) {
                kept++;
            } else if (!firstAfter) {
                firstAfter = sample.after[i];
                firstFinal = now;
            }
        }

        char text[300];
        snprintf(text, sizeof(text), "    end of frame %%s: %u of %u drawn pixels survive to the end of the frame%s [first lost %08x -> %08x]",
            kept, drawn, captured ? "" : " (target not captured)", firstAfter, firstFinal);
        Log(text, sample.name.c_str(), 0);
    }

    s_pending.clear();
}

void Keep(const char* const* keys, size_t count) {
    std::unordered_set<std::string> keep;

    for (size_t i = 0; i < count; ++i) {
        keep.insert(keys[i]);
    }

    for (auto it = s_resident.begin(); it != s_resident.end();) {
        if (keep.count(it->first)) {
            ++it;
        } else {
            HandleClose(reinterpret_cast<HOBJECT>(it->second));
            it = s_resident.erase(it);
        }
    }

    // decoded pictures are 256 KB each; the ones nobody shows go once there are many
    if (s_pixels.size() > 64) {
        for (auto it = s_pixels.begin(); it != s_pixels.end();) {
            it = s_resident.count(it->first) ? std::next(it) : s_pixels.erase(it);
        }
    }
}

void ReleaseTargets() {
    ReleaseScratch();
}

void Clear() {
    for (auto& entry : s_resident) {
        HandleClose(reinterpret_cast<HOBJECT>(entry.second));
    }

    s_resident.clear();
    ReleaseScratch();
    s_valid.clear();
    s_files = 0;

    std::error_code ec;

    for (const auto& entry : fs::directory_iterator(kDirectory, ec)) {
        if (entry.path().extension() == ".snp") {
            fs::remove(entry.path(), ec);
        }
    }
}

size_t Count() {
    return s_resident.size();
}

unsigned Saved() {
    return s_saved;
}

unsigned Rejected() {
    return s_rejected;
}

int Failed() {
    return s_failed;
}

}  // namespace ModelSnapshot
