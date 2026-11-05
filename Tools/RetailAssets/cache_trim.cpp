#include "Engine/Source/Retail/DecodedCache.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>

namespace fs = std::filesystem;
bool HashName(const std::string& name)
{
    return name.size() == 32 && name.find_first_not_of("0123456789abcdef") == std::string::npos;
}
int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3 || (argc == 3 && std::string(argv[2]) != "--apply"))
    {
        std::fprintf(stderr, "Usage: CdnCacheTrim <Data/Retail/Cache> [--apply]; close clients/downloaders first\n");
        return 2;
    }
    try
    {
        const auto root = fs::canonical(argv[1]);
        if (root.filename() != "Cache" || root.parent_path().filename() != "Retail")
            throw std::runtime_error("Expected the explicit Data/Retail/Cache directory");
        std::vector<fs::path> archives;
        uint64_t bytes = 0;
        size_t originals = 0;
        for (const auto& file : fs::recursive_directory_iterator(root))
        {
            if (GetFileAttributesW(file.path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)
                throw std::runtime_error("Refusing a cache containing links/junctions: " + file.path().string());
            if (!file.is_regular_file())
                continue;
            if (file.path().extension() == ".bin")
            {
                std::vector<uint8_t> decoded;
                if (!Retail::ReadDecodedCache(file.path(), decoded))
                    throw std::runtime_error("Invalid decoded original; nothing removed: " + file.path().string());
                ++originals;
            }
            const auto name = file.path().filename().string();
            if (!HashName(name))
                continue;
            const auto parent = file.path().parent_path();
            if (parent.filename() != name.substr(2, 2) || parent.parent_path().filename() != name.substr(0, 2)
                || parent.parent_path().parent_path().filename() != "data")
                continue;
            auto index = file.path();
            index += ".index";
            if (!fs::is_regular_file(index))
                continue; // Standalone encoded assets and metadata must remain available.
            std::ifstream input(index, std::ios::binary | std::ios::ate);
            if (input.tellg() < 28)
                throw std::runtime_error("Invalid archive index: " + index.string());
            unsigned char footer[28];
            input.seekg(-28, std::ios::end);
            input.read(reinterpret_cast<char*>(footer), sizeof(footer));
            if (!input || footer[8] != 1 || footer[12] != 4 || footer[13] != 4 || footer[14] != 16)
                throw std::runtime_error("Unrecognized archive index: " + index.string());
            archives.push_back(file.path());
            bytes += file.file_size();
        }
        if (!originals)
            throw std::runtime_error("No validated decoded originals; refusing to trim");
        std::printf("Validated %zu decoded originals; %zu archive containers; %llu bytes reclaimable.\n",
            originals, archives.size(), static_cast<unsigned long long>(bytes));
        std::fflush(stdout);
        if (argc == 3)
        {
            for (const auto& archive : archives)
            {
                if (!fs::remove(archive))
                    throw std::runtime_error("Cannot remove archive: " + archive.string());
                std::printf("Removed %s\n", archive.string().c_str());
            }
            std::printf("Removed %zu archive containers; decoded originals, individual slices and metadata retained.\n",
                archives.size());
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
