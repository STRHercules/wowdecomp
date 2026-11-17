#ifndef WOW_SOURCE_EXTENSION_SCRIPTDEFINES_UNITSCRIPT_H
#define WOW_SOURCE_EXTENSION_SCRIPTDEFINES_UNITSCRIPT_H

#include <cstdint>
#include "WoW/Source/Extension/ScriptObject.h"

class AuraData;
class C3Vector;
class C2Vector;
class CDataStore;
class CGUnit_C;
class CMovement_C;

// Which stock call site a mover fall seam fires from (the 12340 return address in brackets). The
// same function means different things per caller: WillPassObstacle's falls are a throwaway
// step-down probe that CMovement_C::RestoreMoveState undoes, the others move the unit.
enum MoverFallSite : uint32_t {
    MOVER_FALL_SITE_MOVE_FACETS,          // CMovement_C::GetMoveFacets, sizing the query box [0x760154]
    MOVER_FALL_SITE_FALL,                 // CMovement_C::Fall, the real fall step [0x7618F6, 0x7619A9]
    MOVER_FALL_SITE_FLOOR_PROBE,          // CMovement_C::WillPassObstacle probe [0x761A20, 0x761A66]
    MOVER_FALL_SITE_HOVER_MOVE,           // CMovement_C::HoverMove calling Fall [0x7629F5]
    MOVER_FALL_SITE_COLLIDE_REQUEST_MOVE, // CMovement_C::CollideRequestMove calling Fall [0x763008]
};

// Units and players (CGUnit_C, WoW/Source/Object/ObjectClient/Unit_C.cpp): the method-level
// seams of a unit's life that ObjectScript's create/update/remove/destroy does not see. Every hook
// passes the native unit by reference; unit.m_obj->m_type & TYPEMASK_PLAYER tells a player, and
// unit.m_obj->m_guid == ClntObjMgrGetActivePlayer() the local player.
class UnitScript : public ScriptObject {
    protected:
    explicit UnitScript(const char* name);

    public:
    // End of CGUnit_C::OnMoveUpdate: the unit's world object, vehicle matrix, ground terrain type
    // (m_terrainTypeId), flight and swimming status are updated for a movement step. Fires for
    // every unit that moved (server movement and the local player's own movement alike). time is
    // the movement timestamp.
    virtual void OnUnitMoveUpdate(CGUnit_C& unit, uint32_t time) {}

    // End of CGUnit_C::UpdateDisplayInfo when it did rebuild the display (an early return skips
    // the rebuild and this hook when nothing changed and force was 0): model, mount attachment,
    // geosets, base animation, creature sounds and the object effect manager reflect the current
    // display id, items and shapeshift. The right place to adjust or decorate a unit's model.
    virtual void OnUnitDisplayInfoUpdated(CGUnit_C& unit) {}

    // Start of CGUnit_C::OnDeath: the unit's health reached zero; its combat state is about to be
    // cleared and the death animation started. Everything about the unit is still as alive.
    virtual void OnUnitDeath(CGUnit_C& unit) {}

    // End of CGUnit_C::OnAuraAdded: the aura's visual effect is applied (AddAuraEffect), the
    // combat log line written and, for the local player, the action bar / tracking / shapeshift
    // side effects run. index is the aura slot, spellId the Spell.dbc id.
    virtual void OnUnitAuraAdded(CGUnit_C& unit, uint32_t index, AuraData& aura, int32_t spellId) {}

    // Start of CGUnit_C::OnAuraRemoved: the aura is still applied; RemoveAuraEffect and the local
    // player side effects follow.
    virtual void OnUnitAuraRemoved(CGUnit_C& unit, uint32_t index, AuraData& aura, int32_t spellId) {}

    // End of CGUnit_C::UpdateAuraEffectMask: unit.m_auraEffectMask is rebuilt from the unit's
    // auras (one bit per SpellAura effect type present). Fires whenever the aura set changes.
    virtual void OnUnitAuraEffectMaskUpdated(CGUnit_C& unit) {}

    // End of CGUnit_C::UpdateWorldTransform, and of CGUnit_C::Animate's swim / fly matrix path that replaces it while
    // swimming or flying in motion: the smoothed ground normal is updated and the unit's
    // model received its world transform (position, render facing, true scale, ground normal).
    // Per frame per displayed unit: keep it cheap.
    virtual void OnUnitWorldTransformUpdated(CGUnit_C& unit, float elapsed) {}

    // CGUnit_C::SendMovementUpdate right before the message goes to the connection: the local
    // player's (or controlled unit's) movement message is complete and finalized (cursor at the
    // 32-bit opcode: MSG_MOVE_*, CMSG_MOVE_*_ACK, CMSG_CHANGE_SEATS_ON_CONTROLLED_VEHICLE). Fires
    // only for messages the unit is allowed to send. Observe only; NetScript::OnMessageSent sees
    // the same message a step later.
    virtual void OnUnitMovementMessage(CGUnit_C& unit, uint32_t msgId, CDataStore& msg) {}

    // Mover fall seams. Each fires only for the active mover (CGUnit_C::s_activeMover: the unit
    // this client moves, which is a vehicle or possessed unit instead of the player while one is
    // controlled; compare mover.m_obj->m_guid with ClntObjMgrGetActivePlayer() for the player
    // only), on the main thread, from inside the movement step. movement is &mover.m_movementData.
    // Values passed by non-const reference hold the stock value and may be replaced.

    // CMovementShared::Jump, at its StartFalling call [0x98846D]: a jump passed every check and
    // velocity (the initial fall velocity, negative is upward: -7.9555 on land, -9.0967 swimming)
    // is about to start the fall. Other StartFalling callers (ledges, knockback, the step-down
    // probe) do not fire.
    virtual void OnMoverStartFalling(CGUnit_C& mover, CMovement_C& movement, float& velocity) {}

    // A RelDistanceFallen(time) result at one of three sites (MOVE_FACETS, FALL, FLOOR_PROBE):
    // distance is how far the unit fell after time ms of fall (movement.m_fallTimeMs plus the step,
    // or the probe's own time from 0), positive downward.
    virtual void OnMoverFallDistance(CGUnit_C& mover, CMovement_C& movement, MoverFallSite site, uint32_t time, float& distance) {}

    // The inverse, in CMovement_C::GetTimeJustFallen [0x75EB78]: seconds is the stock
    // CalcTimeFallen(distance, upward) result, the fall time at which the unit has fallen distance
    // (m_fallStartZ - target z). Used to find when within a step the unit reached a contact.
    virtual void OnMoverFallTime(CGUnit_C& mover, CMovement_C& movement, float distance, int32_t upward, float& seconds) {}

    // Right before CMovement_C::Fall is called from HoverMove or CollideRequestMove (site): the
    // falling unit is about to move elapsed ms of its step, distance yards horizontally along
    // direction (both copies, changing them affects only this call).
    virtual void OnMoverFall(CGUnit_C& mover, CMovement_C& movement, MoverFallSite site, uint32_t time, uint32_t elapsed, float& distance, C2Vector& direction) {}

    // Around CMovement_C::FallDown, the swept fall move with collision, from Fall or the floor
    // probe (site FALL or FLOOR_PROBE). OnMoverFallDown fires before: move is the step's
    // displacement (x/y horizontal, z the fall). OnMoverFallDownDone fires after with consumed,
    // the ms FallDown used (its return value): position, flags and the fall clock are committed,
    // and FallDown may have reset the fall clock (m_fallTimeMs 0, m_fallStartZ the current z)
    // on contact.
    virtual void OnMoverFallDown(CGUnit_C& mover, CMovement_C& movement, MoverFallSite site, uint32_t time, uint32_t elapsed, C3Vector& move) {}
    virtual void OnMoverFallDownDone(CGUnit_C& mover, CMovement_C& movement, MoverFallSite site, uint32_t consumed) {}

    // CMovement_C::AttemptStepUp after its WillPassObstacle call [0x76207F]: the stock step-down
    // probe (a simulated fall from position along direction over height yards, undone with
    // RestoreMoveState before returning) decided result, nonzero when the unit would clear the
    // obstacle. The probe's own falls fired the seams above with site FLOOR_PROBE in between.
    virtual void OnMoverFloorProbe(CGUnit_C& mover, CMovement_C& movement, const C2Vector& direction, const C3Vector& position, float height, int32_t& result) {}
};

#endif
