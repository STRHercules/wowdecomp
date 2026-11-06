#ifndef ENGINE_SOURCE_RETAIL_PACKEDARCHIVES_H
#define ENGINE_SOURCE_RETAIL_PACKEDARCHIVES_H
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Retail {
// Standalone tools use the same root patch-*.MPQ originals as the client's SFile reader.
class PackedArchives {
public:
    PackedArchives();
    ~PackedArchives();
    bool Open(const std::filesystem::path& configDirectory, const std::filesystem::path& sourcePath,
        std::string& error);
    int Read(const char* name, std::vector<uint8_t>& bytes, std::string& error, uint32_t* size);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
#endif
