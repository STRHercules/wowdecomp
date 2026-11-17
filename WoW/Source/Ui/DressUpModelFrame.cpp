#include "DressUpModelFrame.h"
#include <new>
#include <typeinfo>
#include "Engine/Source/Frame/CScriptObject.h"
#include "Engine/Source/Frame/CScriptRegion.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "Engine/Source/Model2/M2Model.h"
#include "WoW/Common/ComponentCore/CharacterComponent.h"
#include "WoW/Common/ComponentCore/RetailRace.h"
#include "Engine/Source/Frame/CSimpleModel.h"
#include "Engine/Source/Frame/ModelSnapshot.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Tempest/C44Matrix.h"
#include "Engine/Source/Tempest/CImVector.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/Services/Camera.h"
#include "Engine/Source/Base/Handle.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/DBClient/DBCacheInstances.h"
#include "WoW/Source/DB/DBClient/CreatureStats_C.h"
#include "WoW/Source/DB/Rec/SpellRec.h"
#include "WoW/Source/Object/ItemStats.h"
#include "WoW/Source/Object/Player.h"
#include "WoW/Source/Object/Unit.h"
#include "WoW/Source/Object/ObjectClient/Item_C.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Object/ObjectClient/VisibleItem_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Game/GameClient/CDrawBowString.h"
#include "WoW/Source/Ui/GameUI.h"
#include <cstdlib>
#include <cstring>

// STATIC: 0x00ACF54C
const char* CGDressUpModelFrame::s_objectTypeName = "DressUpModel";

// STATIC: 0x00ACF568
int32_t CGDressUpModelFrame::s_metatable = LUA_NOREF;

// STATIC: 0x00C0E4F4
int32_t CGDressUpModelFrame::s_objectType;

// STATIC: 0x00C0E500
CDataAllocator CGDressUpModelFrame::s_dressUpModelHeap(sizeof(CGDressUpModelFrame), 5);

// INLINED
void CGDressUpModelFrame::operator delete(void* ptr) {
    if (ptr) {
        CGDressUpModelFrame::s_dressUpModelHeap.PutData(ptr, nullptr, 0);
    }
}

// INLINED
int32_t CGDressUpModelFrame::GetObjectType() {
    if (!CGDressUpModelFrame::s_objectType) {
        CGDressUpModelFrame::s_objectType = ++FrameScript_Object::s_objectTypes;
    }

    return CGDressUpModelFrame::s_objectType;
}

// VERIFY: 3888121b auto
// OFFSET: 0x00514300
CSimpleFrame* CGDressUpModelFrame::Create(CSimpleFrame* parent) {
    if (CGDressUpModelFrame::s_dressUpModelHeap.m_bytesPerData < sizeof(CGDressUpModelFrame)) {
        SErrDisplayAppFatalMessage("", "", 0);
    }

    void* data = CGDressUpModelFrame::s_dressUpModelHeap.GetData(0, typeid(CGDressUpModelFrame).raw_name(), SLOG_OBJECT);
    return data ? new (data) CGDressUpModelFrame(parent) : nullptr;
}

// VERIFY: 62525612 auto
// OFFSET: 0x00597B50
CGDressUpModelFrame::CGDressUpModelFrame(CSimpleFrame* parent) : CGCharacterModelBase(parent) {
    this->m_component = nullptr;
    this->m_sheathed = 0;
    this->m_bodyHidden = 0;
    this->m_silhouette = 0;
    this->m_previewSex = 0;
    this->m_previewHair = 0;
    this->m_snapshotOnly = 0;
    this->m_snapshotPending = 0;
    this->m_ready = 0;
    this->m_readyFrames = 0;
    this->m_drawnFrames = 0;
    this->m_minFrames = 0;
    this->m_needAttach = 0;
    this->m_force = 0;
    this->m_commitNext = 0;
    this->m_snapshotKey[0] = '\0';
    this->m_refKey[0] = '\0';
}

// VERIFY: cc335fc2 auto
// OFFSET: 0x00597B80
const char* CGDressUpModelFrame::GetObjectTypeName() {
    return CGDressUpModelFrame::s_objectTypeName;
}

// VERIFY: 44b27d0d auto
// OFFSET: 0x00597B90
int32_t CGDressUpModelFrame::GetScriptMetaTable() {
    return CGDressUpModelFrame::s_metatable;
}

// VERIFY: 76453306 manual
// OFFSET: [0x00597D10, 0x00597DB0]
CGDressUpModelFrame::~CGDressUpModelFrame() {
    if (this->m_component) {
        CCharacterComponent::FreeComponent(this->m_component);
        this->m_component = nullptr;
    }
}

// OFFSET: 0x00597D20
bool CGDressUpModelFrame::IsA(int32_t type) {
    return type == CGDressUpModelFrame::s_objectType || type == CGCharacterModelBase::s_objectType || type == CSimpleModel::s_objectType || type == CSimpleFrame::s_objectType || type == CScriptRegion::s_objectType || type == CScriptObject::s_objectType;
}

// OFFSET: 0x00597D70
bool CGDressUpModelFrame::IsA(const char* typeName) {
    return !SStrCmpI(typeName, CGDressUpModelFrame::s_objectTypeName, STORM_MAX_STR) || CGCharacterModelBase::IsA(typeName);
}

// NOT IN BINARY. Whether the model and everything attached to it (shoulders, weapons) has its textures loaded; a
// texture still loading draws black. CM2Model::IsDrawable asks the same but remembers a yes, which a re-dressed model
// must not.
static bool TexturesLoaded(CM2Model* model) {
    if (!model->IsLoaded(0, 0)) {
        return false;
    }

    for (uint32_t i = 0; i < model->m_shared->m_data->m_textures.m_count; i++) {
        if (model->m_textures[i] && !TextureGetGxTex(model->m_textures[i], 0, nullptr)) {
            return false;
        }
    }

    for (CM2Model* attached = model->m_attachList; attached; attached = attached->m_attachNext) {
        if (!TexturesLoaded(attached)) {
            return false;
        }
    }

    return true;
}

// VERIFY: 79d529ac manual
// OFFSET: 0x00597F40
void CGDressUpModelFrame::UpdateModel() {
    CGCharacterModelBase::UpdateModel();

    if (!this->m_component) {
        return;
    }

    this->m_component->SetSilhouette(this->m_silhouette != 0);

    uint32_t ready = this->m_component->RenderPrep(0) & 1;
    CM2Model* model = this->m_component->m_data.m_model;

    if (this->m_bodyHidden && ready) {
        model->SetGeometryVisible(0, 0xFFFF, 0);
    }

    // a snapshot is only taken once the model and its textures have held still for a few frames
    this->m_ready = ready && this->m_model && TexturesLoaded(this->m_model) && !this->m_component->m_dirtySections;

    if (model->m_attachParent) {
        model->m_fFlags = (model->m_fFlags & ~0x80u) | (ready << 7);
        model->m_fFlags = (model->m_fFlags & ~0x20000u) | (ready << 17);
    } else {
        model->m_fFlags = (model->m_fFlags & ~0x8u) | (ready << 3);
        model->m_fFlags = (model->m_fFlags & ~0x10000u) | (ready << 16);
    }
}

// NOT IN BINARY. A snapshot-only frame draws nothing on screen; asked for a snapshot it queues the offscreen render.
void CGDressUpModelFrame::OnFrameRender(CRenderBatch* batch, uint32_t layer) {
    if (!this->m_snapshotOnly) {
        CGCharacterModelBase::OnFrameRender(batch, layer);
        return;
    }

    this->CSimpleFrame::OnFrameRender(batch, layer);

    // always queued: the batch is only rebuilt when the frame is marked changed, which a request is not, so a
    // callback queued only while one is pending would never be there; RenderSnapshot returns when nothing is
    if (this->m_model && layer == 2) {
        batch->QueueCallback(&CGDressUpModelFrame::RenderSnapshot, this);
    }
}


// NOT IN BINARY. Draws the model into the snapshot cache entry named by m_snapshotKey. Binding and restoring the
// targets follows SkyCapture::Update.
void CGDressUpModelFrame::RenderSnapshot(void* arg) {
    constexpr uint8_t kReadyFrames = 6;
    CGDressUpModelFrame* frame = static_cast<CGDressUpModelFrame*>(arg);

    if (!frame->m_snapshotPending || !frame->m_model || !g_theGxDevicePtr) {
        return;
    }

    CGxTex* target = ModelSnapshot::Scratch(frame);

    if (!target) {
        frame->m_snapshotPending = 0;
        frame->m_commitNext = 0;
        return;
    }

    // The last draw was queued a frame ago and is only finished now: a read back in the frame that drew it finds
    // the target as it was before (blank). Nothing draws into the target in between, so it is read first thing.
    if (frame->m_commitNext) {
        frame->m_commitNext = 0;
        frame->m_snapshotPending = 0;
        ModelSnapshot::Commit(frame->m_snapshotKey, target, frame->m_force, frame->m_refKey);
        return;
    }

    // The framing camera: the card framing is tuned against the plain sphere-fitted camera (the one ConfigureCamera
    // makes when the model has no paperdoll camera), but OnModelLoaded picks the model's own paperdoll camera when it
    // has one, which frames a far-away full figure. Snapshots always use the plain one, made again every frame
    // because the model's load callback can swap it back at any time.
    if (!frame->m_model->IsLoaded(0, 0)) {
        return;
    }

    CAaSphere sphere = frame->m_model->GetBoundingSphere();
    CCamera* camera = CameraCreate();
    CameraSetFloat(camera, 4, 0.5f);
    CameraSetCoord(camera, 8, sphere.c, 0);
    CameraSetCoord(camera, 7, C3Vector(5.5555558f, 0.0f, 2.4166667f), 0);
    CameraSetFloat(camera, 3, 0.027777778f);
    frame->SetCamera(camera);
    HandleClose(reinterpret_cast<HOBJECT>(camera));

    frame->UpdateModel();
    frame->m_readyFrames =frame->m_ready ? (frame->m_readyFrames < 255 ? frame->m_readyFrames + 1 : 255) : 0;

    // Drawn every frame while it waits: a character only builds its skin and loads its textures while it is being
    // drawn, so waiting to draw until it is ready would wait for ever. The picture only counts from the frame it is ready.
    if (frame->m_drawnFrames < 0xFFFF) {
        frame->m_drawnFrames++;
    }

    // An attached item's model is made after the character reports ready, so a picture taken at once has no helm.
    // It is waited for; an item that never gets one is photographed anyway after three times the minimum.
    const bool attached = !frame->m_needAttach || frame->m_model->m_attachList || frame->m_drawnFrames >= 3 * frame->m_minFrames;
    const bool done = frame->m_readyFrames >= kReadyFrames && frame->m_drawnFrames >= frame->m_minFrames && attached;

    float oldMinX, oldMaxX, oldMinY, oldMaxY, oldMinZ, oldMaxZ;
    GxXformViewport(oldMinX, oldMaxX, oldMinY, oldMaxY, oldMinZ, oldMaxZ);

    C44Matrix oldView;
    GxXformView(oldView);
    C44Matrix oldProjection = g_theGxDevicePtr->m_projection;

    CGxTex* oldColour = nullptr;
    CGxTex* oldDepth = nullptr;
    g_theGxDevicePtr->DeviceGetRenderTarget(GxBuffers_Color, oldColour);
    g_theGxDevicePtr->DeviceGetRenderTarget(GxBuffers_Depth, oldDepth);

    GxRenderTargetSet(GxBuffers_Color, nullptr, 0);
    GxRenderTargetSet(GxBuffers_Depth, nullptr, 0);
    GxRenderTargetSet(GxBuffers_Color, target, 0);

    g_theGxDevicePtr->RsPush();
    GxXformSetViewport(0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);
    g_theGxDevicePtr->SceneClear(GxBufferBit_Color | GxBufferBit_Depth, CImVector(0, 0, 0, 255));
    CSimpleModel::RenderModelOffscreen(frame);
    g_theGxDevicePtr->RsPop();

    // colour out first (a null depth asked for under a bound colour texture dereferences it), then back in last
    GxRenderTargetSet(GxBuffers_Color, nullptr, 0);
    GxRenderTargetSet(GxBuffers_Depth, oldDepth, 0);

    if (oldColour) {
        GxRenderTargetSet(GxBuffers_Color, oldColour, 0);
    }

    g_theGxDevicePtr->XformSetProjection(oldProjection);
    g_theGxDevicePtr->XformSetView(oldView);
    GxXformSetViewport(oldMinX, oldMaxX, oldMinY, oldMaxY, oldMinZ, oldMaxZ);

    if (done) {
        // read back, checked and stored next frame; a picture that is empty or cannot be stored is not marked done,
        // so the script sees no picture and makes the job again
        frame->m_commitNext = 1;
    }
}

// STATIC: 0x00C0E4F8
static int32_t s_mainHandItemId;

// STATIC: 0x00C0E4FC
static int32_t s_offHandItemId;

// STATIC: 0x00ACF548
static int32_t s_nextHand = 15;

// VERIFY: 67467efb auto
// OFFSET: 0x00597BA0
void CGDressUpModelFrame::Undress() {
    if (!this->m_model) {
        return;
    }

    if (this->m_component) {
        for (uint32_t i = 0; i < 12; i++) {
            this->m_component->RemoveItem(static_cast<ITEM_SLOT>(i));
        }
    }

    if (this->m_mainHand.m_classID) {
        CCharacterComponent::RemoveHandItem(this->m_model, static_cast<INVENTORY_SLOTS>(15), static_cast<SHEATHE_TYPE>(this->m_mainHand.m_sheatheType), this->m_mainHand.m_inventoryType == 14);
        this->m_mainHand.m_classID = 0;
        this->m_mainHand.m_inventoryType = 0;
    }

    if (this->m_offHand.m_classID) {
        CCharacterComponent::RemoveHandItem(this->m_model, static_cast<INVENTORY_SLOTS>(16), static_cast<SHEATHE_TYPE>(this->m_offHand.m_sheatheType), this->m_offHand.m_inventoryType == 14);
        this->m_offHand.m_classID = 0;
        this->m_offHand.m_inventoryType = 0;
    }
}

// VERIFY: d67e4d73 auto
// OFFSET: 0x00597C50
static bool HandItemsCompatible(uint32_t mainHandType, uint32_t offHandType, bool dualWield) {
    if (mainHandType != 13 && mainHandType != 21) {
        return mainHandType == 17 && offHandType != 15 && CGItem_C::CanGoInSlot(s_mainHandItemId, 16, 0);
    }

    if (offHandType == 14 || offHandType == 23) {
        return true;
    }

    if (offHandType != 13 && offHandType != 22) {
        return (offHandType == 21 || offHandType == 17) && CGItem_C::CanGoInSlot(s_offHandItemId, 16, 0);
    }

    return dualWield;
}

// VERIFY: 1bee737b auto
// OFFSET: 0x00597E10
CM2Model* CGDressUpModelFrame::GetDuplicateUnitCharacterModel(CGUnit_C* unit) {
    if (!unit) {
        return nullptr;
    }

    // Native display records name the Wrath body; the live Retail body owns custom emote sequences and saved forms.
    if ((unit->m_obj->m_type & TYPEMASK_PLAYER) && RetailRaceMapped(unit->m_unit->m_unitFieldBytes0[0]) &&
        unit->m_unit->m_unitFieldDisplayid == unit->m_unit->m_unitFieldNativedisplayid) {
        return CGCharacterModelBase::GetDuplicateUnitCharacterModel(unit);
    }

    CreatureDisplayInfoRec* display = g_creatureDisplayInfoDB.GetRecord(unit->m_unit->m_unitFieldNativedisplayid);

    if (!display) {
        return nullptr;
    }

    CreatureModelDataRec* modelData = g_creatureModelDataDB.GetRecord(display->m_modelID);

    if (!modelData) {
        return nullptr;
    }

    const char* modelName = modelData->m_modelName;

    if (!modelName || !*modelName) {
        return nullptr;
    }

    return this->GetScene()->CreateModel(modelName, 0);
}

// VERIFY: d215162c manual
// OFFSET: 0x00597FC0
void CGDressUpModelFrame::TryOn(int32_t itemId, const uint32_t* enchants, int32_t hand) {
    ItemVisualsRec* visuals = nullptr;
    WOWGUID none = WOWGUID();
    ItemStats_C* stats = g_itemDBCache.GetRecord(itemId, none, nullptr, nullptr, false);

    if (stats) {
        ItemDisplayInfoRec display;

        if (g_itemDisplayInfoDB.GetRecord(stats->m_displayId, &display)) {
            visuals = g_itemVisualsDB.GetRecord(display.m_itemVisual);
        }
    }

    int32_t visual = 0;

    if (enchants && !visuals) {
        for (uint32_t i = 0; i < 12; i++) {
            SpellItemEnchantmentRec* enchant = g_spellItemEnchantmentDB.GetRecord(enchants[i]);

            if (enchant && enchant->m_itemVisual) {
                visual = enchant->m_itemVisual;
                break;
            }
        }
    }

    this->TryOnWithVisual(itemId, visual, hand);
}

// VERIFY: 67beb92f auto
// OFFSET: 0x005980B0
static void DressUpItemCallback(int32_t id, const WOWGUID& guid, void* param, bool success) {
    if (success) {
        static_cast<CGDressUpModelFrame*>(param)->TryOn(id, nullptr, -1);
    }
}

// VERIFY: a2d24597 manual
// OFFSET: 0x005980D0
void CGDressUpModelFrame::TryOnWithVisual(int32_t itemId, int32_t visual, int32_t hand) {
    WOWGUID none = WOWGUID();
    ItemStats_C* stats = g_itemDBCache.GetRecord(itemId, none, DressUpItemCallback, this, false);

    if (!stats || !stats->m_displayId || !this->m_model) {
        return;
    }

    if (visual < 0) {
        visual = 0;
    }

    if (!stats->m_inventoryType) {
        for (uint32_t i = 0; i < 5; i++) {
            if (!stats->m_spellId[i]) {
                continue;
            }

            SpellRec spell;

            if (!g_spellDB.GetRecord(stats->m_spellId[i], &spell)) {
                continue;
            }

            SpellRec learned;

            if (spell.m_effect[0] != 36) {
                continue;
            }

            int32_t learnedId = spell.m_effectTriggerSpell[0];

            if (!learnedId) {
                for (uint32_t j = 0; j < 5; j++) {
                    if (stats->m_spellTrigger[j] == 6) {
                        learnedId = stats->m_spellId[j];
                        break;
                    }
                }
            }

            if (g_spellDB.GetRecord(learnedId, &learned) && learned.m_effect[0] == 24) {
                this->TryOn(learned.m_effectItemType[0], nullptr, -1);
                return;
            }
        }
    }

    ITEM_SLOT slot;

    switch (stats->m_inventoryType) {
        case 1:
            slot = static_cast<ITEM_SLOT>(0);
            break;

        case 2:
        case 11:
        case 12:
        case 28:
            return;

        case 3:
            slot = static_cast<ITEM_SLOT>(1);
            break;

        case 4:
            slot = static_cast<ITEM_SLOT>(2);
            break;

        case 5:
        case 20:
            slot = static_cast<ITEM_SLOT>(3);
            break;

        case 6:
            slot = static_cast<ITEM_SLOT>(4);
            break;

        case 7:
            slot = static_cast<ITEM_SLOT>(5);
            break;

        case 8:
            slot = static_cast<ITEM_SLOT>(6);
            break;

        case 9:
            slot = static_cast<ITEM_SLOT>(7);
            break;

        case 10:
            slot = static_cast<ITEM_SLOT>(8);
            break;

        case 16:
            slot = static_cast<ITEM_SLOT>(10);
            break;

        case 19: {
            if (!this->m_component) {
                return;
            }

            this->m_component->RemoveItem(static_cast<ITEM_SLOT>(9));
            this->m_component->AddItem(static_cast<ITEM_SLOT>(9), stats->m_displayId, visual);
            ItemDisplayInfoRec display;

            if (g_itemDisplayInfoDB.GetRecord(stats->m_displayId, &display) && (display.m_flags & 0x1)) {
                CGPlayer_C* player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(this->m_unitGuid, TYPEMASK_PLAYER, __FILE__, __LINE__));

                if (player) {
                    int32_t emblemStyle;
                    int32_t emblemColor;
                    int32_t borderStyle;
                    int32_t borderColor;
                    int32_t background;

                    if (GuildGetGuildTabard(player->m_obj->m_guid, player->m_player->m_playerGuildid, nullptr, nullptr, emblemStyle, emblemColor, borderStyle, borderColor, background)) {
                        this->m_component->ApplyGuildColor(emblemStyle, emblemColor, borderStyle, borderColor, background);
                    }
                }
            }

            return;
        }

        case 13:
        case 14:
        case 15:
        case 17:
        case 21:
        case 22:
        case 23:
        case 25:
        case 26: {
            CGPlayer_C* player = nullptr;

            if (this->m_unitGuid == ClntObjMgrGetActivePlayer()) {
                player = ClntObjMgrGetActivePlayerObj();
            }

            CGUnitVirtualItem* handItem;

            if (hand == -1) {
                switch (stats->m_inventoryType) {
                    case 13: {
                        uint8_t mainType;

                        if (!this->m_mainHand.m_classID || ((mainType = this->m_mainHand.m_inventoryType) != 13 && mainType != 21 && mainType != 17) || !player || !CGPlayer_C::CanDualWield()) {
                            hand = 15;
                            goto mainHand;
                        }

                        hand = s_nextHand;
                        s_nextHand = (s_nextHand == 15) + 15;
                        break;
                    }

                    case 17:
                    case 21: {
                        if (!CGItem_C::CanGoInSlot(itemId, 16, 0)) {
                            hand = 15;
                            s_nextHand = 15;
                            goto mainHand;
                        }

                        hand = s_nextHand;
                        s_nextHand = (s_nextHand == 15) + 15;
                        break;
                    }

                    case 25:
                    case 26: {
                        hand = 15;
                        s_nextHand = 15;
                        goto mainHand;
                    }

                    case 14:
                    case 15:
                    case 22:
                    case 23: {
                        hand = 16;
                        s_nextHand = 15;
                        goto offHand;
                    }

                    default:
                        return;
                }
            }

            if (hand == 15) {
mainHand:
                s_mainHandItemId = itemId;

                if (this->m_offHand.m_classID && !HandItemsCompatible(stats->m_inventoryType, this->m_offHand.m_inventoryType, player && CGPlayer_C::CanDualWield())) {
                    CCharacterComponent::RemoveHandItem(this->m_model, static_cast<INVENTORY_SLOTS>(16), static_cast<SHEATHE_TYPE>(this->m_offHand.m_sheatheType), this->m_offHand.m_inventoryType == 14);
                    s_offHandItemId = 0;
                }

                handItem = &this->m_mainHand;
            } else {
offHand:
                s_offHandItemId = itemId;

                if (this->m_mainHand.m_classID && !HandItemsCompatible(this->m_mainHand.m_inventoryType, stats->m_inventoryType, player && CGPlayer_C::CanDualWield())) {
                    CCharacterComponent::RemoveHandItem(this->m_model, static_cast<INVENTORY_SLOTS>(15), static_cast<SHEATHE_TYPE>(this->m_mainHand.m_sheatheType), this->m_mainHand.m_inventoryType == 14);
                    s_mainHandItemId = 0;
                }

                handItem = &this->m_offHand;
            }

            if (handItem->m_classID) {
                CCharacterComponent::RemoveHandItem(this->m_model, static_cast<INVENTORY_SLOTS>(hand), static_cast<SHEATHE_TYPE>(handItem->m_sheatheType), handItem->m_inventoryType == 14);
            }

            handItem->m_classID = static_cast<uint8_t>(stats->m_class);
            handItem->m_subclassID = static_cast<uint8_t>(stats->m_subClass);
            handItem->m_inventoryType = static_cast<uint8_t>(stats->m_inventoryType);
            handItem->m_sheatheType = static_cast<uint8_t>(stats->m_sheatheType);
            ItemDisplayInfoRec display;

            if (g_itemDisplayInfoDB.GetRecord(stats->m_displayId, &display)) {
                CCharacterComponent::AddHandItem(this->m_model, &display, static_cast<INVENTORY_SLOTS>(hand), static_cast<SHEATHE_TYPE>(stats->m_sheatheType), this->m_sheathed != 0, stats->m_inventoryType == 14, stats->m_inventoryType == 26, visual);
            }

            return;
        }

        default:
            CGGameUI::DisplayError(ERR_NOT_EQUIPPABLE);
            return;
    }

    if (this->m_component) {
        this->m_component->RemoveItem(slot);
        this->m_component->AddItem(slot, stats->m_displayId, visual);
    }
}

// VERIFY: e534bcd9 auto
// OFFSET: 0x00598800
void CGDressUpModelFrame::TryOnItem(CGItem_C* item, int32_t hand) {
    if (item) {
        int32_t itemId = item->m_obj->m_entryID;
        this->TryOnWithVisual(itemId, item->GetItemEnchantmentVisualID(), hand);
    }
}

// VERIFY: 8eaa7ea4 auto
// OFFSET: 0x005989E0
void CGDressUpModelFrame::SetModel(CM2Model* model) {
    CSimpleModel::SetModel(model);

    if (this->m_component) {
        CCharacterComponent::FreeComponent(this->m_component);
        this->m_component = nullptr;
    }

    if (!model) {
        return;
    }

    if (this->m_previewSex) {
        // NOT IN BINARY: a plain human, whoever the unit is; the cards only show a silhouette wearing the item
        this->m_component = CCharacterComponent::AllocComponent();
        model->m_refCount++;
        ComponentData data;
        data.m_preferences.m_raceID = 1;
        data.m_preferences.m_sexID = this->m_previewSex - 1;
        data.m_preferences.m_classId = 1;
        data.m_preferences.m_skinID = 0;
        data.m_preferences.m_faceId = 0;
        data.m_preferences.m_hairStyleId = this->m_previewHair;
        data.m_preferences.m_hairColorId = 0;
        data.m_preferences.m_facialHairId = 0;
        data.m_model = model;
        CCharacterComponent::ValidateComponentData(&data, COMPONENT_CONTEXT_2);
        this->m_component->Init(&data, nullptr);
        s_mainHandItemId = 0;
        s_offHandItemId = 0;
        s_nextHand = 15;
        memset(&this->m_mainHand, 0, sizeof(this->m_mainHand));
        memset(&this->m_offHand, 0, sizeof(this->m_offHand));
        this->m_isNativeDisplay = 0;
        return;
    }

    CGPlayer_C* player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(this->m_unitGuid, TYPEMASK_PLAYER, __FILE__, __LINE__));

    if (player) {
        this->m_component = CCharacterComponent::AllocComponent();
        model->m_refCount++;
        ComponentData data;
        data.m_preferences.m_raceID = player->m_unit->m_unitFieldBytes0[0];
        data.m_preferences.m_sexID = player->m_player->m_playerBytes3[0];
        data.m_preferences.m_classId = player->m_unit->m_unitFieldBytes0[1];
        data.m_preferences.m_skinID = player->m_player->m_playerBytes[0];
        data.m_preferences.m_faceId = player->m_player->m_playerBytes[1];
        data.m_preferences.m_hairStyleId = player->m_player->m_playerBytes[2];
        data.m_preferences.m_hairColorId = player->m_player->m_playerBytes[3];
        data.m_preferences.m_facialHairId = player->m_player->m_playerBytes2[0];
        data.m_model = model;
        data.m_flags ^= (data.m_flags ^ (static_cast<uint32_t>(player->m_obj->m_guid == ClntObjMgrGetActivePlayer()) << 1)) & 0x2;
        CCharacterComponent::ValidateComponentData(&data, COMPONENT_CONTEXT_2);
        this->m_component->Init(&data, nullptr);
        s_mainHandItemId = 0;
        s_offHandItemId = 0;
        s_nextHand = 15;

        if (player->GetBag()) {
            for (uint32_t slot = 0; slot <= 18; slot++) {
                const VisibleItem_C* item;
                int32_t hand;

                if (slot == 15) {
                    item = player->GetVisibleItem(15);

                    if (!item) {
                        continue;
                    }

                    hand = 15;
                } else if (slot == 16) {
                    item = player->GetVisibleItem(16);

                    if (!item) {
                        continue;
                    }

                    hand = 16;
                } else {
                    if (slot == 0 ? (player->m_player->m_playerFlags & 0x400) != 0 : slot == 14 ? (player->m_player->m_playerFlags & 0x800) != 0 : slot == 17) {
                        continue;
                    }

                    item = player->GetVisibleItem(slot);

                    if (!item) {
                        continue;
                    }

                    hand = -1;
                }

                int32_t itemId = std::abs(item->m_itemId);
                this->TryOnWithVisual(itemId, const_cast<VisibleItem_C*>(item)->GetItemEnchantmentVisualID(), hand);
            }
        }

        const CGUnitVirtualItem* mainHand = player->GetVirtualItem(0, true);

        if (mainHand) {
            this->m_mainHand = *mainHand;
        } else {
            memset(&this->m_mainHand, 0, sizeof(this->m_mainHand));
        }

        const CGUnitVirtualItem* offHand = player->GetVirtualItem(1, true);

        if (offHand) {
            this->m_offHand = *offHand;
        } else {
            memset(&this->m_offHand, 0, sizeof(this->m_offHand));
        }

        this->m_isNativeDisplay = player->m_unit->m_unitFieldNativedisplayid == player->m_unit->m_unitFieldDisplayid;
        return;
    }

    if (!this->m_creatureStats) {
        return;
    }

    CreatureDisplayInfoRec* display = g_creatureDisplayInfoDB.GetRecord(this->m_creatureStats->m_displayId[0]);

    if (!display) {
        return;
    }

    CreatureDisplayInfoExtraRec* extra = g_creatureDisplayInfoExtraDB.GetRecord(display->m_extendedDisplayInfoID);

    if (!extra) {
        return;
    }

    const char* bakeName = extra->m_bakeName;

    if (!bakeName || !*bakeName) {
        return;
    }

    this->m_component = CCharacterComponent::AllocComponent();
    ComponentData data;
    data.m_preferences.m_sexID = extra->m_displaySexID;
    data.m_preferences.m_faceId = extra->m_faceID;
    data.m_preferences.m_hairColorId = extra->m_hairColorID;
    data.m_preferences.m_raceID = extra->m_displayRaceID;
    data.m_flags |= 0x1;
    data.m_preferences.m_skinID = extra->m_skinID;
    data.m_preferences.m_classId = 1;
    data.m_preferences.m_hairStyleId = extra->m_hairStyleID;
    data.m_preferences.m_facialHairId = extra->m_facialHairID;
    data.m_model = model;
    SStrPrintf(data.m_npcSkinTexture, sizeof(data.m_npcSkinTexture), "%s%s", "Textures\\BakedNpcTextures\\", bakeName);
    data.m_model->m_refCount++;
    this->m_component->Init(&data, nullptr);

    for (uint32_t i = 0; i < 11; i++) {
        if (extra->m_npcitemDisplay[i]) {
            this->m_component->AddItem(static_cast<ITEM_SLOT>(i), extra->m_npcitemDisplay[i], 0);
        }
    }
}
