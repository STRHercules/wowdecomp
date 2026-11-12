#include "Engine/Source/Retail/CascSource.h"
#include "Engine/Source/Retail/DecodedCache.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    const auto temp = std::filesystem::temp_directory_path() / ("hots-source-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temp);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{temp};
    std::string error;
    try {
        const auto longPath = temp / std::string(120, 'a') / std::string(120, 'b') / "asset.bin";
        std::vector<uint8_t> cached;
        Check(Retail::WriteDecodedCache(longPath, {1, 2, 3}, error)
            && Retail::ReadDecodedCache(longPath, cached) && cached == std::vector<uint8_t>({1, 2, 3}), "long named-asset cache roundtrip");
    {
        Check(Retail::IsCdnPath("HOTS\\Assets\\test.m3") && !Retail::IsCdnPath("hotsx/test.m3"), "HotS namespace boundary");
        Retail::SourceScope scope(Retail::SourceKind::HotS);
        Check(std::string(Retail::SourcePrefix()) == "hots/", "HotS source scope");
        const auto assets = temp / "hots";
        std::filesystem::create_directories(assets / "assets");
        const std::string original = "43DM-native-source";
        for (const char* name : {"test.m3", "test.m3a", "test.dds", "123.m3"})
            std::ofstream(assets / "assets" / name, std::ios::binary) << original;
        std::ofstream(temp / "hots-paths.csv") << "hots/assets/alias.dds;assets/test.dds\n";
        std::ofstream(temp / "hots.ini") << "mode=files\npath=hots\nproduct=hero\nbuild=0123456789abcdef0123456789abcdef\npaths=hots-paths.csv\n";
        Retail::CascSource hots(Retail::SourceKind::HotS);
        Check(hots.Configure((temp / "hots.ini").string().c_str(), error), error.c_str());
        Check(hots.Handles("HOTS\\Assets\\test.m3") && !hots.Handles("retail/test.m3"), "HotS source isolation");
        for (const char* name : {"hots/assets/test.m3", "hots/assets/test.m3a", "hots/assets/test.dds", "hots/assets/123.m3", "hots/assets/alias.dds"}) {
            std::vector<uint8_t> bytes;
            uint32_t size = 0;
            Check(hots.Exists(name, error) && hots.CachedOriginalSize(name, size) && size == original.size(), "named HotS offline metadata");
            Check(hots.Read(name, bytes, error) && std::string(bytes.begin(), bytes.end()) == original, "HotS bytes unchanged by runtime read");
            Check(hots.CachedSize(name, size) && size == original.size(), "HotS exact prepared size");
        }
        std::vector<uint8_t> bytes;
        Check(!hots.Read("hots/../assets/test.m3", bytes, error) && bytes.empty(), "HotS traversal rejected");
        Check(!hots.Read("retail/assets/test.m3", bytes, error), "HotS rejects foreign source");
        Retail::CascSource wrongProduct;
        Check(!wrongProduct.Configure((temp / "hots.ini").string().c_str(), error), "WoW provider rejects hero product");
        std::ofstream(temp / "hots-paths.csv") << "hots/assets/alias.dds;../escape.dds\n";
        Retail::CascSource unsafe(Retail::SourceKind::HotS);
        Check(!unsafe.Configure((temp / "hots.ini").string().c_str(), error), "HotS mapping traversal rejected");
    }
        if (argc == 3) {
            Retail::CascSource installed(Retail::SourceKind::HotS);
            std::vector<uint8_t> original, native;
            Check(installed.Configure(argv[1], error) && installed.Inspect(argv[2], original, error)
                && installed.Read(argv[2], native, error) && original == native && !native.empty(), error.c_str());
            Check(installed.Stats().acquired == 0, "staged native read must stay offline");
            std::cout << "Real staged HotS asset: native read equals original bytes.\n";
        } else Check(argc == 1, "Usage: HotSSourceTests [HotSCasc.ini hots/path.m3|m3a|dds]");
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    std::cout << "HotS source isolation, native bytes, named dependencies and path validation passed.\n";
}
