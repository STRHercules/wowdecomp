#include "WoW/Common/ComponentCore/MountCustomize.h"

#include "Engine/Source/Base/Handle.h"
#include "Engine/Source/Base/Status.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/BlizzardCore/Source/System/Log/Log.h"
#include "Engine/Source/Gx/CGxTexFlags.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Retail/Appearance.h"
#include "Engine/Source/Retail/BusyNotice.h"
#include "Engine/Source/Retail/CascSource.h"
#include "Engine/Source/Retail/Compose.h"
#include "Engine/Source/Retail/RetailModel.h"
#include "Engine/Source/Services/Texture.h"

#include <charconv>
#include <deque>
#include <sstream>

namespace app = Retail::Db2::appearance;

namespace MountCustomize
{
namespace
{
struct Catalog {
    uint32_t chrModel = 0;                             // composed by the appearance resolver
    std::vector<Option> options;                       // client-defined options (no ChrModel)
    std::multimap<uint32_t, std::pair<uint32_t, uint32_t>> textures; // client-defined choice -> (texture type, file)
    std::vector<uint32_t> defaults;
};

uint32_t Number(const std::string& text) {
    uint32_t value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

// Rows (Tools/RetailAssets/mount_custom.py): display;m;chrModel | display;d;choice | display;o;option;name |
// display;c;option;choice;swatchARGB;name | display;t;choice;textureType;fileDataId
const std::map<uint32_t, Catalog>& Catalogs() {
    static const auto catalogs = [] {
        std::map<uint32_t, Catalog> result;
        void* bytes = nullptr;
        uint32_t size = 0;
        if (!SFile::LoadFile("EsteriaMounts\\custom.csv", &bytes, &size, 0, nullptr)) return result;
        std::istringstream lines(std::string(static_cast<const char*>(bytes), size));
        SFileUnloadFile(bytes);
        for (std::string line; std::getline(lines, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            std::vector<std::string> f;
            std::istringstream columns(line);
            for (std::string field; std::getline(columns, field, ';');) f.push_back(field);
            const uint32_t display = f.size() >= 3 ? Number(f[0]) : 0;
            if (!display) continue;
            Catalog& catalog = result[display];
            if (f[1] == "m") catalog.chrModel = Number(f[2]);
            else if (f[1] == "d") catalog.defaults.push_back(Number(f[2]));
            else if (f[1] == "o" && f.size() >= 4) catalog.options.push_back({Number(f[2]), f[3], {}});
            else if (f[1] == "c" && f.size() >= 6) {
                for (auto& option : catalog.options)
                    if (option.id == Number(f[2])) option.choices.push_back({Number(f[3]), f[5], Number(f[4])});
            } else if (f[1] == "t" && f.size() >= 5) catalog.textures.emplace(Number(f[2]), std::make_pair(Number(f[3]), Number(f[4])));
        }
        return result;
    }();
    return catalogs;
}

const Catalog* Find(uint32_t display) {
    const auto found = Catalogs().find(display);
    return found == Catalogs().end() ? nullptr : &found->second;
}

std::map<uint64_t, std::map<uint32_t, std::vector<uint32_t>>>& Looks() {
    static std::map<uint64_t, std::map<uint32_t, std::vector<uint32_t>>> looks;
    return looks;
}

bool FetchBlp(uint32_t fileDataId, std::vector<uint8_t>& bytes, void*) {
    std::string error;
    return Retail::Source().Read(Retail::FileName(fileDataId, ".blp").c_str(), bytes, error);
}

// A composed look: the textures per M2 texture type and the geosets to show.
struct Composed {
    std::map<uint32_t, CTexture*> textures;
    std::vector<uint16_t> geosets;
};

const Composed& Compose(uint32_t chrModel, const std::vector<uint32_t>& choices) {
    static std::map<std::string, Composed> cache;
    static std::deque<std::string> order;
    std::string key = "Retail\\Mount\\" + std::to_string(chrModel);
    for (uint32_t choice : choices) key += ":" + std::to_string(choice);
    const auto found = cache.find(key);
    if (found != cache.end()) return found->second;
    // Keep a few recent looks: the customizer cycles through many while previewing.
    while (order.size() >= 8) {
        for (auto& texture : cache[order.front()].textures) HandleClose(reinterpret_cast<HOBJECT>(texture.second));
        cache.erase(order.front());
        order.pop_front();
    }
    order.push_back(key);
    Composed& composed = cache[key];
    Retail::BusyNotice busy("Preparing mount appearance");
    app::Recipe recipe;
    if (!app::BuildForChrModel(chrModel, choices.data(), uint32_t(choices.size()), recipe)) return composed;
    std::vector<Retail::ComposedTexture> images;
    std::string warnings;
    Retail::ComposeTextures(recipe, FetchBlp, nullptr, images, warnings);
    if (!warnings.empty()) Blizzard::Log::Printf(1, "Logs/RetailAssets.log", "mount compose %s: %s\n", key.c_str(), warnings.c_str());
    for (const auto& image : images) {
        std::vector<uint8_t> blp;
        Retail::BuildBlp(image.image, blp);
        const std::string name = key.substr(0, 200) + "#" + std::to_string(image.textureType)
            + "#" + std::to_string(std::hash<std::string>{}(key));
        CGxTexFlags flags(GxTex_LinearMipNearest, 0, 0, 0, 0, 0, 1);
        if (CTexture* texture = TextureCreateFromBlpData(name.c_str(), flags, blp.data())) composed.textures[image.textureType] = texture;
    }
    composed.geosets = recipe.geosets;
    return composed;
}

void ShowGeosets(CM2Model* model, const std::vector<uint16_t>& geosets) {
    for (const uint16_t id : geosets) {
        const uint32_t group = id / 100;
        // Group 0's mesh 0 is the shared base; its 1..99 are alternative bodies (as in ReplaceMonsterSkin).
        model->SetGeometryVisible(group ? group * 100 : 1, group * 100 + 99, 0);
    }
    for (const uint16_t id : geosets) model->SetGeometryVisible(id, id, 1);
    if (!geosets.empty()) model->OptimizeVisibleGeometry();
}
}

bool Options(uint32_t display, std::vector<Option>& out) {
    out.clear();
    const Catalog* catalog = Find(display);
    if (!catalog) return false;
    if (!catalog->chrModel) {
        out = catalog->options;
        return !out.empty();
    }
    for (uint32_t i = 0, n = app::OptionCount(catalog->chrModel); i < n; ++i) {
        app::ChrOption option{};
        if (!app::OptionAt(catalog->chrModel, i, option)) continue;
        Option entry{option.id, app::OptionName(option.id), {}};
        for (uint32_t j = 0, m = app::ChoiceCount(option.id); j < m; ++j) {
            app::ChrChoice choice{};
            if (!app::ChoiceAt(option.id, j, choice)) continue;
            std::string name = app::ChoiceName(choice.id);
            entry.choices.push_back({choice.id, name.empty() ? std::to_string(j + 1) : name, choice.swatchColor});
        }
        if (!entry.choices.empty()) out.push_back(std::move(entry));
    }
    return !out.empty();
}

std::vector<uint32_t> Defaults(uint32_t display) {
    const Catalog* catalog = Find(display);
    return catalog ? catalog->defaults : std::vector<uint32_t>();
}

void SetLooks(uint64_t guid, std::map<uint32_t, std::vector<uint32_t>> looks) {
    Looks()[guid] = std::move(looks);
}

const std::vector<uint32_t>* Look(uint64_t guid, uint32_t display) {
    const auto player = Looks().find(guid);
    if (player == Looks().end()) return nullptr;
    const auto look = player->second.find(display);
    return look == player->second.end() ? nullptr : &look->second;
}

void ClearLooks() {
    Looks().clear();
}

void Apply(CM2Model* model, uint32_t display, const std::vector<uint32_t>& choices) {
    const Catalog* catalog = model ? Find(display) : nullptr;
    if (!catalog || choices.empty()) return;
    if (catalog->chrModel) {
        const Composed& composed = Compose(catalog->chrModel, choices);
        for (const auto& texture : composed.textures) model->ReplaceTexture(texture.first, texture.second);
        ShowGeosets(model, composed.geosets);
        return;
    }
    CGxTexFlags flags(GxTex_LinearMipLinear, 1, 1, 0, 0, 0, 1);
    for (const uint32_t choice : choices) {
        const auto range = catalog->textures.equal_range(choice);
        for (auto it = range.first; it != range.second; ++it) {
            CStatus status;
            const std::string path = "retail\\" + std::to_string(it->second.second) + ".blp";
            if (CTexture* texture = TextureCreate(path.c_str(), flags, &status, 0)) {
                model->ReplaceTexture(it->second.first, texture);
                HandleClose(reinterpret_cast<HOBJECT>(texture));
            }
        }
    }
}

void ApplyOwner(CM2Model* model, uint32_t display, uint64_t ownerGuid) {
    if (const std::vector<uint32_t>* look = Look(ownerGuid, display)) Apply(model, display, *look);
}
}
