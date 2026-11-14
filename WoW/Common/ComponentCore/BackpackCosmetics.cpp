#include "BackpackCosmetics.h"
#include "CharacterComponent.h"
#include "RetailRace.h"
#include "Engine/Source/Retail/BackpackCatalog.h"
#include "Engine/Source/Retail/AttachmentBindingPolicy.h"
#include "Engine/Source/Retail/CloakAnchor.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Model2/M2Shared.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/ItemDisplayInfoRec.h"
#include "Engine/Source/BlizzardCore/Source/System/Log/Log.h"
#include <cstring>
#include <mutex>
#include <set>
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"

namespace {
constexpr uint32_t BackpackSocket = 0xfffffffdu;
struct State { CM2Model* parent = nullptr; const M2Data* data = nullptr; uint32_t display = 0; bool fitted = false; };
std::map<CCharacterComponent*, State> states;
std::mutex stateMutex;
std::mutex fitMutex;
std::map<std::pair<std::string,uint32_t>,Retail::BackpackFit> fitProfiles;
std::string BodyName(const char* name) {
    std::string result=name;
    std::replace(result.begin(),result.end(),'\\','/');
    std::transform(result.begin(),result.end(),result.begin(),[](unsigned char c){return char(std::tolower(c));});
    return result;
}

const Retail::BackpackAppearance* Appearance(uint32_t display) {
    static const auto catalog = [] {
        std::map<uint32_t, Retail::BackpackAppearance> result;
        void* data = nullptr;
        uint32_t size = 0;
        if (SFile::LoadFile("EsteriaCosmetics\\Backpacks.csv", &data, &size, 0, nullptr)) {
            if (size <= 1024 * 1024)
                Retail::ParseBackpacks(std::string(static_cast<const char*>(data), size), result);
            SFileUnloadFile(data);
        }
        Blizzard::Log::Printf(1, "Logs/Cosmetics.log", "Loaded %u backpack appearances\n", unsigned(result.size()));
        return result;
    }();
    const auto found = catalog.find(display);
    return found == catalog.end() ? nullptr : &found->second;
}

CM2Model* Child(CM2Model* parent) {
    for (auto* child = parent->m_attachList; child; child = child->m_attachNext)
        if (child->m_attachmentId == BackpackSocket) return child;
    return nullptr;
}

bool Anchor(CM2Model* parent, uint32_t geoset, Retail::CloakAnchor& anchor) {
    const auto& data = *parent->m_shared->m_data;
    const auto& skin = *parent->m_shared->m_skinData;
    std::vector<Retail::CloakVertex> vertices;
    std::vector<std::array<float, 3>> surface;
    const auto collect = [&](CM2Model* source) {
      const auto& sourceData = *source->m_shared->m_data;
      const auto& sourceSkin = *source->m_shared->m_skinData;
      for (uint32_t i = 0; i < sourceSkin.m_skinSections.m_count; ++i) {
        const auto& section = sourceSkin.m_skinSections[i];
        const uint32_t id = section.m_skinSectionId & 0xffff;
        const bool cloak = id == geoset;
        if (!cloak && (!source->m_skinSections[i] || id >= 1500)) continue;
        const uint32_t end = uint32_t(section.m_vertexStart) + section.m_vertexCount;
        if (end > sourceSkin.m_vertices.m_count) return false;
        for (uint32_t j = section.m_vertexStart; j < end; ++j) {
            if (sourceSkin.m_vertices[j] >= sourceData.m_vertices.m_count) return false;
            const auto& v = sourceData.m_vertices[sourceSkin.m_vertices[j]];
            surface.push_back({v.m_position.x, v.m_position.y, v.m_position.z});
            if (!cloak) continue;
            Retail::CloakVertex sample{{v.m_position.x,v.m_position.y,v.m_position.z},{},{}};
            for (unsigned k = 0; k < 4; ++k) {
                uint16_t bone = v.m_indices[k];
                // The section palette is authoritative while a shared model is still preparing its draw buffers.
                if (v.m_weights[k] && j < sourceSkin.m_bones.m_count) {
                    const uint32_t combo = uint32_t(section.m_boneComboIndex) + sourceSkin.m_bones[j][k];
                    if (combo >= sourceData.m_boneCombos.m_count) return false;
                    bone = sourceData.m_boneCombos[combo];
                }
                if (source != parent && v.m_weights[k]) {
                    if (bone >= sourceData.m_bones.m_count) return false;
                    const auto& sourceBone = sourceData.m_bones[bone];
                    uint16_t mapped = 0xffff;
                    if (sourceData.m_bones.m_count == data.m_bones.m_count &&
                        bone < data.m_bones.m_count && data.m_bones[bone].m_boneNameCRC == sourceBone.m_boneNameCRC)
                        mapped = bone;
                    else if (sourceBone.m_boneNameCRC)
                        for (uint32_t n = 0; n < data.m_bones.m_count; ++n)
                            if (data.m_bones[n].m_boneNameCRC == sourceBone.m_boneNameCRC) { mapped = uint16_t(n); break; }
                    if (mapped == 0xffff) return false;
                    bone = mapped;
                }
                sample.bones[k] = bone;
                sample.weights[k] = v.m_weights[k];
            }
            vertices.push_back(sample);
        }
      }
      return true;
    };
    if (!collect(parent)) return false;
    if (vertices.empty())
        for (auto* child = parent->m_attachList; child; child = child->m_attachNext)
            if (child->IsLoaded(0,0) && child->HasSkinParent(parent) && child->m_shared->m_skinData)
                if (!collect(child)) return false;
    if (!Retail::FindCloakAnchor(vertices, data.m_bones.m_count, anchor)) return false;
    return Retail::FitCloakBack(surface, anchor);
}

void HideCloak(CM2Model* parent) {
    parent->SetGeometryVisible(1500,1599,0);
    for (auto* child = parent->m_attachList; child; child = child->m_attachNext)
        if (child->m_attachmentId != BackpackSocket && child->HasSkinParent(parent))
            child->SetGeometryVisible(1500,1599,0);
}

bool Sync(CM2Model* parent, const Retail::BackpackAppearance* appearance, uint32_t geoset, bool refit) {
    if (!parent || !parent->IsLoaded(0, 0) || !parent->m_shared->m_skinData) return false;
    auto* child = Child(parent);
    if (child && (!appearance || !child->m_shared ||
        _stricmp(child->m_shared->m_fileName, appearance->model.c_str()))) {
        child->DetachFromParent();
        child = nullptr;
    }
    if (!appearance) return false;
    if (child && Retail::ReuseBackpackFit(child->IsLoaded(0,0), refit, child->HasAttachmentFrame())) {
        HideCloak(parent);
        return true;
    }
    Retail::CloakAnchor anchor;
    if (!Anchor(parent, geoset, anchor)) return false;
    Retail::BackpackFit fit=appearance->fit;
    {
        std::lock_guard<std::mutex> lock(fitMutex);
        const auto found=fitProfiles.find({BodyName(parent->m_shared->m_fileName),appearance->display});
        if (found!=fitProfiles.end()) fit=found->second;
    }
    const float scale = std::max(0.5f, std::min(3.0f, anchor.width / 0.373f))*fit.scale;
    const C3Vector position(anchor.position[0]+fit.x, anchor.position[1]+fit.y, anchor.position[2]+fit.z);
    if (!child) {
        child = parent->m_scene->CreateModel(appearance->model.c_str(), 0);
        if (!child) return false;
        // A private root attachment avoids detaching weapons or auras sharing the stock back socket.
        child->AttachToParent(parent, BackpackSocket, nullptr, 1);
        if (!child->SetAttachmentFrame(anchor.bone, position, scale)) child->DetachFromParent();
        child->Release();
        child = Child(parent);
    } else child->SetAttachmentFrame(anchor.bone, position, scale);
    if (!child || !child->IsLoaded(0, 0)) return false;
    HideCloak(parent);
    return true;
}
}

void EsteriaBackpackEquipment(CCharacterComponent* character) {
    std::lock_guard<std::mutex> lock(stateMutex);
    const uint32_t display = RetailRaceHidesArmor(character) ? 0 : character->m_itemDisplayID[ITEMSLOT_CAPE];
    const auto* appearance = Appearance(display);
    auto* parent = character->m_data.m_model;
    const auto found = states.find(character);
    if (!appearance && found == states.end()) return;
    if (!parent || !parent->IsLoaded(0,0)) return;
    auto& state = states[character];
    const bool changed = state.parent != parent || state.data != parent->m_shared->m_data || state.display != display;
    if (changed) {
        if (auto* old = Child(parent)) old->DetachFromParent();
        state = {parent,parent->m_shared->m_data,display,false};
    }
    ItemDisplayInfoRec cape;
    uint32_t geoset = 1502;
    if (appearance && g_itemDisplayInfoDB.GetRecord(display,&cape)) geoset = 1501 + cape.m_geosetGroup[0];
    // This runs after geometry selection; earlier cached bounds may describe the previous outfit.
    const bool fitted = Sync(parent,appearance,geoset,true);
    if (fitted && !state.fitted)
        Blizzard::Log::Printf(1, "Logs/Cosmetics.log", "Attached display %u to %s using cloak geoset %u\n",
            display,parent->m_shared->m_fileName,geoset);
    state.fitted = fitted;
    if (!appearance) states.erase(character);
}

bool EsteriaBackpackPending(CCharacterComponent* character) {
    std::lock_guard<std::mutex> lock(stateMutex);
    const uint32_t display=RetailRaceHidesArmor(character) ? 0 : character->m_itemDisplayID[ITEMSLOT_CAPE];
    if (!Appearance(display)) return false;
    auto* parent=character->m_data.m_model;
    if (!parent || !parent->IsLoaded(0,0)) return true;
    const auto found=states.find(character);
    auto* child=Child(parent);
    const bool fitted=found!=states.end() && found->second.parent==parent &&
        found->second.data==parent->m_shared->m_data && found->second.display==display && found->second.fitted;
    return Retail::BackpackFitPending(true,child && child->IsLoaded(0,0),fitted,child && child->HasAttachmentFrame());
}

void EsteriaBackpackForget(CCharacterComponent* character) {
    std::lock_guard<std::mutex> lock(stateMutex);
    states.erase(character);
}

bool EsteriaBackpackPreview(CM2Model* model, uint32_t display) {
    return Sync(model,Appearance(display),1502,true);
}

bool EsteriaBackpackSetFit(uint32_t display, const std::string& body, const Retail::BackpackFit& fit) {
    if (!Appearance(display) || body.empty() || body.size()>240 || body.find_first_of(";\r\n")!=std::string::npos ||
        !Retail::ValidBackpackFit(fit)) return false;
    const auto name=BodyName(body.c_str());
    {
        std::lock_guard<std::mutex> lock(fitMutex);
        fitProfiles[{name,display}]=fit;
    }
    std::lock_guard<std::mutex> lock(stateMutex);
    for (auto& entry:states) {
        auto* parent=entry.first->m_data.m_model;
        if (entry.second.display==display && parent && parent->m_shared && BodyName(parent->m_shared->m_fileName)==name) {
            entry.second.fitted=false;
            entry.first->m_flags|=0x4;
        }
    }
    return true;
}

bool EsteriaBackpackFitInfo(uint32_t& display, std::string& body) {
    auto* player=ClntObjMgrGetActivePlayerObj();
    auto* character=player ? player->m_characterComponent : nullptr;
    auto* parent=character ? character->m_data.m_model : nullptr;
    if (!parent || !parent->m_shared) return false;
    display=character->m_itemDisplayID[ITEMSLOT_CAPE];
    if (!Appearance(display)) return false;
    body=BodyName(parent->m_shared->m_fileName);
    return true;
}
