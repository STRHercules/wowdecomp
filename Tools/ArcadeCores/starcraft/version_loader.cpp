// version.dll for PC cabinet games that check things before they load ddraw.dll (32-bit; beside the game's exe).
//
// Warcraft II BNE looks for its CD before it ever touches DirectDraw, so the ddraw.dll bridge (whose hooks answer
// that check) would load too late. The game and its storm.dll import VERSION.dll, which is not a protected system
// DLL, so this file loads first: it loads the bridge (ddraw.dll from the same folder) and forwards every version.dll
// export to the real one in the system folder.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {
HMODULE g_real;
FARPROC g_fn[17];
const char* const kNames[17] = {
    "GetFileVersionInfoA", "GetFileVersionInfoByHandle", "GetFileVersionInfoExA", "GetFileVersionInfoExW",
    "GetFileVersionInfoSizeA", "GetFileVersionInfoSizeExA", "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW",
    "GetFileVersionInfoW", "VerFindFileA", "VerFindFileW", "VerInstallFileA", "VerInstallFileW", "VerLanguageNameA",
    "VerLanguageNameW", "VerQueryValueA", "VerQueryValueW"};
}  // namespace

// Each export jumps to the real function with the caller's arguments untouched (stdcall: the callee pops them).
#define FORWARD(i, name) extern "C" __declspec(naked) void name() { __asm { jmp dword ptr [g_fn + 4 * i] } }
FORWARD(0, Fwd_GetFileVersionInfoA)
FORWARD(1, Fwd_GetFileVersionInfoByHandle)
FORWARD(2, Fwd_GetFileVersionInfoExA)
FORWARD(3, Fwd_GetFileVersionInfoExW)
FORWARD(4, Fwd_GetFileVersionInfoSizeA)
FORWARD(5, Fwd_GetFileVersionInfoSizeExA)
FORWARD(6, Fwd_GetFileVersionInfoSizeExW)
FORWARD(7, Fwd_GetFileVersionInfoSizeW)
FORWARD(8, Fwd_GetFileVersionInfoW)
FORWARD(9, Fwd_VerFindFileA)
FORWARD(10, Fwd_VerFindFileW)
FORWARD(11, Fwd_VerInstallFileA)
FORWARD(12, Fwd_VerInstallFileW)
FORWARD(13, Fwd_VerLanguageNameA)
FORWARD(14, Fwd_VerLanguageNameW)
FORWARD(15, Fwd_VerQueryValueA)
FORWARD(16, Fwd_VerQueryValueW)

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    char path[MAX_PATH];
    GetSystemDirectoryA(path, MAX_PATH);  // SysWOW64 for this 32-bit process
    lstrcatA(path, "\\version.dll");
    g_real = LoadLibraryA(path);
    for (int i = 0; i < 17; i++) g_fn[i] = g_real ? GetProcAddress(g_real, kNames[i]) : nullptr;
    // the bridge, beside this file; its DllMain does nothing unless the cabinet started the game
    char bridge[MAX_PATH];
    const DWORD n = GetModuleFileNameA(self, bridge, MAX_PATH);
    for (DWORD i = n; i-- > 0;)
        if (bridge[i] == '\\') { lstrcpyA(bridge + i + 1, "ddraw.dll"); break; }
    if (n) LoadLibraryA(bridge);
    return TRUE;
}
