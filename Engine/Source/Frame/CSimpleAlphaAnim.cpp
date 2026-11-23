#include <cstddef>
#include <cstdint>
#include "Engine/Source/Frame/CSimpleAlphaAnim.h"
#include "Engine/Source/Frame/CScriptObject.h"
#include "Engine/Source/Frame/CSimpleAnim.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/Base/Coordinate.h"
#include "Engine/Source/Base/Status.h"
#include "Engine/Source/Frame/CScriptRegion.h"
#include "Engine/Source/Frame/CSimpleAnimGroup.h"
#include "Engine/Source/FrameXML/LoadXML.h"
#include "Engine/Source/FrameXML/XMLTree.h"

// STATIC: 0x00AC1AB0
int32_t CSimpleAlphaAnim::s_metatable = LUA_NOREF;

// STATIC: 0x00B499E0
int32_t CSimpleAlphaAnim::s_objectType;

// STATIC: 0x00AC13A4
const char* CSimpleAlphaAnim::s_objectTypeName = "Alpha";

// INLINED
int32_t CSimpleAlphaAnim::GetObjectType() {
    if (!CSimpleAlphaAnim::s_objectType) {
        CSimpleAlphaAnim::s_objectType = ++FrameScript_Object::s_objectTypes;
    }

    return CSimpleAlphaAnim::s_objectType;
}

// VERIFY: 5e0d6557 auto
// OFFSET: 0x00499670
bool CSimpleAlphaAnim::IsA(int32_t type) {
    return type == CSimpleAlphaAnim::s_objectType || type == CSimpleAnim::s_objectType || type == CScriptObject::s_objectType;
}

// VERIFY: b32b8c67 auto
// OFFSET: 0x004996A0
bool CSimpleAlphaAnim::IsA(const char* typeName) {
    return !SStrCmpI(typeName, CSimpleAlphaAnim::s_objectTypeName, STORM_MAX_STR) || !SStrCmpI(typeName, CSimpleAnim::s_objectTypeName, STORM_MAX_STR) || !SStrCmpI(typeName, CScriptObject::s_objectTypeName, STORM_MAX_STR);
}

// VERIFY: 76fd86c5 auto
// OFFSET: 0x00499700
const char* CSimpleAlphaAnim::GetObjectTypeName() {
    return CSimpleAlphaAnim::s_objectTypeName;
}

// VERIFY: c0bf94aa auto
// OFFSET: 0x00499710
int32_t CSimpleAlphaAnim::GetScriptMetaTable() {
    return CSimpleAlphaAnim::s_metatable;
}

// VERIFY: c8bcaede auto
// OFFSET: 0x00497700
void CSimpleAlphaAnim::ApplyInverse(float progress) {
    this->Apply(-progress);
}

// VERIFY: 41369f46 auto
// OFFSET: 0x004982E0
void CSimpleAlphaAnim::SetChange(float change) {
    float value = change < -1.0f ? -1.0f : (change >= 1.0f ? 1.0f : change);
    this->m_change = static_cast<int16_t>(value * 255.0f);
}

// VERIFY: edf73845 manual
// OFFSET: 0x00498330
void CSimpleAlphaAnim::Apply(float progress) {
    CScriptRegion* region = this->GetRegionParent();

    if (region) {
        region->AddAnimAlpha(static_cast<int16_t>(this->m_change * progress));
    }
}

// VERIFY: 3fbc954e manual
// OFFSET: 0x004999F0
CSimpleAlphaAnim::~CSimpleAlphaAnim() {
}

// VERIFY: dfb32dfa auto
// OFFSET: 0x00499E80
CSimpleAlphaAnim::CSimpleAlphaAnim(CSimpleAnimGroup* group) : CSimpleAnim(group) {
    this->m_change = 0;
}

// VERIFY: 178448cd auto
// OFFSET: 0x0049C170
void CSimpleAlphaAnim::LoadXML(XMLNode* node, CStatus* status) {
    CSimpleAnim::LoadXML(node, status);

    float value;
    const char* change = node->GetAttributeByName("change");

    if (change && *change) {
        value = SStrToFloat(change);

        if (value < -1.0f || value > 1.0f) {
            const char* name = this->GetName();
            status->Add(STATUS_WARNING, "%s: Invalid change value: %s. Value must be between %d and %d, inclusive.", name ? name : "<unnamed>", change, -1.0f, 1.0f);
        }

        value = value < -1.0f ? -1.0f : (value < 1.0f ? value : 1.0f);
    } else {
        value = 0.0f;
    }

    // Not in the original: retail fromAlpha/toAlpha. Alpha animations add to the region's alpha here, so this is
    // exact when the region rests at fromAlpha, which is how retail sets them up.
    const char* fromAlpha = node->GetAttributeByName("fromAlpha");
    const char* toAlpha = node->GetAttributeByName("toAlpha");

    if ((fromAlpha && *fromAlpha) || (toAlpha && *toAlpha)) {
        value = (toAlpha && *toAlpha ? SStrToFloat(toAlpha) : 1.0f) - (fromAlpha && *fromAlpha ? SStrToFloat(fromAlpha) : 1.0f);
        value = value < -1.0f ? -1.0f : (value < 1.0f ? value : 1.0f);
    }

    this->m_change = static_cast<int16_t>(value * 255.0f);

    for (XMLNode* child = node->m_child; child; child = child->m_next) {
        if (!SStrCmpI(child->m_name.m_str, "Scripts", STORM_MAX_STR)) {
            this->LoadXML_Scripts(child, status);
        }
    }
}
