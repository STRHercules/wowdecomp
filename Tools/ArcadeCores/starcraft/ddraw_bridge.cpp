// ddraw.dll for StarCraft 1.16.1 inside an Esteria arcade cabinet (32-bit; sits next to StarCraft.exe).
//
// StarCraft (Storm's SDraw) and Smacker load ddraw.dll by name, so this file is the DirectDraw they get. It implements
// the 8-bit 640x480 subset they use entirely in memory, never touching a display: each presented frame and its palette
// go to the page shared with starcraft_libretro.dll (ScShared.h). The game runs on a desktop nobody looks at (the core
// creates it), so nothing of it is ever on screen; this DLL also
//   - answers GetCursorPos with the cabinet's mouse and makes SetCursorPos/ClipCursor harmless,
//   - reports the game's window as the active foreground one (it is never the user's),
//   - feeds the cabinet's mouse and keys to the window as the messages a real mouse and keyboard produce.
// Without ESTERIA_SC_SHM in the environment (StarCraft started the normal way) every call goes to the real ddraw.dll.
#define WIN32_LEAN_AND_MEAN
#define INITGUID
#include <windows.h>
#include <ddraw.h>
#include <mmreg.h>
#include <dsound.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "ScShared.h"

namespace net {  // net_bridge.cpp
void Install(sc::Shared*);
void Pump();
bool HotPatch(HMODULE module, const char* name, void* hook, void** original);
}

// ---- sound -------------------------------------------------------------------------------------------------------
// DirectSound mutes a buffer whose window is not the foreground one, and a cabinet game's window never is (it lives
// on a desktop nobody looks at). So the system dsound.dll's DirectSoundCreate(8) are hooked the moment it loads, and
// every secondary buffer created through the device they return is made DSBCAPS_GLOBALFOCUS. (A stand-in dsound.dll
// beside the game did the same for StarCraft but crashed Miles Sound System, Lord of the Clans' audio library.)
void BridgeLog(const char* fmt, ...);
namespace snd {
using CreateBufferFn = HRESULT(STDMETHODCALLTYPE*)(IDirectSound*, LPCDSBUFFERDESC, LPDIRECTSOUNDBUFFER*, LPUNKNOWN);
using CreateFn = HRESULT(WINAPI*)(LPCGUID, LPDIRECTSOUND*, LPUNKNOWN);
CreateBufferFn g_createBuffer, g_createBuffer8;
CreateFn o_create, o_create8;
bool g_hooked;

HRESULT Create(CreateBufferFn real, IDirectSound* self, LPCDSBUFFERDESC desc, LPDIRECTSOUNDBUFFER* out, LPUNKNOWN outer) {
    if (desc && !(desc->dwFlags & DSBCAPS_PRIMARYBUFFER)) {
        DSBUFFERDESC copy = {};
        memcpy(&copy, desc, desc->dwSize < sizeof copy ? desc->dwSize : sizeof copy);
        copy.dwFlags = (copy.dwFlags | DSBCAPS_GLOBALFOCUS) & ~DSBCAPS_STICKYFOCUS;
        return real(self, &copy, out, outer);
    }
    return real(self, desc, out, outer);
}
HRESULT STDMETHODCALLTYPE CreateBuffer(IDirectSound* s, LPCDSBUFFERDESC d, LPDIRECTSOUNDBUFFER* o, LPUNKNOWN u) { return Create(g_createBuffer, s, d, o, u); }
HRESULT STDMETHODCALLTYPE CreateBuffer8(IDirectSound* s, LPCDSBUFFERDESC d, LPDIRECTSOUNDBUFFER* o, LPUNKNOWN u) { return Create(g_createBuffer8, s, d, o, u); }

// CreateSoundBuffer is entry 3 of the device's table (after QueryInterface, AddRef, Release), in IDirectSound8 too.
// The table is shared by every device of its kind, so patching it once covers them all.
void Wrap(IDirectSound* ds, CreateBufferFn& real, CreateBufferFn wrapper) {
    void** table = *reinterpret_cast<void***>(ds);
    if (table[3] == reinterpret_cast<void*>(wrapper)) return;
    real = reinterpret_cast<CreateBufferFn>(table[3]);
    DWORD old;
    VirtualProtect(&table[3], sizeof(void*), PAGE_READWRITE, &old);
    table[3] = reinterpret_cast<void*>(wrapper);
    VirtualProtect(&table[3], sizeof(void*), old, &old);
    BridgeLog("sound: device buffers made audible without focus");
}
HRESULT WINAPI HookCreate(LPCGUID guid, LPDIRECTSOUND* out, LPUNKNOWN outer) {
    const HRESULT hr = o_create(guid, out, outer);
    if (SUCCEEDED(hr) && *out) Wrap(*out, g_createBuffer, CreateBuffer);
    return hr;
}
HRESULT WINAPI HookCreate8(LPCGUID guid, LPDIRECTSOUND* out, LPUNKNOWN outer) {
    const HRESULT hr = o_create8(guid, out, outer);
    if (SUCCEEDED(hr) && *out) Wrap(*out, g_createBuffer8, CreateBuffer8);
    return hr;
}
void Hook(HMODULE dsound) {  // the system dsound.dll (called under the loader lock: patching only, no loading)
    if (g_hooked || !dsound) return;
    g_hooked = true;
    net::HotPatch(dsound, "DirectSoundCreate", reinterpret_cast<void*>(HookCreate), reinterpret_cast<void**>(&o_create));
    net::HotPatch(dsound, "DirectSoundCreate8", reinterpret_cast<void*>(HookCreate8), reinterpret_cast<void**>(&o_create8));
    BridgeLog("sound: hooked the system dsound.dll");
}
bool IsSystemDsound(const wchar_t* path, size_t chars) {
    wchar_t sys[MAX_PATH];
    const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    return n && chars > n && !_wcsnicmp(path, sys, n) && !_wcsnicmp(path + n, L"\\dsound.dll", chars - n);
}
}  // namespace snd

namespace {

sc::Shared* g_sh;
HWND g_hwnd;
DWORD g_modeW = 640, g_modeH = 480;
FILE* g_log;

void Log(const char* fmt, ...) {
    if (!g_log) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    va_end(args);
    fputc('\n', g_log);
    fflush(g_log);
}

#define UNHANDLED(name) (Log("unhandled %s", name), DDERR_UNSUPPORTED)

// ---- palette / surfaces ----------------------------------------------------------------------------------------
struct Palette;
struct Surface;
Surface* g_primary;
void Present();

struct Palette : IDirectDrawPalette {
    ULONG ref = 1;
    PALETTEENTRY entries[256] = {};
    STDMETHOD(QueryInterface)(REFIID, LPVOID* out) { *out = nullptr; return E_NOINTERFACE; }
    STDMETHOD_(ULONG, AddRef)() { return ++ref; }
    STDMETHOD_(ULONG, Release)() { ULONG r = --ref; if (!r) delete this; return r; }
    STDMETHOD(GetCaps)(LPDWORD caps) { *caps = DDPCAPS_8BIT | DDPCAPS_ALLOW256; return DD_OK; }
    STDMETHOD(GetEntries)(DWORD, DWORD start, DWORD count, LPPALETTEENTRY out) {
        if (start + count > 256) return DDERR_INVALIDPARAMS;
        memcpy(out, entries + start, count * sizeof(PALETTEENTRY));
        return DD_OK;
    }
    STDMETHOD(Initialize)(LPDIRECTDRAW, DWORD, LPPALETTEENTRY) { return DDERR_ALREADYINITIALIZED; }
    STDMETHOD(SetEntries)(DWORD, DWORD start, DWORD count, LPPALETTEENTRY in);
};

struct Surface : IDirectDrawSurface {
    ULONG ref = 1;
    DWORD w = 0, h = 0, caps = 0;
    std::vector<uint8_t> px;
    Palette* palette = nullptr;
    Surface* back = nullptr;   // a flipping primary's back buffer
    bool keyed = false;
    DWORD colorKey = 0;

    Surface(DWORD width, DWORD height, DWORD surfaceCaps) : w(width), h(height), caps(surfaceCaps), px(size_t(width) * height) {}
    bool IsPrimary() const { return (caps & DDSCAPS_PRIMARYSURFACE) != 0; }

    STDMETHOD(QueryInterface)(REFIID iid, LPVOID* out) {
        if (iid == IID_IDirectDrawSurface || iid == IID_IUnknown) { *out = this; AddRef(); return S_OK; }
        Log("surface QueryInterface %08lx", iid.Data1);
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() { return ++ref; }
    STDMETHOD_(ULONG, Release)() {
        ULONG r = --ref;
        if (!r) {
            if (g_primary == this) g_primary = nullptr;
            if (palette) palette->Release();
            if (back) back->Release();
            delete this;
        }
        return r;
    }
    STDMETHOD(AddAttachedSurface)(LPDIRECTDRAWSURFACE) { return UNHANDLED("AddAttachedSurface"); }
    STDMETHOD(AddOverlayDirtyRect)(LPRECT) { return UNHANDLED("AddOverlayDirtyRect"); }
    STDMETHOD(Blt)(LPRECT dst, LPDIRECTDRAWSURFACE src, LPRECT srcRect, DWORD flags, LPDDBLTFX fx);
    STDMETHOD(BltBatch)(LPDDBLTBATCH, DWORD, DWORD) { return UNHANDLED("BltBatch"); }
    STDMETHOD(BltFast)(DWORD x, DWORD y, LPDIRECTDRAWSURFACE src, LPRECT srcRect, DWORD flags);
    STDMETHOD(DeleteAttachedSurface)(DWORD, LPDIRECTDRAWSURFACE) { return DD_OK; }
    STDMETHOD(EnumAttachedSurfaces)(LPVOID ctx, LPDDENUMSURFACESCALLBACK cb) {
        if (back) {
            DDSURFACEDESC d = {};
            back->Describe(d);
            back->AddRef();
            cb(back, &d, ctx);
        }
        return DD_OK;
    }
    STDMETHOD(EnumOverlayZOrders)(DWORD, LPVOID, LPDDENUMSURFACESCALLBACK) { return UNHANDLED("EnumOverlayZOrders"); }
    STDMETHOD(Flip)(LPDIRECTDRAWSURFACE, DWORD) {
        if (back) {
            px.swap(back->px);
            if (IsPrimary()) Present();
        }
        return DD_OK;
    }
    STDMETHOD(GetAttachedSurface)(LPDDSCAPS c, LPDIRECTDRAWSURFACE* out) {
        if (!back) { *out = nullptr; return DDERR_NOTFOUND; }
        back->AddRef();
        *out = back;
        (void)c;
        return DD_OK;
    }
    STDMETHOD(GetBltStatus)(DWORD) { return DD_OK; }
    STDMETHOD(GetCaps)(LPDDSCAPS out) { out->dwCaps = caps; return DD_OK; }
    STDMETHOD(GetClipper)(LPDIRECTDRAWCLIPPER*) { return DDERR_NOCLIPPERATTACHED; }
    STDMETHOD(GetColorKey)(DWORD, LPDDCOLORKEY key) {
        if (!keyed) return DDERR_NOCOLORKEY;
        key->dwColorSpaceLowValue = key->dwColorSpaceHighValue = colorKey;
        return DD_OK;
    }
    STDMETHOD(GetDC)(HDC*) { return UNHANDLED("GetDC"); }
    STDMETHOD(GetFlipStatus)(DWORD) { return DD_OK; }
    STDMETHOD(GetOverlayPosition)(LPLONG, LPLONG) { return UNHANDLED("GetOverlayPosition"); }
    STDMETHOD(GetPalette)(LPDIRECTDRAWPALETTE* out) {
        if (!palette) { *out = nullptr; return DDERR_NOPALETTEATTACHED; }
        palette->AddRef();
        *out = palette;
        return DD_OK;
    }
    STDMETHOD(GetPixelFormat)(LPDDPIXELFORMAT pf) { Format(*pf); return DD_OK; }
    STDMETHOD(GetSurfaceDesc)(LPDDSURFACEDESC d) { Describe(*d); return DD_OK; }
    STDMETHOD(Initialize)(LPDIRECTDRAW, LPDDSURFACEDESC) { return DDERR_ALREADYINITIALIZED; }
    STDMETHOD(IsLost)() { return DD_OK; }
    STDMETHOD(Lock)(LPRECT rect, LPDDSURFACEDESC d, DWORD, HANDLE) {
        Describe(*d);
        uint8_t* at = px.data();
        if (rect) at += size_t(rect->top) * w + rect->left;
        d->lpSurface = at;
        return DD_OK;
    }
    STDMETHOD(ReleaseDC)(HDC) { return UNHANDLED("ReleaseDC"); }
    STDMETHOD(Restore)() { return DD_OK; }
    STDMETHOD(SetClipper)(LPDIRECTDRAWCLIPPER) { return DD_OK; }
    STDMETHOD(SetColorKey)(DWORD, LPDDCOLORKEY key) {
        keyed = key != nullptr;
        if (key) colorKey = key->dwColorSpaceLowValue;
        return DD_OK;
    }
    STDMETHOD(SetOverlayPosition)(LONG, LONG) { return UNHANDLED("SetOverlayPosition"); }
    STDMETHOD(SetPalette)(LPDIRECTDRAWPALETTE p) {
        if (p) p->AddRef();
        if (palette) palette->Release();
        palette = static_cast<Palette*>(p);
        if (IsPrimary()) Present();
        return DD_OK;
    }
    STDMETHOD(Unlock)(LPVOID) {
        if (IsPrimary()) Present();
        return DD_OK;
    }
    STDMETHOD(UpdateOverlay)(LPRECT, LPDIRECTDRAWSURFACE, LPRECT, DWORD, LPDDOVERLAYFX) { return UNHANDLED("UpdateOverlay"); }
    STDMETHOD(UpdateOverlayDisplay)(DWORD) { return UNHANDLED("UpdateOverlayDisplay"); }
    STDMETHOD(UpdateOverlayZOrder)(DWORD, LPDIRECTDRAWSURFACE) { return UNHANDLED("UpdateOverlayZOrder"); }

    static void Format(DDPIXELFORMAT& pf) {
        memset(&pf, 0, sizeof pf);
        pf.dwSize = sizeof pf;
        pf.dwFlags = DDPF_RGB | DDPF_PALETTEINDEXED8;
        pf.dwRGBBitCount = 8;
    }
    void Describe(DDSURFACEDESC& d) const {
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        d.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT;
        d.dwWidth = w;
        d.dwHeight = h;
        d.lPitch = LONG(w);
        d.ddsCaps.dwCaps = caps;
        Format(d.ddpfPixelFormat);
    }
    void Copy(int dx, int dy, const Surface* s, RECT r, bool key) {
        for (LONG y = r.top; y < r.bottom; y++) {
            const int ty = dy + (y - r.top);
            if (ty < 0 || ty >= int(h) || y < 0 || y >= LONG(s->h)) continue;
            for (LONG x = r.left; x < r.right; x++) {
                const int tx = dx + (x - r.left);
                if (tx < 0 || tx >= int(w) || x < 0 || x >= LONG(s->w)) continue;
                const uint8_t v = s->px[size_t(y) * s->w + x];
                if (!key || v != s->colorKey) px[size_t(ty) * w + tx] = v;
            }
        }
    }
};

STDMETHODIMP Palette::SetEntries(DWORD, DWORD start, DWORD count, LPPALETTEENTRY in) {
    if (start + count > 256) return DDERR_INVALIDPARAMS;
    memcpy(entries + start, in, count * sizeof(PALETTEENTRY));
    if (g_primary && g_primary->palette == this) Present();
    return DD_OK;
}

STDMETHODIMP Surface::Blt(LPRECT dst, LPDIRECTDRAWSURFACE src, LPRECT srcRect, DWORD flags, LPDDBLTFX fx) {
    RECT d = dst ? *dst : RECT{ 0, 0, LONG(w), LONG(h) };
    if (flags & DDBLT_COLORFILL) {
        const uint8_t v = uint8_t(fx ? fx->dwFillColor : 0);
        for (LONG y = max(d.top, 0L); y < min(d.bottom, LONG(h)); y++)
            for (LONG x = max(d.left, 0L); x < min(d.right, LONG(w)); x++) px[size_t(y) * w + x] = v;
    } else if (src) {
        const Surface* s = static_cast<Surface*>(src);
        RECT r = srcRect ? *srcRect : RECT{ 0, 0, LONG(s->w), LONG(s->h) };
        if (r.right - r.left == d.right - d.left && r.bottom - r.top == d.bottom - d.top) {
            Copy(d.left, d.top, s, r, (flags & DDBLT_KEYSRC) && s->keyed);
        } else {  // a stretch: nearest pixel
            for (LONG y = d.top; y < d.bottom; y++)
                for (LONG x = d.left; x < d.right; x++) {
                    if (x < 0 || y < 0 || x >= LONG(w) || y >= LONG(h)) continue;
                    const LONG sx = r.left + (x - d.left) * (r.right - r.left) / max(1L, d.right - d.left);
                    const LONG sy = r.top + (y - d.top) * (r.bottom - r.top) / max(1L, d.bottom - d.top);
                    if (sx >= 0 && sy >= 0 && sx < LONG(s->w) && sy < LONG(s->h)) px[size_t(y) * w + x] = s->px[size_t(sy) * s->w + sx];
                }
        }
    }
    if (IsPrimary()) Present();
    return DD_OK;
}

STDMETHODIMP Surface::BltFast(DWORD x, DWORD y, LPDIRECTDRAWSURFACE src, LPRECT srcRect, DWORD flags) {
    if (!src) return DDERR_INVALIDPARAMS;
    const Surface* s = static_cast<Surface*>(src);
    RECT r = srcRect ? *srcRect : RECT{ 0, 0, LONG(s->w), LONG(s->h) };
    Copy(int(x), int(y), s, r, (flags & DDBLTFAST_SRCCOLORKEY) && s->keyed);
    if (IsPrimary()) Present();
    return DD_OK;
}

// The primary surface and its palette, to the shared page.
void Present() {
    if (!g_sh || !g_primary) return;
    const Surface* s = g_primary;
    const DWORD w = min(s->w, sc::kMaxW), h = min(s->h, sc::kMaxH);
    if (s->palette)
        for (int i = 0; i < 256; i++) {
            const PALETTEENTRY& e = s->palette->entries[i];
            g_sh->palette[i] = (DWORD(e.peRed) << 16) | (DWORD(e.peGreen) << 8) | e.peBlue;
        }
    for (DWORD y = 0; y < h; y++) memcpy(g_sh->pixels + y * w, s->px.data() + size_t(y) * s->w, w);
    g_sh->width = w;
    g_sh->height = h;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_sh->frame));
}

// ---- input -----------------------------------------------------------------------------------------------------
DWORD WINAPI InputThread(LPVOID) {
    net::Install(g_sh);  // here, not in DllMain: it loads ws2_32.dll
    DWORD gameThread = 0;
    BYTE state[256] = {};
    int32_t lastX = -1, lastY = -1;
    uint32_t lastButtons = 0;
    for (;;) {
        Sleep(2);
        g_sh->bridgeAlive = GetTickCount();
        net::Pump();
        const HWND hwnd = g_hwnd;
        if (!hwnd) continue;
        if (!gameThread) {
            gameThread = GetWindowThreadProcessId(hwnd, nullptr);
            // shared input state: SetKeyboardState below is the state the game thread's GetKeyState/TranslateMessage see
            AttachThreadInput(GetCurrentThreadId(), gameThread, TRUE);
        }
        while (g_sh->keyRead != g_sh->keyWrite) {
            const uint32_t e = g_sh->keys[g_sh->keyRead % sc::kKeyRing];
            g_sh->keyRead = g_sh->keyRead + 1;
            const UINT vk = e & 0xFF;
            const bool down = (e >> 16) & 1;
            state[vk] = down ? 0x80 : 0;
            if (vk == VK_LSHIFT || vk == VK_RSHIFT) state[VK_SHIFT] = (state[VK_LSHIFT] | state[VK_RSHIFT]) & 0x80;
            if (vk == VK_LCONTROL || vk == VK_RCONTROL) state[VK_CONTROL] = (state[VK_LCONTROL] | state[VK_RCONTROL]) & 0x80;
            if (vk == VK_LMENU || vk == VK_RMENU) state[VK_MENU] = (state[VK_LMENU] | state[VK_RMENU]) & 0x80;
            SetKeyboardState(state);
            const UINT generic = (vk == VK_LSHIFT || vk == VK_RSHIFT) ? VK_SHIFT
                : (vk == VK_LCONTROL || vk == VK_RCONTROL) ? VK_CONTROL : (vk == VK_LMENU || vk == VK_RMENU) ? VK_MENU : vk;
            const LPARAM scan = LPARAM(MapVirtualKeyA(generic, MAPVK_VK_TO_VSC)) << 16;
            const bool alt = state[VK_MENU] & 0x80;
            const UINT msg = alt || generic == VK_MENU ? (down ? WM_SYSKEYDOWN : WM_SYSKEYUP) : (down ? WM_KEYDOWN : WM_KEYUP);
            PostMessageA(hwnd, msg, generic, 1 | scan | (down ? 0 : (3u << 30)));
        }
        const int32_t x = g_sh->mouseX, y = g_sh->mouseY;
        const uint32_t buttons = g_sh->buttons;
        const WPARAM mk = ((buttons & 1) ? MK_LBUTTON : 0) | ((buttons & 2) ? MK_RBUTTON : 0) | ((buttons & 4) ? MK_MBUTTON : 0)
            | ((state[VK_SHIFT] & 0x80) ? MK_SHIFT : 0) | ((state[VK_CONTROL] & 0x80) ? MK_CONTROL : 0);
        static const struct { uint32_t bit; UINT vk, down, up; } map[] = {
            { 1, VK_LBUTTON, WM_LBUTTONDOWN, WM_LBUTTONUP }, { 2, VK_RBUTTON, WM_RBUTTONDOWN, WM_RBUTTONUP },
            { 4, VK_MBUTTON, WM_MBUTTONDOWN, WM_MBUTTONUP } };
        // A release lands where the button went down (before any move this poll), a press where the cursor now is:
        // the game's buttons only fire when released over themselves.
        const auto buttonsChanged = [&](bool pressing, LPARAM where) {
            for (const auto& b : map) {
                const bool changed = (buttons ^ lastButtons) & b.bit, isDown = (buttons & b.bit) != 0;
                if (!changed || isDown != pressing) continue;
                state[b.vk] = isDown ? 0x80 : 0;  // the game checks GetKeyState(VK_LBUTTON) too
                SetKeyboardState(state);
                PostMessageA(hwnd, isDown ? b.down : b.up, mk, where);
            }
        };
        if (lastX >= 0) buttonsChanged(false, MAKELPARAM(lastX, lastY));
        if (x != lastX || y != lastY) PostMessageA(hwnd, WM_MOUSEMOVE, mk, MAKELPARAM(x, y));
        buttonsChanged(true, MAKELPARAM(x, y));
        lastX = x;
        lastY = y;
        lastButtons = buttons;
    }
}

// ---- user32 hooks (patched into user32 itself, see HookUser32) ----------------------------------------------------
BOOL WINAPI HookGetCursorPos(LPPOINT p) {
    p->x = g_sh->mouseX;
    p->y = g_sh->mouseY;
    if (g_hwnd) ClientToScreen(g_hwnd, p);
    return TRUE;
}
BOOL WINAPI HookSetCursorPos(int, int) { return TRUE; }   // never the player's cursor
BOOL WINAPI HookClipCursor(const RECT*) { return TRUE; }
HWND WINAPI HookGetForegroundWindow() { return g_hwnd ? g_hwnd : GetDesktopWindow(); }
HWND WINAPI HookGetActiveWindow() { return g_hwnd; }

BOOL WINAPI HookSetForegroundWindow(HWND) { return TRUE; }  // never take focus from WoW

// ---- the game's CD -----------------------------------------------------------------------------------------------
// Warcraft II BNE plays from its CD: it reads Install.exe at the root of a CD-ROM drive (the one the War2CD registry
// value names, else any), and a "CD" on the drive the game is installed on does not count. The cabinet ships the
// CD's Install.exe beside the exe, so this process gets a virtual CD drive: a free letter, listed as present, typed
// CD-ROM, labelled as the disc, named by War2CD, and every path on it is the game folder (read-only, as on a disc).
// Only this process sees it.
char g_cd;          // the virtual drive's letter (0 = none free)
bool g_traceFiles;  // ESTERIA_SC_TRACE_FILES: log every open (diagnosing what a game looks for)

void ChooseCdLetter(DWORD drives) {
    for (char l = 'Z'; l >= 'D' && !g_cd; --l)
        if (!(drives & (1u << (l - 'A')))) g_cd = l;
}

bool OnCd(LPCSTR p) { return g_cd && p && toupper(BYTE(p[0])) == g_cd && p[1] == ':'; }

const char* CdFile(LPCSTR name, char* out) {  // the game-folder path for a path on the virtual drive, or name itself
    if (!OnCd(name)) return name;
    char exe[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    char* slash = n ? strrchr(exe, '\\') : nullptr;
    if (!slash) return name;
    const char* rest = name + 2;
    while (*rest == '\\' || *rest == '/') ++rest;
    lstrcpyA(slash + 1, rest);
    lstrcpyA(out, exe);
    return out;
}

using RegQueryValueExA_t = LONG(WINAPI*)(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
RegQueryValueExA_t o_RegQueryValueExA;
LONG WINAPI HookRegQueryValueExA(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size) {
    if (g_cd && name && !_stricmp(name, "War2CD")) {
        const char root[4] = { g_cd, ':', '\\', 0 };
        if (type) *type = REG_SZ;
        if (!size) return ERROR_SUCCESS;
        const bool fits = data && *size >= sizeof root;
        if (fits) memcpy(data, root, sizeof root);
        *size = sizeof root;
        return data && !fits ? ERROR_MORE_DATA : ERROR_SUCCESS;
    }
    return o_RegQueryValueExA(key, name, reserved, type, data, size);
}

using GetLogicalDrives_t = DWORD(WINAPI*)();
GetLogicalDrives_t o_GetLogicalDrives;
DWORD WINAPI HookGetLogicalDrives() { return o_GetLogicalDrives() | (g_cd ? 1u << (g_cd - 'A') : 0); }

using GetLogicalDriveStringsA_t = DWORD(WINAPI*)(DWORD, LPSTR);
GetLogicalDriveStringsA_t o_GetLogicalDriveStringsA;
DWORD WINAPI HookGetLogicalDriveStringsA(DWORD size, LPSTR out) {  // "C:\<0>D:\<0>...<0>", the virtual drive in order
    char real[512];
    const DWORD n = o_GetLogicalDriveStringsA(sizeof real, real);
    if (!g_cd || !n || n >= sizeof real) return o_GetLogicalDriveStringsA(size, out);
    char all[520];
    DWORD at = 0;
    bool added = false;
    for (const char* p = real; *p; p += strlen(p) + 1) {
        if (!added && toupper(BYTE(*p)) > g_cd) { all[at++] = g_cd; all[at++] = ':'; all[at++] = '\\'; all[at++] = 0; added = true; }
        lstrcpyA(all + at, p);
        at += DWORD(strlen(p) + 1);
    }
    if (!added) { all[at++] = g_cd; all[at++] = ':'; all[at++] = '\\'; all[at++] = 0; }
    all[at] = 0;  // at = length without the final NUL, as the API reports it
    if (size <= at) return at + 1;
    memcpy(out, all, at + 1);
    return at;
}

using GetDriveTypeA_t = UINT(WINAPI*)(LPCSTR);
GetDriveTypeA_t o_GetDriveTypeA;
UINT WINAPI HookGetDriveTypeA(LPCSTR root) {
    const UINT t = OnCd(root) ? DRIVE_CDROM : o_GetDriveTypeA(root);
    if (g_traceFiles) Log("GetDriveTypeA(%s) = %u", root ? root : "-", t);
    return t;
}

using GetVolumeInformationA_t = BOOL(WINAPI*)(LPCSTR, LPSTR, DWORD, LPDWORD, LPDWORD, LPDWORD, LPSTR, DWORD);
GetVolumeInformationA_t o_GetVolumeInformationA;
BOOL WINAPI HookGetVolumeInformationA(LPCSTR root, LPSTR label, DWORD labelSize, LPDWORD serial, LPDWORD maxLen,
                                      LPDWORD flags, LPSTR fs, DWORD fsSize) {
    if (!OnCd(root)) {
        const BOOL r = o_GetVolumeInformationA(root, label, labelSize, serial, maxLen, flags, fs, fsSize);
        if (g_traceFiles)
            Log("GetVolumeInformationA(%s) = %d label %s serial %08lx maxLen %lu flags %08lx fs %s (asked: label %d serial %d len %d flags %d fs %d)",
                root ? root : "-", r, label && r ? label : "-", serial ? *serial : 0, maxLen ? *maxLen : 0, flags ? *flags : 0,
                fs && r ? fs : "-", label != nullptr, serial != nullptr, maxLen != nullptr, flags != nullptr, fs != nullptr);
        return r;
    }
    if (g_traceFiles) Log("GetVolumeInformationA(%s) virtual", root);
    if (label && labelSize) lstrcpynA(label, "WAR2BNECD", int(labelSize));  // the disc's ISO 9660 volume name
    if (serial) *serial = 0x0B2EB2E0;
    if (maxLen) *maxLen = 110;
    if (flags) *flags = 0x01080001;  // what Windows reports for a mounted disc (CDFS); the game checks them
    if (fs && fsSize) lstrcpynA(fs, "CDFS", int(fsSize));
    return TRUE;
}

using CreateFileA_t = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
CreateFileA_t o_CreateFileA;
HANDLE WINAPI HookCreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    char cd[MAX_PATH];
    const char* path = CdFile(name, cd);
    if (path != name && (access & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA))) {  // a disc is read-only
        SetLastError(ERROR_WRITE_PROTECT);
        return INVALID_HANDLE_VALUE;
    }
    const HANDLE h = o_CreateFileA(path, access, share, sa, disp, flags, tmpl);
    const DWORD err = GetLastError();
    if (g_traceFiles || path != name)
        Log("open %s%s%s -> %s (error %lu)", name ? name : "-", path != name ? " = " : "", path != name ? path : "",
            h == INVALID_HANDLE_VALUE ? "FAILED" : "ok", h == INVALID_HANDLE_VALUE ? err : 0UL);
    SetLastError(err);
    return h;
}

using GetFileAttributesA_t = DWORD(WINAPI*)(LPCSTR);
GetFileAttributesA_t o_GetFileAttributesA;
DWORD WINAPI HookGetFileAttributesA(LPCSTR name) {
    char cd[MAX_PATH];
    const char* path = CdFile(name, cd);
    DWORD a = o_GetFileAttributesA(path);
    if (path != name && a != INVALID_FILE_ATTRIBUTES) a = (a & FILE_ATTRIBUTE_DIRECTORY) | FILE_ATTRIBUTE_READONLY;
    if (g_traceFiles) Log("GetFileAttributesA(%s) = %lx", name ? name : "-", a);
    return a;
}

using FindFirstFileA_t = HANDLE(WINAPI*)(LPCSTR, LPWIN32_FIND_DATAA);
FindFirstFileA_t o_FindFirstFileA;
HANDLE WINAPI HookFindFirstFileA(LPCSTR name, LPWIN32_FIND_DATAA data) {
    char cd[MAX_PATH];
    const char* path = CdFile(name, cd);
    const HANDLE h = o_FindFirstFileA(path, data);
    if (path != name && h != INVALID_HANDLE_VALUE) data->dwFileAttributes = (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) | FILE_ATTRIBUTE_READONLY;
    if (g_traceFiles) Log("FindFirstFileA(%s) -> %s", name ? name : "-", h == INVALID_HANDLE_VALUE ? "none" : "found");
    return h;
}

using GetDiskFreeSpaceA_t = BOOL(WINAPI*)(LPCSTR, LPDWORD, LPDWORD, LPDWORD, LPDWORD);
GetDiskFreeSpaceA_t o_GetDiskFreeSpaceA;
BOOL WINAPI HookGetDiskFreeSpaceA(LPCSTR root, LPDWORD spc, LPDWORD bps, LPDWORD freeClusters, LPDWORD total) {
    if (g_traceFiles) Log("GetDiskFreeSpaceA(%s)", root ? root : "-");
    if (!OnCd(root)) return o_GetDiskFreeSpaceA(root, spc, bps, freeClusters, total);
    if (spc) *spc = 1;  // a full 650 MB disc: 2048-byte sectors, nothing free
    if (bps) *bps = 2048;
    if (freeClusters) *freeClusters = 0;
    if (total) *total = 332800;
    return TRUE;
}

void HookCd() {
    g_traceFiles = GetEnvironmentVariableA("ESTERIA_SC_TRACE_FILES", nullptr, 0) != 0;
    HMODULE kb = GetModuleHandleA("kernelbase.dll"), k32 = GetModuleHandleA("kernel32.dll");
    if (!net::HotPatch(kb, "GetLogicalDrives", reinterpret_cast<void*>(HookGetLogicalDrives), reinterpret_cast<void**>(&o_GetLogicalDrives)))
        return;  // without it no letter can be listed: leave the CD alone
    ChooseCdLetter(o_GetLogicalDrives());
    Log("cd: virtual drive %c:", g_cd ? g_cd : '-');
    net::HotPatch(GetModuleHandleA("advapi32.dll"), "RegQueryValueExA", reinterpret_cast<void*>(HookRegQueryValueExA),
                  reinterpret_cast<void**>(&o_RegQueryValueExA));
    net::HotPatch(kb, "GetDriveTypeA", reinterpret_cast<void*>(HookGetDriveTypeA), reinterpret_cast<void**>(&o_GetDriveTypeA));
    if (!net::HotPatch(kb, "GetLogicalDriveStringsA", reinterpret_cast<void*>(HookGetLogicalDriveStringsA),
                       reinterpret_cast<void**>(&o_GetLogicalDriveStringsA)))
        net::HotPatch(k32, "GetLogicalDriveStringsA", reinterpret_cast<void*>(HookGetLogicalDriveStringsA),
                      reinterpret_cast<void**>(&o_GetLogicalDriveStringsA));
    net::HotPatch(k32, "GetVolumeInformationA", reinterpret_cast<void*>(HookGetVolumeInformationA),
                  reinterpret_cast<void**>(&o_GetVolumeInformationA));  // kernelbase's has no slot; kernel32's stub does
    net::HotPatch(kb, "CreateFileA", reinterpret_cast<void*>(HookCreateFileA), reinterpret_cast<void**>(&o_CreateFileA));
    net::HotPatch(kb, "GetFileAttributesA", reinterpret_cast<void*>(HookGetFileAttributesA), reinterpret_cast<void**>(&o_GetFileAttributesA));
    for (HMODULE m : { kb, k32 })  // where the export has a slot or is a forwarding stub
        if (net::HotPatch(m, "FindFirstFileA", reinterpret_cast<void*>(HookFindFirstFileA), reinterpret_cast<void**>(&o_FindFirstFileA))) break;
    for (HMODULE m : { kb, k32 })
        if (net::HotPatch(m, "GetDiskFreeSpaceA", reinterpret_cast<void*>(HookGetDiskFreeSpaceA), reinterpret_cast<void**>(&o_GetDiskFreeSpaceA))) break;
}

// Each hook replaces the user32 function itself (a jmp at its entry), so every caller in the process gets it: the
// import tables, GetProcAddress lookups, the .snp/Local.dll modules. No hook calls the original, so no trampoline.
void HookUser32() {
    HMODULE user32 = GetModuleHandleA("user32.dll");
    // StarCraft loads ddraw.dll only after its window exists, and clips the (player's, system-wide) cursor to that
    // window first: undo it before the hook makes every later clip a no-op.
    ClipCursor(nullptr);
    const struct { const char* name; void* hook; } hooks[] = {
        { "GetCursorPos", reinterpret_cast<void*>(HookGetCursorPos) }, { "SetCursorPos", reinterpret_cast<void*>(HookSetCursorPos) },
        { "ClipCursor", reinterpret_cast<void*>(HookClipCursor) }, { "GetForegroundWindow", reinterpret_cast<void*>(HookGetForegroundWindow) },
        { "GetActiveWindow", reinterpret_cast<void*>(HookGetActiveWindow) },
        { "SetForegroundWindow", reinterpret_cast<void*>(HookSetForegroundWindow) } };
    for (const auto& h : hooks) {
        auto* at = reinterpret_cast<BYTE*>(GetProcAddress(user32, h.name));
        DWORD old;
        if (!at || !VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &old)) { Log("cannot hook %s", h.name); continue; }
        at[0] = 0xE9;  // jmp rel32
        *reinterpret_cast<int32_t*>(at + 1) = int32_t(reinterpret_cast<BYTE*>(h.hook) - (at + 5));
        VirtualProtect(at, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), at, 5);
    }
}

// ---- DirectDraw ---------------------------------------------------------------------------------------------------
struct DirectDraw : IDirectDraw {
    ULONG ref = 1;
    STDMETHOD(QueryInterface)(REFIID iid, LPVOID* out) {
        if (iid == IID_IDirectDraw || iid == IID_IUnknown) { *out = this; AddRef(); return S_OK; }
        Log("ddraw QueryInterface %08lx", iid.Data1);
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() { return ++ref; }
    STDMETHOD_(ULONG, Release)() { ULONG r = --ref; if (!r) delete this; return r; }
    STDMETHOD(Compact)() { return DD_OK; }
    STDMETHOD(CreateClipper)(DWORD, LPDIRECTDRAWCLIPPER*, IUnknown*) { return UNHANDLED("CreateClipper"); }
    STDMETHOD(CreatePalette)(DWORD, LPPALETTEENTRY entries, LPDIRECTDRAWPALETTE* out, IUnknown*) {
        auto* p = new Palette();
        if (entries) memcpy(p->entries, entries, sizeof p->entries);
        *out = p;
        return DD_OK;
    }
    STDMETHOD(CreateSurface)(LPDDSURFACEDESC d, LPDIRECTDRAWSURFACE* out, IUnknown*) {
        const DWORD caps = (d->dwFlags & DDSD_CAPS) ? d->ddsCaps.dwCaps : 0;
        Surface* s;
        if (caps & DDSCAPS_PRIMARYSURFACE) {
            s = new Surface(g_modeW, g_modeH, caps | DDSCAPS_VISIBLE);
            if ((d->dwFlags & DDSD_BACKBUFFERCOUNT) && d->dwBackBufferCount)
                s->back = new Surface(g_modeW, g_modeH, DDSCAPS_BACKBUFFER | DDSCAPS_FLIP);
            g_primary = s;
        } else {
            s = new Surface(d->dwWidth, d->dwHeight, caps);
        }
        Log("CreateSurface caps %08lx %lux%lu back %d", caps, s->w, s->h, s->back != nullptr);
        *out = s;
        return DD_OK;
    }
    STDMETHOD(DuplicateSurface)(LPDIRECTDRAWSURFACE, LPDIRECTDRAWSURFACE*) { return UNHANDLED("DuplicateSurface"); }
    STDMETHOD(EnumDisplayModes)(DWORD, LPDDSURFACEDESC, LPVOID ctx, LPDDENUMMODESCALLBACK cb) {
        static const DWORD modes[][2] = { { 640, 480 } };
        for (const auto& m : modes) {
            DDSURFACEDESC d = {};
            d.dwSize = sizeof d;
            d.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT;
            d.dwWidth = m[0];
            d.dwHeight = m[1];
            d.lPitch = LONG(m[0]);
            Surface::Format(d.ddpfPixelFormat);
            if (cb(&d, ctx) == DDENUMRET_CANCEL) break;
        }
        return DD_OK;
    }
    STDMETHOD(EnumSurfaces)(DWORD, LPDDSURFACEDESC, LPVOID, LPDDENUMSURFACESCALLBACK) { return UNHANDLED("EnumSurfaces"); }
    STDMETHOD(FlipToGDISurface)() { return DD_OK; }
    STDMETHOD(GetCaps)(LPDDCAPS hw, LPDDCAPS sw) {
        if (hw) { const DWORD size = hw->dwSize; memset(hw, 0, size); hw->dwSize = size; hw->dwPalCaps = DDPCAPS_8BIT; }
        if (sw) { const DWORD size = sw->dwSize; memset(sw, 0, size); sw->dwSize = size; }
        return DD_OK;
    }
    STDMETHOD(GetDisplayMode)(LPDDSURFACEDESC d) {
        Surface probe(g_modeW, g_modeH, 0);
        probe.Describe(*d);
        return DD_OK;
    }
    STDMETHOD(GetFourCCCodes)(LPDWORD count, LPDWORD) { *count = 0; return DD_OK; }
    STDMETHOD(GetGDISurface)(LPDIRECTDRAWSURFACE*) { return UNHANDLED("GetGDISurface"); }
    STDMETHOD(GetMonitorFrequency)(LPDWORD hz) { *hz = 60; return DD_OK; }
    STDMETHOD(GetScanLine)(LPDWORD line) { *line = 0; return DD_OK; }
    STDMETHOD(GetVerticalBlankStatus)(LPBOOL vb) { *vb = TRUE; return DD_OK; }
    STDMETHOD(Initialize)(GUID*) { return DDERR_ALREADYINITIALIZED; }
    STDMETHOD(RestoreDisplayMode)() { return DD_OK; }
    STDMETHOD(SetCooperativeLevel)(HWND hwnd, DWORD flags) {
        Log("SetCooperativeLevel %p %08lx", hwnd, flags);
        if (hwnd) g_hwnd = hwnd;
        return DD_OK;
    }
    STDMETHOD(SetDisplayMode)(DWORD w, DWORD h, DWORD bpp) {
        Log("SetDisplayMode %lux%lux%lu", w, h, bpp);
        g_modeW = w;
        g_modeH = h;
        return DD_OK;
    }
    STDMETHOD(WaitForVerticalBlank)(DWORD, HANDLE) { return DD_OK; }
};

HMODULE g_real;
template <class F> F Real(const char* name) {
    if (!g_real) {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH);  // SysWOW64 for this 32-bit process
        strcat_s(path, "\\ddraw.dll");
        g_real = LoadLibraryA(path);
    }
    return g_real ? reinterpret_cast<F>(GetProcAddress(g_real, name)) : nullptr;
}

}  // namespace

void BridgeLog(const char* fmt, ...) {  // Log for net_bridge.cpp
    if (!g_log) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    va_end(args);
    fputc('\n', g_log);
    fflush(g_log);
}

extern "C" HRESULT WINAPI DirectDrawCreate(GUID* guid, LPDIRECTDRAW* out, IUnknown* outer) {
    if (!g_sh) {
        auto real = Real<HRESULT(WINAPI*)(GUID*, LPDIRECTDRAW*, IUnknown*)>("DirectDrawCreate");
        return real ? real(guid, out, outer) : DDERR_GENERIC;
    }
    *out = new DirectDraw();
    return DD_OK;
}

extern "C" HRESULT WINAPI DirectDrawEnumerateA(LPDDENUMCALLBACKA cb, LPVOID ctx) {
    if (!g_sh) {
        auto real = Real<HRESULT(WINAPI*)(LPDDENUMCALLBACKA, LPVOID)>("DirectDrawEnumerateA");
        return real ? real(cb, ctx) : DDERR_GENERIC;
    }
    cb(nullptr, const_cast<char*>("Esteria arcade"), const_cast<char*>("display"), ctx);
    return DD_OK;
}

extern "C" HRESULT WINAPI DirectDrawCreateClipper(DWORD flags, LPDIRECTDRAWCLIPPER* out, IUnknown* outer) {
    auto real = Real<HRESULT(WINAPI*)(DWORD, LPDIRECTDRAWCLIPPER*, IUnknown*)>("DirectDrawCreateClipper");
    return real ? real(flags, out, outer) : DDERR_GENERIC;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    char name[128];
    if (!GetEnvironmentVariableA("ESTERIA_SC_SHM", name, sizeof name)) return TRUE;  // StarCraft started normally
    HANDLE map = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
    g_sh = map ? static_cast<sc::Shared*>(MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(sc::Shared))) : nullptr;
    if (!g_sh || g_sh->magic != sc::kMagic) { g_sh = nullptr; return TRUE; }
    char logPath[MAX_PATH];
    if (GetEnvironmentVariableA("ESTERIA_SC_LOG", logPath, sizeof logPath)) fopen_s(&g_log, logPath, "w");
    Log("bridge attached");
    HookUser32();
    HookCd();
    // DLL loads and unloads with their addresses: a crash address outside every module is often one unloaded earlier.
    using Notify = VOID(CALLBACK*)(ULONG, const void*, PVOID);
    using Register = LONG(NTAPI*)(ULONG, Notify, PVOID, PVOID*);
    if (auto reg = reinterpret_cast<Register>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "LdrRegisterDllNotification"))) {
        static PVOID cookie;
        reg(0, [](ULONG reason, const void* data, PVOID) {
            // LDR_DLL_NOTIFICATION_DATA: { ULONG flags; PCUNICODE_STRING fullName; PCUNICODE_STRING baseName; PVOID base; ULONG size; }
            struct Str { USHORT len, max; PWSTR buf; };
            struct Data { ULONG flags; const Str* full; const Str* base; PVOID dllBase; ULONG size; };
            const Data* d = static_cast<const Data*>(data);
            Log("dll %s %.*ls at %p (+%lx)", reason == 1 ? "loaded" : "unloaded", int(d->base->len / 2), d->base->buf, d->dllBase, d->size);
            if (reason == 1 && snd::IsSystemDsound(d->full->buf, d->full->len / 2)) snd::Hook(static_cast<HMODULE>(d->dllBase));
        }, nullptr, &cookie);
    }
    if (HMODULE ds = GetModuleHandleA("dsound.dll")) {  // already loaded before us (StarCraft loads ddraw.dll late)
        wchar_t path[MAX_PATH];
        const DWORD n = GetModuleFileNameW(ds, path, MAX_PATH);
        if (snd::IsSystemDsound(path, n)) snd::Hook(ds);
    }
    // A game that dies inside the cabinet leaves no window to show it: log where (first chance; the first few only).
    AddVectoredExceptionHandler(1, [](EXCEPTION_POINTERS* e) -> LONG {
        static volatile LONG count;
        const EXCEPTION_RECORD* r = e->ExceptionRecord;
        if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && InterlockedIncrement(&count) <= 5) {
            HMODULE mod = nullptr;
            char name[MAX_PATH] = "?";
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   static_cast<LPCSTR>(r->ExceptionAddress), &mod)) GetModuleFileNameA(mod, name, MAX_PATH);
            const char* base = strrchr(name, '\\');
            Log("access violation at %p (%s+0x%lx) %s %p", r->ExceptionAddress, base ? base + 1 : name,
                mod ? DWORD(static_cast<BYTE*>(r->ExceptionAddress) - reinterpret_cast<BYTE*>(mod)) : 0UL,
                r->ExceptionInformation[0] == 8 ? "executing" : r->ExceptionInformation[0] ? "writing" : "reading",
                reinterpret_cast<void*>(r->ExceptionInformation[1]));
            // the caller: the return address on top of the stack (right when a call lands on a bad address)
            DWORD ret = 0;
            if (!IsBadReadPtr(reinterpret_cast<void*>(e->ContextRecord->Esp), 4)) ret = *reinterpret_cast<DWORD*>(e->ContextRecord->Esp);
            HMODULE rm = nullptr;
            char rn[MAX_PATH] = "?";
            if (ret && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                          reinterpret_cast<LPCSTR>(uintptr_t(ret)), &rm)) GetModuleFileNameA(rm, rn, MAX_PATH);
            const char* rb = strrchr(rn, '\\');
            Log("  [esp] %08lx (%s+0x%lx)", ret, rb ? rb + 1 : rn, rm ? DWORD(ret - DWORD(uintptr_t(rm))) : 0UL);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    });
    CreateThread(nullptr, 0, InputThread, nullptr, 0, nullptr);
    return TRUE;
}
