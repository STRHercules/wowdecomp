#include "WoW/Source/Extension/ExtensionMgr.h"
#if defined(WOWDECOMP_RETAIL_ASSETS)
#include "Engine/Source/Frame/CSimpleModel.h"
#include "Engine/Source/Base/Coordinate.h"
#include "Engine/Source/Base/Handle.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Cache.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Retail/CloakAnchor.h"
#include "Engine/Source/Retail/CosmeticPreviewFit.h"
#include "WoW/Source/DB/Rec/SpellRec.h"
#include "WoW/Source/DB/Rec/SpellVisualRec.h"
#include "WoW/Source/DB/Rec/SpellVisualEffectNameRec.h"
#include "WoW/Source/DB/Rec/SpellVisualKitModelAttachRec.h"
#include "WoW/Source/Magic/MagicClient/Spell_C.h"
#include "WoW/Source/Magic/MagicClient/SpellVisuals.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "Engine/Source/Model2/M2Shared.h"
#include "Engine/Source/Retail/RetailModel.h"
#include "Engine/Source/Retail/CascSource.h"
#include "Engine/Source/Retail/Ethereal.h"
#include "WoW/Common/ComponentCore/CharacterComponent.h"
#include "WoW/Common/ComponentCore/BackpackCosmetics.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/ItemDisplayInfoRec.h"
#include "WoW/Source/Glue/CharacterSelection.h"
#include "Engine/Source/Services/Camera.h"
#include "Engine/Source/Services/CameraInt.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/Services/GfxSingletonManager.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include <charconv>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <map>
#include <filesystem>
#include <fstream>
#include <ctime>
#include "Engine/Source/Retail/BackpackFitExport.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"

namespace {
bool ParseEquipment(const char* arguments, Retail::HandItemAppearance& appearance) {
    std::istringstream input(arguments ? arguments : "");
    uint32_t ids[2] = {};
    std::string token;
    for (auto& id : ids) {
        if (!(input >> token)) return 0;
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), id);
        if (!id || parsed.ec != std::errc() || parsed.ptr != token.data() + token.size()) return 0;
    }
    if (input >> token) return 0;
    appearance = {ids[0], ids[1]};
    return true;
}

int32_t EquipPreview(const char*, const char* arguments) {
    Retail::HandItemAppearance appearance;
    if (!ParseEquipment(arguments, appearance)) return 0;
    const int32_t index = CCharacterSelection::s_selectionIndex;
    if (index < 0 || index >= static_cast<int32_t>(CCharacterSelection::s_characterList.Count())) return 0;
    auto& selected = CCharacterSelection::s_characterList[index];
    if (!selected.m_component || !selected.m_component->m_data.m_model ||
        !selected.m_component->m_data.m_model->IsLoaded(0, 0)) return 0;
    const auto displayId = selected.m_characterInfo.m_inventoryItemDisplayID[INVSLOT_MAINHAND];
    ItemDisplayInfoRec display;
    if (!displayId || !g_itemDisplayInfoDB.GetRecord(displayId, &display)) return 0;
    if (!Retail::Source().SetHandItem(displayId, appearance)) return 0;
    const auto link = CCharacterComponent::AddHandItem(selected.m_component->m_data.m_model,
        &display, INVSLOT_MAINHAND, SHEATHE_NONE, false, false, false,
        selected.m_characterInfo.m_inventoryItemVisualID[INVSLOT_MAINHAND]);
    sExtensionMgr->Log("Retail equipped preview: Wrath display %u -> model %u, texture %u; hand link %u. Persist with equipment CSV row: %u;%u;%u",
        displayId, appearance.modelId, appearance.textureId, static_cast<uint32_t>(link), displayId, appearance.modelId, appearance.textureId);
    FrameScript_Execute("if RetailModelPreview then RetailModelPreview:Hide() end", "RetailPreview", nullptr);
    return 1;
}

int BindTexture(lua_State* L) {
    auto* frame = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, CSimpleModel::GetObjectType()));
    Retail::SourceScope scope(frame->m_model && !_strnicmp(frame->m_model->m_shared->m_fileName, "forever/", 8)
        ? Retail::SourceKind::Forever : Retail::SourceKind::Retail);
    const int id = luaL_checkint(L, 2), type = luaL_checkint(L, 3);
    if (id <= 0 || type < 0 || type > 14) return luaL_error(L, "Invalid Retail texture ID or replaceable texture type");
    frame->ReplaceTexture(static_cast<uint32_t>(type), Retail::FileName(static_cast<uint32_t>(id), ".blp").c_str());
    return 0;
}

int BindDisplay(lua_State* L) {
    auto* frame = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, CSimpleModel::GetObjectType()));
    const auto* display = g_creatureDisplayInfoDB.GetRecord(luaL_checkint(L, 2));
    const auto* model = display ? g_creatureModelDataDB.GetRecord(display->m_modelID) : nullptr;
    if (frame->m_model && display && model) CCharacterComponent::ReplaceMonsterSkin(frame->m_model, display, model);
    return 0;
}

int ReportLoaded(lua_State* L) {
    auto* frame = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, CSimpleModel::GetObjectType()));
    if (!frame->m_model || !frame->m_model->IsLoaded(0, 0)) { lua_pushboolean(L, false); return 1; }
    const auto* shared = frame->m_model->m_shared;
    if (const uint32_t id = Retail::Ethereal::PathModel(shared->m_fileName)) {
        Retail::Db2::appearance::Recipe recipe;
        const uint32_t form = id == Retail::Ethereal::Bodies[2]
            ? Retail::Db2::appearance::FormDreadnaught : Retail::Db2::appearance::FormDarkweaver;
        if (Retail::Ethereal::Build(id == Retail::Ethereal::Bodies[1], nullptr, 0, recipe, form)) {
            frame->m_model->SetGeometryVisible(0, 65535, 0);
            for (uint16_t geoset : recipe.geosets) frame->m_model->SetGeometryVisible(geoset, geoset, 1);
            for (const auto& layer : recipe.layers)
                frame->ReplaceTexture(layer.textureType, Retail::FileName(layer.fileDataId, ".blp").c_str());
            frame->m_model->SetEmittersEnabled(1);
            frame->m_model->SetRibbonsEnabled(1);
            if (form == Retail::Db2::appearance::FormDreadnaught)
                Retail::Ethereal::ApplyEffects(*frame->m_model, nullptr, 0);
        }
    }
    // A bare Model frame has no studio light or embedded model camera. Fit the
    // actual bounds in a separate perspective camera instead of using UI-pixel scale.
    frame->m_facing = 0;
    const float uiScale = frame->m_layoutScale * NDCToDDCHeight(1.0f) * 1.6666666f;
    frame->m_modelScale = uiScale > 0 ? 1.0f / uiScale : 1.0f;
    frame->m_position = C3Vector(0, 0, 0);
    const auto& box = shared->m_data->m_bounds.m_extent;
    const C3Vector center((box.b.x + box.t.x) * 0.5f, (box.b.y + box.t.y) * 0.5f, (box.b.z + box.t.z) * 0.5f);
    const float radius = Retail::Ethereal::PathModel(shared->m_fileName)
        ? 1.8f : std::max(0.1f, shared->m_data->m_bounds.m_radius);
    if (CCamera* camera = CameraCreate()) {
        CameraSetCoord(camera, 7, C3Vector(center.x + radius * 2.5f, center.y - radius * 2, center.z + radius));
        CameraSetCoord(camera, 8, center);
        CameraSetFloat(camera, 4, 0.8f);
        CameraSetFloat(camera, 3, std::max(0.01f, radius * 0.01f));
        CameraSetFloat(camera, 2, radius * 20);
        frame->SetCamera(camera);
        HandleClose(reinterpret_cast<HOBJECT>(camera));
    }
    frame->m_light.SetLightType(M2LIGHT_DIRECT);
    frame->m_light.SetDirection(C3Vector(-0.5f, -0.8f, -1));
    frame->m_light.m_ambColor = C3Vector(0.7f, 0.7f, 0.7f);
    frame->m_light.m_dirColor = C3Vector(0.6f, 0.6f, 0.6f);
    frame->m_light.m_specColor = C3Vector(0.2f, 0.2f, 0.2f);
    frame->m_light.SetVisible(1);
    sExtensionMgr->Log("Retail model loaded by engine: %s; %u vertices, %u bones, %u textures, %u skin sections",
        shared->m_fileName, shared->m_data->m_vertices.m_count, shared->m_data->m_bones.m_count,
        shared->m_data->m_textures.m_count, shared->m_skinData->m_skinSections.m_count);
    lua_pushboolean(L, true);
    return 1;
}

// RetailModelFit(modelFrame[, zoom]) -> true once the model has loaded and is framed. Frames the bind-pose vertex box
// rather than the header radius, which converted models inflate with effect and collision extents (tiny previews).
// zoom > 1 moves the camera closer. Used by the incarnation picker's cards and stage.
int Fit(lua_State* L) {
    auto* frame = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, CSimpleModel::GetObjectType()));
    const float zoom = std::max(0.2f, std::min(5.0f, static_cast<float>(luaL_optnumber(L, 2, 1.0))));
    if (!frame->m_model || !frame->m_model->IsLoaded(0, 0)) { lua_pushboolean(L, false); return 1; }
    const auto* data = frame->m_model->m_shared->m_data;
    C3Vector lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
    for (uint32_t i = 0; i < data->m_vertices.m_count; ++i) {
        const C3Vector& p = data->m_vertices[i].m_position;
        lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); lo.z = std::min(lo.z, p.z);
        hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); hi.z = std::max(hi.z, p.z);
    }
    if (!data->m_vertices.m_count) { lo = data->m_bounds.m_extent.b; hi = data->m_bounds.m_extent.t; }
    const C3Vector center((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f);
    const float dx = hi.x - lo.x, dy = hi.y - lo.y, dz = hi.z - lo.z;
    const float radius = std::max(0.1f, 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz));
    const float fov = 0.8f, distance = radius / std::sin(fov * 0.5f) * 1.02f / zoom;
    // front three-quarter view, slightly above
    C3Vector dir(0.84f, -0.46f, 0.28f);
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    frame->m_facing = 0;
    const float uiScale = frame->m_layoutScale * NDCToDDCHeight(1.0f) * 1.6666666f;
    frame->m_modelScale = uiScale > 0 ? 1.0f / uiScale : 1.0f;
    frame->m_position = C3Vector(0, 0, 0);
    if (CCamera* camera = CameraCreate()) {
        CameraSetCoord(camera, 7, C3Vector(center.x + dir.x / len * distance, center.y + dir.y / len * distance,
                                           center.z + dir.z / len * distance));
        CameraSetCoord(camera, 8, center);
        CameraSetFloat(camera, 4, fov);
        CameraSetFloat(camera, 3, std::max(0.01f, radius * 0.01f));
        CameraSetFloat(camera, 2, radius * 40);
        frame->SetCamera(camera);
        HandleClose(reinterpret_cast<HOBJECT>(camera));
    }
    frame->m_light.SetLightType(M2LIGHT_DIRECT);
    frame->m_light.SetDirection(C3Vector(-0.5f, -0.8f, -1));
    frame->m_light.m_ambColor = C3Vector(0.75f, 0.75f, 0.75f);
    frame->m_light.m_dirColor = C3Vector(0.65f, 0.65f, 0.65f);
    frame->m_light.m_specColor = C3Vector(0.2f, 0.2f, 0.2f);
    frame->m_light.SetVisible(1);
    lua_pushboolean(L, true);
    return 1;
}

int32_t Preview(const char*, const char* arguments) {
    if (!FrameScript_GetContext()) return 0;
    if (arguments && std::string(arguments) == "hide") {
        FrameScript_Execute("if RetailModelPreview then RetailModelPreview:Hide() end", "RetailPreview", nullptr);
        return 1;
    }
    std::istringstream input(arguments ? arguments : "");
    uint32_t values[4] = {0, 0, 2, 0};
    std::string token;
    size_t count = 0;
    auto kind = Retail::SourceKind::Retail;
    while (input >> token) {
        if (count == 4) return 0;
        if (!count && token.compare(0, 8, "forever/") == 0) { kind = Retail::SourceKind::Forever; token.erase(0, 8); }
        auto parsed = std::from_chars(token.data(), token.data() + token.size(), values[count]);
        if (parsed.ec != std::errc() || parsed.ptr != token.data() + token.size()) return 0;
        ++count;
    }
    if (!count || !values[0] || values[2] > 14) {
        sExtensionMgr->Log("Usage: retailmodel <model FileDataID> [texture FileDataID [texture type, default 2]]; retailmodel hide");
        return 0;
    }
    Retail::SourceScope scope(kind);
    const bool character = std::getenv("WOW_RETAIL_PREVIEW_CHARACTER") != nullptr;
    if (character) Retail::Source().RegisterCharacterModel(values[0]);
    const auto modelPath = character && kind == Retail::SourceKind::Retail && Retail::Ethereal::Model(values[0])
        ? Retail::Ethereal::Path(values[0]) : Retail::FileName(values[0], ".m2");
    char script[2300];
    std::snprintf(script, sizeof(script),
        "local f=RetailModelPreview or CreateFrame('Model','RetailModelPreview'); "
        "f:SetWidth(650); f:SetHeight(650); f:SetPoint('CENTER'); f:SetFrameStrata('TOOLTIP'); "
        "f:SetModel('%s'); f:SetFacing(0.8); f:SetCamera(0); f:Show(); "
        "f.retailTime=0; f.retailTexture=%u; f.retailType=%u; f.retailTexture2=%u; "
        "f:SetScript('OnUpdate',function(self,dt) "
        "self.retailTime=self.retailTime+dt; "
        "if RetailModelReportLoaded(self) then "
        "if self.retailTexture>0 then RetailModelBindTexture(self,self.retailTexture,self.retailType) end; "
        "if self.retailTexture2>0 then RetailModelBindTexture(self,self.retailTexture2,12) end; "
        "self:SetScript('OnUpdate',nil) "
        "elseif self.retailTime>30 then self:SetScript('OnUpdate',nil) end end)",
        modelPath.c_str(), values[1], values[2], values[3]);
    sExtensionMgr->Log("Retail preview requested: model %u, texture %u, type %u", values[0], values[1], values[2]);
    FrameScript_Execute(script, "RetailPreview", nullptr);
    return 1;
}

int32_t MountPreview(const char*, const char* arguments) {
    const std::string value = arguments ? arguments : "";
    uint32_t id = 0;
    auto parsed = std::from_chars(value.data(), value.data() + value.size(), id);
    if (!id || parsed.ec != std::errc() || parsed.ptr != value.data() + value.size()) return 0;
    const auto* display = g_creatureDisplayInfoDB.GetRecord(id);
    const auto* model = display ? g_creatureModelDataDB.GetRecord(display->m_modelID) : nullptr;
    if (!display || !model) return 0;
    uint32_t modelId = 0, textures[2] = {};
    if (std::sscanf(model->m_modelName, "retail\\%u.m2", &modelId) != 1) return 0;
    for (unsigned i = 0; i < 2; ++i) if (std::sscanf(display->m_textureVariation[i], "%u.blp", &textures[i]) != 1) return 0;
    char request[100]; std::snprintf(request, sizeof(request), "%u %u 11 %u", modelId, textures[0], textures[1]);
    sExtensionMgr->Log("Retail mount display %u resolved through client tables: model %u, textures %u/%u", id, modelId, textures[0], textures[1]);
    if (std::getenv("WOW_RETAIL_WORLD_BIND_PREVIEW")) {
        char script[1600];
        std::snprintf(script, sizeof(script),
            "local f=RetailModelPreview or CreateFrame('Model','RetailModelPreview'); "
            "f:SetWidth(650); f:SetHeight(650); f:SetPoint('CENTER'); f:SetFrameStrata('TOOLTIP'); "
            "f:SetModel('retail\\\\%u.m2'); RetailModelBindDisplay(f,%u); f:Show(); "
            "f:SetScript('OnUpdate',function(self) if RetailModelReportLoaded(self) then self:SetScript('OnUpdate',nil) end end)", modelId, id);
        FrameScript_Execute(script, "RetailPreview", nullptr);
        return 1;
    }
    return Preview("retailmodel", request);
}

#include "JourneyShieldTrial.inl"
#include "CosmeticPreview.inl"

int CosmeticsPreview(lua_State* L) {
    auto* frame = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, CSimpleModel::GetObjectType()));
    const int display = luaL_checkint(L, 2);
    if (display < 0 || display > 5000000) return luaL_error(L, "Invalid backpack display");
    lua_pushboolean(L, EsteriaBackpackPreview(frame->m_model, static_cast<uint32_t>(display)));
    return 1;
}

int BackpackFitInfo(lua_State* L) {
    uint32_t display=0;
    std::string body;
    if (!EsteriaBackpackFitInfo(display,body)) return 0;
    lua_pushinteger(L,display);
    lua_pushlstring(L,body.data(),body.size());
    return 2;
}
int BackpackSetFit(lua_State* L) {
    const int display=luaL_checkint(L,1);
    const char* body=luaL_checkstring(L,2);
    const Retail::BackpackFit fit{float(luaL_checknumber(L,3)),float(luaL_checknumber(L,4)),
        float(luaL_checknumber(L,5)),float(luaL_checknumber(L,6))};
    lua_pushboolean(L,display>0 && EsteriaBackpackSetFit(uint32_t(display),body,fit));
    return 1;
}

int BackpackExportFits(lua_State* L) {
    size_t size=0;
    const char* data=luaL_checklstring(L,1,&size);
    if (!Retail::ValidBackpackFitExport(std::string(data,size))) return luaL_error(L,"Invalid backpack fit export");
    try {
        const auto directory=std::filesystem::path(SFile::m_basePath)/"WTF"/"Esteria"/"BackpackFits";
        std::filesystem::create_directories(directory);
        const std::time_t now=std::time(nullptr);
        std::tm time{};
        localtime_s(&time,&now);
        char stamp[32];
        std::strftime(stamp,sizeof(stamp),"%Y%m%d-%H%M%S",&time);
        auto path=directory/(std::string("BackpackFits-")+stamp+".txt");
        unsigned index=1;
        while (std::filesystem::exists(path)) path=directory/(std::string("BackpackFits-")+stamp+"-"+std::to_string(index++)+".txt");
        std::ofstream output(path,std::ios::binary);
        output.write(data,std::streamsize(size));
        output.close();
        if (!output) throw std::runtime_error("Could not write backpack fit export");
        lua_pushboolean(L,true);
        const auto filename=path.string();
        lua_pushlstring(L,filename.data(),filename.size());
    } catch (const std::exception& error) {
        lua_pushboolean(L,false);
        lua_pushstring(L,error.what());
    }
    return 2;
}

class RetailPreview : public ClientScript {
    bool m_previewAttempted = false;
    float m_elapsed = 0;
public:
    RetailPreview() : ClientScript("RetailPreview") {}
    void OnStartup() override {
        sExtensionMgr->RegisterLuaFunction("EsteriaBackpackExportFits", BackpackExportFits, FRAMESCRIPT_CONTEXTS_ALL);
        sExtensionMgr->RegisterLuaFunction("EsteriaBackpackFitInfo", BackpackFitInfo, FRAMESCRIPT_CONTEXTS_ALL);
        sExtensionMgr->RegisterLuaFunction("EsteriaBackpackSetFit", BackpackSetFit, FRAMESCRIPT_CONTEXTS_ALL);
        sExtensionMgr->RegisterLuaFunction("EsteriaCosmeticsFit", CosmeticFit, FRAMESCRIPT_CONTEXTS_ALL);
        sExtensionMgr->RegisterLuaFunction("EsteriaCosmeticsWingPreview", WingPreview, FRAMESCRIPT_CONTEXTS_ALL);
        sExtensionMgr->RegisterLuaFunction("EsteriaCosmeticsPreview", CosmeticsPreview, FRAMESCRIPT_CONTEXTS_ALL);
        sExtensionMgr->RegisterConsoleCommand("journeyshield", JourneyCommand, CATEGORY_DEBUG, "Journey Mentor Shield cloak trial: on|off|model");
        sExtensionMgr->RegisterLuaFunction("JourneyShieldTrial", JourneyLua, FRAMESCRIPT_CONTEXTS_ALL);
        if (const char* mode = std::getenv("WOW_JOURNEY_SHIELD_TRIAL")) g_journeyTrial = std::string(mode) == "on";
        sExtensionMgr->RegisterConsoleCommand("retailmodel", Preview, CATEGORY_DEBUG, "Preview a Retail CASC model by FileDataID");
        sExtensionMgr->RegisterConsoleCommand("retailmount", MountPreview, CATEGORY_DEBUG, "Diagnostic preview of a registered native Retail mount display ID");
        sExtensionMgr->RegisterConsoleCommand("retailweapon", EquipPreview, CATEGORY_DEBUG, "Replace selected character's equipped mainhand appearance: <modelID> <textureID>");
        sExtensionMgr->RegisterLuaFunction("RetailModelBindTexture", BindTexture, FRAMESCRIPT_CONTEXTS_ALL);
        sExtensionMgr->RegisterLuaFunction("RetailModelBindDisplay", BindDisplay, FRAMESCRIPT_CONTEXTS_ALL);
        sExtensionMgr->RegisterLuaFunction("RetailModelReportLoaded", ReportLoaded, FRAMESCRIPT_CONTEXTS_ALL);
        sExtensionMgr->RegisterLuaFunction("RetailModelFit", Fit, FRAMESCRIPT_CONTEXTS_ALL);
        if (const char* request = std::getenv("WOW_RETAIL_EQUIP_PREVIEW")) {
            Retail::HandItemAppearance appearance;
            if (ParseEquipment(request, appearance) && Retail::Source().SetMainHandPreview(appearance))
                sExtensionMgr->Log("Retail mainhand smoke test enabled for all equipped mainhands: model %u, texture %u", appearance.modelId, appearance.textureId);
            else
                sExtensionMgr->Log("Invalid Retail equipment preview or CASC configuration");
        }
    }
    void OnUpdate(float elapsed) override {
        SyncJourneyShield();
        m_elapsed += elapsed;
        if (!m_previewAttempted && m_elapsed > 2) {
            m_previewAttempted = true;
            if (const char* mode = std::getenv("WOW_JOURNEY_SHIELD_TRIAL"))
                if (std::string(mode) == "model") JourneyCommand("journeyshield", "model");
            if (g_theGxDevicePtr && std::getenv("WOW_RETAIL_CONFIG"))
                sExtensionMgr->Log("Retail active renderer: %s; native large-palette mounts require D3D11",
                    g_gxApiNames[g_theGxDevicePtr->DeviceApi()]);
            if (const char* preview = std::getenv("WOW_RETAIL_PREVIEW")) Preview("retailmodel", preview);
            if (const char* preview = std::getenv("WOW_RETAIL_MOUNT_PREVIEW")) MountPreview("retailmount", preview);
        }
    }
};

class RetailEquipmentModels : public ModelScript {
    bool m_reported = false;
    bool m_hasRetail = false;
    std::map<const CM2Model*, unsigned> m_diagnostics;
    static bool IsRetailModel(const char* name) {
        return Retail::IsCdnPath(name);
    }
    void ReportAttachment(CM2Model& parent, unsigned depth) {
        // Glue backgrounds own the character, which in turn owns its equipment.
        for (auto* child = parent.m_attachList; child && !m_reported; child = child->m_attachNext) {
            const std::string path = child->m_shared->m_fileName;
            if (IsRetailModel(path.c_str()) && child->IsLoaded(0, 0)) {
                sExtensionMgr->Log("Retail equipment animated with parent %s: %s; attachment %u, %u vertices",
                    parent.m_shared->m_fileName, path.c_str(), child->m_attachmentId, child->m_shared->m_data->m_vertices.m_count);
                m_reported = true;
            } else if (depth < 16) {
                ReportAttachment(*child, depth + 1);
            }
        }
    }
public:
    RetailEquipmentModels() : ModelScript("RetailEquipmentModels") {}
    void OnModelSkinLoaded(CM2Shared& loaded) override {
        auto* shared = &loaded;
        if (!IsRetailModel(shared->m_fileName)) return;
        for (unsigned i = 0; i < shared->m_skinData->m_batches.m_count; ++i) {
            const auto& batch = shared->m_skinData->m_batches[i];
            sExtensionMgr->Log("Retail mesh %s batch %u: shader %u, textures %u, section %u, effect %u",
                shared->m_fileName, i, batch.m_shader, batch.m_textureCount, batch.m_skinSectionIndex, shared->m_effects[i] != nullptr);
        }
    }
    void OnModelCreated(CM2Model& model) override {
        if (IsRetailModel(model.m_shared->m_fileName)) m_hasRetail = true;
    }
    void OnModelAnimated(CM2Model& root) override {
        const auto* shared = root.m_shared;
        const unsigned count = std::getenv("WOW_RETAIL_TRACE") && IsRetailModel(shared->m_fileName) && root.IsLoaded(0, 0) ? ++m_diagnostics[&root] : 0;
        if (count == 1 || count == 120) {
            sExtensionMgr->Log("Retail animation %s: alpha %f, opacity %f, sections %u, first visible %u, first effect %u",
                shared->m_fileName, root.m_alpha,
                shared->m_data->m_textureWeights.m_count ? root.m_textureWeights[0].m_currentValue : -1.0f,
                shared->m_skinData->m_skinSections.m_count, root.m_skinSections[0], shared->m_effects[0] != nullptr);
            sExtensionMgr->Log("Retail draw state %s: flags %u/%u, drawable %u, cache %u", shared->m_fileName,
                root.m_flags, root.m_fFlags, root.IsDrawable(0, 0), shared->m_cache->m_flags);
            sExtensionMgr->Log("Retail instance %p: optimized %u, effective alpha %f", &root, root.m_optGeo != nullptr, root.m_effectiveAlpha);
            for (unsigned i = 0; i < shared->m_data->m_textures.m_count; ++i) {
                auto* texture = root.m_textures[i];
                sExtensionMgr->Log("Retail texture %u type %u: %s; ready %u", i, shared->m_data->m_textures[i].m_type,
                    texture ? TextureGetFilename(texture) : "<unset>", texture && TextureGetGxTex(texture, 0, nullptr));
            }
        }
        if (m_hasRetail && !m_reported) ReportAttachment(root, 0);
    }
};

class RetailPackAudit : public ClientDBScript {
public:
    RetailPackAudit() : ClientDBScript("RetailPackAudit") {}
    void OnClientDBInitialize() override {
        const char* request = std::getenv("WOW_RETAIL_VERIFY_ITEM");
        uint32_t id = 0;
        if (request) {
            const std::string text = request;
            auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
            if (!id || parsed.ec != std::errc() || parsed.ptr != text.data() + text.size()) return;
        } else if (Retail::Source().HasClientTable("DBFilesClient/Item.dbc")) {
            // A normal pack launch audits one imported item without enabling a preview.
            for (int32_t i = 0; i < g_itemDB.m_numRecords; ++i) {
                const auto* candidate = g_itemDB.GetRecordByIndex(i);
                Retail::HandItemAppearance mapped;
                if (candidate && Retail::Source().HandItem(candidate->m_displayInfoID, mapped)) { id = candidate->m_id; break; }
            }
        }
        if (!id) return;
        const auto* item = g_itemDB.GetRecord(id);
        ItemDisplayInfoRec display;
        Retail::HandItemAppearance appearance;
        if (!item || !g_itemDisplayInfoDB.GetRecord(item->m_displayInfoID, &display) ||
            !Retail::Source().HandItem(item->m_displayInfoID, appearance)) {
            sExtensionMgr->Log("Retail pack audit FAILED: missing item/display/asset registration for item %u", id);
            return;
        }
        std::vector<uint8_t> bytes;
        std::string error;
        const auto path = Retail::FileName(appearance.modelId, ".m2");
        const bool loaded = Retail::Source().Read(path.c_str(), bytes, error);
        if (loaded) Retail::Source().ReleaseModel(path.c_str());
        sExtensionMgr->Log("Retail pack audit %s: new item %u, display %u, model %u, texture %u; %u model bytes; %s",
            loaded ? "PASSED" : "FAILED", id, item->m_displayInfoID, appearance.modelId, appearance.textureId,
            static_cast<uint32_t>(bytes.size()), error.c_str());
    }
};
}

void AddRetailPreviewExtension() { new RetailPreview(); new RetailEquipmentModels(); new RetailPackAudit(); }
#else
void AddRetailPreviewExtension() {}
#endif
