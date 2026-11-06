#include "Engine/Source/Retail/CascSource.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    const bool expected = std::string(argv[3]) != "-";
    const auto kind = std::string(argv[2]).compare(0, 8, "forever/") == 0
        ? Retail::SourceKind::Forever : Retail::SourceKind::Retail;
    Retail::CascSource source(kind);
    std::string error;
    std::vector<uint8_t> actual;
    const bool read = source.Configure(argv[1], error) && source.Inspect(argv[2], actual, error);
    if (read != expected || source.Stats().acquired) {
        std::fprintf(stderr, "Unexpected result: %s\n", error.c_str()); return 3;
    }
    if (!expected) return 0;
    std::ifstream input(argv[3], std::ios::binary);
    const std::vector<uint8_t> wanted{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    uint32_t size = 0;
    if (!input.is_open() || actual != wanted || !source.Exists(argv[2], error)
        || !source.CachedOriginalSize(argv[2], size) || size != wanted.size()
        || source.Stats().reused != 1) return 4;
    return 0;
}
