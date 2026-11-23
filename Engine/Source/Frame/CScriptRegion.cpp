#include <cstddef>
#include <cstdint>
#include "Engine/Source/Frame/CScriptRegion.h"
#include "Engine/Source/Base/Coordinate.h"
#include "Engine/Source/Base/Status.h"
#include "Engine/Source/Frame/CSimpleAlphaAnim.h"
#include "Engine/Source/Frame/CSimpleAnimGroup.h"
#include "Engine/Source/Frame/CSimpleFrame.h"
#include "Engine/Source/Frame/CSimpleTop.h"
#include "Engine/Source/Frame/RetailXML.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/FrameXML/LoadXML.h"
#include "Engine/Source/FrameXML/XMLTree.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Tempest/CRect.h"
#include "Engine/Source/lua-5.1/src/lua.h"

// STATIC: 0x00B49978
int32_t CScriptRegion::s_objectType;

// STATIC: 0x00AC0FB0
const char* CScriptRegion::s_objectTypeName = "Region";

// INLINED
int32_t CScriptRegion::GetObjectType() {
    if (!CScriptRegion::s_objectType) {
        CScriptRegion::s_objectType = ++FrameScript_Object::s_objectTypes;
    }

    return CScriptRegion::s_objectType;
}

// VERIFY: 7bfc2639 auto
// OFFSET: 0x00482B40
CScriptObject* CScriptRegion::GetScriptObjectParent() {
    return this->m_parent;
}

// VERIFY: 35de1b9a auto
// OFFSET: 0x00482B50
bool CScriptRegion::IsA(int32_t type) {
    return type == CScriptRegion::s_objectType || type == CScriptObject::s_objectType;
}

// VERIFY: 25d91e66 auto
// OFFSET: 0x00482B80
bool CScriptRegion::IsA(const char* typeName) {
    return !SStrCmpI(typeName, CScriptRegion::s_objectTypeName, STORM_MAX_STR) || !SStrCmpI(typeName, CScriptObject::s_objectTypeName, STORM_MAX_STR);
}

// VERIFY: 0eb6bce3 auto
// OFFSET: 0x004881C0
const char* CScriptRegion::GetObjectTypeName() {
    return CScriptRegion::s_objectTypeName;
}

// VERIFY: c4694f97 manual
// OFFSET: 0x00488310
CScriptRegion::CScriptRegion() {
    this->m_parent = nullptr;
    this->m_animGroups = nullptr;
    this->m_playingAnimGroups = nullptr;
}

// VERIFY: 59f5e1dd manual
// OFFSET: 0x00488350
bool CScriptRegion::IsDragging() {
    return this->m_parent ? this->m_parent->IsDragging() : false;
}

// VERIFY: e4b70ae0 auto
// OFFSET: 0x004883A0
void CScriptRegion::SetParent(CSimpleFrame* parent) {
    this->m_parent = parent;
}

// VERIFY: 2e5f1acc manual
// OFFSET: 0x004883F0
void CScriptRegion::LoadXML_Animations(XMLNode* node, CStatus* status) {
    XMLNode* animations = node->GetChildByName("Animations");

    if (!animations) {
        return;
    }

    for (XMLNode* child = animations->m_child; child; child = child->m_next) {
        if (SStrCmpI(child->m_name.m_str, "AnimationGroup", STORM_MAX_STR)) {
            const char* name = this->GetName();
            status->Add(STATUS_WARNING, "%s %s: Unknown child node in %s element: %s", this->GetObjectTypeName(), name ? name : "<unnamed>", animations->m_name.m_str, child->m_name.m_str);
        } else {
            LoadXML_AnimGroup(child, this, status);
        }
    }
}

// VERIFY: 1c1c5c31 manual
// OFFSET: 0x004884B0
CLayoutFrame* CScriptRegion::GetLayoutParent() {
    CLayoutFrame* parent = this->m_parent;
    return parent ? parent : CSimpleTop::s_instance;
}

// VERIFY: c93d6a9b manual
// OFFSET: 0x004884D0
CLayoutFrame* CScriptRegion::GetLayoutFrameByName(const char* name) {
    char fullName[1024];
    this->CreateName(name, fullName, sizeof(fullName));

    CScriptRegion* region = static_cast<CScriptRegion*>(CScriptObject::GetScriptObjectByName(fullName, CScriptRegion::GetObjectType()));
    return region;
}

// VERIFY: 6a00f6bb manual
// OFFSET: 0x00488540
bool CScriptRegion::ProtectedFunctionsAllowed() {
    return !this->IsProtected() || !lua_tainted || CSimpleTop::s_instance->m_allowProtectedActions != 0;
}

// VERIFY: 5c7fa85d auto
// OFFSET: 0x00488570
bool CScriptRegion::IsMouseOver(float top, float bottom, float left, float right) {
    CSimpleTop* simpleTop = CSimpleTop::s_instance;

    if (!simpleTop) {
        return false;
    }

    if (this->IsResizePending()) {
        this->Resize(1);
    }

    CRect rect = { 0.0f, 0.0f, 0.0f, 0.0f };

    if (!this->GetRect(&rect)) {
        return false;
    }

    float ddcX = 0.0f;
    float ddcY = 0.0f;
    NDCToDDC(simpleTop->m_mousePosition.m_x, simpleTop->m_mousePosition.m_y, &ddcX, &ddcY);

    float x = 1.0f / this->m_layoutScale * ddcX;
    float y = 1.0f / this->m_layoutScale * ddcY;
    float scale = 1.0f / this->m_layoutScale;

    return rect.minX * scale + left < x && rect.maxX * scale + right > x && scale * rect.minY + bottom < y && scale * rect.maxY + top > y;
}

// VERIFY: 6702c0a1 manual
// OFFSET: 0x00488670
void CScriptRegion::LoadXML(XMLNode* node, CStatus* status) {
    CLayoutFrame::LoadXML(node, status);

    const char* parentKey = node->GetAttributeByName("parentKey");

    if (parentKey && *parentKey) {
        lua_State* L = FrameScript_GetContext();
        CScriptObject* parent = this->GetScriptObjectParent();

        if (parent) {
            if (!parent->m_luaRegistered) {
                parent->RegisterScriptObject(nullptr);
            }

            lua_rawgeti(L, LUA_REGISTRYINDEX, parent->m_luaObjectRef);
            lua_pushstring(L, parentKey);

            if (!this->m_luaRegistered) {
                this->RegisterScriptObject(nullptr);
            }

            lua_rawgeti(L, LUA_REGISTRYINDEX, this->m_luaObjectRef);
            lua_settable(L, -3);
            lua_settop(L, -2);
        }
    }

    // Not in the original: retail mixin/KeyValues/parentArray and relativeKey anchors.
    RetailXML_LoadObject(this, this->GetScriptObjectParent(), node, status);
    RetailXML_LoadRelativeAnchors(this, node, status);

    this->LoadXML_Animations(node, status);
}

// VERIFY: 23893fb6 auto
// OFFSET: 0x00488740
int32_t CScriptRegion::HasActiveAnimGroup() {
    for (CScriptRegion* region = this; region; region = region->m_parent) {
        if (region->m_playingAnimGroups && region->m_playingAnimGroups->Head()) {
            return 1;
        }
    }

    return 0;
}

// VERIFY: 0d8bdeba manual
// OFFSET: 0x00488770
int32_t CScriptRegion::HasActiveAnimOfType(int32_t type) {
    for (CScriptRegion* region = this; region; region = region->m_parent) {
        if (region->m_playingAnimGroups && region->m_playingAnimGroups->Head()) {
            for (CSimpleAnimGroup* group = region->m_playingAnimGroups->Head(); group; group = region->m_playingAnimGroups->Next(group)) {
                if (group->HasAnimOfType(type)) {
                    return 1;
                }
            }
        }
    }

    return 0;
}

// VERIFY: cd671bd7 manual
// OFFSET: 0x004887F0
void CScriptRegion::GetActiveAnimInfo(int32_t type, int32_t& active, int32_t& activeOfType) {
    active = 0;
    activeOfType = 0;

    for (CScriptRegion* region = this; region; region = region->m_parent) {
        if (region->m_playingAnimGroups && region->m_playingAnimGroups->Head()) {
            active = 1;

            for (CSimpleAnimGroup* group = region->m_playingAnimGroups->Head(); group; group = region->m_playingAnimGroups->Next(group)) {
                if (group->HasAnimOfType(type)) {
                    activeOfType = 1;
                    break;
                }
            }
        }
    }
}

// VERIFY: 1fdc6a93 manual
// OFFSET: 0x00488890
void CScriptRegion::OnAnimUpdate(float elapsedSec) {
    if (!this->m_animGroups) {
        return;
    }

    for (CSimpleAnimGroup* group = this->m_animGroups->Head(); group; group = this->m_animGroups->Next(group)) {
        group->OnUpdate(elapsedSec);
    }
}

// VERIFY: a1b47d00 manual
// OFFSET: 0x004888F0
void CScriptRegion::StopAnimating() {
    if (!this->m_playingAnimGroups) {
        return;
    }

    for (CSimpleAnimGroup* group = this->m_playingAnimGroups->Head(); group; group = this->m_playingAnimGroups->Next(group)) {
        group->Stop(true);
    }
}

// VERIFY: 21de8cdf auto
// OFFSET: 0x00488980
void CScriptRegion::NotifyAnimEnd(CSimpleAnimGroup* group) {
    if (!group || !this->m_playingAnimGroups) {
        return;
    }

    this->m_playingAnimGroups->UnlinkNode(group);
    this->AnimDeactivated(group, !this->HasActiveAnimGroup());
}

// VERIFY: 3973d91a manual
// OFFSET: 0x004889C0
void CScriptRegion::NotifyAnimBegin(CSimpleAnimGroup* group) {
    if (!group) {
        return;
    }

    if (!this->m_playingAnimGroups) {
        this->m_playingAnimGroups = new (SMemAlloc(sizeof(*this->m_playingAnimGroups), __FILE__, __LINE__, 0)) TSExplicitList<CSimpleAnimGroup, offsetof(CSimpleAnimGroup, m_playingLink)>;

        if (!this->m_playingAnimGroups) {
            return;
        }
    }

    this->AnimActivated(group, !this->HasActiveAnimGroup(), group->HasAnimOfType(CSimpleAlphaAnim::GetObjectType()));
    this->m_playingAnimGroups->LinkToTail(group);
}

// VERIFY: 5d173137 manual
// OFFSET: 0x00488AE0
void CScriptRegion::RegisterAnimGroup(CSimpleAnimGroup* group) {
    if (!group) {
        return;
    }

    if (!this->m_animGroups) {
        this->m_animGroups = new (SMemAlloc(sizeof(*this->m_animGroups), __FILE__, __LINE__, 0)) TSExplicitList<CSimpleAnimGroup, offsetof(CSimpleAnimGroup, m_regionLink)>;
    }

    if (this->m_animGroups) {
        this->m_animGroups->LinkToTail(group);
    }
}

// VERIFY: 37e65262 manual
// OFFSET: [0x00488370, 0x00488B50, 0x00488BB0]
CScriptRegion::~CScriptRegion() {
    if (this->m_animGroups) {
        this->m_animGroups->Clear();
        delete this->m_animGroups;
    }

    if (this->m_playingAnimGroups) {
        delete this->m_playingAnimGroups;
    }
}

// VERIFY: 4d845176 manual
// OFFSET: 0x005EEB70
void CScriptRegion::PreOnAnimUpdate() {
}

// VERIFY: c64042c5 auto
// OFFSET: 0x0047ADD0
void CScriptRegion::AnimActivated(CSimpleAnimGroup* group, int32_t first, int32_t hasAlpha) {
}

// VERIFY: 83a5621b auto
// OFFSET: 0x00653A10
void CScriptRegion::AnimDeactivated(CSimpleAnimGroup* group, int32_t last) {
}

// VERIFY: 3b0fa002 auto
// OFFSET: 0x00632050
void CScriptRegion::AddAnimTranslation(const C2Vector& translation) {
}

// VERIFY: 3cc88c19 auto
// OFFSET: 0x0047ADD0
void CScriptRegion::AddAnimRotation(FRAMEPOINT origin, const C2Vector& offset, float radians) {
}

// VERIFY: 3b3ba4d8 auto
// OFFSET: 0x0047ADD0
void CScriptRegion::AddAnimScale(FRAMEPOINT origin, const C2Vector& offset, const C2Vector& scale) {
}

// VERIFY: db7e550e auto
// OFFSET: 0x00632050
void CScriptRegion::AddAnimAlpha(int16_t alpha) {
}
