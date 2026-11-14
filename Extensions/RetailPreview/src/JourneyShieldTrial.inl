// Local-only art trial. No item IDs, saved appearances or server state are changed.
bool g_journeyTrial = false;
constexpr const char* JourneyModel = "custom/fortnite/journey_mentor_shield.m2";

CCharacterComponent* JourneyCharacter() {
    if (auto* player = ClntObjMgrGetActivePlayerObj()) return player->m_characterComponent;
    const int32_t index = CCharacterSelection::s_selectionIndex;
    if (index < 0 || index >= static_cast<int32_t>(CCharacterSelection::s_characterList.Count())) return nullptr;
    return CCharacterSelection::s_characterList[index].m_component;
}

int32_t JourneyCommand(const char*, const char* arguments) {
    const std::string mode = arguments ? arguments : "";
    if (mode == "model") {
        FrameScript_Execute(
            "local f=RetailModelPreview or CreateFrame('Model','RetailModelPreview'); "
            "f:SetWidth(650); f:SetHeight(650); f:SetPoint('CENTER'); f:SetFrameStrata('TOOLTIP'); "
            "f:SetModel('custom/fortnite/journey_mentor_shield.m2'); f:Show(); "
            "f:SetScript('OnUpdate',function(self) if RetailModelReportLoaded(self) then "
            "self:SetScript('OnUpdate',nil) end end)", "JourneyShieldTrial", nullptr);
        return 1;
    }
    if (mode != "on" && mode != "off") {
        sExtensionMgr->Log("Usage: journeyshield on|off|model (on replaces the selected player's equipped cloak locally)");
        return 0;
    }
    g_journeyTrial = mode == "on";
    if (auto* character = JourneyCharacter()) character->m_flags |= 0x4;
    sExtensionMgr->Log("Journey shield cloak trial: %s", mode.c_str());
    return 1;
}

int JourneyLua(lua_State* L) {
    lua_pushboolean(L, JourneyCommand("journeyshield", luaL_checkstring(L, 1)) != 0);
    return 1;
}

void SyncJourneyShield() {
    auto* character = JourneyCharacter();
    CM2Model* parent = character ? character->m_data.m_model : nullptr;
    if (!parent || !parent->IsLoaded(0, 0)) return;
    CM2Model* shield = nullptr;
    for (auto* child = parent->m_attachList; child; child = child->m_attachNext)
        if (child->m_shared && !SStrCmpI(child->m_shared->m_fileName, JourneyModel, STORM_MAX_STR)) shield = child;
    if (!g_journeyTrial || !character->m_itemDisplayID[ITEMSLOT_CAPE]) {
        if (shield) {
            shield->DetachFromParent();
            character->m_flags |= 0x4;
        }
        return;
    }
    if (shield && shield->IsLoaded(0, 0)) {
        parent->SetGeometryVisible(1500, 1599, 0);
        return;
    }
    if (shield) return; // Keep the ordinary cloak visible while the mesh loads.

    const auto& data = *parent->m_shared->m_data;
    const auto& skin = *parent->m_shared->m_skinData;
    ItemDisplayInfoRec cape;
    if (!g_itemDisplayInfoDB.GetRecord(character->m_itemDisplayID[ITEMSLOT_CAPE], &cape)) return;
    const uint32_t geoset = 1501 + cape.m_geosetGroup[0];
    std::vector<Retail::CloakVertex> vertices;
    std::vector<std::array<float, 3>> surface;
    for (uint32_t i = 0; i < skin.m_skinSections.m_count; ++i) {
        const auto& section = skin.m_skinSections[i];
        const uint32_t id = section.m_skinSectionId & 0xffff;
        const bool cloak = id == geoset;
        if (!cloak && (!parent->m_skinSections[i] || id >= 1500)) continue;
        const uint32_t end = uint32_t(section.m_vertexStart) + section.m_vertexCount;
        if (end > skin.m_vertices.m_count) return;
        for (uint32_t j = section.m_vertexStart; j < end; ++j) {
            if (skin.m_vertices[j] >= data.m_vertices.m_count) return;
            const auto& v = data.m_vertices[skin.m_vertices[j]];
            surface.push_back({v.m_position.x, v.m_position.y, v.m_position.z});
            if (!cloak) continue;
            Retail::CloakVertex sample{{v.m_position.x, v.m_position.y, v.m_position.z}, {}, {}};
            for (unsigned k = 0; k < 4; ++k) {
                sample.bones[k] = v.m_indices[k];
                sample.weights[k] = v.m_weights[k];
            }
            vertices.push_back(sample);
        }
    }
    Retail::CloakAnchor anchor;
    if (!Retail::FindCloakAnchor(vertices, data.m_bones.m_count, anchor) ||
        !Retail::FitCloakBack(surface, anchor)) return;
    uint32_t socketId = 0xffffffff;
    for (uint32_t id : {12u, 30u, 28u, 11u})
        if (id < data.m_attachmentIndicesById.m_count && data.m_attachmentIndicesById[id] < data.m_attachments.m_count) {
            socketId = id;
            break;
        }
    if (socketId == 0xffffffff) return;
    shield = parent->m_scene->CreateModel(JourneyModel, 0);
    if (!shield) return;
    shield->AttachToParent(parent, socketId, nullptr, 0);
    // shortcut: fit to the cloak's upper width; unusually shaped races need a measured profile after this trial.
    const float scale = std::max(0.5f, std::min(3.0f, anchor.width / 0.373f));
    const C3Vector position(anchor.position[0], anchor.position[1], anchor.position[2]);
    if (!shield->SetAttachmentFrame(anchor.bone, position, scale)) shield->DetachFromParent();
    else sExtensionMgr->Log("Journey shield attached to cloak geoset %u of %s; bone %u, position %.3f %.3f %.3f, scale %.3f",
        geoset, parent->m_shared->m_fileName, anchor.bone, position.x, position.y, position.z, scale);
    shield->Release();
}
