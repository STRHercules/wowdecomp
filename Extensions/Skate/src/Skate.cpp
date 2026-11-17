#include "SkateHelper.h"
#include "SkateInput.h"
#include "SkateDrive.h"
#include "SkateRig.h"
#include "SkateAssets.h"
#include "SkateBoard.h"
#include "SkateCamera.h"
#include "SkateAudio.h"
#include "SkatePeers.h"
#include "SkatePeerPose.h"
#include "SkatePeerTrace.h"
#include "SkatePeerSpeed.h"
#include "Engine/Source/Base/CDataStore.h"
#include <map>
#include "SkateLogic/model-board-size.h"
#include <algorithm>
#include <fstream>
#include "Engine/Source/Model2/M2Data.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Model2/M2Shared.h"
#include "Engine/Source/ConsoleBase/ConsoleCommand.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Os/TimeManager.h"
#include "WoW/Source/Extension/ExtensionMgr.h"
#include "WoW/Source/Extension/VirtualAssets.h"
#include "WoW/Source/Object/ObjectClient/Object_C.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/Object/Types.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/UIUtil/Camera.h"
#include "WoW/Source/UIUtil/InputControl.h"
#include "WoW/Source/Ui/WorldFrame.h"
#include "WoW/Source/Ui/ScriptEvents.h"
#include "WoW/Source/WorldClient/Map.h"
#include "WoW/Source/WorldClient/World.h"

namespace {
skate::Helper helper;
skate::InputGate input;
xboxinput::Reader reader;
bool inWorld = false, focused = false, connected = false;
bool debugPending = false;
// Main-thread collision gather state; the worker owns every pipe operation.
std::vector<C3Vector> gatherTris;
std::vector<uint64_t> gatherIds;
float lastCentre[3]{}, gatherCooldown = 0, lastHalf = skatecollision::HalfXY, gatherHalf = skatecollision::HalfXY;
bool haveCentre = false;
uint32_t gatherStatus = 0, gatherCount = 0, collisionGeneration = 0;
// Phase 3: the collision state UpdateCollision last saw, and a requested re-init at the player.
skate::CollisionState collision;
bool resync = false;
void InitializeDebugOverlay();
skateaudio::Service audio;
skateaudio::Status audioStatus=skateaudio::Status::Disabled;
std::wstring Wide(const char* text) {
    if (!text || !*text) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (count <= 1) return {};
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, result.data(), count);
    result.pop_back(); return result;
}
std::wstring Config(const char* name) {
    auto* var = CVar::Lookup(name);
    return var ? Wide(var->m_stringValue.m_str) : std::wstring{};
}
bool Down(int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; }
float AudioGain() {
    const auto enabled=[](const char* name) { const auto* v=CVar::Lookup(name); return v && v->m_intValue!=0; };
    const auto volume=[](const char* name) { const auto* v=CVar::Lookup(name); return v?v->m_floatValue:0.f; };
    return skateaudio::GameGain(enabled("Sound_EnableAllSound"),enabled("Sound_EnableSFX"),
        volume("Sound_MasterVolume"),volume("Sound_SFXVolume"),focused,enabled("Sound_EnableSoundWhenGameIsInBG"));
}
int32_t LocalPlayer(lua_State* L) {
    lua_pushboolean(L, inWorld && lua_isstring(L,1) && !strcmp(lua_tostring(L,1),"player") && ClntObjMgrGetActivePlayerObj());
    return 1;
}
int32_t Focus(lua_State* L) { lua_pushboolean(L,focused); return 1; }
int32_t Pad(lua_State* L) { lua_pushboolean(L,connected); return 1; }
int32_t Pose(lua_State* L) {
    skatetransport::Pose p; bool failed;
    const bool valid = helper.Snapshot(p,failed);
    lua_pushboolean(L,valid); lua_pushboolean(L,failed);
    for (float v : p.position) lua_pushnumber(L,v);
    for (float v : p.velocity) lua_pushnumber(L,v);
    lua_pushnumber(L,p.airborne); lua_pushnumber(L,p.ground); lua_pushnumber(L,collisionGeneration);
    return 11;
}
// Gather the client's own collision around the board (the player before a session exists), main thread only,
// and hand a complete copy to the helper worker: op 48 for a new session, op 49 when it nears the box edge.
void UpdateCollision(float elapsed) {
    gatherCooldown -= elapsed;
    const auto state = helper.Collision();
    collision = state;
    collisionGeneration = state.installed;
    if (state.retry) { haveCentre = false; sExtensionMgr->Log("Skate: collision snapshot not installed (%s); regathering",state.error.c_str()); }
    auto* player = ClntObjMgrGetActivePlayerObj();
    if (state.pending || gatherCooldown > 0 || !player) return;
    const C3Vector position = player->GetPosition();
    float centre[3]{position.x,position.y,position.z};
    const uint32_t map = ClntObjMgrGetMapID();
    skatetransport::Pose pose; bool failed;
    // The box follows the board pose; a resync re-inits at the player, which puts the board under it.
    if (!resync && state.session && map == state.map && helper.Snapshot(pose,failed))
        skatedrive::World(state.origin,pose.position,centre);
    float offset = 0;
    for (int i = 0; i < 3; ++i) offset = (std::max)(offset,fabsf(centre[i]-state.origin[i]));
    const bool init = resync || !state.session || map != state.map || offset > 5000.f;
    const float reach = skatecollision::RefreshDistance(lastHalf);
    if (!init && haveCentre && fabsf(centre[0]-lastCentre[0]) < reach && fabsf(centre[1]-lastCentre[1]) < reach
        && fabsf(centre[2]-lastCentre[2]) < 20.f) return;
    gatherCooldown = .5f;
    if (gatherTris.empty()) { gatherTris.resize(skatecollision::MaxTriangles*3); gatherIds.resize(skatecollision::MaxTriangles); }
    // Dense cities overflow the cap: take the largest box that fits; if none does, the installed world stays.
    float half = skatecollision::HalfXY;
    for (; half > 0; half = skatecollision::Shrink(half)) {
        CAaBox box;
        box.b = C3Vector(centre[0]-half,centre[1]-half,centre[2]-skatecollision::HalfZ);
        box.t = C3Vector(centre[0]+half,centre[1]+half,centre[2]+skatecollision::HalfZ);
        gatherStatus = 0;
        gatherCount = World::GatherCollisionTriangles(box,gatherTris.data(),skatecollision::MaxTriangles,gatherIds.data(),0x80100111,&gatherStatus);
        if (!(gatherStatus & World::COLLISION_GATHER_TRUNCATED)) break;
    }
    static uint32_t loggedStatus = 0;
    if (gatherStatus != loggedStatus && gatherStatus) {
        loggedStatus = gatherStatus;
        sExtensionMgr->Log("Skate: collision gather status %u (%u triangles), retrying",gatherStatus,gatherCount);
    }
    if (gatherStatus) return;
    loggedStatus = 0;
    if (half != lastHalf || init) sExtensionMgr->Log("Skate: collision box %.0f x %.0f x %.0f yd, %u triangles",
        half*2,half*2,skatecollision::HalfZ*2,gatherCount);
    gatherHalf = half;
    skate::CollisionSnapshot snapshot;
    snapshot.init = init; snapshot.map = map; snapshot.heading = player->GetFacing();
    for (int i = 0; i < 3; ++i) { snapshot.centre[i] = centre[i]; snapshot.origin[i] = init ? centre[i] : state.origin[i]; }
    // GUIDs never reach the wire: drop the local player's and the active mover's own collision here.
    const uint64_t self = ClntObjMgrGetActivePlayer().m_guid, mover = CGUnit_C::s_activeMover.m_guid;
    snapshot.tris.reserve(gatherCount*9);
    for (uint32_t i = 0; i < gatherCount; ++i) {
        if (gatherIds[i] && (gatherIds[i] == self || gatherIds[i] == mover)) continue;
        const C3Vector* t = &gatherTris[i*3];
        const float v[9]{t[0].x,t[0].y,t[0].z,t[1].x,t[1].y,t[1].z,t[2].x,t[2].y,t[2].z};
        if (skatecollision::Keep(v,snapshot.origin)) snapshot.tris.insert(snapshot.tris.end(),v,v+9);
    }
    if (snapshot.tris.empty()) return;
    for (int i = 0; i < 3; ++i) lastCentre[i] = centre[i];
    haveCentre = true; lastHalf = gatherHalf;
    if (init) sExtensionMgr->Log("Skate: collision init map %u, %u of %u triangles at (%.1f %.1f %.1f)",
        map,static_cast<uint32_t>(snapshot.tris.size()/9),gatherCount,centre[0],centre[1],centre[2]);
    resync = false;
    helper.Submit(std::move(snapshot));
}
// Phase 3: drive the local player from the board, through the stock movement calls the DLL used
// (run speed, timestamped facing, the forward control bit, the stock jump), so the client sends its
// ordinary movement packets and its own collision moves the character. Main thread only.
skatedrive::Driver driver;
skatedrive::Lease lease;
bool driving = false;
uint64_t driveGuid = 0;
skatedrive::PendingJump pendingJump;
uint32_t lastResync = 0;
// The ollie's vertical path: the character's fall follows the board's arc (SkateUnit::OnMoverFallDistance).
skatedrive::Arc arc;
uint32_t arcAt = 0;
uint64_t arcTick = 0;
bool arcFalling = false;
bool arcProbe = false;
uint32_t forceBailUntil = 0; // skateBail: draw the ragdoll until then
void StopArc() { arc.Stop(); arcTick = 0; arcFalling = false; pendingJump.Clear(); }
// Phase 5 camera: Drive publishes the travel heading, SkateWorld::OnCameraTargetFacing eases the view toward it.
skatecamera::Follow cameraFollow;
float cameraTarget = 0, cameraSpeed = 0;
uint32_t cameraAt = 0;
constexpr uint32_t Unsafe = 0x100 /* walk mode */ | MOVEMENTFLAG_ONTRANSPORT | MOVEMENTFLAG_ROOT | MOVEMENTFLAG_SWIMMING | MOVEMENTFLAG_FLYING;
// The run speed allowance, isolated for Phase 6 (server opcode). Phase 3 uses the GM command the DLL
// used: on AzerothCore `.modify speed walk N` sets the run rate (2 = 14 yd/s). It acts on a selected
// player, so it is skipped with another player targeted.
void SpeedAllowance(bool on) {
    FrameScript_Execute(on
        ? "if not (UnitIsPlayer('target') and not UnitIsUnit('target','player')) then SendChatMessage('.modify speed walk 2','SAY') end"
        : "if not (UnitIsPlayer('target') and not UnitIsUnit('target','player')) then SendChatMessage('.modify speed walk 1','SAY') "
          "else DEFAULT_CHAT_FRAME:AddMessage('Skate: clear your target and type .modify speed walk 1') end",
        "SkateSpeed",nullptr);
    sExtensionMgr->Log("Skate: sent .modify speed walk %d (an ack shows as lease=14.00 in the drive line)",on?2:1);
}
// The keyboard fallback reads W/S/A/D/arrows/Space itself; their stock bindings would also move/turn/jump.
void KeyOverrides(bool on) {
    FrameScript_Execute(on
        ? "local o=WrathSkateKeys or CreateFrame('Button','WrathSkateKeys') o:UnregisterAllEvents() if InCombatLockdown() then return end "
          "for _,k in ipairs({'W','S','A','D','Q','E','UP','DOWN','LEFT','RIGHT','SPACE'}) do SetOverrideBindingClick(o,true,k,'WrathSkateKeys') end"
        : "local o=WrathSkateKeys if not o then return end if not InCombatLockdown() then ClearOverrideBindings(o) return end "
          "o:RegisterEvent('PLAYER_REGEN_ENABLED') o:SetScript('OnEvent',function(s) ClearOverrideBindings(s) s:UnregisterAllEvents() end)",
        "SkateKeys",nullptr);
}
void StopKeys(uint32_t now) {
    // Also releases bits whose key-up went to an override binding.
    if (auto* input = CGInputControl::s_inputControl) input->ToggleControlBit(INPUT_MOVE_STRAFE_AND_TURN_MASK,0,now,0);
}
void Release(const char* reason) {
    StopArc();
    if (!driving) return;
    driving = false; driver.Clear(); cameraFollow.Clear(); cameraAt = 0;
    StopKeys(OsGetAsyncTimeMs());
    const float restore = lease.Release();
    auto* player = ClntObjMgrGetActivePlayerObj();
    if (player && ClntObjMgrGetActivePlayer().m_guid == driveGuid) player->m_movementData.ChangeRunSpeed(restore);
    if (inWorld) { SpeedAllowance(false); KeyOverrides(false); }
    sExtensionMgr->Log("Skate: drive off (%s), run speed restored to %.2f",reason,restore);
}
void Drive(const skate::Controls& controls) {
    auto* player = ClntObjMgrGetActivePlayerObj();
    const uint64_t guid = ClntObjMgrGetActivePlayer().m_guid;
    if (!controls.active || !player || CGUnit_C::s_activeMover.m_guid != guid) {
        Release(controls.active ? "not controlling the player" : "J released or focus lost"); return;
    }
    CMovement_C& movement = player->m_movementData;
    if ((movement.m_flags & Unsafe) || movement.IsOnSpline()) { Release("walk mode, swimming, flying, rooted, transport or spline"); return; }
    const uint32_t now = OsGetAsyncTimeMs();
    if (!driving) {
        driving = true; driveGuid = guid; driver.Clear(); lease = {};
        StopKeys(now); SpeedAllowance(true); KeyOverrides(true);
        sExtensionMgr->Log("Skate: drive on, run speed %.2f",movement.m_runSpeed);
    }
    skatetransport::Pose pose; bool failed;
    skatedrive::Frame frame;
    frame.valid = helper.Snapshot(pose,failed,frame.origin) && !failed && collision.session && collision.map == ClntObjMgrGetMapID();
    frame.airborne = pose.airborne != 0; frame.tick = pose.tick;
    for (int i = 0; i < 3; ++i) { frame.position[i] = pose.position[i]; frame.velocity[i] = pose.velocity[i]; }
    const C3Vector p = player->GetPosition();
    const float actual[3]{p.x,p.y,p.z};
    const float allowance = (std::min)(lease.Observe(movement.m_runSpeed),groundtuning::MaxYards);
    // The board may not outrun the character (the donor's op 11 cap): a board 20 yd ahead forces a re-init.
    helper.Cap(allowance * skatedrive::Yards);
    const auto c = driver.Step(frame,actual,allowance,controls.pop > 0,now);
    // Never queue another re-init while one is in flight (its old pose still reads as apart).
    if (c.resync && !resync && !collision.pending && now - lastResync > 3000) {
        resync = true; lastResync = now;
        sExtensionMgr->Log("Skate: board and character apart, re-initializing the board at the player");
    }
    if (c.drive) { cameraTarget = c.facing; cameraSpeed = c.speed; cameraAt = now; }
    if (c.forward) {
        movement.ChangeRunSpeed(c.speed); lease.Applied(c.speed);
        if (skatedrive::Turn(c.facing,player->GetFacing())) player->OnSetRawFacingLocal(now,c.facing);
    }
    auto* input = CGInputControl::s_inputControl;
    if (input && c.forward != ((input->m_flags & INPUT_MOVE_PLAYER_FORWARD_KEY) != 0))
        input->ToggleControlBit(INPUT_MOVE_PLAYER_FORWARD_KEY,c.forward,now,0);
    skate::VisualSnapshot visual;
    const bool bail = (helper.Visual(visual) && skaterig::Ragdoll(visual.state)) || static_cast<int32_t>(forceBailUntil - now) > 0;
    if (!frame.valid || bail) StopArc();
    if (c.jump && !bail) {
        StopArc();
        pendingJump.Arm({c.jumpVelocity,pose.position[1],pose.velocity[1],pose.tick},now);
        player->TryJumpOrAscend(now);
    }
    else if (arc.Active()) {
        if (frame.valid && pose.tick != arcTick) {
            arcTick = pose.tick;
            arc.Add(movement.m_fallTimeMs * .001f,pose.position[1],pose.velocity[1]);
        }
        // Done once the character has landed (falling seen, then cleared), or if the jump never started falling.
        if (movement.m_flags & MOVEMENTFLAG_FALLING) arcFalling = true;
        else if (arcFalling || now - arcAt > 500) StopArc();
        if (now - arcAt > static_cast<uint32_t>(skatedrive::Arc::MaxSeconds * 1000)) StopArc();
    }
}
void UpdateAudio(bool active) {
    skate::VisualSnapshot visual;
    skateaudio::Sample sample{};
    const uint32_t now=GetTickCount();
    static std::shared_ptr<const skatevisual::Mesh> mesh;
    static uint64_t tick=0;
    static uint32_t epoch=1,frameAt=0;
    if (helper.Visual(visual)) {
        if (mesh!=visual.mesh) { mesh=visual.mesh; tick=0; if (!++epoch) ++epoch; }
        const auto& pose=visual.frame->pose;
        if (pose.tick!=tick) { tick=pose.tick; frameAt=now; }
        sample.active=active && focused && driving && collision.session && collision.map==ClntObjMgrGetMapID() && now-frameAt<250;
        sample.epoch=epoch; sample.tick=pose.tick; sample.state=visual.state;
        sample.speed=hypotf(pose.velocity[0],pose.velocity[2]); sample.vertical=pose.velocity[1];
        // As in the donor, unknown support material uses concrete; no extra collision rays or guessed terrain tags.
    }
    sample.at=now; sample.gain=AudioGain(); audio.Publish(sample);
    const auto status=audio.GetStatus();
    if (status!=audioStatus) {
        audioStatus=status;
        const char* message=status==skateaudio::Status::Ready?"ready (48 sounds)":status==skateaudio::Status::Waiting?"waiting for skating"
            :status==skateaudio::Status::Disabled?"disabled":status==skateaudio::Status::ComUnavailable?"unavailable (COM)"
            :status==skateaudio::Status::Unavailable?"unavailable (bank or device; check skateAudioPath)":"worker failed";
        sExtensionMgr->Log("Skate: audio %s; movement unchanged",message);
    }
}
// Phase 4: the skater pose on the local player's model. Everything here is written on the main thread from
// OnWorldUpdate, before CM2Scene::Animate; the bone hook (either thread, M2UseThreads=1) only reads rigPose.
CM2Model* rigModel = nullptr; // the model whose m_boneHook we set
bool rigSupported = false;
std::string rigStatus;
std::vector<skaterig::SourceBone> rigBones; // the bound model's bones
skaterig::Rig rig;                           // its role skeleton, solved by the donor code
skaterig::Pose rigPose;
skaterig::Rider rider;
characterretarget::Pose rigTarget;
skaterig::Point rigOffset{};
uint32_t rigSignature=0;
uint32_t rigEpoch = 1;
void RigStatus(const std::string& status) {
    static uint32_t loggedAt = 0;
    const uint32_t now = OsGetAsyncTimeMs();
    // Changes only, at most 4 per second, so a status flipping every frame cannot flood the log.
    if (status == rigStatus || now - loggedAt < 250) return;
    rigStatus = status; loggedAt = now;
    sExtensionMgr->Log("Skate: rig %s",status.c_str());
}
// Console `skateBail [seconds]`: draws the current skater frame through the bail (ragdoll) path, to check it on
// demand. The helper has no op that forces a real wipeout; a flip landed early or crooked bails for real.
int32_t ConsoleSkateBail(const char*, const char* arguments) {
    StopArc();
    const float seconds = arguments && *arguments ? static_cast<float>(atof(arguments)) : 3.f;
    forceBailUntil = OsGetAsyncTimeMs() + static_cast<uint32_t>((std::min)((std::max)(seconds,.5f),30.f) * 1000);
    sExtensionMgr->Log("Skate: skateBail, ragdoll for %.1f s (hold J and skate)",(std::min)((std::max)(seconds,.5f),30.f));
    return 1;
}
void RigRelease() {
    if (rigModel) rigModel->m_boneHook = false;
    rigModel = nullptr; rigSupported = false; rigPose = {}; rider.Clear(); ++rigEpoch;
}
// Reads the model's skeleton (M2 bones and key-bone table) and maps the donor's 23 body roles onto it: the donor's
// Discover for Wrath rigs, else the structural planner (Retail bodies, custom races). Unmapped rigs stay stock.
// Logs\SkateRig.txt: the refused skeleton (index key parent flags pivot), to map a new rig from the next test.
void RigDump(const M2Data* data, const char* name) {
    FILE* file = nullptr;
    if (fopen_s(&file,"Logs\\SkateRig.txt","w") || !file) return;
    fprintf(file,"model %s, %u bones, %u key bones\nindex key parent flags pivot_x pivot_y pivot_z\n",name,data->m_bones.m_count,data->m_boneIndicesById.m_count);
    for (uint32_t i = 0; i < data->m_bones.m_count; ++i) {
        const M2CompBone& b = data->m_bones[i];
        fprintf(file,"%u %d %d 0x%x %.4f %.4f %.4f\n",i,static_cast<int32_t>(b.m_boneId),b.m_parentIndex == 0xffff ? -1 : b.m_parentIndex,
            b.m_flags,b.m_pivot.x,b.m_pivot.y,b.m_pivot.z);
    }
    fclose(file);
}
bool RigBind(CM2Model* model) {
    const M2Data* data = model->m_shared ? model->m_shared->m_data : nullptr;
    // The model file (Retail bodies have an empty internal M2 name). Storm's ordered printf (enabled by the client)
    // rejects "%.*s", so it is copied and printed with %s.
    std::string name = model->m_shared ? std::string(model->m_shared->m_fileName,strnlen(model->m_shared->m_fileName,sizeof(model->m_shared->m_fileName))) : "";
    if (name.empty() && data && data->m_name.m_count)
        name.assign(data->m_name.Data(),strnlen(data->m_name.Data(),(std::min)(data->m_name.m_count,64u)));
    if (!data || !data->m_bones.m_count) {
        sExtensionMgr->Log("Skate: rig %s: model has no bones, stock animation",name.c_str()); return false;
    }
    const uint32_t count = data->m_bones.m_count;
    rigBones.assign(count,{});
    for (uint32_t i = 0; i < count; ++i) {
        const M2CompBone& b = data->m_bones[i];
        rigBones[i] = {static_cast<int32_t>(b.m_boneId), b.m_parentIndex == 0xffff ? -1 : static_cast<int32_t>(b.m_parentIndex),
            b.m_flags, {b.m_pivot.x,b.m_pivot.y,b.m_pivot.z}};
        rigBones[i].rotationSequences = 0;
        for (uint32_t sequence=0;sequence<b.m_rotationTrack.m_sequenceKeys.m_count;++sequence)
            if (b.m_rotationTrack.m_sequenceKeys[sequence].m_count) ++rigBones[i].rotationSequences;
    }
    std::vector<uint16_t> keys(data->m_boneIndicesById.m_count);
    for (uint32_t k = 0; k < keys.size(); ++k) keys[k] = data->m_boneIndicesById[k];
    bool donor = false;
    const char* why = skaterig::Plan(rigBones.data(),count,keys.data(),static_cast<uint32_t>(keys.size()),rig,&donor);
    if (!why) skaterig::Bind(rigBones.data(),count,rig,rigPose,&why); // sets why to "" on success
    const bool ok = why && !*why;
    rigSignature=ok?skatepeerpose::RigId(rigBones,rig):0;
    if (ok) sExtensionMgr->Log("Skate: rig %s, %u bones, %u key bones: %s plan, skater pose on %u bones, the rest stock",
        name.c_str(),count,static_cast<uint32_t>(keys.size()),donor ? "donor" : "structural",static_cast<uint32_t>(rigPose.entries.size()));
    else {
        RigDump(data,name.c_str());
        sExtensionMgr->Log("Skate: rig %s, %u bones, %u key bones: not mapped (%s), stock animation; skeleton in Logs\\SkateRig.txt",
            name.c_str(),count,static_cast<uint32_t>(keys.size()),why);
    }
    return ok;
}
void UpdateRig() {
    // Every return names its reason once (RigStatus logs changes only).
    auto* player = ClntObjMgrGetActivePlayerObj();
    CM2Model* model = !driving || !player ? nullptr : player->m_worldModel;
    if (model && !model->IsLoaded()) model = nullptr;
    if (model != rigModel) {
        RigRelease();
        if (!model) {
            RigStatus(!driving ? "off (stock animation)" : !player ? "off: no active player" : !player->m_worldModel
                ? "off: player has no world model" : "off: player model still loading");
            return;
        }
        rigModel = model; rigSupported = RigBind(model);
        model->m_boneHook = rigSupported;
    }
    if (!rigModel) return; // not skating (status already logged)
    if (!rigSupported) { RigStatus("off: rig not mapped (see the rig line above)"); return; }
    skate::VisualSnapshot visual;
    skatetransport::Pose pose; bool failed; float origin[3];
    if (!helper.Visual(visual)) {
        rigPose.ready = false; RigStatus("waiting: no skater frame from the helper (needs op 7 mesh and op 22 frames)"); return;
    }
    // The frame carries its own pose; only the session origin comes from Snapshot (a re-init clears the frame).
    if (!helper.Snapshot(pose,failed,origin) || failed) {
        rigPose.ready = false; RigStatus("waiting: helper failed or restarting"); return;
    }
    // A frame that stopped advancing (helper blocked) would hold the last pose, e.g. mid-air: stock instead.
    static uint64_t frameTick = 0; static uint32_t frameAt = 0;
    const uint32_t now = OsGetAsyncTimeMs();
    if (visual.frame->pose.tick != frameTick) { frameTick = visual.frame->pose.tick; frameAt = now; }
    else if (now - frameAt > skatedrive::StallMs) {
        rigPose.ready = false; RigStatus("waiting: helper busy, frame not advancing (stock animation)"); return;
    }
    const C3Vector p = player->GetPosition();
    skaterig::Point offset{};
    const char* why = "";
    const bool forced = static_cast<int32_t>(forceBailUntil - now) > 0;
    const uint32_t state = forced ? 300u : visual.state;
    const bool solved = skaterig::Solve(rider,rig.skeleton,visual.mesh.get(),*visual.frame,state,player->GetFacing(),
        {origin[0],origin[1],origin[2]},{p.x,p.y,p.z},now,rigEpoch,rigTarget,offset,&why);
    if (!solved) { rigPose.ready = false; RigStatus(std::string("pose rejected: ") + why + " (stock animation)"); return; }
    if (!skaterig::Fill(rigBones.data(),rig,rigTarget,offset,rigPose)) {
        rigPose.ready = false; RigStatus("pose rejected: non-finite local (stock animation)"); return;
    }
    rigOffset=offset;
    RigStatus(forced ? "ragdoll (skateBail)" : skaterig::Ragdoll(visual.state) ? "ragdoll (bail)"
        : visual.frame->pose.airborne ? "skater pose (air)" : "skater pose");
}
// Phase 5: the board as a real client-only M2 under the skater. The M2, its skin and its BLPs are generated in memory
// from the helper's own op 7 mesh and first frame (SkateBoard.h) and served by name through VirtualAssets, so the normal
// model and texture loaders (and every renderer) draw it with world lighting. No game data is stored anywhere.
// Standalone world object, the way Housing draws its ghost: CreateModel + CMap::ObjectCreate, moved every frame.
skateassets::Registry assets;
VirtualAssets::Reader previousReader = nullptr;
bool readerInstalled = false;
// Called for every file open on any thread.
VirtualAssets::Bytes ReadAsset(const char* name) {
    if (auto bytes = assets.Find(name)) return bytes;
    return previousReader ? previousReader(name) : nullptr;
}
void InstallAssetReader() {
    if (readerInstalled) return;
    readerInstalled = true; previousReader = VirtualAssets::reader.load(); VirtualAssets::reader.store(ReadAsset);
}
void PublishAsset(const std::string& name, std::vector<uint8_t> bytes) { assets.Publish(name,std::move(bytes)); }
// CVar skateBoardDump = a folder: the generated board files are written there when built (to inspect or validate them).
void DumpBoard(const skateboard::Model& files) {
    const std::wstring dir = Config("skateBoardDump");
    if (dir.empty()) return;
    const auto write = [&](const std::string& name, const std::vector<uint8_t>& bytes) {
        std::string leaf = name.substr(name.find_last_of('\\') + 1);
        std::ofstream out(dir + L"\\" + std::wstring(leaf.begin(),leaf.end()),std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    };
    write(files.m2Name,files.m2); write(files.skinName,files.skin);
    for (size_t i = 0; i < files.blp.size(); ++i) write(files.blpName[i],files.blp[i]);
    sExtensionMgr->Log("Skate: board files written to the skateBoardDump folder");
}
struct BoardModel {
    CM2Model* model = nullptr;
    CMapEntity* entity = nullptr;
    std::shared_ptr<const skatevisual::Mesh> mesh; // the helper session these assets were built for
    skateboard::Rest rest;
    skateboard::Model files;
    bool built = false, refused = false;
    skateboard::M3 peerBasis;
    uint32_t peerMesh=0;
    bool peerReady=false;
} board;
struct PeerBoard {
    uint64_t guid=0;
    std::string token;
    skatepeer::Packet packet;
    skatepeer::Lease lease;
    CM2Model* model=nullptr;
    CMapEntity* entity=nullptr;
    C44Matrix matrix;
    struct Rider {
        CM2Model* model=nullptr;
        const M2Data* data=nullptr;
        skatepeerpose::Hook<CM2Model> hook;
        std::vector<skaterig::SourceBone> bones;
        skaterig::Rig rig;
        skaterig::Pose pose;
        uint32_t signature=0;
        bool supported=false,reported=false,applied=false;
    } rider;
    bool paired=false;
    skatepeer::Packet displayed;
    skatepeerpose::Packet receivedPose;
    skatepeerpose::Presentation presentation;
    skatepeerpose::Packet renderPose;
    C3Vector renderActor;
    uint32_t preparedAt=0;
    skatepeerspeed::Lease speed;
    CM2Model* speedModel=nullptr;
    uint32_t poseAt=0;
};
struct PeerPoseView { CM2Model* model=nullptr; const skaterig::Pose* pose=nullptr; };
std::array<PeerPoseView,8> peerPoseViews{};
unsigned peerPoseCount=0;
void DropPeerPose(CM2Model* model) {
    for(auto& view:peerPoseViews) if(view.model==model) view={};
}
std::map<uint64_t,PeerBoard> peers;
skatepeertrace::Capture peerTrace;
uint32_t traceMessages=0,traceOpcode=0,traceMessageAt=0;
void TraceUnit(CGUnit_C& unit,unsigned stage,const PeerBoard* peer=nullptr) {
    if(!peerTrace.Active() || !unit.m_obj) return;
    skatepeertrace::Row row;
    row.guid=unit.m_obj->m_guid.m_guid; row.local=unit.m_obj->m_guid==ClntObjMgrGetActivePlayer(); row.stage=stage;
    if(!row.local) {
        const auto it=peers.find(row.guid);
        if(it==peers.end() || std::distance(peers.begin(),it)>=8) return;
    }
    const auto p=unit.GetPosition(); const auto& movement=unit.m_movementData;
    row.actor[0]=p.x; row.actor[1]=p.y; row.actor[2]=p.z;
    row.movement[0]=movement.m_position.x; row.movement[1]=movement.m_position.y; row.movement[2]=movement.m_position.z;
    row.run=movement.m_runSpeed; row.current=movement.m_currentSpeed; row.flags=movement.m_flags;
    if(unit.m_worldModel) {
        const auto* m=&unit.m_worldModel->m_worldTransform.a0;
        for(unsigned i=0;i<3;++i) { row.model[i]=m[12+i]; for(unsigned j=0;j<3;++j) row.basis[i*3+j]=m[i*4+j]; }
    }
    row.lease=row.local?lease.server:0; row.messages=row.local?traceMessages:0; row.opcode=row.local?traceOpcode:0; row.messageAt=row.local?traceMessageAt:0;
    if(peer) {
        row.epoch=peer->packet.epoch; row.sequence=peer->packet.state.sequence; row.age=OsGetAsyncTimeMs()-peer->lease.at;
        row.boardSpeed=peer->packet.state.speed;
        for(unsigned i=0;i<3;++i) { row.offset[i]=peer->packet.state.offset[i]; row.worldOffset[i]=(&peer->renderPose.worldOffset.x)[i]; }
    } else if(row.local) {
        skate::VisualSnapshot visual;
        if(helper.Visual(visual)) row.boardSpeed=hypotf(visual.frame->pose.velocity[0],visual.frame->pose.velocity[2])/.9144f;
    }
    peerTrace.Write(OsGetAsyncTimeMs(),row);
}
void UpdateTrace() {
    const auto* enabled=CVar::Lookup("skatePeerTrace");
    if(!enabled || !enabled->m_intValue) { peerTrace.Stop(); return; }
    if(!peerTrace.Used()) {
        bool activity=driving;
        for(const auto& entry:peers) activity=activity || (entry.second.paired && entry.second.lease.Visible(OsGetAsyncTimeMs()));
        if(!activity) return; // Loading/idle time must not consume the diagnostic capture window.
        CreateDirectoryW(L"Logs",nullptr);
        const auto path="Logs/SkatePeerTrace-"+std::to_string(GetCurrentProcessId())+".csv";
        const bool opened=peerTrace.Start(path,OsGetAsyncTimeMs(),GetCurrentProcessId());
        sExtensionMgr->Log("Skate: pid %u diagnostic trace %s (%s; 60 s/10000 rows max)",GetCurrentProcessId(),opened?"started":"failed",path.c_str());
    }
    if(auto* local=ClntObjMgrGetActivePlayerObj()) TraceUnit(*local,0);
}
uint32_t peerEpoch=1,peerShownAt=0;
uint16_t peerSequence=0;
bool peerShown=false;
skatepeer::Packet peerOutgoing;
skatepeerpose::Packet peerPoseOutgoing;
bool peerPoseOutgoingReady=false;
void ReleasePeerSpeed(PeerBoard& peer) {
    if(!peer.speed.Held()) return;
    auto* object=ClntObjMgrObjectPtr(WOWGUID{peer.guid},TYPEMASK_PLAYER,__FILE__,__LINE__);
    float restore;
    if(object && object->m_obj && object->m_obj->m_guid!=ClntObjMgrGetActivePlayer()) {
        auto& movement=static_cast<CGUnit_C*>(object)->m_movementData;
        if(peer.speed.Release(movement.m_runSpeed,restore)) movement.ChangeRunSpeed(restore);
    } else peer.speed.Release(NAN,restore);
    peer.speedModel=nullptr;
}
void UpdatePeerSpeed(PeerBoard& peer,CGUnit_C& unit) {
    const uint32_t now=OsGetAsyncTimeMs();
    auto& movement=unit.m_movementData;
    if(peer.speedModel && peer.speedModel!=unit.m_worldModel) ReleasePeerSpeed(peer);
    const uint32_t unsafe=MOVEMENTFLAG_ONTRANSPORT|MOVEMENTFLAG_ROOT|MOVEMENTFLAG_SWIMMING|MOVEMENTFLAG_FLYING
        |MOVEMENTFLAG_WALKING|MOVEMENTFLAG_FALLING|MOVEMENTFLAG_FALLING_FAR|MOVEMENTFLAG_BACKWARD;
    const bool eligible=unit.m_obj && unit.m_obj->m_guid!=ClntObjMgrGetActivePlayer()
        && unit.m_obj->m_guid!=CGUnit_C::s_activeMover && peer.packet.map==ClntObjMgrGetMapID()
        && peer.lease.Visible(now) && (peer.packet.state.flags&skatepeer::Active)
        && !(peer.packet.state.flags&(skatepeer::Airborne|skatepeer::Free|skatepeer::Hidden))
        && (movement.m_flags&MOVEMENTFLAG_FORWARD) && !(movement.m_flags&unsafe) && !movement.IsOnSpline();
    float predicted;
    if(peer.speed.Apply(movement.m_runSpeed,peer.packet.state.speed,eligible,predicted)) {
        movement.ChangeRunSpeed(predicted); peer.speedModel=unit.m_worldModel;
    } else ReleasePeerSpeed(peer);
}
void PeerDestroy(PeerBoard& peer) {
    ReleasePeerSpeed(peer);
    peer.paired=false; peer.presentation.Clear();
    DropPeerPose(peer.rider.model); peer.rider.pose.ready=false; peer.rider.hook.Release();
    if(peer.entity) World::ObjectDestroy(peer.entity);
    if(peer.model) peer.model->Release();
    peer.entity=nullptr; peer.model=nullptr;
}
void PeerClear() {
    for(auto& entry:peers) PeerDestroy(entry.second);
    peers.clear(); peerPoseViews={}; peerPoseCount=0; peerShown=false; peerPoseOutgoingReady=false; if(!++peerEpoch) ++peerEpoch;
}
void PeerForget() { peerPoseViews={}; peerPoseCount=0; peers.clear(); peerShown=false; peerPoseOutgoingReady=false; }
uint32_t boardGeneration = 0;
C44Matrix boardMatrix;
std::string boardStatus;
void BoardStatus(const std::string& status) {
    static uint32_t loggedAt = 0;
    const uint32_t now = OsGetAsyncTimeMs();
    if (status == boardStatus || now - loggedAt < 250) return;
    boardStatus = status; loggedAt = now;
    sExtensionMgr->Log("Skate: board %s",status.c_str());
}
void BoardDestroy() {
    if (board.entity) World::ObjectDestroy(board.entity);
    if (board.model) board.model->Release();
    board.entity = nullptr; board.model = nullptr;
}
// The engine is gone (shutdown): drop the pointers without touching it.
void BoardForget() { board.entity = nullptr; board.model = nullptr; }
int32_t BoardObject(void*, uint32_t, uint64_t, uintptr_t) {
    if (!board.model) return 1;
    board.model->m_fFlags |= 0x8000;
    board.model->m_worldTransform = boardMatrix;
    return 1;
}
void UpdateBoard() {
    auto* player = ClntObjMgrGetActivePlayerObj();
    skate::VisualSnapshot visual; skatetransport::Pose pose; bool failed; float origin[3];
    peerShown=false;
    peerPoseOutgoingReady=false;
    const bool ready = player && World::s_m2Scene && helper.Visual(visual) && helper.Snapshot(pose,failed,origin) && !failed;
    if (!ready) {
        BoardDestroy();
        BoardStatus(!driving ? "off (not skating)" : "off: waiting for the helper's skater frame");
        return;
    }
    if (visual.mesh != board.mesh) { // a new helper session: new mesh, new rest shape, new file names
        PeerClear();
        BoardDestroy();
        assets.Clear();
        board = BoardModel{}; board.mesh = visual.mesh;
    }
    if (!board.built) {
        if (board.refused) return;
        if (!board.rest.Make(visual.frame->vertices) || !skateboard::Build(*visual.mesh,board.rest,++boardGeneration,board.files)) {
            board.refused = true; BoardStatus("off: the helper's mesh could not be built into an M2"); return;
        }
        PublishAsset(board.files.m2Name,board.files.m2); PublishAsset(board.files.skinName,board.files.skin);
        for (size_t i = 0; i < board.files.blp.size(); ++i) PublishAsset(board.files.blpName[i],board.files.blp[i]);
        board.built = true;
        board.peerReady=skatepeer::Basis(*visual.mesh,board.rest,board.peerBasis);
        board.peerMesh=skatepeer::MeshId(*visual.mesh);
        DumpBoard(board.files);
        sExtensionMgr->Log("Skate: board M2 built from the helper mesh: %u vertices, %u triangles, %u textures (%s, %u bytes)",
            static_cast<uint32_t>(board.rest.points.size()),static_cast<uint32_t>(visual.mesh->indices.size()/3),
            static_cast<uint32_t>(board.files.blp.size()),board.files.m2Name.c_str(),static_cast<uint32_t>(board.files.m2.size()));
    }
    if (!driving) { BoardDestroy(); BoardStatus("off (not skating; private peer mesh ready)"); return; }
    const uint32_t now = OsGetAsyncTimeMs();
    static uint64_t frameTick = 0; static uint32_t frameAt = 0;
    if (visual.frame->pose.tick != frameTick) { frameTick = visual.frame->pose.tick; frameAt = now; }
    const bool stalled = now - frameAt > skatedrive::StallMs;
    // What the loaders asked the reader for (the first time each name is asked, hit or miss): from any thread, logged here.
    for (const auto& request : assets.TakeRequests())
        sExtensionMgr->Log("Skate: board file %s %s (%u bytes)",request.hit ? "served" : "NOT FOUND",request.name.c_str(),static_cast<uint32_t>(request.size));
    if (!board.model) {
        board.model = World::s_m2Scene->CreateModel(board.files.m2Name.c_str(),0);
        if (!board.model) { BoardStatus("off: CreateModel failed"); return; }
        board.entity = CMap::ObjectCreate(board.model,BoardObject,nullptr,0,0,10);
        sExtensionMgr->Log("Skate: board model created (%s)",board.files.m2Name.c_str());
    }
    const C3Vector p = player->GetPosition();
    skatevisual::Point centre{};
    for (const auto& v : visual.frame->vertices) { centre.x += v.x; centre.y += v.y; centre.z += v.z; }
    const float count = static_cast<float>(visual.frame->vertices.size());
    centre = {centre.x/count,centre.y/count,centre.z/count};
    float modelScale = 1;
    if (player->m_worldModel && !modelboardsize::UniformScale(&player->m_worldModel->m_worldTransform.a0,modelScale)) modelScale = 1;
    const bool forced = static_cast<int32_t>(forceBailUntil - now) > 0;
    const bool bail = forced || skaterig::Ragdoll(visual.state);
    const auto fit = skateboard::ChooseFit(bail,*visual.frame,rider.air,modelScale,{origin[0],origin[1],origin[2]},centre);
    skateboard::Placed placed;
    bool shown = !stalled && !visual.frame->boardHidden;
    if (shown && !skateboard::Place(board.rest,*visual.frame,{p.x,p.y,p.z},fit,placed)) {
        shown = false; BoardStatus("hidden: the streamed board no longer matches its rest shape");
    }
    else if (shown) memcpy(&boardMatrix.a0,placed.matrix,sizeof(placed.matrix));
    if (shown && board.peerReady) {
        peerOutgoing={}; peerOutgoing.epoch=peerEpoch; peerOutgoing.map=ClntObjMgrGetMapID(); peerOutgoing.mesh=board.peerMesh;
        peerOutgoing.state.flags=skatepeer::Active | (visual.frame->pose.airborne?skatepeer::Airborne:0) | (bail?skatepeer::Free:0);
        peerOutgoing.state.speed=player->m_movementData.m_runSpeed; // Ground prediction must match the actual mover, not helper velocity.
        peerOutgoing.actorKnown=true;
        peerOutgoing.actor[0]=p.x; peerOutgoing.actor[1]=p.y; peerOutgoing.actor[2]=p.z;
        skatepeer::Capture(peerOutgoing,placed,{p.x,p.y,p.z},board.peerBasis);
        peerShown=true; peerShownAt=now;
        if(rigModel==player->m_worldModel && rigPose.ready && rigSupported && rigSignature) {
            peerPoseOutgoing={}; peerPoseOutgoing.epoch=peerEpoch; peerPoseOutgoing.map=peerOutgoing.map;
            peerPoseOutgoing.mesh=board.peerMesh; peerPoseOutgoing.rig=rigSignature;
            peerPoseOutgoingReady=skatepeerpose::Capture(rigTarget,rigOffset,&rigModel->m_worldTransform.a0,{p.x,p.y,p.z},peerPoseOutgoing);
        }
    }
    if (shown) BoardStatus(bail ? "free (bail)" : visual.frame->pose.airborne ? "shown (air)" : "shown");
    else if (stalled) BoardStatus("hidden: helper busy, frame not advancing");
    if (!board.entity) return;
    World::ObjectSetHidden(board.entity,!shown);
    CAaBox box; box.b = C3Vector(0,0,0); box.t = C3Vector(0,0,0);
    CAaSphere sphere; C3Vector middle(0,0,0);
    if (board.model->IsLoaded(0,0)) { box = board.model->GetBoundingBox(); sphere = board.model->GetBoundingSphere(); middle = sphere.c; }
    CMap::ObjectUpdate(board.entity,boardMatrix,box,sphere,middle,false,0xFFFFFFFF);
}
int32_t ScriptPeerState(lua_State* L) {
    auto packet=peerOutgoing;
    const bool active=inWorld && driving && focused && peerShown && OsGetAsyncTimeMs()-peerShownAt<250;
    if (!active) { packet={}; packet.epoch=peerEpoch; packet.map=inWorld?ClntObjMgrGetMapID():0; packet.mesh=board.peerMesh; }
    packet.state.sequence=++peerSequence;
    const std::string text=skatepeer::Encode(packet);
    lua_pushlstring(L,text.data(),text.size()); lua_pushboolean(L,active && !text.empty());
    if(active && peerPoseOutgoingReady) {
        auto pose=peerPoseOutgoing; pose.sequence=packet.state.sequence;
        const auto encoded=skatepeerpose::Encode(pose); lua_pushlstring(L,encoded.data(),encoded.size());
    } else lua_pushnil(L);
    return 3;
}
int32_t ScriptPeerReceive(lua_State* L) {
    bool accepted=false;
    size_t size=0,tokenSize=0;
    const char* token=lua_type(L,1)==LUA_TSTRING?lua_tolstring(L,1,&tokenSize):nullptr;
    const char* text=lua_type(L,2)==LUA_TSTRING?lua_tolstring(L,2,&size):nullptr;
    skatepeer::Packet packet;
    if (inWorld && token && text && tokenSize<=6 && strlen(token)==tokenSize && skatepeer::Token(token) && skatepeer::Decode(text,size,packet)
        && packet.map==ClntObjMgrGetMapID() && board.built && board.peerReady && packet.mesh==board.peerMesh) {
        auto* unit=GetUnitFromName(token);
        if (unit && unit->m_obj && (unit->m_obj->m_type&TYPEMASK_PLAYER) && unit->m_obj->m_guid!=ClntObjMgrGetActivePlayer()
            && skatepeer::NearActor(packet,{unit->GetPosition().x,unit->GetPosition().y,unit->GetPosition().z})) {
            const uint64_t guid=unit->m_obj->m_guid.m_guid;
            auto found=peers.find(guid);
            if (found!=peers.end() || peers.size()<39) {
                const bool first=found==peers.end();
                auto& peer=peers[guid]; peer.guid=guid;
                if (peer.lease.Accept(packet,OsGetAsyncTimeMs())) {
                    if(packet.epoch!=peer.packet.epoch) ReleasePeerSpeed(peer);
                    if(peer.paired && (packet.epoch!=peer.packet.epoch || !(packet.state.flags&skatepeer::Active) || (packet.state.flags&skatepeer::Hidden))) {
                        peer.paired=false; peer.presentation.Clear(); DropPeerPose(peer.rider.model); peer.rider.pose.ready=false; peer.rider.hook.Release();
                    }
                    peer.packet=packet; peer.token=token; accepted=true;
                    UpdatePeerSpeed(peer,*unit);
                    TraceUnit(*unit,2,&peer);
                    if (first) sExtensionMgr->Log("Skate: pid %u group peer %s accepted (private mesh matched)",GetCurrentProcessId(),token);
                }
            }
        }
    }
    lua_pushboolean(L,accepted); return 1;
}
int32_t ScriptPeerPoseReceive(lua_State* L) {
    size_t tokenSize=0,size=0;
    const char* token=lua_type(L,1)==LUA_TSTRING?lua_tolstring(L,1,&tokenSize):nullptr;
    const char* text=lua_type(L,2)==LUA_TSTRING?lua_tolstring(L,2,&size):nullptr;
    skatepeerpose::Packet packet; bool accepted=false;
    if(inWorld && token && text && tokenSize<=6 && strlen(token)==tokenSize && skatepeer::Token(token)
        && skatepeerpose::Decode(text,size,packet)) {
        auto* unit=GetUnitFromName(token);
        if(unit && unit->m_obj && unit->m_obj->m_guid!=ClntObjMgrGetActivePlayer()) {
            auto found=peers.find(unit->m_obj->m_guid.m_guid);
            if(found!=peers.end()) {
                auto& peer=found->second;
                if(skatepeerpose::Match(packet,peer.packet) && peer.lease.Visible(OsGetAsyncTimeMs())
                    && !(peer.rider.model==unit->m_worldModel && peer.rider.signature && peer.rider.signature!=packet.rig)
                    && (!peer.paired || packet.epoch!=peer.receivedPose.epoch || packet.sequence!=peer.receivedPose.sequence)) {
                    peer.displayed=peer.packet; peer.receivedPose=packet; peer.paired=true; peer.poseAt=OsGetAsyncTimeMs();
                    const C3Vector actor=unit->GetPosition();
                    const bool grounded=(unit->m_movementData.m_flags&(MOVEMENTFLAG_FALLING|MOVEMENTFLAG_FALLING_FAR))==0;
                    peer.presentation.Push(peer.packet,packet,peer.poseAt,{actor.x,actor.y,actor.z},grounded); accepted=true;
                }
            }
        }
    }
    lua_pushboolean(L,accepted); return 1;
}
void UpdatePeerRider(PeerBoard& peer,CGUnit_C& unit,const C3Vector& actor) {
    auto& r=peer.rider;
    CM2Model* model=unit.m_worldModel;
    if(!peer.paired || !model || !model->IsLoaded() || !model->m_shared || !model->m_shared->m_data) {
        DropPeerPose(r.model); r.pose.ready=false; r.hook.Release(); return;
    }
    const M2Data* data=model->m_shared->m_data;
    if(r.model!=model || r.data!=data) {
        DropPeerPose(r.model); r.hook.Release(); r={}; r.model=model; r.data=data;
        r.bones.resize(data->m_bones.m_count);
        for(uint32_t i=0;i<data->m_bones.m_count;++i) {
            const auto& b=data->m_bones[i];
            r.bones[i]={int32_t(b.m_boneId),b.m_parentIndex==0xffff?-1:int32_t(b.m_parentIndex),b.m_flags,{b.m_pivot.x,b.m_pivot.y,b.m_pivot.z},0};
            for(uint32_t s=0;s<b.m_rotationTrack.m_sequenceKeys.m_count;++s) if(b.m_rotationTrack.m_sequenceKeys[s].m_count) ++r.bones[i].rotationSequences;
        }
        std::vector<uint16_t> keys(data->m_boneIndicesById.m_count);
        for(uint32_t i=0;i<keys.size();++i) keys[i]=data->m_boneIndicesById[i];
        const char* why=skaterig::Plan(r.bones.data(),uint32_t(r.bones.size()),keys.data(),uint32_t(keys.size()),r.rig);
        r.supported=!why && skaterig::Bind(r.bones.data(),uint32_t(r.bones.size()),r.rig,r.pose);
        if(r.supported) r.signature=skatepeerpose::RigId(r.bones,r.rig);
        sExtensionMgr->Log("Skate: pid %u peer %s rider %s",GetCurrentProcessId(),peer.token.c_str(),r.supported?"rig bound":"unsupported rig (stock fallback)");
    }
    if(!r.supported || r.signature!=peer.receivedPose.rig) {
        r.pose.ready=false; r.hook.Release(); peer.paired=false;
        if(!r.reported) { r.reported=true; sExtensionMgr->Log("Skate: peer %s rider model mismatch; stock fallback",peer.token.c_str()); }
        return;
    }
    characterretarget::Pose target; skaterig::Point offset;
    if(!skatepeerpose::Restore(peer.renderPose,r.rig,&model->m_worldTransform.a0,{actor.x,actor.y,actor.z},target,offset)
        || !skaterig::Fill(r.bones.data(),r.rig,target,offset,r.pose)) { r.pose.ready=false; r.hook.Release(); return; }
    r.hook.Own(model);
    if(!r.applied) { r.applied=true; sExtensionMgr->Log("Skate: pid %u peer %s rider pose active (%u semantic roles)",GetCurrentProcessId(),peer.token.c_str(),skaterig::BodyCount); }
    for(unsigned i=0;i<peerPoseCount;++i) if(peerPoseViews[i].model==model) { peerPoseViews[i].pose=&r.pose; return; }
    if(peerPoseCount<peerPoseViews.size()) peerPoseViews[peerPoseCount++]={model,&r.pose};
}
int32_t PeerObject(void* context,uint32_t,uint64_t,uintptr_t) {
    auto* peer=static_cast<PeerBoard*>(context);
    if (peer && peer->model) { peer->model->m_fFlags|=0x8000; peer->model->m_worldTransform=peer->matrix; }
    return 1;
}
void UpdatePeers() {
    peerPoseViews={}; peerPoseCount=0;
    if (!inWorld || !World::s_m2Scene) return;
    auto* local=ClntObjMgrGetActivePlayerObj();
    if (!local) return;
    const C3Vector player=local->GetPosition();
    const uint32_t now=OsGetAsyncTimeMs();
    unsigned drawn=0;
    for(auto it=peers.begin();it!=peers.end();) {
        auto& peer=it->second;
        auto* unit=GetUnitFromName(peer.token.c_str());
        if (!unit || !unit->m_obj || unit->m_obj->m_guid.m_guid!=peer.guid) { PeerDestroy(peer); it=peers.erase(it); continue; }
        const C3Vector p=unit->GetPosition();
        UpdatePeerSpeed(peer,*unit);
        auto displayed=peer.paired?peer.displayed:peer.packet;
        if(peer.paired) peer.presentation.Sample(now,displayed,peer.renderPose);
        const auto anchor=peer.paired?peer.presentation.Actor(now,{p.x,p.y,p.z}):skaterig::Point{p.x,p.y,p.z};
        peer.renderActor=C3Vector(anchor.x,anchor.y,anchor.z);
        const bool visible=board.built && board.peerReady && peer.packet.mesh==board.peerMesh
            && peer.packet.map==ClntObjMgrGetMapID() && peer.lease.Visible(now)
            && (!peer.paired || now-peer.poseAt<skatepeer::ExpireMs)
            && (peer.packet.state.flags&skatepeer::Active) && !(peer.packet.state.flags&skatepeer::Hidden)
            && hypotf(p.x-player.x,p.y-player.y)<100 && fabsf(p.z-player.z)<100 && drawn<8;
        if (!visible || !skatepeer::Matrix(displayed,board.peerBasis,{anchor.x,anchor.y,anchor.z},&peer.matrix.a0)) {
            PeerDestroy(peer); ++it; continue;
        }
        if (!peer.model) {
            peer.model=World::s_m2Scene->CreateModel(board.files.m2Name.c_str(),0);
        }
        if (peer.model && !peer.entity) {
            peer.entity=CMap::ObjectCreate(peer.model,PeerObject,&peer,0,0,10);
            if (peer.entity) sExtensionMgr->Log("Skate: pid %u group peer %s board created",GetCurrentProcessId(),peer.token.c_str());
        }
        if (peer.model && peer.entity) {
            if(drawn<8) TraceUnit(*unit,1,&peer);
            peer.preparedAt=now;
            UpdatePeerRider(peer,*unit,peer.renderActor);
            CAaBox box; box.b=C3Vector(0,0,0); box.t=C3Vector(0,0,0);
            CAaSphere sphere; C3Vector middle(0,0,0);
            if (peer.model->IsLoaded(0,0)) { box=peer.model->GetBoundingBox(); sphere=peer.model->GetBoundingSphere(); middle=sphere.c; }
            CMap::ObjectUpdate(peer.entity,peer.matrix,box,sphere,middle,false,0xFFFFFFFF);
            ++drawn;
        }
        ++it;
    }
}
class SkatePeerObjects final : public ObjectScript {
    void Remove(CGObject_C& object) {
        if (!object.m_obj) return;
        const auto found=peers.find(object.m_obj->m_guid.m_guid);
        if (found!=peers.end()) { PeerDestroy(found->second); peers.erase(found); }
    }
public:
    SkatePeerObjects() : ObjectScript("SkatePeerObjects") {}
    void OnObjectRemoved(CGObject_C& object) override { Remove(object); }
    void OnObjectDestroy(CGObject_C& object) override { Remove(object); }
};
class SkatePeerSpeedMessages final : public NetScript {
public:
    SkatePeerSpeedMessages() : NetScript("SkatePeerSpeedMessages") {}
    void OnMessageReceived(NETMESSAGE opcode,uint32_t,CDataStore& msg) override {
        if(!inWorld || peers.empty() || (opcode!=MSG_MOVE_SET_RUN_SPEED && opcode!=SMSG_FORCE_RUN_SPEED_CHANGE
            && opcode!=SMSG_MOVE_SPLINE_SET_RUN_SPEED)) return;
        if(!msg.m_data || msg.m_read<msg.m_base || msg.m_read>=msg.m_size || msg.m_read-msg.m_base>=msg.m_alloc) return;
        const size_t size=(std::min)(msg.m_size-msg.m_read,msg.m_alloc-(msg.m_read-msg.m_base));
        uint64_t guid=0;
        if(!skatepeerspeed::PackedGuid(msg.m_data+msg.m_read-msg.m_base,size,guid)) return;
        const auto found=peers.find(guid);
        if(found!=peers.end()) ReleasePeerSpeed(found->second); // Let the ordinary handler apply even a server value equal to our lease.
    }
};
class SkateModel final : public ModelScript {
public:
    SkateModel() : ModelScript("SkateModel") {}
    void OnModelBoneLocal(CM2Model& model, uint32_t boneIndex, C44Matrix& local) override {
        static_assert(sizeof(C44Matrix) == 16 * sizeof(float), "row-major 4x4");
        if (&model == rigModel) skaterig::Apply(rigPose,boneIndex,&local.a0);
        for(unsigned i=0;i<peerPoseCount;++i) if(peerPoseViews[i].model==&model && peerPoseViews[i].pose)
            skaterig::Apply(*peerPoseViews[i].pose,boneIndex,&local.a0);
    }
    void OnModelDestroy(CM2Model& model) override {
        if (&model == rigModel) { rigModel = nullptr; rigSupported = false; rigPose = {}; }
        if (&model == board.model) BoardForget();
        for (auto& entry:peers) if (entry.second.model==&model) { entry.second.model=nullptr; entry.second.entity=nullptr; }
        DropPeerPose(&model);
        for(auto& entry:peers) if(entry.second.rider.model==&model) {
            entry.second.rider.hook.Forget(&model); entry.second.rider.model=nullptr; entry.second.rider.pose.ready=false;
        }
    }
};
// The ollie's takeoff velocity goes into the stock jump (CMovementShared::Jump -> StartFalling).
class SkateUnit final : public UnitScript {
public:
    SkateUnit() : UnitScript("SkateUnit") {}
    void OnUnitMovementMessage(CGUnit_C& unit,uint32_t opcode,CDataStore&) override {
        if(!peerTrace.Active() || !unit.m_obj || unit.m_obj->m_guid!=ClntObjMgrGetActivePlayer()) return;
        ++traceMessages; traceOpcode=opcode; traceMessageAt=OsGetAsyncTimeMs(); TraceUnit(unit,3);
    }
    void OnUnitWorldTransformUpdated(CGUnit_C& unit,float) override {
        if(!inWorld || !unit.m_obj) return;
        if(unit.m_obj->m_guid==ClntObjMgrGetActivePlayer()) {
            // Capture the sender's rendered basis after native Animate placed it for this frame.
            if(driving && peerShown && rigModel==unit.m_worldModel && rigPose.ready && rigSupported && rigSignature) {
                const skaterig::Point actor{peerOutgoing.actor[0],peerOutgoing.actor[1],peerOutgoing.actor[2]};
                peerPoseOutgoingReady=skatepeerpose::Capture(rigTarget,rigOffset,&rigModel->m_worldTransform.a0,actor,peerPoseOutgoing);
            }
            TraceUnit(unit,4); return;
        }
        const auto found=peers.find(unit.m_obj->m_guid.m_guid);
        if(found==peers.end()) return;
        auto& peer=found->second;
        UpdatePeerSpeed(peer,unit);
        TraceUnit(unit,4,&peer);
        const uint32_t now=OsGetAsyncTimeMs();
        if(!peer.paired || !peer.entity || !peer.presentation.ready || now-peer.preparedAt>100
            || !peer.lease.Visible(now) || now-peer.poseAt>=skatepeer::ExpireMs) return;
        // Animate just placed the avatar with this frame's native facing/origin. Compensate THAT matrix,
        // not the preceding frame's matrix sampled by OnWorldUpdate; MT will only read the finished locals.
        UpdatePeerRider(peer,unit,peer.presentation.airAnchor?peer.renderActor:unit.GetPosition());
    }
    void OnMoverStartFalling(CGUnit_C& mover, CMovement_C&, float& velocity) override {
        if (!driving || mover.m_obj->m_guid != ClntObjMgrGetActivePlayer()) return;
        skatedrive::JumpRequest request;
        const uint32_t now=OsGetAsyncTimeMs();
        if (pendingJump.Take(now,request)) {
            velocity = request.velocity;
            arc.Start(request.rootY,request.vy); arcAt = now; arcFalling = false; arcTick = request.tick;
        }
    }
    // Replace the remaining real displacement and query-box reach; leave the stock step-down probe alone.
    void OnMoverFallDistance(CGUnit_C& mover, CMovement_C& movement, MoverFallSite site, uint32_t time, float& distance) override {
        if (!driving || !arc.Active() || site == MOVER_FALL_SITE_FLOOR_PROBE
            || mover.m_obj->m_guid != ClntObjMgrGetActivePlayer() || !(movement.m_flags & MOVEMENTFLAG_FALLING)) return;
        arc.Step(time * .001f,movement.m_position.z,movement.m_fallStartZ,distance);
    }
    void OnMoverFallTime(CGUnit_C& mover, CMovement_C& movement, float distance, int32_t upward, float& seconds) override {
        if (!driving || !arc.Active() || arcProbe || mover.m_obj->m_guid != ClntObjMgrGetActivePlayer()
            || !(movement.m_flags & MOVEMENTFLAG_FALLING)) return;
        arc.Time(distance,upward != 0,seconds);
    }
    void OnMoverFallDown(CGUnit_C&, CMovement_C&, MoverFallSite site, uint32_t, uint32_t, C3Vector&) override {
        arcProbe = site == MOVER_FALL_SITE_FLOOR_PROBE;
    }
    void OnMoverFallDownDone(CGUnit_C& mover, CMovement_C& movement, MoverFallSite site, uint32_t consumed) override {
        if (site == MOVER_FALL_SITE_FLOOR_PROBE) { arcProbe = false; return; }
        if (arc.Active() && site == MOVER_FALL_SITE_FALL && consumed && !movement.m_fallTimeMs
            && mover.m_obj->m_guid == ClntObjMgrGetActivePlayer()) StopArc(); // Stock contact reset the fall clock/datum.
    }
};
class SkateClient final : public ClientScript {
    float eventTime_ = 0;
    float statusTime_ = 0;
    bool previousHeld_ = false, spaceHeld_ = false;
    DWORD popUntil_ = 0;
public:
    SkateClient() : ClientScript("SkateClient") {}
    void OnClientInitialized() override {
        CVar::Register("skateAssetPath","User-owned converted Skate assets directory (rig.json)",0,"",nullptr,0,false,nullptr,false);
        CVar::Register("skateHelperPath","User-supplied x64 wrath-skate-bridge.exe",0,"",nullptr,0,false,nullptr,false);
        CVar::Register("skateAudioPath","User-owned exported Skate WAV bank; empty disables Skate audio",0,"",nullptr,0,false,nullptr,false);
        CVar::Register("skatePeerTrace","Opt-in 60-second per-process movement/peer diagnostic CSV",0,"0",nullptr,0,false,nullptr,false);
        ConsoleCommandRegister("skateBail",ConsoleSkateBail,CATEGORY_GAME,"[seconds, default 3] draw the Skate bail (ragdoll) pose while skating");
        CVar::Register("skateBoardDump","Folder to write the generated Skate board M2, skin and BLPs into (debug)",0,"",nullptr,0,false,nullptr,false);
        InstallAssetReader();
    }
    void OnEnterWorld(uint32_t) override {
        peerTrace.Reset(); traceMessages=traceOpcode=traceMessageAt=0;
        inWorld = true; input = {}; haveCentre = false; gatherCooldown = 0; helper.Start(Config("skateHelperPath"),Config("skateAssetPath"));
        audio.Start(Config("skateAudioPath"));
        const auto& log=helper.LogPath();
        std::string path;
        for(wchar_t ch:log) path+=static_cast<char>(ch); // Generated log filenames contain ASCII only.
        sExtensionMgr->Log("Skate: pid %u helper stderr %s",GetCurrentProcessId(),path.c_str());
        sExtensionMgr->Log("Skate Phase 3: hold J, neutral controls to arm; A/W push, B/S brake, arrows/A/D steer; Y toggles mode");
    }
    // The player and its movement are gone here; logout resets the server speed rate and the UI bindings.
    void OnLeaveWorld() override { peerTrace.Stop(); PeerClear(); audio.Stop(); StopArc(); RigRelease(); inWorld = false; driving = false; cameraFollow.Clear(); cameraAt = 0; BoardDestroy(); helper.Stop(); }
    void OnShutdown() override { peerTrace.Stop(); PeerForget(); audio.Stop(); StopArc(); inWorld = false; driving = false; BoardForget(); helper.Stop(); }
    void OnClientDestroy() override { peerTrace.Stop(); PeerForget(); audio.Stop(); StopArc(); inWorld = false; driving = false; BoardForget(); helper.Stop(); }
    void OnUpdate(float elapsed) override {
        if (!inWorld) return;
        if (debugPending) InitializeDebugOverlay();
        DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        const bool oldFocus = focused, oldPad = connected;
        focused = pid == GetCurrentProcessId();
        xboxinput::State state{}; int slot = -1;
        connected = reader.Poll(slot,state);
        if (oldFocus != focused) {
            auto event = sExtensionMgr->GetFrameScriptEventIndex("WRATH_SKATE_FOCUS_CHANGED");
            if (event != UINT32_MAX) FrameScript_SignalEvent(event,"%d",focused);
        }
        if (oldPad != connected) {
            auto event = sExtensionMgr->GetFrameScriptEventIndex("WRATH_SKATE_PAD_CHANGED");
            if (event != UINT32_MAX) FrameScript_SignalEvent(event,"%d",connected);
        }
        if (!connected && focused) {
            if (Down('W') || Down(VK_UP)) state.pad.buttons |= 0x1000;
            if (Down('S') || Down(VK_DOWN)) state.pad.buttons |= 0x2000;
            if (Down('Y')) state.pad.buttons |= 0x8000;
            const bool left = Down('A') || Down(VK_LEFT), right = Down('D') || Down(VK_RIGHT);
            state.pad.lx = left == right ? 0 : left ? -32767 : 32767;
            // Space: hold to crouch (stick down), release to pop (stick flicked up for 120 ms), like the donor's ollie key.
            const bool space = Down(VK_SPACE);
            if (spaceHeld_ && !space) popUntil_ = GetTickCount() + 120;
            spaceHeld_ = space;
            state.pad.ry = space ? -32767 : static_cast<LONG>(GetTickCount() - popUntil_) < 0 ? 32767 : 0;
            // Q/E held through the pop: flip left/right (the pad's right stick X).
            if (state.pad.ry) state.pad.rx = Down('Q') == Down('E') ? 0 : Down('Q') ? -32767 : 32767;
        }
        const bool held = Down('J');
        const auto controls = input.Step(focused,held,true,state.pad);
        helper.Input(controls.active,controls.push,controls.brake,controls.steer,controls.pop,controls.flip);
        Drive(controls);
        UpdateTrace();
        UpdateAudio(controls.active);
        statusTime_ += elapsed;
        if (held != previousHeld_ || (held && statusTime_ >= 1.f)) {
            statusTime_ = 0;
            skatetransport::Pose pose; bool failed;
            const bool ready = helper.Snapshot(pose,failed);
            sExtensionMgr->Log("Skate: J=%d focus=%d pad=%d buttons=%04x ry=%d active=%d push=%d brake=%d steer=%d pop=%d ready=%d failed=%d ground=%u air=%u gen=%u gather=%u/%u tick=%llu pos=(%.2f %.2f %.2f) vel=(%.2f %.2f %.2f)",
                held,focused,connected,state.pad.buttons,state.pad.ry,controls.active,controls.push,controls.brake,controls.steer,controls.pop,ready,failed,
                pose.ground,pose.airborne,collisionGeneration,gatherCount,gatherStatus,
                static_cast<unsigned long long>(pose.tick),pose.position[0],pose.position[1],pose.position[2],
                pose.velocity[0],pose.velocity[1],pose.velocity[2]);
            // Drive summary: what reached the mover (Phase 3).
            auto* player = ClntObjMgrGetActivePlayerObj();
            const auto* in = CGInputControl::s_inputControl;
            if (player) sExtensionMgr->Log("Skate: drive=%d mover=%d run=%.2f current=%.2f flags=%08x input=%08x lease=%.2f",
                driving,CGUnit_C::s_activeMover.m_guid == ClntObjMgrGetActivePlayer().m_guid,player->m_movementData.m_runSpeed,
                player->m_movementData.m_currentSpeed,player->m_movementData.m_flags,in ? in->m_flags : 0u,lease.server);
        }
        previousHeld_ = held;
        eventTime_ += elapsed;
        if (eventTime_ >= .1f) {
            eventTime_ = 0;
            auto event = sExtensionMgr->GetFrameScriptEventIndex("WRATH_SKATE_DEBUG");
            if (event != UINT32_MAX) FrameScript_SignalEvent(event,"%d",focused && held);
        }
    }
};
void InitializeDebugOverlay() {
    lua_State* L = FrameScript_GetContext();
    if (!L) return;
    const int top = lua_gettop(L);
    lua_getglobal(L,"UIParent");
    lua_getglobal(L,"GameFontNormal");
    lua_getglobal(L,"CreateFrame");
    const bool ready = !lua_isnil(L,-3) && !lua_isnil(L,-2) && lua_isfunction(L,-1);
    lua_settop(L,top);
    if (!ready) return;
    // FrameXML globals and the event array do not exist inside OnFrameScriptInitialized.
    debugPending = false;
    FrameScript_Execute(R"lua(
local f=CreateFrame("Frame",nil,UIParent)
f:SetPoint("TOP",0,-100) f:SetSize(700,60)
local t=f:CreateFontString(nil,"OVERLAY","GameFontNormal") t:SetAllPoints()
f:RegisterEvent("WRATH_SKATE_DEBUG")
f:SetScript("OnEvent",function(_,_,held)
 if held==0 then t:SetText("") return end
 local valid,failed,x,y,z,vx,vy,vz,air,ground,gen=WrathSkateGetPose()
 if valid then t:SetText(string.format("Skate | %s | ground %d air %d | collision gen %d\npos %.2f %.2f %.2f | vel %.2f %.2f %.2f",
  ground==1 and "GROUNDED" or air==1 and "AIRBORNE" or "NO CONTACT",ground,air,gen,x,y,z,vx,vy,vz))
 else t:SetText(failed and "Skate helper failed; see the per-process Logs/SkateHelper log" or "Skate helper starting or waiting for world collision (needs skateHelperPath and skateAssetPath, then re-enter world)") end
end)
WrathSkateDebugReady=true
)lua","SkatePhase1",nullptr);
    lua_getglobal(L,"WrathSkateDebugReady");
    const bool installed = lua_toboolean(L,-1) != 0;
    lua_settop(L,top);
    sExtensionMgr->Log("Skate: debug overlay initialization %s",installed?"ready":"failed (see Lua errors)");
    if (installed) FrameScript_Execute(skatepeer::Lua,"SkateGroupPeers",nullptr);
}
class SkateWorld final : public WorldScript {
public:
    SkateWorld() : WorldScript("SkateWorld") {}
    void OnWorldUpdate(float elapsed) override { if (inWorld) { UpdateCollision(elapsed); UpdateRig(); UpdateBoard(); UpdatePeers(); } }
    // The board model is a map object: it goes with the map (a transfer does not fire OnLeaveWorld).
    void OnMapUnload() override { PeerClear(); audio.Publish({}); StopArc(); RigRelease(); BoardDestroy(); }
    void OnLogoutComplete() override { PeerClear(); BoardDestroy(); }
    // Phase 5 camera. Fires several times per frame for the local camera; only the player's, only while skating.
    void OnCameraTargetFacing(CGCamera& camera, CGObject_C& target, float& yaw, float& pitch, float& roll) override {
        const auto* frame = CGWorldFrame::s_currentWorldFrame;
        const uint32_t now = OsGetAsyncTimeMs();
        const bool applies = driving && focused && collision.session && collision.map == ClntObjMgrGetMapID()
            && frame && frame->m_camera == &camera && target.m_obj
            && target.m_obj->m_guid == ClntObjMgrGetActivePlayer() && now - cameraAt < 250;
        // Either mouse button is mouse look: the view is the user's then (WoW-style), and eases again on release.
        cameraFollow.Apply(camera,yaw,pitch,roll,applies,cameraTarget,cameraSpeed,Down(VK_LBUTTON) || Down(VK_RBUTTON),now);
    }
};
class SkateLua final : public LuaScript {
public:
    SkateLua() : LuaScript("SkateLua") {}
    void OnFrameScriptInitialized(lua_State*, FrameScriptContext context) override {
        debugPending = context == FRAMESCRIPT_CONTEXT_GAME;
    }
    void OnFrameScriptDestroy(lua_State*) override { debugPending = false; PeerClear(); }
};
}
void AddSkateExtension() {
    peerEpoch=(GetTickCount()^GetCurrentProcessId())|1u;
    sExtensionMgr->RegisterLuaFunction("WrathSkateIsLocalPlayer",LocalPlayer);
    sExtensionMgr->RegisterLuaFunction("WrathSkateWindowFocused",Focus);
    sExtensionMgr->RegisterLuaFunction("WrathSkatePadConnected",Pad);
    sExtensionMgr->RegisterLuaFunction("WrathSkateGetPose",Pose);
    sExtensionMgr->RegisterLuaFunction("WrathSkatePeerState",ScriptPeerState,FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("WrathSkatePeerReceive",ScriptPeerReceive,FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("WrathSkatePeerPoseReceive",ScriptPeerPoseReceive,FRAMESCRIPT_CONTEXTS_GAME);
    // /run WrathSkateBail(seconds): the console's skateBail, for clients started without the console.
    sExtensionMgr->RegisterLuaFunction("WrathSkateBail",[](lua_State* L) -> int32_t {
        ConsoleSkateBail("skateBail",lua_isnumber(L,1) ? lua_tostring(L,1) : ""); return 0; });
    sExtensionMgr->RegisterFrameScriptEvent("WRATH_SKATE_DEBUG");
    sExtensionMgr->RegisterFrameScriptEvent("WRATH_SKATE_FOCUS_CHANGED");
    sExtensionMgr->RegisterFrameScriptEvent("WRATH_SKATE_PAD_CHANGED");
    new SkateClient(); new SkateLua(); new SkateWorld(); new SkateUnit(); new SkateModel(); new SkatePeerObjects(); new SkatePeerSpeedMessages();
}
