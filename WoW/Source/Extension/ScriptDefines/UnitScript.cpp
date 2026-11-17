#include "WoW/Source/Extension/ScriptDefines/UnitScript.h"
#include "WoW/Source/Extension/ExtensionMgr.h"
#include "WoW/Source/Extension/ExtensionMgrMacros.h"
#include "WoW/Source/Object/Movement.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"

// The mover the fall seams fire for, or null (no UnitScript, or not the active mover).
static CGUnit_C* ActiveMover(CMovement_C* movement) {
    if (ScriptRegistry<UnitScript>::ScriptPointerList.empty() || !movement->m_unit || movement->m_unit->m_obj->m_guid != CGUnit_C::s_activeMover) {
        return nullptr;
    }

    return movement->m_unit;
}

void ExtensionMgr::OnUnitMoveUpdate(CGUnit_C* unit, uint32_t time) {
    CALL_HOOKS(UnitScript, script->OnUnitMoveUpdate(*unit, time));
}

void ExtensionMgr::OnUnitDisplayInfoUpdated(CGUnit_C* unit) {
    CALL_HOOKS(UnitScript, script->OnUnitDisplayInfoUpdated(*unit));
}

void ExtensionMgr::OnUnitDeath(CGUnit_C* unit) {
    CALL_HOOKS(UnitScript, script->OnUnitDeath(*unit));
}

void ExtensionMgr::OnUnitAuraAdded(CGUnit_C* unit, uint32_t index, AuraData* aura, int32_t spellId) {
    CALL_HOOKS(UnitScript, script->OnUnitAuraAdded(*unit, index, *aura, spellId));
}

void ExtensionMgr::OnUnitAuraRemoved(CGUnit_C* unit, uint32_t index, AuraData* aura, int32_t spellId) {
    CALL_HOOKS(UnitScript, script->OnUnitAuraRemoved(*unit, index, *aura, spellId));
}

void ExtensionMgr::OnUnitAuraEffectMaskUpdated(CGUnit_C* unit) {
    CALL_HOOKS(UnitScript, script->OnUnitAuraEffectMaskUpdated(*unit));
}

void ExtensionMgr::OnUnitWorldTransformUpdated(CGUnit_C* unit, float elapsed) {
    CALL_HOOKS(UnitScript, script->OnUnitWorldTransformUpdated(*unit, elapsed));
}

void ExtensionMgr::OnUnitMovementMessage(CGUnit_C* unit, uint32_t msgId, CDataStore* msg) {
    CALL_HOOKS(UnitScript, script->OnUnitMovementMessage(*unit, msgId, *msg));
}

void ExtensionMgr::OnMoverStartFalling(CMovementShared* shared, float& velocity) {
    // Every client CMovementShared is the base of a CMovement_C (Jump's callers are all CMovement_C).
    CMovement_C* movement = static_cast<CMovement_C*>(shared);

    if (CGUnit_C* mover = ActiveMover(movement)) {
        CALL_HOOKS(UnitScript, script->OnMoverStartFalling(*mover, *movement, velocity));
    }
}

void ExtensionMgr::OnMoverFallDistance(CMovement_C* movement, MoverFallSite site, uint32_t time, float& distance) {
    if (CGUnit_C* mover = ActiveMover(movement)) {
        CALL_HOOKS(UnitScript, script->OnMoverFallDistance(*mover, *movement, site, time, distance));
    }
}

void ExtensionMgr::OnMoverFallTime(CMovement_C* movement, float distance, int32_t upward, float& seconds) {
    if (CGUnit_C* mover = ActiveMover(movement)) {
        CALL_HOOKS(UnitScript, script->OnMoverFallTime(*mover, *movement, distance, upward, seconds));
    }
}

void ExtensionMgr::OnMoverFall(CMovement_C* movement, MoverFallSite site, uint32_t time, uint32_t elapsed, float& distance, C2Vector& direction) {
    if (CGUnit_C* mover = ActiveMover(movement)) {
        CALL_HOOKS(UnitScript, script->OnMoverFall(*mover, *movement, site, time, elapsed, distance, direction));
    }
}

void ExtensionMgr::OnMoverFallDown(CMovement_C* movement, MoverFallSite site, uint32_t time, uint32_t elapsed, C3Vector& move) {
    if (CGUnit_C* mover = ActiveMover(movement)) {
        CALL_HOOKS(UnitScript, script->OnMoverFallDown(*mover, *movement, site, time, elapsed, move));
    }
}

void ExtensionMgr::OnMoverFallDownDone(CMovement_C* movement, MoverFallSite site, uint32_t consumed) {
    if (CGUnit_C* mover = ActiveMover(movement)) {
        CALL_HOOKS(UnitScript, script->OnMoverFallDownDone(*mover, *movement, site, consumed));
    }
}

void ExtensionMgr::OnMoverFloorProbe(CMovement_C* movement, const C2Vector& direction, const C3Vector& position, float height, int32_t& result) {
    if (CGUnit_C* mover = ActiveMover(movement)) {
        CALL_HOOKS(UnitScript, script->OnMoverFloorProbe(*mover, *movement, direction, position, height, result));
    }
}

UnitScript::UnitScript(const char* name) : ScriptObject(name) {
    ScriptRegistry<UnitScript>::AddScript(this);
    sExtensionMgr->Log("Extensions: registered UnitScript %s", name);
}
