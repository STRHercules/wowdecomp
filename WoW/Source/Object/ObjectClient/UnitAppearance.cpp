#include "WoW/Source/Extension/AppearanceOverrides.h"
#include "WoW/Source/DB/DBClient/DBClient.h"

const CGUnitVirtualItem* CGUnit_C::GetAppearanceItem(uint32_t slot, bool ignoreDisarm) const {
    if (slot < 3) {
        if (const auto* look = ClientAppearance::Resolve(*this, static_cast<int32_t>(slot + 15))) {
            if (!look->display) return nullptr;
            if (!ignoreDisarm && (slot < 2 ? IsDisarmed(slot) != 0 : (m_unit->m_unitFieldFlags2 & 0x400) != 0)) return nullptr;
            return &look->weapon;
        }
    }
    return GetVirtualItem(slot, ignoreDisarm);
}

uint32_t CGUnit_C::GetAppearanceDisplayID(uint32_t slot) const {
    if (slot < 3) {
        if (const auto* look = ClientAppearance::Resolve(*this, static_cast<int32_t>(slot + 15)))
            return GetAppearanceItem(slot) ? look->display : 0;
    }
    return GetVirtualItemDisplayID(slot);
}

int32_t CGUnit_C::GetAppearanceDisplayRec(uint32_t slot, ItemDisplayInfoRec& rec) const {
    return g_itemDisplayInfoDB.GetRecord(GetAppearanceDisplayID(slot), &rec);
}
