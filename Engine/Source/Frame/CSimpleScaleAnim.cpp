#include <cstddef>
#include <cstdint>
#include "Engine/Source/Frame/CSimpleScaleAnim.h"
#include "Engine/Source/Frame/CScriptObject.h"
#include "Engine/Source/Frame/CSimpleAnim.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include <cmath>
#include "Engine/Source/Base/Coordinate.h"
#include "Engine/Source/Base/Status.h"
#include "Engine/Source/Frame/CScriptRegion.h"
#include "Engine/Source/Frame/CSimpleAnimGroup.h"
#include "Engine/Source/FrameXML/LoadXML.h"
#include "Engine/Source/FrameXML/XMLTree.h"

// STATIC: 0x00AC1A44
int32_t CSimpleScaleAnim::s_metatable = LUA_NOREF;

// STATIC: 0x00B499EC
int32_t CSimpleScaleAnim::s_objectType;

// STATIC: 0x00AC1398
const char* CSimpleScaleAnim::s_objectTypeName = "Scale";

// INLINED
int32_t CSimpleScaleAnim::GetObjectType() {
    if (!CSimpleScaleAnim::s_objectType) {
        CSimpleScaleAnim::s_objectType = ++FrameScript_Object::s_objectTypes;
    }

    return CSimpleScaleAnim::s_objectType;
}

// VERIFY: d7142d24 auto
// OFFSET: 0x00498F70
bool CSimpleScaleAnim::IsA(int32_t type) {
    return type == CSimpleScaleAnim::s_objectType || type == CSimpleAnim::s_objectType || type == CScriptObject::s_objectType;
}

// VERIFY: 0caaa318 auto
// OFFSET: 0x00498FA0
bool CSimpleScaleAnim::IsA(const char* typeName) {
    return !SStrCmpI(typeName, CSimpleScaleAnim::s_objectTypeName, STORM_MAX_STR) || !SStrCmpI(typeName, CSimpleAnim::s_objectTypeName, STORM_MAX_STR) || !SStrCmpI(typeName, CScriptObject::s_objectTypeName, STORM_MAX_STR);
}

// VERIFY: b62e6600 auto
// OFFSET: 0x00499000
const char* CSimpleScaleAnim::GetObjectTypeName() {
    return CSimpleScaleAnim::s_objectTypeName;
}

// VERIFY: f86ff650 auto
// OFFSET: 0x00499010
int32_t CSimpleScaleAnim::GetScriptMetaTable() {
    return CSimpleScaleAnim::s_metatable;
}

// VERIFY: 8ee7502a manual
// OFFSET: 0x00497720
void CSimpleScaleAnim::SetScale(const C2Vector& scale) {
    this->m_scaleChange.x = 1.0f - (scale.x >= 0.001f ? scale.x : 0.001f);
    this->m_scaleChange.y = 1.0f - (scale.y >= 0.001f ? scale.y : 0.001f);
}

// VERIFY: 34a3e181 auto
// OFFSET: 0x004980D0
void CSimpleScaleAnim::GetScale(C2Vector& scale) {
    scale.x = 1.0f - this->m_scaleChange.x;
    scale.y = 1.0f - this->m_scaleChange.y;
}

// VERIFY: 7583af41 manual
// OFFSET: 0x004980F0
void CSimpleScaleAnim::Apply(float progress) {
    CScriptRegion* region = this->GetRegionParent();

    if (region) {
        C2Vector scale;
        scale.x = this->m_scaleChange.x * progress;
        scale.y = progress * this->m_scaleChange.y;
        region->AddAnimScale(this->m_origin, this->m_originOffset, scale);
    }
}

// VERIFY: a3ba9709 manual
// OFFSET: 0x00498150
void CSimpleScaleAnim::ApplyInverse(float progress) {
    CScriptRegion* region = this->GetRegionParent();

    if (region) {
        float x = this->m_scaleChange.x * progress;
        float y = progress * this->m_scaleChange.y;
        float inverseY = fabsf(y) >= 0.00000023841858f ? 1.0f / y : 1.0f;
        float inverseX = fabsf(x) >= 0.00000023841858f ? 1.0f / x : 1.0f;

        C2Vector scale;
        scale.x = 1.0f - inverseX;
        scale.y = 1.0f - inverseY;
        region->AddAnimScale(this->m_origin, this->m_originOffset, scale);
    }
}

// VERIFY: 8433c62a manual
// OFFSET: 0x00499810
CSimpleScaleAnim::~CSimpleScaleAnim() {
}

// VERIFY: 9e3f32b8 auto
// OFFSET: 0x00499CC0
CSimpleScaleAnim::CSimpleScaleAnim(CSimpleAnimGroup* group) : CSimpleAnim(group) {
    this->m_origin = FRAMEPOINT_CENTER;
}

// VERIFY: 464c163f manual
// OFFSET: 0x0049BD20
void CSimpleScaleAnim::LoadXML(XMLNode* node, CStatus* status) {
    CSimpleAnim::LoadXML(node, status);

    C2Vector scale;

    const char* scaleX = node->GetAttributeByName("scaleX");

    if (scaleX && *scaleX) {
        scale.x = SStrToFloat(scaleX);

        if (scale.x < 0.001f) {
            const char* name = this->GetName();
            status->Add(STATUS_WARNING, "%s: Invalid scaleX value: %s. Value must be at least %d.", name ? name : "<unnamed>", scaleX, 0.001f);
        }
    }

    const char* scaleY = node->GetAttributeByName("scaleY");

    if (scaleY && *scaleY) {
        scale.y = SStrToFloat(scaleY);

        if (scale.y < 0.001f) {
            const char* name = this->GetName();
            status->Add(STATUS_WARNING, "%s: Invalid scaleY value: %s. Value must be at least %d.", name ? name : "<unnamed>", scaleY, 0.001f);
        }
    }

    // Not in the original: retail fromScaleX/toScaleX (and Y). Scale animations multiply here, so to/from is exact
    // when the region rests at the from scale.
    const char* fromX = node->GetAttributeByName("fromScaleX");
    const char* toX = node->GetAttributeByName("toScaleX");
    const char* fromY = node->GetAttributeByName("fromScaleY");
    const char* toY = node->GetAttributeByName("toScaleY");

    if ((fromX && *fromX) || (toX && *toX) || (fromY && *fromY) || (toY && *toY)) {
        auto ratio = [](const char* from, const char* to) {
            float f = from && *from ? SStrToFloat(from) : 1.0f;
            float t = to && *to ? SStrToFloat(to) : 1.0f;
            return f < 0.001f ? (t < 0.001f ? 0.001f : t) : (t / f < 0.001f ? 0.001f : t / f);
        };
        scale.x = ratio(fromX, toX);
        scale.y = ratio(fromY, toY);
    }

    this->SetScale(scale);

    for (XMLNode* child = node->m_child; child; child = child->m_next) {
        if (!SStrCmpI(child->m_name.m_str, "Scripts", STORM_MAX_STR)) {
            this->LoadXML_Scripts(child, status);
        } else if (!SStrCmpI(child->m_name.m_str, "Origin", STORM_MAX_STR) && !LoadXML_AnimOrigin(child, this->m_origin, this->m_originOffset, status)) {
            const char* name = this->GetName();
            status->Add(STATUS_WARNING, "%s %s: Error loading Origin element", this->GetObjectTypeName(), name ? name : "<unnamed>");
        }
    }
}
