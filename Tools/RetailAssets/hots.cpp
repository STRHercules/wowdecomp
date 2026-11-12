// Original HotS acquisition through the same pinned CASC/cache provider as the client.
#include "Engine/Source/Retail/CascSource.h"
#include "Engine/Source/Retail/DecodedCache.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc != 5) {
        std::fprintf(stderr, "Usage: HotSAssetTool <config> list <mask> <output.txt>\n"
            "       HotSAssetTool <config> fetch <paths.txt> <output-directory>\n");
        return 2;
    }
    try {
        Retail::CascSource source(Retail::SourceKind::HotS);
        std::string error;
        if (!source.Configure(argv[1], error)) throw std::runtime_error(error);
        const std::string command = argv[2];
        if (command == "list") {
            std::vector<std::string> names;
            if (!source.List(argv[3], names, error)) throw std::runtime_error(error);
            std::ofstream output(argv[4]);
            for (const auto& name : names) output << name << '\n';
            output.close();
            if (!output) throw std::runtime_error("Cannot save source name list");
            std::printf("Listed %zu source paths\n", names.size());
        } else if (command == "fetch") {
            std::ifstream input(argv[3]);
            if (!input) throw std::runtime_error("Cannot read requested paths");
            const auto root = std::filesystem::absolute(argv[4]);
            unsigned failures = 0;
            for (std::string name; std::getline(input, name);) {
                if (!name.empty() && name.back() == '\r') name.pop_back();
                if (name.empty()) continue;
                for (auto& c : name) if (c == '\\') c = '/';
                // Inspect validates virtual paths before they can reach the output directory.
                if (name.compare(0, 5, "hots/") != 0) throw std::runtime_error("Expected hots/ source path");
                std::vector<uint8_t> bytes;
                if (!source.Inspect(name.c_str(), bytes, error)) {
                    std::cerr << "FAILED\t" << name << '\t' << error << '\n';
                    ++failures;
                    continue;
                }
                const auto target = Retail::AssetDiskPath(root / name.substr(5));
                std::filesystem::create_directories(target.parent_path());
                if (std::filesystem::exists(target)) {
                    std::ifstream previous(target, std::ios::binary);
                    std::vector<uint8_t> old{std::istreambuf_iterator<char>(previous), {}};
                    if (!previous || old != bytes) throw std::runtime_error("Existing staged asset differs: " + target.string());
                } else {
                    const auto temporary = target.string() + ".tmp";
                    std::ofstream output(temporary, std::ios::binary);
                    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                    output.close();
                    if (!output) throw std::runtime_error("Cannot stage source asset");
                    std::filesystem::rename(temporary, target);
                }
                std::cout << "OK\t" << name << '\t' << bytes.size() << std::endl;
            }
            return failures ? 1 : 0;
        } else throw std::runtime_error("Unknown command");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
