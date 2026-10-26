#if defined(WOWDECOMP_ARCH_64)

#include <windows.h>
#include <shellapi.h>
#include <cstdint>
#include "WoW/Source/Glue/CGlueMgr.h"
#include "Engine/Source/BlizzardCore/Source/System/Process/Process.h"
#include "Engine/Source/BlizzardCore/Source/System/Unicode/Unicode.h"

// OFFSET: 0x004620A0
static bool ShellExecuteRunAs(const char* path, const char* arguments) {
    if (!arguments) {
        arguments = "";
    }

    Blizzard::StringConversion_CodepageToUTF8::ToNative nativeArguments(arguments);
    Blizzard::StringConversion_CodepageToUTF8::ToNative nativePath(path);
    Blizzard::StringConversion_CodepageToUTF8::ToNative verb("runas");

    return reinterpret_cast<uintptr_t>(ShellExecuteA(nullptr, verb.m_data, nativePath.m_data, nativeArguments.m_data, nullptr, SW_SHOWNORMAL)) > 32;
}

// OFFSET: 0x00462190
bool CGlueMgr::PatchDownloadExecuteCommand(const char* path, const char* arguments) {
    // CoA64: security hardening -- prepatch.lst runs unauthenticated on Ascension (patch 0x4E57FE), so "execute" would
    // run any server's program (with "runas"); it is refused and the prepatch fails (PatchCommandPolicy.h).
    (void)path;
    (void)arguments;
    return false;

    OSVERSIONINFOA versionInfo;
    versionInfo.dwOSVersionInfoSize = sizeof(versionInfo);
    GetVersionExA(&versionInfo);

    if (versionInfo.dwMajorVersion < 6) {
        return Blizzard::Process::LaunchChildProcessWithArguments(path, arguments, nullptr, nullptr);
    }

    return ShellExecuteRunAs(path, arguments);
}

#endif
