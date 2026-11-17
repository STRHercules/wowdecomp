#include "WoW/Source/Extension/ScriptDefines/WorldScript.h"
#include "WoW/Source/Extension/ExtensionMgr.h"
#include "WoW/Source/Extension/ExtensionMgrMacros.h"

void ExtensionMgr::OnLoginVerifyWorld(uint32_t mapId, const C3Vector* position, float facing) {
    CALL_HOOKS(WorldScript, script->OnLoginVerifyWorld(mapId, *position, facing));
}

void ExtensionMgr::OnLogoutComplete() {
    CALL_HOOKS(WorldScript, script->OnLogoutComplete());
}

void ExtensionMgr::OnMapLoaded(uint32_t mapId, const char* mapName) {
    CALL_HOOKS(WorldScript, script->OnMapLoaded(mapId, mapName));
}

void ExtensionMgr::OnMapUnload() {
    CALL_HOOKS(WorldScript, script->OnMapUnload());
}

void ExtensionMgr::OnWorldUpdate(float elapsedSec) {
    CALL_HOOKS(WorldScript, script->OnWorldUpdate(elapsedSec));
}

void ExtensionMgr::OnCameraUpdated(CGCamera* camera) {
    CALL_HOOKS(WorldScript, script->OnCameraUpdated(*camera));
}

void ExtensionMgr::OnCameraTargetFacing(CGCamera* camera, CGObject_C* target, float& yaw, float& pitch, float& roll) {
    if (target) {
        CALL_HOOKS(WorldScript, script->OnCameraTargetFacing(*camera, *target, yaw, pitch, roll));
    }
}

WorldScript::WorldScript(const char* name) : ScriptObject(name) {
    ScriptRegistry<WorldScript>::AddScript(this);
    sExtensionMgr->Log("Extensions: registered WorldScript %s", name);
}
