// Arcade cabinets: a small libretro frontend inside the client. The server (mod-esteria-arcade) charges for a game
// at a cabinet and sends SMSG_ESTERIA_ARCADE START with the core, the ROM's path in the patch MPQs and the player's
// saved battery RAM / resume state; the frontend boots the core from <client>\Arcade\Cores\<core>_libretro.dll,
// draws into a texture the EsteriaArcade add-on puts on screen, plays audio through an FMOD user stream and sends
// battery RAM and a resume state back when the game ends (and battery RAM every 30 s while it changes).
//
// Wire (one opcode each way, first byte is the sub-command; blobs are [u32 rawSize][zlib], chunked):
//   SMSG 0x54E  1 START  u32 session, cstr core, cstr rom, cstr title, u32 sramBytes, u32 stateBytes, u64 cabinet
//               (rom: archive paths under Arcade\Roms\ joined by '|'; the first is the game, the rest sit beside it:
//               a .cue's .bin, an arcade set's BIOS)
//               2 DATA   u32 session, u8 kind, u32 total, u32 offset, bytes
//               3 STOP   u32 session, cstr reason            (server ended it: walked away, logged out, ...)
//               4 VIEW   u32 session, u64 cabinet, u32 frame, u8 flags, u32 total, u32 offset, bytes  (someone else's game)
//               5 VIEW_END u32 session, u64 cabinet
//               6 NET_ADDR u32 session, u32 address                (this cabinet's address on the arcade network)
//               7 NET    u32 session, u32 from, u16 srcPort, u16 dstPort, bytes     (a datagram from another cabinet)
//   CMSG 0x54D  1 DATA   u32 session, u8 kind, u32 total, u32 offset, bytes
//               2 QUIT   u32 session                         (after the final DATA; the server then stores them)
//               3 VIEW   u32 session, u32 frame, u8 flags, u32 total, u32 offset, bytes
//               4 NET_JOIN u32 session                       (the core plays over the arcade network)
//               5 NET    u32 session, u32 to, u16 srcPort, u16 dstPort, bytes       (0xFFFFFFFF = every cabinet)
// VIEW pictures are the cabinet screen at 128x128 RGB565, zlib'd; flags bit 0 = full picture, otherwise XOR with the
// previous one. The server relays them to the players around, so they see the game on the cabinet too.
// The arcade network is a virtual LAN between cabinets playing the same game: a core that exports esteria_net_*
// (Tools/ArcadeCores/starcraft) hands over the game's UDP datagrams, the server assigns addresses (10.77.x.y) and
// relays them. StarCraft's LAN games run on it.
#include "Extensions/Arcade/src/libretro.h"

#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/Base/Handle.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/Frame/CScriptObject.h"
#include "Engine/Source/Frame/CSimpleTexture.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Shared.h"
#include "Engine/Source/SoundEngine/SoundEngine.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "WoW/Source/Extension/ExtensionMgr.h"
#include "WoW/Source/Object/ObjectClient/Object_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"

#include <windows.h>
#include <xinput.h>
#include <zlib.h>

#pragma comment(lib, "xinput9_1_0.lib")

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {
constexpr uint32_t CMSG_ARCADE = 0x54D, SMSG_ARCADE = 0x54E;
enum : uint8_t { S_START = 1, S_DATA = 2, S_STOP = 3, S_VIEW = 4, S_VIEW_END = 5, S_NET_ADDR = 6, S_NET = 7,
                 C_DATA = 1, C_QUIT = 2, C_VIEW = 3, C_NET_JOIN = 4, C_NET = 5 };
constexpr uint32_t kNetMax = 2048;            // one datagram (Tools/ArcadeCores/starcraft/ScShared.h kNetMax)
constexpr int kNetPerFrame = 128;             // datagrams sent per client frame at most (StarCraft sends ~10 a second)
enum : uint8_t { KIND_SRAM = 0, KIND_STATE = 1 };
constexpr uint32_t kChunk = 8000;             // keeps every packet well under the server's 10 KiB client limit
constexpr uint32_t kMaxBlob = 16u << 20;      // compressed; the server refuses more
constexpr uint32_t kTexW = 1024, kTexH = 1024; // N64 frames are 640 wide; 2x height for scanlines
constexpr uint32_t kFrameMax = 1024;          // the core's frame is clipped to 1024x1024 (none come close)
constexpr uint32_t kScreen = 256;             // the cabinet's in-world screen texture
constexpr float kScreenAspect = 1.56f;        // Tools/ArcadeProp SCREEN_ASPECT: the quad over the cabinet's opening
constexpr uint32_t kScreenType = 11;          // the quad's replaceable texture type (Tools/ArcadeProp SCREEN_TYPE)
constexpr const char* kCabinetModel = "arcade.m2"; // every cabinet look: World\Esteria\Arcade\<Name>Arcade.m2
constexpr uint32_t kView = 128;               // the picture streamed to spectators (the cabinet screen, halved)
constexpr float kViewInterval = 0.125f;       // 8 pictures a second
constexpr uint32_t kViewKeyEvery = 16;        // a full picture every 2 s (also keeps spectators alive while paused)
constexpr float kViewTimeout = 6.0f;          // a spectated screen with no pictures goes back to the attract screen
constexpr const char* kAttract = "Arcade\\Screens\\Attract.blp";

// ---- blob packing -------------------------------------------------------------------------------------------
std::vector<uint8_t> Pack(const void* data, size_t size) {
    uLongf bound = compressBound(uLong(size));
    std::vector<uint8_t> out(4 + bound);
    const uint32_t raw = uint32_t(size);
    std::memcpy(out.data(), &raw, 4);
    if (compress2(out.data() + 4, &bound, static_cast<const Bytef*>(data), uLong(size), 6) != Z_OK) return {};
    out.resize(4 + bound);
    return out;
}

bool Unpack(const std::vector<uint8_t>& blob, std::vector<uint8_t>& out) {
    if (blob.size() < 4) return false;
    uint32_t raw = 0;
    std::memcpy(&raw, blob.data(), 4);
    if (raw > (64u << 20)) return false;
    out.resize(raw);
    uLongf size = raw;
    return uncompress(out.data(), &size, blob.data() + 4, uLong(blob.size() - 4)) == Z_OK && size == raw;
}

// ---- the loaded core ----------------------------------------------------------------------------------------
struct Core {
    HMODULE dll = nullptr;
    void (*init)();
    void (*deinit)();
    void (*getSystemInfo)(retro_system_info*);
    void (*getAvInfo)(retro_system_av_info*);
    void (*setEnvironment)(retro_environment_t);
    void (*setVideo)(retro_video_refresh_t);
    void (*setAudio)(retro_audio_sample_t);
    void (*setAudioBatch)(retro_audio_sample_batch_t);
    void (*setInputPoll)(retro_input_poll_t);
    void (*setInputState)(retro_input_state_t);
    void (*setControllerPort)(unsigned, unsigned);
    void (*run)();
    bool (*loadGame)(const retro_game_info*);
    void (*unloadGame)();
    size_t (*serializeSize)();
    bool (*serialize)(void*, size_t);
    bool (*unserialize)(const void*, size_t);
    void* (*memoryData)(unsigned);
    size_t (*memorySize)(unsigned);
    // optional: the arcade network (see the wire notes above)
    void (*netAddress)(uint32_t) = nullptr;
    unsigned (*netTake)(void*, unsigned, uint32_t*, uint16_t*, uint16_t*) = nullptr;
    void (*netGive)(const void*, unsigned, uint32_t, uint16_t, uint16_t) = nullptr;

    template <class T> bool Get(T& fn, const char* name) {
        fn = reinterpret_cast<T>(GetProcAddress(dll, name));
        return fn != nullptr;
    }
    bool Load(const std::string& path) {
        dll = LoadLibraryA(path.c_str());
        if (!dll) return false;
        Get(netAddress, "esteria_net_address");  // optional (most cores have no network)
        Get(netTake, "esteria_net_take");
        Get(netGive, "esteria_net_give");
        return Get(init, "retro_init") && Get(deinit, "retro_deinit") && Get(getSystemInfo, "retro_get_system_info")
            && Get(getAvInfo, "retro_get_system_av_info") && Get(setEnvironment, "retro_set_environment")
            && Get(setVideo, "retro_set_video_refresh") && Get(setAudio, "retro_set_audio_sample")
            && Get(setAudioBatch, "retro_set_audio_sample_batch") && Get(setInputPoll, "retro_set_input_poll")
            && Get(setInputState, "retro_set_input_state") && Get(setControllerPort, "retro_set_controller_port_device")
            && Get(run, "retro_run") && Get(loadGame, "retro_load_game") && Get(unloadGame, "retro_unload_game")
            && Get(serializeSize, "retro_serialize_size") && Get(serialize, "retro_serialize")
            && Get(unserialize, "retro_unserialize") && Get(memoryData, "retro_get_memory_data")
            && Get(memorySize, "retro_get_memory_size");
    }
    bool Networked() const { return netAddress && netTake && netGive; }
    void Free() {
        if (dll) FreeLibrary(dll);
        dll = nullptr;
        netAddress = nullptr;
        netTake = nullptr;
        netGive = nullptr;
    }
};

// Keyboard and mouse, for computer cores (DOSBox Pure): the add-on passes keys through and the cursor over the
// picture (Arcade_SetKey, Arcade_SetPointer, Arcade_SetMouseButton). Cores read them with input_state and/or a
// keyboard callback; DOSBox Pure needs both (a key the callback pressed is released unless input_state still holds it).
retro_keyboard_event_t s_keyboardEvent;
bool s_keyDown[RETROK_LAST];
int16_t s_pointerX, s_pointerY;  // libretro pointer range, -0x7FFF..0x7FFF across the picture
bool s_pointerIn;
uint8_t s_mouseButtons;  // 1 left, 2 right, 4 middle
void ReleaseKeyboard();

// ---- frontend state -----------------------------------------------------------------------------------------
enum class Phase { Idle, Receiving, Running };

struct Session {
    Phase phase = Phase::Idle;
    uint32_t id = 0;
    std::string core, rom, title;
    uint64_t cabinet = 0;                  // the cabinet's guid: its in-world screen shows the game
    std::vector<uint8_t> blob[2];
    uint32_t expected[2] = {};
    uint32_t received[2] = {};
} s_session;

Core s_core;
bool s_gameLoaded;
std::vector<uint8_t> s_romData;
std::string s_systemDir;
retro_pixel_format s_format = RETRO_PIXEL_FORMAT_0RGB1555;
double s_fps = 60.0, s_clock;
float s_aspect = 4.0f / 3.0f;
bool s_paused;
uint32_t s_keys;  // keyboard: RETRO_DEVICE_ID_JOYPAD_* bits 0-15, then KEY_LSTICK_* / KEY_RSTICK_* (16-23)
enum : uint32_t { KEY_LSTICK = 16, KEY_RSTICK = 20 };  // + 0 up, 1 down, 2 left, 3 right
uint32_t s_sramHash;
float s_sramTimer;

// Display options (Arcade_SetOption), set by the add-on from its saved settings.
struct Options {
    bool smooth = false;       // bilinear instead of sharp pixels in the window
    float scanlines = 0.0f;    // 0..0.9: darkness of every second line in the window
    float brightness = 1.0f;   // 0.3..2
    float saturation = 1.0f;   // 0..2
    int palette = 0;           // 0 normal, 1 grayscale, 2 green screen, 3 sepia, 4 amber, 5 inverted
    bool cabinet = true;       // mirror the game onto the cabinet's screen in the world
    int fastSpeed = 3;         // frames per frame while fast-forward is held
} s_opt;
bool s_fast;

std::vector<uint32_t> s_frame(kFrameMax * kFrameMax, 0xFF000000u);   // the core's last frame, ARGB, stride kFrameMax
std::vector<uint32_t> s_pixels(kTexW * kTexH, 0xFF000000u);          // the window texture
std::vector<uint32_t> s_screenPixels(kScreen * kScreen, 0xFF000000u); // the cabinet screen texture
uint32_t s_width = 256, s_height = 224, s_outHeight = 224;
bool s_dirty;
CTexture* s_texture;
CTexture* s_screenTexture;
CTexture* s_attract;
CM2Model* s_cabinetModel;              // compared only (never dereferenced) to notice a recreated model

// Audio: the core produces int16 stereo, FMOD pulls it from its stream thread.
std::mutex s_audioLock;
std::vector<int16_t> s_ring;              // interleaved frames
size_t s_ringRead, s_ringWrite, s_ringFill; // in frames
FMOD::Sound* s_sound;
FMOD::Channel* s_channel;
float s_volume = 0.8f;

// Fires an add-on event with up to two string arguments.
void Signal(const char* name, const char* a, const char* b = nullptr) {
    const uint32_t index = sExtensionMgr->GetFrameScriptEventIndex(name);
    if (index == UINT32_MAX) return;
    if (b) FrameScript_SignalEvent(index, "%s%s", a, b);
    else FrameScript_SignalEvent(index, "%s", a);
}

// ---- libretro callbacks -------------------------------------------------------------------------------------
void CoreLog(retro_log_level level, const char* format, ...) {
    if (level < RETRO_LOG_WARN) return;
    char text[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    sExtensionMgr->Log("Arcade core: %s", text);
}

// Core options the frontend sets; every other option keeps the core's default.
const char* CoreOption(const char* key) {
    static const struct { const char* core; const char* key; const char* value; } options[] = {
        {"parallel_n64", "parallel-n64-gfxplugin", "angrylion"},  // the software renderer: no GL context here
        {"dosbox_pure", "dosbox_pure_mouse_input", "direct"},       // the game's cursor sits where the player's is
    };
    for (const auto& o : options)
        if (s_session.core == o.core && key && !std::strcmp(key, o.key)) return o.value;
    return nullptr;
}

bool s_coreShutdown = false;  // set by RETRO_ENVIRONMENT_SHUTDOWN, acted on in the frame loop (not inside the core's call)

bool AnalogCore() { return s_session.core == "parallel_n64"; }  // games steered with the stick, not the d-pad

bool Environment(unsigned command, void* data) {
    switch (command & ~RETRO_ENVIRONMENT_EXPERIMENTAL) {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
            const auto format = *static_cast<retro_pixel_format*>(data);
            if (format > RETRO_PIXEL_FORMAT_RGB565) return false;
            s_format = format;
            return true;
        }
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            *static_cast<const char**>(data) = s_systemDir.c_str();
            return true;
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
            static_cast<retro_log_callback*>(data)->log = CoreLog;
            return true;
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
            if (data) *static_cast<bool*>(data) = true;
            return true;
        case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
            if (data) *static_cast<int*>(data) = 3;
            return true;
        case RETRO_ENVIRONMENT_GET_LANGUAGE:
            if (data) *static_cast<unsigned*>(data) = RETRO_LANGUAGE_ENGLISH;
            return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE: {
            auto* variable = static_cast<retro_variable*>(data);
            const char* value = variable ? CoreOption(variable->key) : nullptr;
            if (value) variable->value = value;
            return value != nullptr;
        }
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            if (data) *static_cast<bool*>(data) = false;
            return true;
        case RETRO_ENVIRONMENT_SET_GEOMETRY: {
            const auto* geometry = static_cast<const retro_game_geometry*>(data);
            if (geometry->aspect_ratio > 0) s_aspect = geometry->aspect_ratio;
            return true;
        }
        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
            const auto* info = static_cast<const retro_system_av_info*>(data);
            if (info->timing.fps > 1) s_fps = info->timing.fps;
            if (info->geometry.aspect_ratio > 0) s_aspect = info->geometry.aspect_ratio;
            return true;
        }
        // Accepted and ignored: descriptions, options (cores fall back to their defaults), memory maps.
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
        case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
        case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        case RETRO_ENVIRONMENT_SET_VARIABLES:
        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
        case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
        case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
            return true;
        case RETRO_ENVIRONMENT_SHUTDOWN:  // the game itself exited (a PC game's own Quit): end the session after this frame
            s_coreShutdown = true;
            return true;
        case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
            s_keyboardEvent = data ? static_cast<const retro_keyboard_callback*>(data)->callback : nullptr;
            return true;
        default:
            return false;
    }
}

void Video(const void* data, unsigned width, unsigned height, size_t pitch) {
    if (!data) return;  // duplicated frame
    width = std::min(width, kFrameMax);
    height = std::min(height, kFrameMax);
    for (unsigned y = 0; y < height; ++y) {
        const auto* row = static_cast<const uint8_t*>(data) + y * pitch;
        uint32_t* out = s_frame.data() + y * kFrameMax;
        if (s_format == RETRO_PIXEL_FORMAT_XRGB8888) {
            const auto* in = reinterpret_cast<const uint32_t*>(row);
            for (unsigned x = 0; x < width; ++x) out[x] = in[x] | 0xFF000000u;
        } else {
            const auto* in = reinterpret_cast<const uint16_t*>(row);
            const bool rgb565 = s_format == RETRO_PIXEL_FORMAT_RGB565;
            for (unsigned x = 0; x < width; ++x) {
                const uint32_t p = in[x];
                uint32_t r, g, b;
                if (rgb565) { r = (p >> 11) & 31; g = (p >> 5) & 63; b = p & 31; g = (g << 2) | (g >> 4); }
                else { r = (p >> 10) & 31; g = (p >> 5) & 31; b = p & 31; g = (g << 3) | (g >> 2); }
                r = (r << 3) | (r >> 2);
                b = (b << 3) | (b >> 2);
                out[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
        }
    }
    s_width = width;
    s_height = height;
    s_dirty = true;
}

size_t AudioBatch(const int16_t* data, size_t frames) {
    std::lock_guard<std::mutex> lock(s_audioLock);
    const size_t capacity = s_ring.size() / 2;
    if (!capacity) return frames;
    for (size_t i = 0; i < frames; ++i) {
        if (s_ringFill == capacity) {  // overrun: drop the oldest frame
            s_ringRead = (s_ringRead + 1) % capacity;
            --s_ringFill;
        }
        s_ring[s_ringWrite * 2] = data[i * 2];
        s_ring[s_ringWrite * 2 + 1] = data[i * 2 + 1];
        s_ringWrite = (s_ringWrite + 1) % capacity;
        ++s_ringFill;
    }
    return frames;
}

void AudioSample(int16_t left, int16_t right) {
    const int16_t frame[2] = {left, right};
    AudioBatch(frame, 1);
}

FMOD_RESULT F_CALLBACK AudioRead(FMOD_SOUND*, void* data, unsigned int bytes) {
    auto* out = static_cast<int16_t*>(data);
    const size_t frames = bytes / 4;
    std::lock_guard<std::mutex> lock(s_audioLock);
    const size_t capacity = s_ring.size() / 2;
    for (size_t i = 0; i < frames; ++i) {
        if (s_ringFill && capacity) {
            out[i * 2] = s_ring[s_ringRead * 2];
            out[i * 2 + 1] = s_ring[s_ringRead * 2 + 1];
            s_ringRead = (s_ringRead + 1) % capacity;
            --s_ringFill;
        } else {
            out[i * 2] = out[i * 2 + 1] = 0;
        }
    }
    return FMOD_OK;
}

int16_t s_analog[2][2];  // [left/right stick][x/y], libretro range, +y down

int16_t Axis(bool negative, bool positive) { return int16_t((positive ? 0x7FFF : 0) - (negative ? 0x7FFF : 0)); }

uint16_t PadBits() {
    uint16_t bits = uint16_t(s_keys & 0xFFFF);
    const bool analog = AnalogCore();
    const auto key = [](uint32_t bit) { return (s_keys >> bit) & 1; };
    s_analog[0][0] = Axis(key(KEY_LSTICK + 2), key(KEY_LSTICK + 3));
    s_analog[0][1] = Axis(key(KEY_LSTICK + 0), key(KEY_LSTICK + 1));
    s_analog[1][0] = Axis(key(KEY_RSTICK + 2), key(KEY_RSTICK + 3));
    s_analog[1][1] = Axis(key(KEY_RSTICK + 0), key(KEY_RSTICK + 1));
    if (analog) {  // the keyboard's d-pad keys steer the stick (N64 games are played on it)
        const auto pad = [&](unsigned id) { return (bits >> id) & 1; };
        if (!s_analog[0][0]) s_analog[0][0] = Axis(pad(RETRO_DEVICE_ID_JOYPAD_LEFT), pad(RETRO_DEVICE_ID_JOYPAD_RIGHT));
        if (!s_analog[0][1]) s_analog[0][1] = Axis(pad(RETRO_DEVICE_ID_JOYPAD_UP), pad(RETRO_DEVICE_ID_JOYPAD_DOWN));
        bits &= uint16_t(~((1u << RETRO_DEVICE_ID_JOYPAD_UP) | (1u << RETRO_DEVICE_ID_JOYPAD_DOWN)
            | (1u << RETRO_DEVICE_ID_JOYPAD_LEFT) | (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT)));
    }
    XINPUT_STATE pad = {};
    if (XInputGetState(0, &pad) == ERROR_SUCCESS) {
        const WORD b = pad.Gamepad.wButtons;
        const SHORT lx = pad.Gamepad.sThumbLX, ly = pad.Gamepad.sThumbLY;
        constexpr SHORT dead = 12000;
        auto set = [&](bool on, unsigned id) { if (on) bits |= uint16_t(1u << id); };
        const auto stick = [](SHORT v, bool invert) {
            const int s = std::abs(int(v)) < 7000 ? 0 : std::clamp(invert ? -int(v) : int(v), -0x7FFF, 0x7FFF);
            return int16_t(s);
        };
        if (int16_t x = stick(lx, false)) s_analog[0][0] = x;
        if (int16_t y = stick(ly, true)) s_analog[0][1] = y;
        if (int16_t x = stick(pad.Gamepad.sThumbRX, false)) s_analog[1][0] = x;
        if (int16_t y = stick(pad.Gamepad.sThumbRY, true)) s_analog[1][1] = y;
        const bool stickAsPad = !analog;  // digital-only systems: the left stick also works the d-pad
        set((b & XINPUT_GAMEPAD_DPAD_UP) || (stickAsPad && ly > dead), RETRO_DEVICE_ID_JOYPAD_UP);
        set((b & XINPUT_GAMEPAD_DPAD_DOWN) || (stickAsPad && ly < -dead), RETRO_DEVICE_ID_JOYPAD_DOWN);
        set((b & XINPUT_GAMEPAD_DPAD_LEFT) || (stickAsPad && lx < -dead), RETRO_DEVICE_ID_JOYPAD_LEFT);
        set((b & XINPUT_GAMEPAD_DPAD_RIGHT) || (stickAsPad && lx > dead), RETRO_DEVICE_ID_JOYPAD_RIGHT);
        set(pad.Gamepad.bLeftTrigger > 64, RETRO_DEVICE_ID_JOYPAD_L2);
        set(pad.Gamepad.bRightTrigger > 64, RETRO_DEVICE_ID_JOYPAD_R2);
        set(b & XINPUT_GAMEPAD_LEFT_THUMB, RETRO_DEVICE_ID_JOYPAD_L3);
        set(b & XINPUT_GAMEPAD_RIGHT_THUMB, RETRO_DEVICE_ID_JOYPAD_R3);
        // Nintendo layout: the face button in the same position, not the same letter.
        set(b & XINPUT_GAMEPAD_B, RETRO_DEVICE_ID_JOYPAD_A);
        set(b & XINPUT_GAMEPAD_A, RETRO_DEVICE_ID_JOYPAD_B);
        set(b & XINPUT_GAMEPAD_Y, RETRO_DEVICE_ID_JOYPAD_X);
        set(b & XINPUT_GAMEPAD_X, RETRO_DEVICE_ID_JOYPAD_Y);
        set(b & XINPUT_GAMEPAD_LEFT_SHOULDER, RETRO_DEVICE_ID_JOYPAD_L);
        set(b & XINPUT_GAMEPAD_RIGHT_SHOULDER, RETRO_DEVICE_ID_JOYPAD_R);
        set(b & XINPUT_GAMEPAD_START, RETRO_DEVICE_ID_JOYPAD_START);
        set(b & XINPUT_GAMEPAD_BACK, RETRO_DEVICE_ID_JOYPAD_SELECT);
    }
    return bits;
}

uint16_t s_polled;
void InputPoll() { s_polled = PadBits(); }

int16_t InputState(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (port != 0) return 0;
    switch (device & RETRO_DEVICE_MASK) {
        case RETRO_DEVICE_KEYBOARD:
            return id < RETROK_LAST && s_keyDown[id];
        case RETRO_DEVICE_MOUSE:
            if (id == RETRO_DEVICE_ID_MOUSE_LEFT) return s_mouseButtons & 1;
            if (id == RETRO_DEVICE_ID_MOUSE_RIGHT) return (s_mouseButtons >> 1) & 1;
            if (id == RETRO_DEVICE_ID_MOUSE_MIDDLE) return (s_mouseButtons >> 2) & 1;
            return 0;  // no relative movement: the pointer is absolute
        case RETRO_DEVICE_POINTER:
            if (!s_pointerIn || index != 0) return 0;
            if (id == RETRO_DEVICE_ID_POINTER_X) return s_pointerX;
            if (id == RETRO_DEVICE_ID_POINTER_Y) return s_pointerY;
            if (id == RETRO_DEVICE_ID_POINTER_PRESSED) return s_mouseButtons & 1;
            return 0;
        default:
            break;
    }
    if ((device & RETRO_DEVICE_MASK) == RETRO_DEVICE_ANALOG) {
        if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON) return id < 16 && ((s_polled >> id) & 1) ? 0x7FFF : 0;
        return index < 2 && id < 2 ? s_analog[index][id] : 0;
    }
    if ((device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD) return 0;
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return int16_t(s_polled);
    return id < 16 ? int16_t((s_polled >> id) & 1) : 0;
}

// ---- texture ------------------------------------------------------------------------------------------------
void TextureUpdate(EGxTexCommand command, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t& stride, const void*& texels) {
    if (command == GxTex_Latch) {
        texels = s_pixels.data();
        stride = kTexW * 4;
    }
}

void ScreenTextureUpdate(EGxTexCommand command, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t& stride, const void*& texels) {
    if (command == GxTex_Latch) {
        texels = s_screenPixels.data();
        stride = kScreen * 4;
    }
}

uint32_t Scale(uint32_t p, float f) {
    const auto c = [&](int shift) { return std::min(255u, uint32_t(float((p >> shift) & 255) * f)) << shift; };
    return 0xFF000000u | c(16) | c(8) | c(0);
}

// Colour options on one pixel.
uint32_t Shade(uint32_t p) {
    if (s_opt.palette == 0 && s_opt.saturation == 1.0f && s_opt.brightness == 1.0f) return p;
    float r = float((p >> 16) & 255), g = float((p >> 8) & 255), b = float(p & 255);
    const float l = 0.299f * r + 0.587f * g + 0.114f * b;
    switch (s_opt.palette) {
        case 1: r = g = b = l; break;
        case 2: {  // the original Game Boy's four greens
            static const float ramp[4][3] = {{15, 56, 15}, {48, 98, 48}, {139, 172, 15}, {155, 188, 15}};
            const float* c = ramp[std::min(3, int(l / 64.0f))];
            r = c[0]; g = c[1]; b = c[2];
            break;
        }
        case 3: r = l * 1.07f; g = l * 0.74f; b = l * 0.43f; break;
        case 4: r = l; g = l * 0.69f; b = l * 0.12f; break;
        case 5: r = 255 - r; g = 255 - g; b = 255 - b; break;
        default:
            r = l + (r - l) * s_opt.saturation;
            g = l + (g - l) * s_opt.saturation;
            b = l + (b - l) * s_opt.saturation;
    }
    const auto c = [&](float v) { return uint32_t(std::clamp(v * s_opt.brightness, 0.0f, 255.0f)); };
    return 0xFF000000u | (c(r) << 16) | (c(g) << 8) | c(b);
}

// Builds the window picture (colour options, optional scanlines at 2x height) and the cabinet screen picture
// (letterboxed to the quad) from the core's last frame, once per drawn client frame.
void UploadFrame() {
    if (!s_dirty) return;
    s_dirty = false;
    const bool lines = s_opt.scanlines > 0.0f && 2 * s_height <= kTexH;  // doubled lines must fit
    const float dark = 1.0f - s_opt.scanlines;
    for (uint32_t y = 0; y < s_height; ++y) {
        const uint32_t* in = s_frame.data() + y * kFrameMax;
        uint32_t* out = s_pixels.data() + (lines ? 2 * y : y) * kTexW;
        for (uint32_t x = 0; x < s_width; ++x) out[x] = Shade(in[x]);
        if (lines)
            for (uint32_t x = 0; x < s_width; ++x) out[kTexW + x] = Scale(out[x], dark);
    }
    s_outHeight = lines ? 2 * s_height : s_height;
    if (s_texture)
        if (CGxTex* gx = TextureGetGxTex(s_texture, 0, nullptr)) GxTexUpdate(gx, 0, 0, int32_t(s_width), int32_t(s_outHeight), 1);

    if (!s_session.cabinet) return;
    uint32_t w = kScreen, h = kScreen;
    if (s_aspect < kScreenAspect) w = uint32_t(kScreen * s_aspect / kScreenAspect);
    else h = uint32_t(kScreen * kScreenAspect / s_aspect);
    const uint32_t x0 = (kScreen - w) / 2, y0 = (kScreen - h) / 2;
    std::fill(s_screenPixels.begin(), s_screenPixels.end(), 0xFF000000u);
    for (uint32_t y = 0; y < h; ++y) {
        const uint32_t* in = s_frame.data() + (y * s_height / h) * kFrameMax;
        uint32_t* out = s_screenPixels.data() + (y0 + y) * kScreen + x0;
        const bool odd = y & 1;  // a light CRT line pattern in the world
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t p = Shade(in[x * s_width / w]);
            out[x] = odd ? Scale(p, 0.82f) : p;
        }
    }
    if (s_opt.cabinet && s_screenTexture && s_cabinetModel)
        if (CGxTex* gx = TextureGetGxTex(s_screenTexture, 0, nullptr)) GxTexUpdate(gx, 0, 0, kScreen, kScreen, 1);
}

CTexture* Attract() {
    if (!s_attract) {
        CStatus status;
        s_attract = TextureCreate(kAttract, CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1), &status, 0);
    }
    return s_attract;
}

CM2Model* CabinetModel(uint64_t guid) {
    if (!guid) return nullptr;
    CGObject_C* object = ClntObjMgrObjectPtr(WOWGUID{guid}, TYPEMASK_GAMEOBJECT, __FILE__, __LINE__);
    return object ? object->GetObjectModel() : nullptr;
}

// Keeps the played cabinet's screen on the live texture (its model is recreated when it leaves view and returns).
void ApplyCabinet() {
    CM2Model* model = s_opt.cabinet ? CabinetModel(s_session.cabinet) : nullptr;
    if (model == s_cabinetModel) return;
    s_cabinetModel = model;
    if (!model) return;
    if (!s_screenTexture)
        s_screenTexture = TextureCreate(kScreen, kScreen, GxTex_Argb8888, GxTex_Argb8888,
            CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1), nullptr, ScreenTextureUpdate, "ArcadeCabinetScreen", 0);
    if (s_screenTexture) model->ReplaceTexture(kScreenType, s_screenTexture);
    s_dirty = true;
}

void RestoreCabinet(uint64_t guid) {
    if (CM2Model* model = CabinetModel(guid))
        if (CTexture* attract = Attract()) model->ReplaceTexture(kScreenType, attract);
    s_cabinetModel = nullptr;
}

// ---- spectating ---------------------------------------------------------------------------------------------
uint16_t To565(uint32_t r, uint32_t g, uint32_t b) { return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }

uint32_t From565(uint16_t c) {
    uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

std::vector<uint16_t> s_viewSent(kView * kView);
uint32_t s_viewFrame;
float s_viewTimer;

// Streams this player's cabinet screen (2x2 averaged) to the server for the players around.
void SendView() {
    std::vector<uint16_t> picture(kView * kView);
    for (uint32_t y = 0; y < kView; ++y)
        for (uint32_t x = 0; x < kView; ++x) {
            const uint32_t* a = s_screenPixels.data() + (2 * y) * kScreen + 2 * x;
            const uint32_t* b = a + kScreen;
            const auto avg = [&](int shift) { return (((a[0] >> shift) & 255) + ((a[1] >> shift) & 255) + ((b[0] >> shift) & 255) + ((b[1] >> shift) & 255)) / 4; };
            picture[y * kView + x] = To565(avg(16), avg(8), avg(0));
        }
    const bool full = s_viewFrame % kViewKeyEvery == 0;
    std::vector<uint16_t> payload = picture;
    if (!full)
        for (size_t i = 0; i < payload.size(); ++i) payload[i] ^= s_viewSent[i];
    s_viewSent = picture;
    uLongf size = compressBound(uLong(payload.size() * 2));
    std::vector<uint8_t> blob(size);
    if (compress2(blob.data(), &size, reinterpret_cast<const Bytef*>(payload.data()), uLong(payload.size() * 2), 1) != Z_OK) return;
    const uint32_t total = uint32_t(size), frame = s_viewFrame++;
    const uint8_t flags = full ? 1 : 0;
    for (uint32_t offset = 0; offset < total; offset += kChunk) {
        const uint32_t n = std::min(kChunk, total - offset);
        std::vector<uint8_t> packet(1 + 4 + 4 + 1 + 4 + 4 + n);
        packet[0] = C_VIEW;
        std::memcpy(&packet[1], &s_session.id, 4);
        std::memcpy(&packet[5], &frame, 4);
        packet[9] = flags;
        std::memcpy(&packet[10], &total, 4);
        std::memcpy(&packet[14], &offset, 4);
        std::memcpy(&packet[18], blob.data() + offset, n);
        sExtensionMgr->SendClientMessage(CMSG_ARCADE, packet.data(), uint32_t(packet.size()));
    }
}

// Another player's cabinet, as seen here.
struct Viewer {
    std::vector<uint32_t> pixels = std::vector<uint32_t>(kView * kView, 0xFF000000u);
    std::vector<uint16_t> last = std::vector<uint16_t>(kView * kView);
    std::vector<uint8_t> chunks;
    uint32_t frame = UINT32_MAX, got = 0, lastFrame = 0;
    uint8_t flags = 0;
    bool synced = false;
    float idle = 0;
    CTexture* texture = nullptr;
    CM2Model* applied = nullptr;   // compared only
};
std::map<uint64_t, std::unique_ptr<Viewer>> s_viewers;

void ViewerTextureUpdate(EGxTexCommand command, uint32_t, uint32_t, uint32_t, uint32_t, void* userArg, uint32_t& stride, const void*& texels) {
    if (command == GxTex_Latch) {
        texels = static_cast<Viewer*>(userArg)->pixels.data();
        stride = kView * 4;
    }
}

bool Playing(uint64_t cabinet) { return s_session.phase != Phase::Idle && s_session.cabinet == cabinet; }

void ShowViewer(uint64_t cabinet, Viewer& v) {
    if (!v.texture || !v.synced || Playing(cabinet)) return;
    CM2Model* model = CabinetModel(cabinet);
    if (model && model != v.applied) model->ReplaceTexture(kScreenType, v.texture);
    v.applied = model;
}

void EndViewer(uint64_t cabinet) {
    auto it = s_viewers.find(cabinet);
    if (it == s_viewers.end()) return;
    if (!Playing(cabinet))
        if (CM2Model* model = CabinetModel(cabinet))
            if (CTexture* attract = Attract()) model->ReplaceTexture(kScreenType, attract);
    if (it->second->texture) HandleClose(reinterpret_cast<HOBJECT>(it->second->texture));
    s_viewers.erase(it);
}

void ReceiveView(uint64_t cabinet, uint32_t frame, uint8_t flags, uint32_t total, uint32_t offset, const uint8_t* data, size_t n) {
    if (Playing(cabinet) || total > 96 * 1024 || offset > total || n > total - offset) return;
    auto& slot = s_viewers[cabinet];
    if (!slot) slot = std::make_unique<Viewer>();
    Viewer& v = *slot;
    v.idle = 0;
    if (frame != v.frame) {
        v.frame = frame;
        v.flags = flags;
        v.chunks.assign(total, 0);
        v.got = 0;
    }
    if (v.chunks.size() != total) return;
    std::memcpy(v.chunks.data() + offset, data, n);
    if ((v.got += uint32_t(n)) < total) return;

    std::vector<uint16_t> picture(kView * kView);
    uLongf size = uLongf(picture.size() * 2);
    const bool full = v.flags & 1;
    if (uncompress(reinterpret_cast<Bytef*>(picture.data()), &size, v.chunks.data(), uLong(total)) != Z_OK || size != picture.size() * 2
        || (!full && (!v.synced || frame != v.lastFrame + 1))) {
        v.synced = false;  // wait for the next full picture
        return;
    }
    v.synced = true;
    v.lastFrame = frame;
    for (size_t i = 0; i < picture.size(); ++i) {
        const uint16_t c = full ? picture[i] : uint16_t(picture[i] ^ v.last[i]);
        v.last[i] = c;
        v.pixels[i] = From565(c);
    }
    if (!v.texture)
        v.texture = TextureCreate(kView, kView, GxTex_Argb8888, GxTex_Argb8888, CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1),
            &v, ViewerTextureUpdate, "ArcadeSpectator", 0);
    if (v.texture)
        if (CGxTex* gx = TextureGetGxTex(v.texture, 0, nullptr)) GxTexUpdate(gx, 0, 0, kView, kView, 1);
    ShowViewer(cabinet, v);
}

void UpdateViewers(float elapsed) {
    for (auto it = s_viewers.begin(); it != s_viewers.end();) {
        const uint64_t cabinet = it->first;
        Viewer& v = *it->second;
        ++it;
        if ((v.idle += elapsed) > kViewTimeout) EndViewer(cabinet);
        else ShowViewer(cabinet, v);  // the model is recreated when the cabinet leaves view and returns
    }
}

// ---- network ------------------------------------------------------------------------------------------------
std::string ExeDir() {
    char path[MAX_PATH] = "";
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string dir = path;
    return dir.substr(0, dir.find_last_of("\\/") + 1);
}

bool SafeName(const std::string& name) {
    return !name.empty() && name.size() < 48
        && std::all_of(name.begin(), name.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

// Saves leave at most kOutboxPerFrame packets per client frame (an N64 resume state is hundreds of packets).
constexpr size_t kOutboxPerFrame = 12;
std::deque<std::vector<uint8_t>> s_outbox;

// The game's outgoing datagrams to the server, every frame (also while paused: a PC game keeps running).
void SendNet() {
    if (s_session.phase != Phase::Running || !s_gameLoaded || !s_core.Networked()) return;
    std::vector<uint8_t> packet(1 + 4 + 4 + 2 + 2 + kNetMax);
    packet[0] = C_NET;
    std::memcpy(&packet[1], &s_session.id, 4);
    for (int i = 0; i < kNetPerFrame; ++i) {
        uint32_t to = 0;
        uint16_t srcPort = 0, dstPort = 0;
        const unsigned n = s_core.netTake(&packet[13], kNetMax, &to, &srcPort, &dstPort);
        if (!n) break;
        std::memcpy(&packet[5], &to, 4);
        std::memcpy(&packet[9], &srcPort, 2);
        std::memcpy(&packet[11], &dstPort, 2);
        sExtensionMgr->SendClientMessage(CMSG_ARCADE, packet.data(), 13 + n);
    }
}

void FlushOutbox(size_t limit) {
    for (; limit && !s_outbox.empty(); --limit) {
        sExtensionMgr->SendClientMessage(CMSG_ARCADE, s_outbox.front().data(), uint32_t(s_outbox.front().size()));
        s_outbox.pop_front();
    }
}

void SendBlob(uint8_t kind, const std::vector<uint8_t>& blob) {
    const uint32_t total = uint32_t(blob.size());
    for (uint32_t offset = 0; offset < total; offset += kChunk) {
        const uint32_t n = std::min(kChunk, total - offset);
        std::vector<uint8_t> packet(1 + 4 + 1 + 4 + 4 + n);
        packet[0] = C_DATA;
        std::memcpy(&packet[1], &s_session.id, 4);
        packet[5] = kind;
        std::memcpy(&packet[6], &total, 4);
        std::memcpy(&packet[10], &offset, 4);
        std::memcpy(&packet[14], blob.data() + offset, n);
        s_outbox.push_back(std::move(packet));
    }
}

uint32_t Hash(const void* data, size_t size) {
    return uint32_t(crc32(0, static_cast<const Bytef*>(data), uInt(size)));
}

void SendSram(bool onlyIfChanged) {
    if (!s_gameLoaded) return;
    void* data = s_core.memoryData(RETRO_MEMORY_SAVE_RAM);
    const size_t size = s_core.memorySize(RETRO_MEMORY_SAVE_RAM);
    if (!data || !size) return;
    const uint32_t hash = Hash(data, size);
    if (onlyIfChanged && hash == s_sramHash) return;
    s_sramHash = hash;
    const std::vector<uint8_t> blob = Pack(data, size);
    if (!blob.empty() && blob.size() <= kMaxBlob) SendBlob(KIND_SRAM, blob);
}

void StopAudio() {
    if (s_channel) s_channel->stop();
    s_channel = nullptr;
    if (s_sound) s_sound->release();
    s_sound = nullptr;
    std::lock_guard<std::mutex> lock(s_audioLock);
    s_ring.clear();
    s_ringRead = s_ringWrite = s_ringFill = 0;
}

// Ends the game. With save, battery RAM and a resume state go to the server first, then QUIT.
void Shutdown(bool save, const char* reason) {
    if (s_session.phase == Phase::Running && s_gameLoaded && save) {
        SendSram(false);
        const size_t size = s_core.serializeSize();
        std::vector<uint8_t> state(size);
        if (size && s_core.serialize(state.data(), size)) {
            const std::vector<uint8_t> blob = Pack(state.data(), size);
            if (!blob.empty() && blob.size() <= kMaxBlob) SendBlob(KIND_STATE, blob);
        }
    }
    if (s_session.phase != Phase::Idle) {
        std::vector<uint8_t> packet(5, C_QUIT);
        std::memcpy(&packet[1], &s_session.id, 4);
        s_outbox.push_back(std::move(packet));  // after its uploads
    }
    StopAudio();
    RestoreCabinet(s_session.cabinet);
    ReleaseKeyboard();          // key-ups while the core can still take them
    s_keyboardEvent = nullptr;  // it points into the core's DLL: a later key (or release) after Free crashed the client
    if (s_gameLoaded) s_core.unloadGame();
    if (s_core.dll) s_core.deinit();
    s_core.Free();
    s_gameLoaded = false;
    s_romData.clear();
    s_romData.shrink_to_fit();
    const bool wasActive = s_session.phase != Phase::Idle;
    s_session = Session();
    s_keys = 0;
    if (wasActive) Signal("ARCADE_STOPPED", reason ? reason : "");
}

std::vector<std::string> Split(const std::string& text, char sep) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t at; (at = text.find(sep, start)) != std::string::npos; start = at + 1) parts.push_back(text.substr(start, at - start));
    parts.push_back(text.substr(start));
    return parts;
}

void MakeDirectories(const std::string& path) {  // every folder of a file path
    for (size_t at = path.find('\\', 3); at != std::string::npos; at = path.find('\\', at + 1))
        CreateDirectoryA(path.substr(0, at).c_str(), nullptr);
}

// A core that opens files itself gets <client>\Arcade\Cache\<path under Arcade\Roms\>, copied out of the archives
// once; a "<file>.ok" marker holding the size says the copy finished.
bool CacheFile(const std::string& archived, std::string& out) {
    out = ExeDir() + "Arcade\\Cache\\" + archived.substr(std::strlen("Arcade\\Roms\\"));
    const std::string marker = out + ".ok";
    WIN32_FILE_ATTRIBUTE_DATA info = {};
    if (GetFileAttributesExA(out.c_str(), GetFileExInfoStandard, &info)) {
        char text[32] = "";
        DWORD read = 0;
        HANDLE m = CreateFileA(marker.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (m != INVALID_HANDLE_VALUE) {
            ReadFile(m, text, sizeof(text) - 1, &read, nullptr);
            CloseHandle(m);
            const uint64_t cached = uint64_t(info.nFileSizeHigh) << 32 | info.nFileSizeLow;
            // still the archive's version? (a game repacked under the same name must not keep playing the old copy)
            SFileImpl* file = nullptr;
            uint64_t archivedSize = cached;
            if (SFileOpenFile(archived.c_str(), &file) && file) {
                uint32_t high = 0;
                const uint32_t low = SFileGetFileSize(file, &high);
                archivedSize = uint64_t(high) << 32 | low;
                SFileCloseFile(file);
            }
            if (std::strtoull(text, nullptr, 10) == cached && archivedSize == cached) return true;
        }
    }
    // streamed in chunks: a whole-file load of a several-hundred-MB game file can fail the allocation (fatal in Storm)
    SFileImpl* file = nullptr;
    if (!SFileOpenFile(archived.c_str(), &file) || !file) {
        sExtensionMgr->Log("Arcade: %s is not in the client's archives", archived.c_str());
        return false;
    }
    uint32_t high = 0;
    const uint32_t low = SFileGetFileSize(file, &high);
    const uint64_t size = uint64_t(high) << 32 | low;
    MakeDirectories(out);
    DeleteFileA(marker.c_str());
    HANDLE h = CreateFileA(out.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    bool ok = h != INVALID_HANDLE_VALUE;
    std::vector<uint8_t> chunk(ok ? 8u << 20 : 0);
    DWORD written = 0;
    for (uint64_t done = 0; ok && done < size;) {
        const uint32_t want = uint32_t(std::min<uint64_t>(chunk.size(), size - done));
        uint32_t got = 0;
        ok = SFileReadFile(file, chunk.data(), want, &got, nullptr, 0) && got == want &&
             WriteFile(h, chunk.data(), want, &written, nullptr) && written == want;
        done += want;
    }
    SFileCloseFile(file);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    if (!ok) {
        sExtensionMgr->Log("Arcade: copying %s to the cache failed", archived.c_str());
        DeleteFileA(out.c_str());
        return false;
    }
    HANDLE m = CreateFileA(marker.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    const std::string text = std::to_string(size);
    if (m != INVALID_HANDLE_VALUE) {
        WriteFile(m, text.data(), DWORD(text.size()), &written, nullptr);
        CloseHandle(m);
    }
    return true;
}

bool Boot() {
    const std::string base = ExeDir() + "Arcade\\";
    s_systemDir = base + "System";
    CreateDirectoryA((base).c_str(), nullptr);
    CreateDirectoryA(s_systemDir.c_str(), nullptr);
    if (!s_core.Load(base + "Cores\\" + s_session.core + "_libretro.dll")) {
        sExtensionMgr->Log("Arcade: cannot load core %s", s_session.core.c_str());
        return false;
    }
    s_format = RETRO_PIXEL_FORMAT_0RGB1555;
    s_core.setEnvironment(Environment);
    s_keyboardEvent = nullptr;  // a new core registers its own (in retro_init)
    s_coreShutdown = false;
    ReleaseKeyboard();
    s_core.init();
    s_core.setVideo(Video);
    s_core.setAudio(AudioSample);
    s_core.setAudioBatch(AudioBatch);
    s_core.setInputPoll(InputPoll);
    s_core.setInputState(InputState);

    retro_system_info system = {};
    s_core.getSystemInfo(&system);
    retro_game_info game = {};
    const std::vector<std::string> files = Split(s_session.rom, '|');
    const std::string file = files[0].substr(files[0].find_last_of('\\') + 1);
    std::string path;
    if (system.need_fullpath) {  // the core opens the files itself: the extract cache (companions beside the game)
        for (size_t i = files.size(); i-- > 0;) {
            std::string cached;
            if (!CacheFile(files[i], cached)) return false;
            if (i == 0) path = cached;
        }
        game.path = path.c_str();
    } else {
        void* data = nullptr;
        uint32_t size = 0;
        if (!SFile::LoadFile(files[0].c_str(), &data, &size, 0, nullptr) || !data) {
            sExtensionMgr->Log("Arcade: ROM %s is not in the client's archives", files[0].c_str());
            return false;
        }
        s_romData.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
        SFile::Unload(data);
        game.path = file.c_str();
        game.data = s_romData.data();
        game.size = s_romData.size();
    }
    if (!s_core.loadGame(&game)) {
        sExtensionMgr->Log("Arcade: core %s refused %s", s_session.core.c_str(), s_session.rom.c_str());
        return false;
    }
    s_gameLoaded = true;
    s_core.setControllerPort(0, RETRO_DEVICE_JOYPAD);
    if (s_core.Networked()) {  // ask for an address on the arcade network (NET_ADDR answers)
        std::vector<uint8_t> join(5, C_NET_JOIN);
        std::memcpy(&join[1], &s_session.id, 4);
        s_outbox.push_back(std::move(join));
    }

    retro_system_av_info av = {};
    s_core.getAvInfo(&av);
    s_fps = av.timing.fps > 1 ? av.timing.fps : 60.0;
    s_aspect = av.geometry.aspect_ratio > 0 ? av.geometry.aspect_ratio
                                            : float(av.geometry.base_width) / float(std::max(1u, av.geometry.base_height));
    s_width = std::min(av.geometry.base_width, kFrameMax);
    s_height = std::min(av.geometry.base_height, kFrameMax);

    std::vector<uint8_t> raw;
    if (Unpack(s_session.blob[KIND_SRAM], raw)) {
        void* sram = s_core.memoryData(RETRO_MEMORY_SAVE_RAM);
        const size_t sramSize = s_core.memorySize(RETRO_MEMORY_SAVE_RAM);
        if (sram && sramSize) std::memcpy(sram, raw.data(), std::min(sramSize, raw.size()));
    }
    if (Unpack(s_session.blob[KIND_STATE], raw) && !s_core.unserialize(raw.data(), raw.size()))
        sExtensionMgr->Log("Arcade: resume state for %s did not load; starting fresh", s_session.title.c_str());
    if (void* sram = s_core.memoryData(RETRO_MEMORY_SAVE_RAM))
        s_sramHash = Hash(sram, s_core.memorySize(RETRO_MEMORY_SAVE_RAM));
    s_session.blob[0].clear();
    s_session.blob[1].clear();

    // Audio stream at the core's own rate; about a quarter second of ring buffer.
    const int rate = int(av.timing.sample_rate + 0.5);
    {
        std::lock_guard<std::mutex> lock(s_audioLock);
        s_ring.assign(size_t(std::max(rate, 8000) / 4) * 2, 0);
        s_ringRead = s_ringWrite = s_ringFill = 0;
    }
    if (FMOD::System* fmod = SE2::GetSystem(); fmod && rate > 0) {
        FMOD_CREATESOUNDEXINFO info = {};
        info.cbsize = sizeof(info);
        info.numchannels = 2;
        info.defaultfrequency = rate;
        info.format = FMOD_SOUND_FORMAT_PCM16;
        info.decodebuffersize = 1024;
        info.length = uint32_t(rate) * 4 * 60;
        info.pcmreadcallback = AudioRead;
        const FMOD_MODE mode = FMOD_2D | FMOD_SOFTWARE | FMOD_OPENUSER | FMOD_CREATESTREAM | FMOD_LOOP_NORMAL;
        if (fmod->createSound(nullptr, mode, &info, &s_sound) == FMOD_OK
            && fmod->playSound(FMOD_CHANNEL_FREE, s_sound, false, &s_channel) == FMOD_OK)
            s_channel->setVolume(s_volume);
    }

    s_clock = 0;
    s_sramTimer = 0;
    s_viewFrame = 0;
    s_viewTimer = 0;
    EndViewer(s_session.cabinet);  // was watching someone on this cabinet
    s_paused = false;
    s_session.phase = Phase::Running;
    sExtensionMgr->Log("Arcade: %s on %s, %ux%u at %.2f fps, %d Hz", s_session.title.c_str(), s_session.core.c_str(),
        s_width, s_height, s_fps, rate);
    Signal("ARCADE_STARTED", s_session.title.c_str(), s_session.core.c_str());
    return true;
}

void TryBoot() {
    if (s_session.phase != Phase::Receiving) return;
    for (int k = 0; k < 2; ++k)
        if (s_session.received[k] < s_session.expected[k]) return;
    if (!Boot()) Shutdown(false, "error");
}

int32_t Receive(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    if (!message || message->m_read > message->m_size) return 0;
    const uint32_t size = message->m_size - message->m_read;
    if (!size || size > 64 * 1024 || !message->FetchRead(message->m_read, size)) return 0;
    std::vector<uint8_t> bytes(size);
    message->GetData(bytes.data(), size);
    size_t at = 1;
    auto u32 = [&](uint32_t& v) { if (bytes.size() - at < 4) return false; std::memcpy(&v, &bytes[at], 4); at += 4; return true; };
    auto str = [&](std::string& v) {
        const auto end = std::find(bytes.begin() + at, bytes.end(), uint8_t(0));
        if (end == bytes.end()) return false;
        v.assign(bytes.begin() + at, end);
        at = size_t(end - bytes.begin()) + 1;
        return true;
    };
    uint32_t session = 0;
    if (!u32(session)) return 0;
    switch (bytes[0]) {
        case S_START: {
            Session next;
            next.id = session;
            uint32_t sram = 0, state = 0;
            if (!str(next.core) || !str(next.rom) || !str(next.title) || !u32(sram) || !u32(state)) return 0;
            if (bytes.size() - at >= 8) std::memcpy(&next.cabinet, &bytes[at], 8);
            if (!SafeName(next.core) || next.rom.find("..") != std::string::npos || sram > kMaxBlob || state > kMaxBlob) return 0;
            for (const std::string& part : Split(next.rom, '|'))
                if (part.rfind("Arcade\\Roms\\", 0) != 0 || part.size() <= 12) return 0;
            if (s_session.phase != Phase::Idle) Shutdown(true, "replaced");
            next.phase = Phase::Receiving;
            next.expected[KIND_SRAM] = sram;
            next.expected[KIND_STATE] = state;
            next.blob[KIND_SRAM].resize(sram);
            next.blob[KIND_STATE].resize(state);
            s_session = std::move(next);
            Signal("ARCADE_LOADING", s_session.title.c_str());
            TryBoot();
            return 1;
        }
        case S_DATA: {
            if (session != s_session.id || s_session.phase != Phase::Receiving || bytes.size() - at < 9) return 0;
            const uint8_t kind = bytes[at++];
            uint32_t total = 0, offset = 0;
            u32(total);
            u32(offset);
            const size_t n = bytes.size() - at;
            if (kind > 1 || total != s_session.expected[kind] || offset > total || n > total - offset) return 0;
            std::memcpy(s_session.blob[kind].data() + offset, &bytes[at], n);
            s_session.received[kind] += uint32_t(n);
            TryBoot();
            return 1;
        }
        case S_STOP: {
            std::string reason;
            str(reason);
            if (session == s_session.id) Shutdown(true, reason.c_str());
            return 1;
        }
        case S_VIEW: {
            uint64_t cabinet = 0;
            uint32_t frame = 0, total = 0, offset = 0;
            if (bytes.size() - at < 8 + 4 + 1 + 8) return 0;
            std::memcpy(&cabinet, &bytes[at], 8);
            at += 8;
            u32(frame);
            const uint8_t flags = bytes[at++];
            u32(total);
            u32(offset);
            ReceiveView(cabinet, frame, flags, total, offset, bytes.data() + at, bytes.size() - at);
            return 1;
        }
        case S_NET_ADDR: {
            uint32_t address = 0;
            if (session != s_session.id || !s_core.Networked() || !u32(address)) return 0;
            s_core.netAddress(address);
            sExtensionMgr->Log("Arcade: on the arcade network as %u.%u.%u.%u", address >> 24, address >> 16 & 255,
                address >> 8 & 255, address & 255);
            return 1;
        }
        case S_NET: {
            uint32_t from = 0;
            if (session != s_session.id || !s_core.Networked() || !u32(from) || bytes.size() - at < 4) return 0;
            uint16_t srcPort, dstPort;
            std::memcpy(&srcPort, &bytes[at], 2);
            std::memcpy(&dstPort, &bytes[at + 2], 2);
            at += 4;
            const size_t n = bytes.size() - at;
            if (n > kNetMax) return 0;
            s_core.netGive(bytes.data() + at, unsigned(n), from, srcPort, dstPort);
            return 1;
        }
        case S_VIEW_END: {
            uint64_t cabinet = 0;
            if (bytes.size() - at < 8) return 0;
            std::memcpy(&cabinet, &bytes[at], 8);
            EndViewer(cabinet);
            return 1;
        }
    }
    return 0;
}

// ---- Lua ----------------------------------------------------------------------------------------------------
// Arcade_SetScreen(texture): shows the game picture on that Texture region.
int SetScreen(lua_State* L) {
    auto* region = static_cast<CSimpleTexture*>(FrameScript_GetObjectThis(L, CSimpleTexture::GetObjectType()));
    if (!region) return 0;
    if (!s_texture)
        s_texture = TextureCreate(kTexW, kTexH, GxTex_Argb8888, GxTex_Argb8888,
            CGxTexFlags(s_opt.smooth ? GxTex_Linear : GxTex_Nearest, 0, 0, 0, 0, 0, 1), nullptr, TextureUpdate, "ArcadeScreen", 0);
    s_dirty = true;
    if (s_texture) region->SetTexture(s_texture);
    return 0;
}

// Arcade_GetState() -> phase ("idle"|"loading"|"running"), title, width, height, uMax, vMax, aspect, paused, core
// (uMax/vMax: the texture coordinates of the picture's bottom-right corner)
int GetState(lua_State* L) {
    static const char* names[] = {"idle", "loading", "running"};
    lua_pushstring(L, names[int(s_session.phase)]);
    lua_pushstring(L, s_session.title.c_str());
    lua_pushnumber(L, s_width);
    lua_pushnumber(L, s_height);
    lua_pushnumber(L, double(s_width) / kTexW);
    lua_pushnumber(L, double(s_outHeight) / kTexH);
    lua_pushnumber(L, s_aspect);
    lua_pushboolean(L, s_paused);
    lua_pushstring(L, s_session.core.c_str());
    return 9;
}

// Arcade_SetOption(name, value): smooth, scanlines, brightness, saturation, palette, cabinet, fastSpeed.
// Changing smooth drops the window texture; the add-on then calls Arcade_SetScreen again.
int SetOption(lua_State* L) {
    const std::string name = luaL_checkstring(L, 1);
    const double v = lua_isboolean(L, 2) ? (lua_toboolean(L, 2) ? 1.0 : 0.0) : luaL_checknumber(L, 2);
    if (name == "smooth") {
        const bool smooth = v != 0;
        if (smooth != s_opt.smooth && s_texture) {
            HandleClose(reinterpret_cast<HOBJECT>(s_texture));
            s_texture = nullptr;
        }
        s_opt.smooth = smooth;
    } else if (name == "scanlines") s_opt.scanlines = float(std::clamp(v, 0.0, 0.9));
    else if (name == "brightness") s_opt.brightness = float(std::clamp(v, 0.3, 2.0));
    else if (name == "saturation") s_opt.saturation = float(std::clamp(v, 0.0, 2.0));
    else if (name == "palette") s_opt.palette = std::clamp(int(v), 0, 5);
    else if (name == "fastSpeed") s_opt.fastSpeed = std::clamp(int(v), 2, 8);
    else if (name == "cabinet") {
        s_opt.cabinet = v != 0;
        if (!s_opt.cabinet) RestoreCabinet(s_session.cabinet);
    } else return luaL_error(L, "unknown arcade option %s", name.c_str());
    s_dirty = true;
    return 0;
}

int SetFastForward(lua_State* L) { s_fast = lua_toboolean(L, 1) != 0; return 0; }

// Arcade_SetButton(name, down): UP DOWN LEFT RIGHT A B X Y L R START SELECT L2 R2 L3 R3, or a stick direction
// (LSTICK_UP ... RSTICK_RIGHT).
int SetButton(lua_State* L) {
    static const struct { const char* name; unsigned id; } map[] = {
        {"UP", RETRO_DEVICE_ID_JOYPAD_UP}, {"DOWN", RETRO_DEVICE_ID_JOYPAD_DOWN}, {"LEFT", RETRO_DEVICE_ID_JOYPAD_LEFT},
        {"RIGHT", RETRO_DEVICE_ID_JOYPAD_RIGHT}, {"A", RETRO_DEVICE_ID_JOYPAD_A}, {"B", RETRO_DEVICE_ID_JOYPAD_B},
        {"X", RETRO_DEVICE_ID_JOYPAD_X}, {"Y", RETRO_DEVICE_ID_JOYPAD_Y}, {"L", RETRO_DEVICE_ID_JOYPAD_L},
        {"R", RETRO_DEVICE_ID_JOYPAD_R}, {"START", RETRO_DEVICE_ID_JOYPAD_START}, {"SELECT", RETRO_DEVICE_ID_JOYPAD_SELECT},
        {"L2", RETRO_DEVICE_ID_JOYPAD_L2}, {"R2", RETRO_DEVICE_ID_JOYPAD_R2}, {"L3", RETRO_DEVICE_ID_JOYPAD_L3},
        {"R3", RETRO_DEVICE_ID_JOYPAD_R3},
        {"LSTICK_UP", KEY_LSTICK}, {"LSTICK_DOWN", KEY_LSTICK + 1}, {"LSTICK_LEFT", KEY_LSTICK + 2}, {"LSTICK_RIGHT", KEY_LSTICK + 3},
        {"RSTICK_UP", KEY_RSTICK}, {"RSTICK_DOWN", KEY_RSTICK + 1}, {"RSTICK_LEFT", KEY_RSTICK + 2}, {"RSTICK_RIGHT", KEY_RSTICK + 3}};
    const char* name = luaL_checkstring(L, 1);
    const bool down = lua_toboolean(L, 2) != 0;
    for (const auto& entry : map)
        if (!std::strcmp(entry.name, name)) s_keys = down ? (s_keys | (1u << entry.id)) : (s_keys & ~(1u << entry.id));
    return 0;
}

void ReleaseKeyboard() {
    for (unsigned k = 0; k < RETROK_LAST; k++)
        if (s_keyDown[k]) {
            s_keyDown[k] = false;
            if (s_keyboardEvent) s_keyboardEvent(false, k, 0, 0);
        }
    s_mouseButtons = 0;
}

int ReleaseButtons(lua_State*) {
    s_keys = 0;
    ReleaseKeyboard();
    return 0;
}

// Arcade_SetKey(wowKeyName, down): a key straight to a computer core (letters, digits, F-keys, arrows, modifiers...).
int SetKey(lua_State* L) {
    static const struct { const char* name; unsigned key; } named[] = {
        {"ENTER", RETROK_RETURN}, {"ESCAPE", RETROK_ESCAPE}, {"SPACE", RETROK_SPACE}, {"BACKSPACE", RETROK_BACKSPACE},
        {"TAB", RETROK_TAB}, {"UP", RETROK_UP}, {"DOWN", RETROK_DOWN}, {"LEFT", RETROK_LEFT}, {"RIGHT", RETROK_RIGHT},
        {"LSHIFT", RETROK_LSHIFT}, {"RSHIFT", RETROK_RSHIFT}, {"LCTRL", RETROK_LCTRL}, {"RCTRL", RETROK_RCTRL},
        {"LALT", RETROK_LALT}, {"RALT", RETROK_RALT}, {"INSERT", RETROK_INSERT}, {"DELETE", RETROK_DELETE},
        {"HOME", RETROK_HOME}, {"END", RETROK_END}, {"PAGEUP", RETROK_PAGEUP}, {"PAGEDOWN", RETROK_PAGEDOWN},
        {"CAPSLOCK", RETROK_CAPSLOCK}, {"NUMLOCK", RETROK_NUMLOCK}, {"PRINTSCREEN", RETROK_PRINT},
        {"NUMPADDIVIDE", RETROK_KP_DIVIDE}, {"NUMPADMULTIPLY", RETROK_KP_MULTIPLY}, {"NUMPADMINUS", RETROK_KP_MINUS},
        {"NUMPADPLUS", RETROK_KP_PLUS}, {"NUMPADDECIMAL", RETROK_KP_PERIOD}};
    const char* name = luaL_checkstring(L, 1);
    const bool down = lua_toboolean(L, 2) != 0;
    unsigned key = RETROK_UNKNOWN;
    for (const auto& n : named)
        if (!std::strcmp(n.name, name)) key = n.key;
    const size_t len = std::strlen(name);
    if (!key && len == 1) {  // letters, digits and punctuation are their own (lower-case) character codes
        const char ch = char(std::tolower(static_cast<unsigned char>(name[0])));
        if (ch > 32 && ch < 127) key = unsigned(ch);
    } else if (!key && name[0] == 'F' && len <= 3 && std::isdigit(static_cast<unsigned char>(name[1]))) {
        const int n = std::atoi(name + 1);
        if (n >= 1 && n <= 15) key = RETROK_F1 + unsigned(n - 1);
    } else if (!key && !std::strncmp(name, "NUMPAD", 6) && len == 7 && std::isdigit(static_cast<unsigned char>(name[6]))) {
        key = RETROK_KP0 + unsigned(name[6] - '0');
    }
    if (!key || key >= RETROK_LAST || s_keyDown[key] == down) return 0;
    s_keyDown[key] = down;
    if (s_keyboardEvent) s_keyboardEvent(down, key, down && key < 128 ? key : 0, 0);
    return 0;
}

// Arcade_SetPointer(u, v, inside): the cursor over the picture, 0..1 from its top-left.
int SetPointer(lua_State* L) {
    // never exactly 0: DOSBox Pure ignores a pointer at (0, 0) with no button down
    const auto range = [](double t) { const int16_t v = int16_t(std::clamp(t * 2.0 - 1.0, -1.0, 1.0) * 0x7FFF); return v ? v : int16_t(1); };
    s_pointerX = range(luaL_checknumber(L, 1));
    s_pointerY = range(luaL_checknumber(L, 2));
    s_pointerIn = lua_toboolean(L, 3) != 0;
    return 0;
}

// Arcade_HideCursor(hide): the player's cursor off while it is over a game that draws its own (a DOS game's mouse).
// The client shows it again whenever it changes its cursor image, so the add-on calls this every frame it applies.
int HideCursor(lua_State* L) {
    if (g_theGxDevicePtr) g_theGxDevicePtr->CursorSetVisible(lua_toboolean(L, 1) ? 0 : 1);
    return 0;
}

// Arcade_SetMouseButton("LeftButton"|"RightButton"|"MiddleButton", down)
int SetMouseButton(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const uint8_t bit = !std::strcmp(name, "LeftButton") ? 1 : !std::strcmp(name, "RightButton") ? 2
        : !std::strcmp(name, "MiddleButton") ? 4 : 0;
    s_mouseButtons = lua_toboolean(L, 2) ? (s_mouseButtons | bit) : (s_mouseButtons & ~bit);
    return 0;
}
int Quit(lua_State*) { Shutdown(true, "quit"); return 0; }
int SetPaused(lua_State* L) {
    s_paused = lua_toboolean(L, 1) != 0;
    if (s_channel) s_channel->setPaused(s_paused);
    return 0;
}
int SetVolume(lua_State* L) {
    s_volume = float(std::clamp(luaL_checknumber(L, 1), 0.0, 1.0));
    if (s_channel) s_channel->setVolume(s_volume);
    return 0;
}

class Frontend : public ClientScript {
public:
    Frontend() : ClientScript("ArcadeFrontend") {}

    void OnUpdate(float elapsed) override {
        FlushOutbox(kOutboxPerFrame);
        SendNet();
        UpdateViewers(elapsed);
        if (s_session.phase == Phase::Running && s_session.cabinet && (s_viewTimer += elapsed) >= kViewInterval) {
            s_viewTimer = 0;
            SendView();
        }
        if (s_session.phase != Phase::Running || s_paused) return;
        s_clock += std::min(elapsed, 0.25f);
        const double frame = 1.0 / s_fps;
        // Audio decides the fine pace: a full ring skips a frame, a starving one runs an extra frame.
        size_t fill, capacity;
        {
            std::lock_guard<std::mutex> lock(s_audioLock);
            fill = s_ringFill;
            capacity = s_ring.size() / 2;
        }
        if (capacity && fill > capacity * 3 / 4) s_clock -= frame;
        if (capacity && fill < capacity / 8) s_clock += frame;
        const int speed = s_fast ? s_opt.fastSpeed : 1;
        for (int n = 0; s_clock >= frame && n < 4; ++n) {
            for (int k = 0; k < speed && !s_coreShutdown; ++k) s_core.run();
            s_clock -= frame;
        }
        if (s_coreShutdown) {
            s_coreShutdown = false;
            Shutdown(true, "game exited");
            return;
        }
        s_clock = std::min(s_clock, frame);
        ApplyCabinet();
        UploadFrame();
        if ((s_sramTimer += elapsed) >= 30.0f) {
            s_sramTimer = 0;
            SendSram(true);
        }
    }

    void OnLeaveWorld() override {
        Shutdown(true, "left world");
        FlushOutbox(SIZE_MAX);  // the connection may be gone next frame
        s_cabinetModel = nullptr;
        for (auto& [cabinet, v] : s_viewers)
            if (v->texture) HandleClose(reinterpret_cast<HOBJECT>(v->texture));
        s_viewers.clear();
    }
    void OnShutdown() override {
        Shutdown(true, "shutdown");
        FlushOutbox(SIZE_MAX);
    }
};
}

namespace {
// Every cabinet model shows the attract screen until someone plays on it.
class CabinetScreens : public ModelScript {
public:
    CabinetScreens() : ModelScript("ArcadeCabinetScreens") {}
    void OnModelLoaded(CM2Model& model) override {
        if (!model.m_shared) return;
        std::string name = model.m_shared->m_fileName;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
        const size_t n = std::strlen(kCabinetModel);
        if (name.size() < n || name.compare(name.size() - n, n, kCabinetModel)) return;
        if (CTexture* attract = Attract()) model.ReplaceTexture(kScreenType, attract);
    }
};
}

void AddArcadeExtension() {
    sExtensionMgr->RegisterMessageHandler(SMSG_ARCADE, Receive, nullptr);
    sExtensionMgr->RegisterFrameScriptEvent("ARCADE_LOADING", FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("ARCADE_STARTED", FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("ARCADE_STOPPED", FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("Arcade_SetScreen", SetScreen, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("Arcade_GetState", GetState, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("Arcade_SetButton", SetButton, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("Arcade_ReleaseButtons", ReleaseButtons, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("Arcade_Quit", Quit, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("Arcade_SetPaused", SetPaused, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("Arcade_SetVolume", SetVolume, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("Arcade_SetOption", SetOption, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("Arcade_SetFastForward", SetFastForward, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("Arcade_SetKey", SetKey, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("Arcade_SetPointer", SetPointer, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("Arcade_SetMouseButton", SetMouseButton, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("Arcade_HideCursor", HideCursor, FRAMESCRIPT_CONTEXTS_GAME);
    new Frontend();
    new CabinetScreens();
}
