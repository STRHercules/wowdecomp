#ifndef ENGINE_SOURCE_RETAIL_DECODEDCACHE_H
#define ENGINE_SOURCE_RETAIL_DECODEDCACHE_H
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Retail {
// Named CASC assets can exceed MAX_PATH, especially below a task-local cache.
inline std::filesystem::path AssetDiskPath(const std::filesystem::path& path) {
#ifdef _WIN32
    auto value = std::filesystem::absolute(path).lexically_normal().make_preferred().wstring();
    if (value.compare(0, 4, L"\\\\?\\") == 0) return value;
    return value.compare(0, 2, L"\\\\") == 0 ? L"\\\\?\\UNC\\" + value.substr(2) : L"\\\\?\\" + value;
#else
    return path;
#endif
}
// Completed entries only: bounded payload, exact length and checksum. Cache keys include
// build/product, locale and read policy at the caller. No prepared client bytes are stored.
bool ReadDecodedCache(const std::filesystem::path& path, std::vector<uint8_t>& bytes);
bool WriteDecodedCache(const std::filesystem::path& path, const std::vector<uint8_t>& bytes, std::string& error);
}
#endif
