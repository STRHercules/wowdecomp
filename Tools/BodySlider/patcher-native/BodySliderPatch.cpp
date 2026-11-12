#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <wincrypt.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include "PatchData.h"

namespace {
struct File {
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
struct Buffer {
    unsigned char* bytes = nullptr;
    ~Buffer() { if (bytes) HeapFree(GetProcessHeap(), 0, bytes); }
};
void Print(HANDLE stream, const wchar_t* text) {
    if (!stream || stream == INVALID_HANDLE_VALUE) return;
    DWORD done = 0;
    if (GetFileType(stream) == FILE_TYPE_CHAR) {
        WriteConsoleW(stream, text, static_cast<DWORD>(std::wcslen(text)), &done, nullptr);
    } else {
        char utf8[4096]{};
        int count = WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), nullptr, nullptr);
        if (count > 0) WriteFile(stream, utf8, static_cast<DWORD>(count-1), &done, nullptr);
    }
}
int Report(bool gui, const wchar_t* text, bool error) {
    if (gui) MessageBoxW(nullptr, text, L"Body Slider Patcher", MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
    else Print(GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE), text);
    return error ? 1 : 0;
}
bool Hash(const unsigned char* data, DWORD size, unsigned char (&digest)[32]) {
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    bool ok = CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) != 0;
    if (ok) ok = CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) != 0;
    if (ok) ok = CryptHashData(hash, data, size, 0) != 0;
    DWORD length = sizeof(digest);
    if (ok) ok = CryptGetHashParam(hash, HP_HASHVAL, digest, &length, 0) && length == sizeof(digest);
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    return ok;
}
int Convert(const wchar_t* source, const wchar_t* destination, int operation, bool gui) {
    File input;
    input.handle = CreateFileW(source, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (input.handle == INVALID_HANDLE_VALUE) return Report(gui, L"Cannot open the input executable for reading.\n", true);
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(input.handle, &size)
            || (size.QuadPart != PatchData::OriginalSize && size.QuadPart != PatchData::PatchedSize))
        return Report(gui, L"Unsupported executable. Select the supported unmodified 3.3.5a build 12340 Wow.exe, or a copy patched by this tool.\n", true);
    Buffer memory;
    memory.bytes = static_cast<unsigned char*>(HeapAlloc(GetProcessHeap(), 0, PatchData::PatchedSize));
    if (!memory.bytes) return Report(gui, L"Not enough memory to read the executable.\n", true);
    DWORD read = 0;
    if (!ReadFile(input.handle, memory.bytes, static_cast<DWORD>(size.QuadPart), &read, nullptr) || read != size.QuadPart)
        return Report(gui, L"Could not read the complete executable. No output was created.\n", true);
    unsigned char digest[32]{};
    if (!Hash(memory.bytes, read, digest)) return Report(gui, L"Windows could not calculate SHA-256. No output was created.\n", true);
    bool stock = read == PatchData::OriginalSize && std::memcmp(digest, PatchData::OriginalHash, 32) == 0;
    bool patched = read == PatchData::PatchedSize && std::memcmp(digest, PatchData::PatchedHash, 32) == 0;
    if (!stock && !patched) return Report(gui, L"SHA-256 does not match the supported executable. This file is modified, corrupt, or a different build. No output was created.\n", true);
    bool restore = operation == 2 || (operation == 0 && patched);
    if ((restore && !patched) || (!restore && !stock))
        return Report(gui, restore ? L"Restore requires an unchanged BodySlider-patched executable.\n" : L"This executable is already patched. Use --restore to create an original copy.\n", true);
    for (const auto& change : PatchData::Changes) memory.bytes[change.offset] = restore ? change.original : change.patched;
    if (!restore) std::memcpy(memory.bytes+PatchData::OriginalSize, PatchData::Append, sizeof(PatchData::Append));
    DWORD outputSize = restore ? PatchData::OriginalSize : PatchData::PatchedSize;
    if (!Hash(memory.bytes, outputSize, digest)
            || std::memcmp(digest, restore ? PatchData::OriginalHash : PatchData::PatchedHash, 32) != 0)
        return Report(gui, L"Internal patch verification failed. No output was created.\n", true);
    wchar_t automatic[32768]{};
    if (!destination) {
        DWORD length = GetFullPathNameW(source, 32768, automatic, nullptr);
        if (!length || length >= 32768) return Report(gui, L"Input path is too long.\n", true);
        wchar_t* slash = std::wcsrchr(automatic, L'\\');
        if (!slash) return Report(gui, L"Cannot resolve the input folder.\n", true);
        const wchar_t* filename = restore ? L"Wow-original.exe" : L"Wow-BodySlider.exe";
        if (wcscpy_s(slash+1, 32768-static_cast<size_t>(slash-automatic)-1, filename))
            return Report(gui, L"Output path is too long.\n", true);
        destination = automatic;
    }
    File output;
    output.handle = CreateFileW(destination, GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output.handle == INVALID_HANDLE_VALUE)
        return Report(gui, L"Cannot create the output copy. Input files and existing output files are never overwritten. Choose a new output filename and ensure the folder is writable.\n", true);
    DWORD written = 0;
    bool writtenOk = WriteFile(output.handle, memory.bytes, outputSize, &written, nullptr) && written == outputSize
        && FlushFileBuffers(output.handle);
    if (!writtenOk) {
        // Mark this handle's file for deletion before releasing exclusive ownership.
        FILE_DISPOSITION_INFO disposition{TRUE};
        bool removed = SetFileInformationByHandle(output.handle, FileDispositionInfo, &disposition, sizeof(disposition)) != 0;
        return Report(gui, removed
            ? L"Could not write the complete output; the incomplete copy was removed. The original executable is unchanged.\n"
            : L"Could not write the complete output. An incomplete output copy may remain; the original executable is unchanged.\n", true);
    }
    bool closedOk = CloseHandle(output.handle) != 0;
    output.handle = INVALID_HANDLE_VALUE;
    if (!closedOk) return Report(gui, L"The output could not be closed successfully. Verify the copy before use; the original executable is unchanged.\n", true);
    if (!gui) {
        Print(GetStdHandle(STD_OUTPUT_HANDLE), restore ? L"Restored original copy: " : L"Created patched copy: ");
        Print(GetStdHandle(STD_OUTPUT_HANDLE), destination);
        Print(GetStdHandle(STD_OUTPUT_HANDLE), L"\nOriginal input unchanged.\n");
        return 0;
    }
    wchar_t message[32768]{};
    const wchar_t* instructions = restore
        ? L"The original executable has been restored byte-for-byte at the new path. Your input file is unchanged."
        : L"Your original executable is unchanged.\n\nTo install: close WoW, keep a backup of the original Wow.exe, then rename this patched copy to Wow.exe. Place BodySlider.dll beside it and install the BodySlider addon. Enter the world and type /bodyslider.";
    _snwprintf_s(message, 32768, _TRUNCATE, L"Created:\n%s\n\n%s", destination, instructions);
    return Report(true, message, false);
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return 1;
    int result = 0;
    if (count > 1) {
        bool patch = count == 4 && std::wcscmp(arguments[1], L"--patch") == 0;
        bool restore = count == 4 && std::wcscmp(arguments[1], L"--restore") == 0;
        if (patch || restore) result = Convert(arguments[2], arguments[3], restore ? 2 : 1, false);
        else result = Report(false, L"BodySliderPatch.exe --patch <stock Wow.exe> <new patched.exe>\nBodySliderPatch.exe --restore <patched.exe> <new original.exe>\n\nRun without arguments for the file picker. Existing files are never overwritten.\n", count != 2 || std::wcscmp(arguments[1], L"--help") != 0);
    } else {
        wchar_t filename[32768]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.lpstrFile = filename;
        dialog.nMaxFile = 32768;
        dialog.lpstrFilter = L"World of Warcraft executable (*.exe)\0*.exe\0All files\0*.*\0\0";
        dialog.lpstrTitle = L"Select stock build 12340 Wow.exe (or patched copy to restore)";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_DONTADDTORECENT;
        if (GetOpenFileNameW(&dialog)) result = Convert(filename, nullptr, 0, true);
        else if (CommDlgExtendedError()) result = Report(true, L"Windows could not open the file picker.\n", true);
    }
    LocalFree(arguments);
    return result;
}
