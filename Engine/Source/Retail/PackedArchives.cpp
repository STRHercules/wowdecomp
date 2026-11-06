#include "Engine/Source/Retail/PackedArchives.h"
#include <algorithm>
#include <windows.h>

namespace Retail {
namespace {
std::wstring Lower(std::wstring value) {
    for (auto& c : value) if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
    return value;
}
std::filesystem::path DataRoot(std::filesystem::path path) {
    while (!path.empty()) {
        if (Lower(path.filename().wstring()) == L"data") return path;
        auto parent = path.parent_path();
        if (parent == path) break;
        path = parent;
    }
    return {};
}
}
struct PackedArchives::Impl {
    HMODULE module = nullptr;
    std::vector<HANDLE> archives;
    bool (WINAPI* openArchive)(const wchar_t*, DWORD, DWORD, HANDLE*) = nullptr;
    bool (WINAPI* closeArchive)(HANDLE) = nullptr;
    bool (WINAPI* openFile)(HANDLE, const char*, DWORD, HANDLE*) = nullptr;
    DWORD (WINAPI* fileSize)(HANDLE, DWORD*) = nullptr;
    bool (WINAPI* readFile)(HANDLE, void*, DWORD, DWORD*, void*) = nullptr;
    bool (WINAPI* closeFile)(HANDLE) = nullptr;
    ~Impl() {
        for (auto archive : archives) closeArchive(archive);
        if (module) FreeLibrary(module);
    }
};
PackedArchives::PackedArchives() = default;
PackedArchives::~PackedArchives() = default;

bool PackedArchives::Open(const std::filesystem::path& configDirectory,
    const std::filesystem::path& sourcePath, std::string& error) {
    auto data = DataRoot(configDirectory);
    if (data.empty()) data = DataRoot(sourcePath);
    if (data.empty() || !std::filesystem::is_directory(data)) return true;
    std::vector<std::filesystem::path> paths;
    for (const auto& file : std::filesystem::directory_iterator(data)) {
        const auto name = Lower(file.path().filename().wstring());
        if (file.is_regular_file() && name.compare(0, 6, L"patch-") == 0
            && Lower(file.path().extension().wstring()) == L".mpq") paths.push_back(file.path());
    }
    if (paths.empty()) return true;
    std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) {
        return Lower(a.filename().wstring()) > Lower(b.filename().wstring());
    });
    auto impl = std::make_unique<Impl>();
    auto dll = data.parent_path() / "StormLib.dll";
    if (!std::filesystem::is_regular_file(dll)) {
        wchar_t executable[32768];
        const DWORD count = GetModuleFileNameW(nullptr, executable, 32768);
        if (count && count < 32768) dll = std::filesystem::path(executable).parent_path() / "StormLib.dll";
    }
    impl->module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!impl->module) { error = "Standalone packed asset reads require StormLib.dll beside the client or tool"; return false; }
#define LOAD_STORM(member, symbol) \
    impl->member = reinterpret_cast<decltype(impl->member)>(GetProcAddress(impl->module, symbol)); \
    if (!impl->member) { error = "Missing StormLib export: " symbol; return false; }
    LOAD_STORM(openArchive, "SFileOpenArchive")
    LOAD_STORM(closeArchive, "SFileCloseArchive")
    LOAD_STORM(openFile, "SFileOpenFileEx")
    LOAD_STORM(fileSize, "SFileGetFileSize")
    LOAD_STORM(readFile, "SFileReadFile")
    LOAD_STORM(closeFile, "SFileCloseFile")
#undef LOAD_STORM
    impl->archives.reserve(paths.size());
    for (const auto& path : paths) {
        HANDLE archive = nullptr;
        if (!impl->openArchive(path.c_str(), 0, 0x100, &archive)) {
            error = "Cannot open packed asset archive: " + path.string();
            return false;
        }
        impl->archives.push_back(archive);
    }
    m_impl = std::move(impl);
    return true;
}

int PackedArchives::Read(const char* name, std::vector<uint8_t>& bytes, std::string& error, uint32_t* size) {
    if (!m_impl) return 0;
    for (auto archive : m_impl->archives) {
        HANDLE file = nullptr;
        if (!m_impl->openFile(archive, name, 0, &file)) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND) continue;
            error = std::string("Cannot open packed Retail asset: ") + name;
            return -1;
        }
        std::unique_ptr<void, decltype(m_impl->closeFile)> owned(file, m_impl->closeFile);
        DWORD high = 0;
        const DWORD length = m_impl->fileSize(file, &high);
        if (high || length > 128 * 1024 * 1024) {
            error = "Invalid packed Retail asset size";
            return -1;
        }
        if (size) {
            *size = length;
            return 1;
        }
        bytes.resize(length);
        DWORD read = 0;
        const bool ok = !length || (m_impl->readFile(file, bytes.data(), length, &read, nullptr) && read == length);
        if (!ok) { bytes.clear(); error = std::string("Cannot read packed Retail asset: ") + name; return -1; }
        return 1;
    }
    return 0;
}
}
