// starcraft_libretro.dll: StarCraft: Brood War 1.16.1 as an Esteria arcade "core" (64-bit, loaded by
// Extensions/Arcade like any libretro core).
//
// There is no emulation: the core starts the real StarCraft.exe (32-bit) from the game's files, which the frontend has
// cached next to each other, on a Windows desktop of its own that nobody looks at - no window, no taskbar button,
// nothing on screen. The ddraw.dll bridge beside the exe (ddraw_bridge.cpp) hands every frame to this core through a
// shared page and plays the cabinet's mouse and keys into the game. Sound plays straight from the game
// (the bridge keeps it audible without focus). The game lives in a job object, so it ends with the cabinet
// session, and with the client if the client dies.
//
// Game files: the frontend passes StarCraft.exe's cached path (the first file of the cabinet's ROM list); the custom
// maps come as maps.zip (stored, not deflated) and are unpacked into maps\ once.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../../../Extensions/Arcade/src/libretro.h"
#include "ScShared.h"

namespace {

retro_environment_t s_env;
retro_video_refresh_t s_video;
retro_audio_sample_batch_t s_audioBatch;
retro_input_poll_t s_poll;
retro_input_state_t s_input;
retro_log_printf_t s_log;

HANDLE s_map, s_job, s_process;
HDESK s_desktop;
// WoW's window when the game started: the game is launched by the foreground process, so it may take the foreground
// (onto its hidden desktop) while its window opens, before the bridge's hooks exist, leaving WoW without keyboard focus.
HWND s_owner, s_lastForeground;
DWORD s_startTick, s_pid;
uint32_t s_netAddr;  // esteria_net_address, kept for a game started after it is set
sc::Shared* s_sh;
std::vector<uint32_t> s_frame(sc::kMaxW * sc::kMaxH);
uint32_t s_seenFrame = ~0u;
unsigned s_width = 640, s_height = 480;
int16_t s_silence[2 * 735];

void Log(retro_log_level level, const char* text) {
    if (s_log) s_log(level, "[starcraft] %s\n", text);
}

// ---- maps.zip (stored entries only) ------------------------------------------------------------------------------
bool UnpackStoredZip(const std::string& zip, const std::string& into) {
    HANDLE f = CreateFileA(zip.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    const DWORD size = GetFileSize(f, nullptr);
    std::vector<uint8_t> data(size);
    DWORD got = 0;
    const bool read = ReadFile(f, data.data(), size, &got, nullptr) && got == size;
    CloseHandle(f);
    if (!read || size < 22) return false;
    size_t eocd = size - 22;
    while (eocd > 0 && std::memcmp(&data[eocd], "PK\5\6", 4)) eocd--;
    if (std::memcmp(&data[eocd], "PK\5\6", 4)) return false;
    uint16_t entries;
    uint32_t dirOffset;
    std::memcpy(&entries, &data[eocd + 10], 2);
    std::memcpy(&dirOffset, &data[eocd + 16], 4);
    size_t at = dirOffset;
    for (uint16_t i = 0; i < entries && at + 46 <= size; i++) {
        uint16_t method, nameLen, extraLen, commentLen;
        uint32_t compressed, local;
        std::memcpy(&method, &data[at + 10], 2);
        std::memcpy(&compressed, &data[at + 20], 4);
        std::memcpy(&nameLen, &data[at + 28], 2);
        std::memcpy(&extraLen, &data[at + 30], 2);
        std::memcpy(&commentLen, &data[at + 32], 2);
        std::memcpy(&local, &data[at + 42], 4);
        std::string name(reinterpret_cast<const char*>(&data[at + 46]), nameLen);
        at += 46 + nameLen + extraLen + commentLen;
        if (method != 0 || name.empty() || name.back() == '/' || name.find("..") != std::string::npos) continue;
        uint16_t localName, localExtra;
        std::memcpy(&localName, &data[local + 26], 2);
        std::memcpy(&localExtra, &data[local + 28], 2);
        const size_t start = local + 30 + localName + localExtra;
        if (start + compressed > size) return false;
        for (char& c : name) if (c == '/') c = '\\';
        const std::string out = into + name;
        for (size_t s = out.find('\\', into.size()); s != std::string::npos; s = out.find('\\', s + 1))
            CreateDirectoryA(out.substr(0, s).c_str(), nullptr);
        HANDLE o = CreateFileA(out.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (o == INVALID_HANDLE_VALUE) return false;
        DWORD wrote = 0;
        WriteFile(o, &data[start], compressed, &wrote, nullptr);
        CloseHandle(o);
    }
    return true;
}

// ---- keyboard: RETROK -> virtual key ------------------------------------------------------------------------------
UINT VirtualKey(unsigned k) {
    if (k >= 'a' && k <= 'z') return k - 'a' + 'A';
    if (k >= '0' && k <= '9') return k;
    if (k >= RETROK_F1 && k <= RETROK_F15) return VK_F1 + (k - RETROK_F1);
    if (k >= RETROK_KP0 && k <= RETROK_KP9) return VK_NUMPAD0 + (k - RETROK_KP0);
    switch (k) {
        case RETROK_RETURN: return VK_RETURN;       case RETROK_ESCAPE: return VK_ESCAPE;
        case RETROK_SPACE: return VK_SPACE;         case RETROK_BACKSPACE: return VK_BACK;
        case RETROK_TAB: return VK_TAB;             case RETROK_UP: return VK_UP;
        case RETROK_DOWN: return VK_DOWN;           case RETROK_LEFT: return VK_LEFT;
        case RETROK_RIGHT: return VK_RIGHT;         case RETROK_LSHIFT: return VK_LSHIFT;
        case RETROK_RSHIFT: return VK_RSHIFT;       case RETROK_LCTRL: return VK_LCONTROL;
        case RETROK_RCTRL: return VK_RCONTROL;      case RETROK_LALT: return VK_LMENU;
        case RETROK_RALT: return VK_RMENU;          case RETROK_INSERT: return VK_INSERT;
        case RETROK_DELETE: return VK_DELETE;       case RETROK_HOME: return VK_HOME;
        case RETROK_END: return VK_END;             case RETROK_PAGEUP: return VK_PRIOR;
        case RETROK_PAGEDOWN: return VK_NEXT;       case RETROK_CAPSLOCK: return VK_CAPITAL;
        case RETROK_MINUS: return VK_OEM_MINUS;     case RETROK_EQUALS: return VK_OEM_PLUS;
        case RETROK_LEFTBRACKET: return VK_OEM_4;   case RETROK_RIGHTBRACKET: return VK_OEM_6;
        case RETROK_SEMICOLON: return VK_OEM_1;     case RETROK_QUOTE: return VK_OEM_7;
        case RETROK_COMMA: return VK_OEM_COMMA;     case RETROK_PERIOD: return VK_OEM_PERIOD;
        case RETROK_SLASH: return VK_OEM_2;         case RETROK_BACKSLASH: return VK_OEM_5;
        case RETROK_BACKQUOTE: return VK_OEM_3;     case RETROK_KP_PLUS: return VK_ADD;
        case RETROK_KP_MINUS: return VK_SUBTRACT;   case RETROK_KP_ENTER: return VK_RETURN;
        case RETROK_PAUSE: return VK_PAUSE;
        default: return 0;
    }
}

void RETRO_CALLCONV KeyEvent(bool down, unsigned keycode, uint32_t, uint16_t) {
    const UINT vk = VirtualKey(keycode);
    if (!s_sh || !vk) return;
    const uint32_t at = s_sh->keyWrite;
    if (at - s_sh->keyRead >= sc::kKeyRing) return;  // the bridge is behind; drop rather than overwrite
    s_sh->keys[at % sc::kKeyRing] = vk | (down ? 1u << 16 : 0);
    s_sh->keyWrite = at + 1;
}

// StarCraft refuses to start while its single-instance mutex exists. stage.py renamed it in the shipped exe (so a
// cabinet runs beside the player's own StarCraft); this makes it unique per launch too, so two cabinets on one PC
// (two clients) both run. The exe is not relocatable, so the string sits at ImageBase + its RVA; the process is
// still suspended here.
void UniqueInstance(HANDLE process, const std::string& exe) {
    static const char kMutex[] = "EsteriaSC Check For Other Instances";
    HANDLE f = CreateFileA(exe.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    std::vector<uint8_t> d(GetFileSize(f, nullptr));
    DWORD got = 0;
    const bool read = ReadFile(f, d.data(), DWORD(d.size()), &got, nullptr) && got == d.size();
    CloseHandle(f);
    if (!read || d.size() < 0x400) return;
    auto u16 = [&](size_t at) { uint16_t v; std::memcpy(&v, &d[at], 2); return v; };
    auto u32 = [&](size_t at) { uint32_t v; std::memcpy(&v, &d[at], 4); return v; };
    const size_t pe = u32(0x3C), opt = pe + 24;
    if (pe + 0x200 > d.size() || (u16(opt + 70) & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE)) return;
    const auto hit = std::search(d.begin(), d.end(), kMutex, kMutex + sizeof kMutex - 1);
    if (hit == d.end()) { Log(RETRO_LOG_WARN, "instance mutex not found; a second cabinet on this PC will not start"); return; }
    const size_t off = size_t(hit - d.begin());
    for (unsigned i = 0, n = u16(pe + 6); i < n; i++) {
        const size_t sec = opt + u16(pe + 20) + i * 40;
        const uint32_t va = u32(sec + 12), raw = u32(sec + 20), rawSize = u32(sec + 16);
        if (off < raw || off >= raw + rawSize) continue;
        char id[9];
        wsprintfA(id, "%08lX", GetCurrentProcessId());  // "EsteriaSC <pid>..." (same length, same NUL)
        auto* at = reinterpret_cast<void*>(uintptr_t(u32(opt + 28)) + va + (off - raw) + 10);
        DWORD old = 0;
        if (VirtualProtectEx(process, at, 8, PAGE_READWRITE, &old)) {
            WriteProcessMemory(process, at, id, 8, nullptr);
            VirtualProtectEx(process, at, 8, old, &old);
        }
        return;
    }
}

void Stop() {
    if (s_owner) { LockSetForegroundWindow(LSFW_UNLOCK); s_owner = nullptr; }
    if (s_sh) s_sh->quit = 1;
    if (s_process) { TerminateProcess(s_process, 0); CloseHandle(s_process); s_process = nullptr; }
    if (s_job) { CloseHandle(s_job); s_job = nullptr; }  // kills anything left in the job
    if (s_sh) { UnmapViewOfFile(s_sh); s_sh = nullptr; }
    if (s_map) { CloseHandle(s_map); s_map = nullptr; }
    if (s_desktop) { CloseDesktop(s_desktop); s_desktop = nullptr; }
}

}  // namespace

extern "C" {

RETRO_API void retro_set_environment(retro_environment_t cb) {
    s_env = cb;
    retro_log_callback log = {};
    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log)) s_log = log.log;
}
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { s_video = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t) {}
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { s_audioBatch = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { s_poll = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { s_input = cb; }
RETRO_API void retro_init(void) {
    static const retro_keyboard_callback keyboard = { KeyEvent };
    if (s_env) s_env(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, const_cast<retro_keyboard_callback*>(&keyboard));
}
RETRO_API void retro_deinit(void) { Stop(); }
RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }
RETRO_API void retro_get_system_info(retro_system_info* info) {
    std::memset(info, 0, sizeof *info);
    info->library_name = "StarCraft";
    info->library_version = "1.16.1 bridge";
    info->valid_extensions = "exe";
    info->need_fullpath = true;
}
RETRO_API void retro_get_system_av_info(retro_system_av_info* info) {
    std::memset(info, 0, sizeof *info);
    info->geometry = { 640, 480, sc::kMaxW, sc::kMaxH, 4.0f / 3.0f };
    info->timing = { 60.0, 44100.0 };
}
RETRO_API void retro_set_controller_port_device(unsigned, unsigned) {}
RETRO_API void retro_reset(void) {}

RETRO_API bool retro_load_game(const retro_game_info* game) {
    if (!game || !game->path) return false;
    retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
    if (!s_env(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format)) return false;
    const std::string exe = game->path;
    const std::string dir = exe.substr(0, exe.find_last_of("\\/") + 1);
    if (GetFileAttributesA((dir + "maps").c_str()) == INVALID_FILE_ATTRIBUTES) {
        CreateDirectoryA((dir + "maps").c_str(), nullptr);
        if (!UnpackStoredZip(dir + "maps.zip", dir + "maps\\")) Log(RETRO_LOG_WARN, "maps.zip not unpacked");
    }

    char name[64];
    wsprintfA(name, "Local\\EsteriaArcadeStarCraft%lu", GetCurrentProcessId());
    s_map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(sc::Shared), name);
    s_sh = s_map ? static_cast<sc::Shared*>(MapViewOfFile(s_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(sc::Shared))) : nullptr;
    if (!s_sh) { Stop(); return false; }
    std::memset(s_sh, 0, sizeof(sc::Shared));
    s_sh->magic = sc::kMagic;
    s_sh->mouseX = 320;
    s_sh->mouseY = 240;
    s_sh->netAddr = s_netAddr;

    // A desktop of its own: whatever windows the game opens are never on the player's screen.
    s_desktop = CreateDesktopA("EsteriaArcadeStarCraft", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!s_desktop) { Log(RETRO_LOG_ERROR, "no desktop"); Stop(); return false; }
    s_job = CreateJobObjectA(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (s_job) SetInformationJobObject(s_job, JobObjectExtendedLimitInformation, &limits, sizeof limits);

    SetEnvironmentVariableA("ESTERIA_SC_SHM", name);
    const std::string log = dir + "esteria_bridge.log";
    SetEnvironmentVariableA("ESTERIA_SC_LOG", log.c_str());
    STARTUPINFOA si = {};
    si.cb = sizeof si;
    si.lpDesktop = const_cast<char*>("WinSta0\\EsteriaArcadeStarCraft");
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNOACTIVATE;
    s_owner = GetForegroundWindow();
    s_lastForeground = s_owner;
    if (s_owner) LockSetForegroundWindow(LSFW_LOCK);  // only the foreground process (WoW) may lock; unlocked in Stop
    PROCESS_INFORMATION pi = {};
    std::string cmd = "\"" + exe + "\"";
    const BOOL started = CreateProcessA(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
        dir.c_str(), &si, &pi);
    SetEnvironmentVariableA("ESTERIA_SC_SHM", nullptr);
    SetEnvironmentVariableA("ESTERIA_SC_LOG", nullptr);
    if (!started) { Log(RETRO_LOG_ERROR, "StarCraft.exe did not start"); Stop(); return false; }
    if (s_job) AssignProcessToJobObject(s_job, pi.hProcess);
    UniqueInstance(pi.hProcess, exe);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    s_process = pi.hProcess;
    s_pid = pi.dwProcessId;
    s_startTick = GetTickCount();
    return true;
}

RETRO_API bool retro_load_game_special(unsigned, const retro_game_info*, size_t) { return false; }
RETRO_API void retro_unload_game(void) { Stop(); }
RETRO_API unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

RETRO_API void retro_run(void) {
    if (s_process && WaitForSingleObject(s_process, 0) == WAIT_OBJECT_0) {  // the player quit the game from its menu
        static HANDLE logged;  // once per process: the exit code tells a quit from a crash
        if (logged != s_process) {
            logged = s_process;
            DWORD code = 0;
            GetExitCodeProcess(s_process, &code);
            char text[64];
            wsprintfA(text, "game exited with code 0x%08lX", code);
            Log(RETRO_LOG_INFO, text);
        }
        if (s_env) s_env(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
    }
    if (s_owner) {
        const HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        if (fg) GetWindowThreadProcessId(fg, &pid);
        if (fg != s_lastForeground) {
            char text[128];
            wsprintfA(text, "foreground changed to %p (pid %lu%s)", fg, pid, pid == s_pid ? ", StarCraft" : "");
            Log(RETRO_LOG_INFO, text);
            s_lastForeground = fg;
        }
        // the first seconds only: later, a foreground change is the player alt-tabbing
        if ((!fg || pid == s_pid) && GetTickCount() - s_startTick < 10000 && IsWindow(s_owner)) SetForegroundWindow(s_owner);
    }
    if (s_poll) s_poll();
    if (s_sh && s_input) {
        // the cabinet's pointer (-0x7FFF..0x7FFF across the picture) in game pixels; buttons from the mouse device
        if (s_input(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X) || s_input(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y)) {
            const int px = s_input(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
            const int py = s_input(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);
            const int w = int(s_sh->width ? s_sh->width : 640), h = int(s_sh->height ? s_sh->height : 480);
            s_sh->mouseX = min(w - 1, max(0, (px + 0x7FFF) * w / 0xFFFE));
            s_sh->mouseY = min(h - 1, max(0, (py + 0x7FFF) * h / 0xFFFE));
        }
        s_sh->buttons = (s_input(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT) ? 1u : 0u)
            | (s_input(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT) ? 2u : 0u)
            | (s_input(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE) ? 4u : 0u);
    }
    if (s_sh && s_sh->frame != s_seenFrame && s_sh->width && s_sh->height) {
        s_seenFrame = s_sh->frame;
        s_width = s_sh->width;
        s_height = s_sh->height;
        for (unsigned i = 0; i < s_width * s_height; i++) s_frame[i] = s_sh->palette[s_sh->pixels[i]];
    }
    if (s_video) s_video(s_frame.data(), s_width, s_height, s_width * sizeof(uint32_t));
    if (s_audioBatch) s_audioBatch(s_silence, 735);  // the game's own sound plays straight from it
}

// ---- the arcade network (not libretro: Extensions/Arcade finds these with GetProcAddress) -------------------------
// The game's UDP datagrams (net_bridge.cpp) leave and arrive here; the frontend carries them to and from the server,
// which relays them between cabinets. Addresses are virtual (10.77.x.y, host order), 0xFFFFFFFF = every cabinet.

// This cabinet's address (0 turns the network off: the game's sends go nowhere).
RETRO_API void esteria_net_address(uint32_t ip) {
    s_netAddr = ip;
    if (s_sh) s_sh->netAddr = ip;
}

// The game's next outgoing datagram: its length (0 = none), destination and ports.
RETRO_API unsigned esteria_net_take(void* data, unsigned cap, uint32_t* ip, uint16_t* srcPort, uint16_t* dstPort) {
    if (!s_sh) return 0;
    sc::NetRing& r = s_sh->out;
    while (r.read != r.write) {
        const sc::NetPacket& p = r.slot[r.read % sc::kNetSlots];
        MemoryBarrier();
        const unsigned len = p.len <= sc::kNetMax && p.len <= cap ? p.len : 0;
        if (len) {
            std::memcpy(data, p.data, len);
            *ip = p.ip;
            *srcPort = p.srcPort;
            *dstPort = p.dstPort;
        }
        r.read = r.read + 1;
        if (len) return len;  // an oversized one is dropped
    }
    return 0;
}

// A datagram from another cabinet (ip = its address).
RETRO_API void esteria_net_give(const void* data, unsigned len, uint32_t ip, uint16_t srcPort, uint16_t dstPort) {
    if (!s_sh || len > sc::kNetMax) return;
    sc::NetRing& r = s_sh->in;
    if (r.write - r.read >= sc::kNetSlots) return;  // the game is behind: drop, as a congested LAN would
    sc::NetPacket& p = r.slot[r.write % sc::kNetSlots];
    p.len = len;
    p.ip = ip;
    p.srcPort = srcPort;
    p.dstPort = dstPort;
    std::memcpy(p.data, data, len);
    MemoryBarrier();
    r.write = r.write + 1;
}

RETRO_API size_t retro_serialize_size(void) { return 0; }
RETRO_API bool retro_serialize(void*, size_t) { return false; }
RETRO_API bool retro_unserialize(const void*, size_t) { return false; }
RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned, bool, const char*) {}
RETRO_API void* retro_get_memory_data(unsigned) { return nullptr; }
RETRO_API size_t retro_get_memory_size(unsigned) { return 0; }

}  // extern "C"
