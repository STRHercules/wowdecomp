#include "Engine/Source/Retail/CascSource.h"
#include "Engine/Source/Retail/RetailModel.h"
#include <charconv>
#include <cstdio>
#include <cstring>
#include <functional>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <sstream>

namespace {
uint32_t Id(const char* argument) {
    const std::string s = argument;
    uint32_t value = 0;
    auto result = std::from_chars(s.data(), s.data() + s.size(), value);
    if (!value || result.ec != std::errc() || result.ptr != s.data() + s.size()) throw std::runtime_error("Invalid FileDataID");
    return value;
}
}

int main(int argc, char** argv) {
    if (argc != 5) {
        std::fprintf(stderr, "Usage: RetailAssetAcquire <config> <modelID> <textureID> <new-output-directory>\n");
        return 2;
    }
    try {
        const auto modelId = Id(argv[2]);
        std::vector<uint32_t> textureIds;
        std::istringstream ids(argv[3]); std::string id;
        while (std::getline(ids, id, ',')) textureIds.push_back(Id(id.c_str()));
        if (textureIds.empty()) throw std::runtime_error("At least one display texture is required");
        const std::filesystem::path output(argv[4]);
        if (std::filesystem::exists(output)) throw std::runtime_error("Acquisition output must be a new directory");
        Retail::CascSource source;
        std::string error;
        if (!source.Configure(argv[1], error)) throw std::runtime_error(error);
        std::map<uint32_t, std::vector<uint8_t>> originals;
        auto fetch = [&](uint32_t id, const char* extension) -> const std::vector<uint8_t>& {
            auto found = originals.find(id);
            if (found != originals.end()) return found->second;
            std::vector<uint8_t> bytes;
            if (!source.Inspect(Retail::FileName(id, extension).c_str(), bytes, error)) throw std::runtime_error(error);
            return originals.emplace(id, std::move(bytes)).first->second;
        };
        const auto& model = fetch(modelId, ".m2");
        Retail::ModelFiles graph;
        std::vector<uint8_t> prepared;
        // A shared skeleton (SKID) and its SKPD parents are merged before preparation, as CascSource::LoadSkeleton does.
        std::vector<uint8_t> skeleton;
        std::function<void(uint32_t, std::vector<uint8_t>&, int)> loadSkeleton = [&](uint32_t id, std::vector<uint8_t>& out, int depth) {
            if (depth >= 8) throw std::runtime_error("Retail skeleton parent cycle or depth limit");
            out = fetch(id, ".skel");
            uint32_t parent = 0;
            for (size_t p = 0; p + 8 <= out.size();) {
                uint32_t tag, size;
                std::memcpy(&tag, out.data() + p, 4);
                std::memcpy(&size, out.data() + p + 4, 4);
                p += 8;
                if (size > out.size() - p) throw std::runtime_error("Retail skeleton chunk exceeds file size");
                if (tag == 0x44504b53 && size >= 12) std::memcpy(&parent, out.data() + p + 8, 4); // SKPD
                p += size;
            }
            if (parent) {
                std::vector<uint8_t> ancestor, merged;
                loadSkeleton(parent, ancestor, depth + 1);
                if (!Retail::MergeSkeleton(out, ancestor, merged, error)) throw std::runtime_error(error);
                out.swap(merged);
            }
        };
        Retail::ModelFiles inspected;
        if (Retail::InspectModel(model, inspected, error) && inspected.skeleton) loadSkeleton(inspected.skeleton, skeleton, 0);
        if (!Retail::PrepareModel(model, prepared, graph, error, skeleton.empty() ? nullptr : &skeleton))
            throw std::runtime_error(error);
        // Validate temporary copies. Persist only the original decoded source bytes.
        for (size_t i = 0; i < graph.skins.size(); ++i) {
            auto copy = fetch(graph.skins[i], ".skin");
            // SFID also advertises Retail-only LOD profiles beyond numSkinProfiles.
            // Preserve those originals, while validating the profiles this engine opens.
            if (i < prepared[0x44] && !Retail::PrepareSkin(copy, prepared, error)) throw std::runtime_error(error);
        }
        for (const auto& anim : graph.animations) {
            if (!anim.fileId) continue;
            auto copy = fetch(anim.fileId, ".anim"); // validate a copy; the stored original keeps its AFM2 chunk header
            if (!Retail::StripAnimationChunk(copy, error) || !Retail::ValidateAnimation(copy, prepared, anim.id, anim.variation, error))
                throw std::runtime_error(error);
        }
        graph.textures.insert(graph.textures.end(), textureIds.begin(), textureIds.end());
        for (auto texture : graph.textures) if (texture) {
            const auto& bytes = fetch(texture, ".blp");
            if (bytes.size() < 148 || std::string(reinterpret_cast<const char*>(bytes.data()), 8) != std::string("BLP2\1\0\0\0", 8))
                throw std::runtime_error("Unsupported texture layout");
        }
        std::filesystem::create_directories(output);
        for (const auto& file : originals) {
            std::ofstream stream(output / (std::to_string(file.first) + ".bin"), std::ios::binary);
            if (!stream.write(reinterpret_cast<const char*>(file.second.data()), file.second.size()))
                throw std::runtime_error("Cannot write original asset");
        }
        std::printf("Acquired %zu original files; model %u; no converted assets written.\n", originals.size(), modelId);
        return 0;
    } catch (const std::exception& exception) {
        std::fprintf(stderr, "%s\n", exception.what());
        return 1;
    }
}
