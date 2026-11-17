# Skate integration: porting wotlkskate into the client

Status: **Phases 1-5 implemented (5: board and camera built, audio and peers open).** Drafted 2026-11-14 from `R:\Users\Zach\Documents\GitHub\wotlkskate`
at `afa45f0` (the 19 October 2026 checkpoint).

## What wotlkskate is today

Three pieces glued onto a stock, byte-audited 12340 `Wow.exe`:

| Piece | Where | What it does |
|---|---|---|
| x86 native adapter | `native-probe/probe.cpp` + ~60 `*-native.h` headers, built as a `dinput8.dll` proxy | MinHook detours at hard-coded 12340 addresses, raw struct offsets (`+0x798`, `+0x44`, `+0xBC`), D3D9 vtable hooks to draw the board, XInput polling, XAudio2 sounds, Lua via `FrameScript_Execute` |
| x64 Rust helper | `native-probe/skate-bridge` (`wrath-skate-bridge.exe`), wraps `skatecraft-wrath/third_party/skate-host` | The actual Skate 3 simulation (board physics, tricks, grinds, ragdoll, rig pose). Talks binary `WSB1` frames over anonymous pipes (`skate-pipe.h`, `src/protocol.rs`, `src/shadow_wire.rs`) |
| Addon | `native-probe/addon` (`WrathRoll`, `SkateUtilities`, `SkatePeers`) | Key bindings, J/Y toggles, score text, save/return points, party board replication via `SendAddonMessage` |

Server side it leans on GM chat commands: `.modify speed walk N` (speed allowance), `.cast 90001/90002 triggered`
(fall-damage immunity from `server/skate-fall-protection.sql`) and `.go xyz` (return point).

Player movement is **not** forged: the adapter steers WoW's own `CMovement_C` (control bits, facing, run speed,
patched fall curves), so the client sends ordinary movement packets.

## Why this gets much simpler in our client

Every function the adapter detours already exists here as source with its `// OFFSET:` tag, and we are x64, so the
helper's bitness problem disappears.

| Adapter detour (12340) | Our source | Replacement |
|---|---|---|
| `0x4D4DB0` object lookup, `0xCA1238` local GUID | `ClntObjMgrObjectPtr` [ObjectMgrClient.cpp:215](../WoW/Source/ObjectMgrClient/ObjectMgrClient.cpp:215) | Call it directly |
| `0x5FBE10` SetControlBit | `CGInputControl::ToggleControlBit` [InputControl.cpp:1305](../WoW/Source/UIUtil/InputControl.cpp:1305) | Call it directly |
| `0x7A3B70` CWorld::Intersect | [Map.cpp:5075](../WoW/Source/WorldClient/Map.cpp:5075) | Call it directly |
| `0x988370 / 0x9870D0 / 0x988280` fall curve | `CMovementShared::StartFalling / RelDistanceFallen / CalcTimeFallen` [MovementShared.cpp](../WoW/Source/Object/MovementShared.cpp:1519) | New seam (Phase 3) |
| `0x7612B0 / 0x7618B0 / 0x7619C0` air sweep, fall, floor probe | `CMovement_C::FallDown / Fall / WillPassObstacle` [Movement.cpp:3452](../WoW/Source/Object/Movement.cpp:3452) | New seam (Phase 3) |
| `0x82F0F0`, `0x4C1DE0`, `0x4C1B30` rig pose | `CM2Model::AnimateMT` [M2Model.cpp:5348](../Engine/Source/Model2/M2Model.cpp:5348), `C44Matrix(quat)`, `C44Matrix::Translate` | New per-bone override seam (Phase 4) |
| `0x604490` camera | `CGCamera::UpdateTargetFacing` [Camera.cpp:1375](../WoW/Source/UIUtil/Camera.cpp:1375) | Existing `WorldScript::OnCameraUpdated` |
| D3D9 EndScene/Draw*/Clear/Reset vtable patch | — | Board becomes a real `CM2Model` (Phase 5); renders on Vulkan/DX11/D3D9 with no depth-capture hacks |
| `0x819210` FrameScript_Execute, Lua register | — | `ExtensionMgr::RegisterLuaFunction` / `RegisterFrameScriptEvent` |
| Raw offsets `+0x44` flags, `+0x94` run speed, `+0x798` position | `CMovementShared::m_flags`, `m_runSpeed`, … [MovementShared.h](../WoW/Source/Object/MovementShared.h:33) | Named members |

Everything the adapter needed `client-fingerprint.h`, `inspect_client.py`, the `audit_*.py` scripts and the
`*-fingerprint.h` byte checks for is gone: we compile against the code instead of pattern-matching it.

## Target shape

```
Extensions/Skate/
  src/Skate.cpp            AddSkateExtension(): registers the scripts below
  src/SkateHelper.cpp/.h   owns wrath-skate-bridge.exe, WSB1 framing (port of skate-pipe.h)
  src/SkateCollision.cpp   gathers triangles around the player from loaded terrain/WMO/M2
  src/SkateInput.cpp       XInput + keyboard -> controller packet (port of xbox-input.h, xbox-native.h)
  src/SkateMotion.cpp      engine pose -> CMovement_C (port of skate-ground/vertical/rolling/ollie-native.h)
  src/SkatePose.cpp        engine bones -> player rig (port of character-pose-native.h + retarget headers)
  src/SkateBoard.cpp       board CM2Model, peers (port of world-board-native.h, peer-visual-native.h)
  src/SkateCamera.cpp      OnCameraUpdated follow (port of camera-follow-native.h)
  src/SkateAudio.cpp       XAudio2 player (port of skate-audio-native.h, unchanged)
  src/SkateLogic/*.h       the engine-independent headers copied as-is (see below)
  Skate.cmake              links xinput/xaudio2, copies the helper next to the exe
Addons/WrathRoll/          the existing addon, trimmed (Phase 6)
<server module>            speed allowance, fall immunity, return point (Phase 6)
```

It is a normal extension (`Extensions/<Name>/src`), so it gets `WOWDECOMP_EXTENSION_SKATE` for free and can be
switched off at configure time.

### Keep the helper as a separate process

The Rust helper is proven (65 unit tests, recorded engine runs) and isolates the Skate engine from the client: a
panic in the engine kills the helper, not WoW. Port the x86 side of the pipe, not the engine. Linking skate-host
as a `staticlib` in-process is possible now that both sides are x64, but it is a later optimisation, not part of
this port.

### Reuse the engine-independent headers verbatim

About half of `native-probe/*.h` is pure logic with its own `*-tests.cpp`: `ollie-controller.h`,
`ground-transition.h`, `ground-heading.h`, `grind-motion.h`, `bail-recovery.h`, `onfoot-state.h`,
`character-retarget.h`, `model-board-size.h`, `run-speed-lease.h`, `rider-air-*.h`, `contact-recovery.h`,
`ledge-authority.h`, `checkpoint-handoff.h`, and the like. Copy them into `src/SkateLogic/` unchanged and bring
their test files along. Only the `*-native.h` files (the ones that cast addresses) get rewritten.

Before copying, check each against `#include <windows.h>` / `<d3d9.h>` / raw address use; anything that has them
is a `*-native` file in disguise.

## Phases

Each phase ends in something you can see in game. Do not start the next one until the current one works live.

### Phase 0: ground rules and inventory (no code)

Inventory done 2026-11-14 against wotlkskate `afa45f0`. Our paths are relative to the repo root; addresses were
looked up by grepping the `OFFSET:` / `STATIC:` tags. "Mid" means the address is inside a function, not an entry.

Ground rules:

1. Decide where the Skate 3 assets live. The helper needs the user's own converted `default.xex` + `data/`; none
   of it can ship in this repository or a client MPQ. Point the helper at it through a CVar
   (`skateAssetPath`) instead of the hard-coded `D:\world of skatecraft 3.3.5\...` paths.
2. Remove the helper's hard-coded MPQ path and the helper-side world loader. This is bigger than one constant,
   see "Helper collision" below.
3. Drop everything that exists only to stay safe inside a stock exe: `LocalWorldConnection()` loopback check,
   fingerprints (12 `*-fingerprint.h` byte tables), `PREFLIGHT_*` refusals, the `.request` arming files, the
   `*-STATUS.md` build banners. The "local player only, never a selected unit" rule stays.
4. Build the helper once with `cargo build --release` from `native-probe/skate-bridge` and confirm `--smoke` passes.
   Not done in the inventory pass (no code, no builds).

#### Counts

The DLL references 102 distinct 12340 addresses outside the fingerprint tables: 47 function entries (all found),
42 mid-function addresses (all located inside a found function), and 13 data-range addresses (9 found through
`STATIC:` tags, 1 found by arithmetic, 2 vtable addresses that have no tag and need none, 1 image base).
Only 10 are detoured, plus 7 D3D9 vtable slots. There are no byte patches and no `WriteProcessMemory`.

#### Patch sites installed (MinHook detours, probe.cpp and `*-native.h`)

| Hook | Address | Our function | Notes |
|---|---|---|---|
| RigModelHook | 0x82F0F0 | `CM2Model::AnimateMT` Engine/Source/Model2/M2Model.cpp:5348 | entry; only armed with `M2UseThreads` = 0 (see Corrections) |
| RigRotationHook | 0x4C1DE0 | `C44Matrix::C44Matrix(const C4Quaternion&)` Engine/Source/Tempest/C44Matrix.cpp:226 | global math function, filtered by return address 0x82FC3D (our bone loop, M2Model.cpp:5682) |
| RigTranslationHook | 0x4C1B30 | `C44Matrix::Translate` Engine/Source/Tempest/C44Matrix.cpp:142 | x86 naked asm bridge; stages 0x82FD16..0x82FE40 (M2Model.cpp:5750 area) |
| CameraAnglesHook | 0x604490 | `CGCamera::UpdateTargetFacing` WoW/Source/UIUtil/Camera.cpp:1375 | callers allowed: 0x604B80 `MakeRelativeTo` :778, 0x605510 `SetTelescope` :4171, 0x6071DE `CalcTargetCamera` :2336 |
| VerticalStartHook | 0x988370 | `CMovementShared::StartFalling` WoW/Source/Object/MovementShared.cpp:1519 | caller gate 0x98846D = `Jump` :1738 |
| VerticalDeltaHook | 0x9870D0 | `CMovementShared::RelDistanceFallen` MovementShared.cpp:1438 | caller gates 0x760154 `GetMoveFacets` Movement.cpp:3116, 0x7618F6 `Fall`, 0x761A20 `WillPassObstacle` |
| VerticalInverseHook | 0x988280 | `CMovementShared::CalcTimeFallen` MovementShared.cpp:292 | caller gate 0x75EB78 = `CMovement_C::GetTimeJustFallen` Movement.cpp:1699 |
| AirRootSweepHook | 0x7612B0 | `CMovement_C::FallDown` WoW/Source/Object/Movement.cpp:3452 | caller gates 0x7619A9 (in `Fall`), 0x761A66 (in `WillPassObstacle`) |
| AirRootFallHook | 0x7618B0 | `CMovement_C::Fall` Movement.cpp:3646 | caller gates 0x7629F5 `HoverMove` :4069, 0x763008 `CollideRequestMove` :4224 |
| NativeFloorProbeHook | 0x7619C0 | `CMovement_C::WillPassObstacle` Movement.cpp:3678 | sole caller 0x76207F = `AttemptStepUp` :3703 |

Every hook except the camera and rig ones decides by `_ReturnAddress()`, so the original acts only for specific
callers. The Phase 3 seams must be placed at those call sites (or pass a caller tag), not just at function entry.
The x87 `_fxsave`/`_fxrstor` around the hooked calls is a 32-bit artifact and goes away.

| D3D9 slot patch (probe.cpp:395-425) | Index | Our function | Notes |
|---|---|---|---|
| Reset, EndScene, Clear, DrawPrimitive, DrawIndexedPrimitive, DrawPrimitiveUP, DrawIndexedPrimitiveUP | 16, 42, 43, 81, 82, 83, 84 | NOT NEEDED | device read from `g_theGxDevicePtr` 0xC5DF88 (Engine/Source/Gx/CGxDevice/CGxDevice.cpp:76) + 0x397C `m_d3dDevice` (CGxDeviceD3d.h:213). Replaced by the board `CM2Model` (Phase 5) |

#### Game functions the DLL calls or byte-audits (not detoured)

| Address | Our function | Notes |
|---|---|---|
| 0x4D4DB0 | `ClntObjMgrObjectPtr` WoW/Source/ObjectMgrClient/ObjectMgrClient.cpp:215 | |
| 0x4D4B30 | `ClntObjMgrEnumVisibleObjects` ObjectMgrClient.cpp:233 | gameobject scan for platforms/doors |
| 0x5FBE10 | `CGInputControl::ToggleControlBit` WoW/Source/UIUtil/InputControl.cpp:1305 | original takes 4 stack args, matches ours |
| 0x77F310 | `World::Intersect` WoW/Source/WorldClient/World.cpp:4251 | the ray the DLL actually calls (not 0x7A3B70, see Corrections) |
| 0x7A3B70 | `CMap::Intersect` WoW/Source/WorldClient/Map.cpp:5075 | comment/audit anchor only; it publishes the hit GUID at 0xCD7768 |
| 0x819210 | `FrameScript_Execute` Engine/Source/FrameScript/FrameScript.cpp:952 | xbox/pose/focus status Lua strings |
| 0x84DBD0 / 0x84DBF0 / 0x84E0E0 / 0x84E590 | `lua_gettop` / `lua_settop` / `lua_tolstring` / `lua_getfield` Engine/Source/lua-5.1/src/lapi.c:193, 200, 409, 644 | read addon-set globals |
| 0x98A0B0 | `CMovementShared::ChangeRunSpeed` MovementShared.cpp:412 | run speed lease |
| 0x987570 | `CMovementShared::GetBaseSpeed` MovementShared.cpp:515 | audit only |
| 0x613290 | `Script_GetUnitSpeed` WoW/Source/Ui/ScriptEvents_Scripts.cpp:1765 | audit only |
| 0x72EA50 | `CGUnit_C::OnSetRawFacingLocal` WoW/Source/Object/ObjectClient/Unit_C.cpp:11959 | facing steer |
| 0x86AE20 | `OsGetAsyncTimeMs` Engine/Source/Os/Win32/TimeManager.cpp:155 | |
| 0x5FBF80 | `Script_JumpOrAscendStart` WoW/Source/UIUtil/InputControl_Scripts.cpp:22 | ollie audit |
| 0x5FC200 / 0x5FC250 | `Script_MoveForwardStart` / `Stop` InputControl_Scripts.cpp:130, 143 | audit only |
| 0x72EB80 | `CGUnit_C::TryJumpOrAscend` Unit_C.cpp:12197 | ollie |
| 0x6ECC20 | `CMovement_C::OnJumpLocal` Movement.cpp:1056 | ollie audit |
| 0x988490 | `CMovementShared::StopFalling` MovementShared.cpp:1264 | landing handoff |
| 0x75B480 / 0x75B4F0 | `CMovement_C::SaveMoveState` / `RestoreMoveState` Movement.cpp:649, 635 | floor-probe scope |
| 0x6E6EF0 / 0x6E6F10 / 0x6E6F40 | `CGUnit_C::GetPosition` Unit_C.cpp:9884 / `GetRawPosition` :6714 / `GetFacing` :9578 | the DLL also calls vtable slots 0x30, 0x38 |
| 0x4F4460 / 0x4F42A0 | `CPassenger::GetPosition` / `GetFacing` WoW/Source/Object/Passenger.cpp:57, 37 | ride frame |
| 0x712D50 | `CGGameObject_C::GetRawPosition` GameObject_C.cpp:591 | platform observer |
| 0x4F5960 / 0x4F6D20 | `CGWorldFrame::GetActiveCamera` WoW/Source/Ui/WorldFrame.cpp:154 / `GetScreenCoordinates` :395 | board draw |
| 0x4C2270 / 0x47BFF0 | `operator*(C4Vector, C44Matrix)` Engine/Source/Tempest/C44Matrix.cpp:318 / `NDCToDDC` Engine/Source/Base/Coordinate.cpp:39 | board projection, goes away |
| 0x5FE5F0 / 0x607BD0 | `CGCamera::UpdateYaw` Camera.cpp:1448 / `CSimpleCamera::SetFacing` WoW/Source/UIUtil/CSimpleCamera.cpp:73 | camera follow |
| 0x8272F0 / 0x8265E0 | `CM2Model::SetBoneProceduralTransform` M2Model.cpp:1981 / `SetBoneFlags` :1474 | rig observer audit |

Mid-function audit anchors (byte-compared only, not called), all inside functions above:
0x4D4B6D `ClntObjMgrEnumVisibleObjects`, 0x4D4DF1 `ClntObjMgrObjectPtr`, 0x714284 / 0x714967 / 0x71436D
`CGGameObject_C::CGGameObject_C` GameObject_C.cpp:1883, 0x712FBC `PostInit` :2010, 0x712EE8 `GetMatrix` :521,
0x70CBB5 `CGUnit_C::SetStorage` Unit_C.cpp:11488, 0x7A3BE2 / 0x7A3C58 `CMap::Intersect`, 0x73E19A
`CGUnit_C::PreAnimate` Unit_C.cpp:1429, 0x833026 `CM2Model::InitializeLoaded` M2Model.cpp:7490, 0x82F2F5 and
0x82FC2E..0x82FE40 `AnimateMT` (model-to-world multiply, bone rotation/translation stages), 0x4027B9 / 0x4028C4
`GetM2CvarFlags` WoW/Source/Client_CVar.cpp:770, 0x7617B6 `FallDown` (inline fall-clock reset), and
0x762066 / 0x7629E1 / 0x762FEB / 0x75C777 in `AttemptStepUp` :3703 / `HoverMove` / `CollideRequestMove` /
`ClipCollisionBox` :2350.

#### Globals read

| Address | Our symbol | Use |
|---|---|---|
| 0xCA1238 | `CGUnit_C::s_activeMover` Unit_C.cpp:206 | what the DLL calls "local player GUID" (see Corrections) |
| 0xAB63BC | `s_simpleMapID` WoW/Source/LoadingScreen.cpp:68 | current map id |
| 0xB499A4 | `CSimpleTop::s_eventTime` Engine/Source/Frame/CSimpleTop.cpp:32 | event time passed to `ToggleControlBit` |
| 0xC24954 | `CGInputControl::s_inputControl` InputControl.cpp:47 | |
| 0xC5DF88 | `g_theGxDevicePtr` CGxDevice.cpp:76 | D3D9 only, goes away |
| 0xD3F78C | `s_context` (lua_State*) FrameScript.cpp:24 | |
| 0xB7436C | `CGWorldFrame::s_currentWorldFrame` WorldFrame.cpp:144 | camera at +0x7E20 |
| 0xB2F00C | `s_M2UseThreadsVar` Client_CVar.cpp:61 | the DLL reads the CVar's int value (+0x30) |
| 0xCD7768 | `s_lastCollisionGUID` Map.cpp:397 | file-static; needs an accessor |
| 0xADBA38 | `s_moveFacets` Movement.cpp:45 (0xADBA34) +4 | collision plane count; found by arithmetic |
| 0xA34640, 0xA34670 | NOT FOUND (no tag) | `CGGameObject_C` vtable and its slot 12 (`GetRawPosition`); use a typed cast instead |
| 0x400000 | image base | preflight only |

#### Raw struct-offset reads

| Raw read | Our member | Notes |
|---|---|---|
| player+0xD8 -> movement | `CGUnit::m_movement` WoW/Source/Object/Unit.h:25 | |
| movement+0x10 / 0x14 / 0x18 | `CPassenger::m_position` Passenger.h:18 | |
| movement+0x28 | `CPassenger::m_ownerGuid` Passenger.h:22 | |
| movement+0x44 | `CMovementShared::m_flags` MovementShared.h:33 | |
| movement+0x80 / 0x84 / 0x8C | `m_fallTimeMs` / `m_fallStartZ` / `m_currentSpeed` | |
| movement+0x94 | `m_runSpeed` | |
| movement+0xB8 / 0xBC | `m_fallVelocity` / `m_spline` | |
| player+0x798 | `CGUnit_C::m_movementData` (0x788) + `m_position` (0x10) | probe only |
| frame+0x7E20, +0x340 | `CGWorldFrame::m_camera`, `m_viewProjection` WorldFrame.h:153, 145 | frame+0x64 is `unused_0064`, NOT FOUND as a used member |
| camera+0x9C (& 0x40), +0x38 | `CGCamera::m_flags` Camera.h:116; +0x38 is `CSimpleCamera::m_nearZ` | |
| model+0x10 / 0x2C / 0x94 / 0x98 / 0xB4 / 0xF4 | `CM2Model::m_fFlags` / `m_shared` / `m_bones` / `m_boneMatrices` / `m_worldTransform` / `m_matrixF4` M2Model.h:142-184 | |
| shared+0x150 | `CM2Shared::m_data` M2Shared.h:59 | |
| header+0xC..0x38, +0xD8..0xE4 | `M2Data` array members | not mapped field by field |
| bone record 0xAC bytes, flags +0x8C | `M2ModelBone` M2Model.h:450, `m_flags` | layout holds in Win32 only; x64 shifts (pointer member) |
| unit+0xB4 (rig observer) | `CGObject_C::m_worldModel` Object_C.h:103 | |
| gameobject +0xB4, +0x1A8 | `m_worldModel`, `CGGameObject_C::m_matrix` GameObject_C.h:295 | +8, +0xD0, +0x218 (field/data pointers, object size) not mapped field by field |
| unit vtable slots 0x30, 0x38 | virtual `GetPosition` / `GetFacing` | call the methods |

#### Header verdicts (native-probe/*.h, 99 files)

63 VERBATIM, 24 REWRITE, 12 fingerprint tables (drop). "VERBATIM" means no game address, no struct offset, no
D3D9 and no hook code. Two caveats apply to many of them without changing the verdict: (a) any header that
includes `skate-pipe.h` pulls `<windows.h>` transitively (bail-recovery, onfoot-possession, passenger-input,
powerslide-input, trick-state, trick-score, skate-visual, and through those board-placement, board-terrain,
character-alignment, character-ragdoll, character-retarget, character-foot-contact, model-board-size,
rider-air-*, ...). That is harmless in a Windows-only client but needs `NOMINMAX` (skate-pipe.h uses
`(std::min)`); splitting the wire structs from the `Pipe` class would remove it. (b) `ground-tuning.h` holds a
mutable `static bool NaturalMomentum`; that only works because the DLL is one translation unit, so make it
`inline` when the headers are shared between TUs.

| Header | Verdict | Reason |
|---|---|---|
| ollie-controller, ground-transition, ground-heading, ground-grid, ground-tuning, grind-motion, bail-recovery, onfoot-state, onfoot-terrain, onfoot-possession, ledge-authority, checkpoint-handoff, contact-recovery, rough-contact, riding-contact, flight-handoff, rolling-controller, run-speed-lease, recovery-transport, ragdoll-root-handoff, start-surface, vanilla-root, skate-vertical, skate-ground-motion, trick-input, trick-state, trick-score, powerslide-input, passenger-input, motion-observation, shadow-state, air-root-motion | VERBATIM | pure math/state on `<cstdint>`/`<cmath>`. air-root-motion.h has `static_assert(sizeof(Point)==12)` (three floats, fine on x64) |
| character-retarget, character-alignment, character-foot-contact, character-height, character-ragdoll, ragdoll-surface, character-equipment-riders, character-rig-plan, character-rig-schema, rig-binding, model-board-size, rider-air-alignment, rider-air-feet, rider-air-legs, visual-pose-sync, board-placement, board-terrain, skate-visual | VERBATIM | pure pose math. `uintptr_t` fields in character-rig-schema / rig-binding / model-board-size are identity tokens only. character-rig-schema `Valid()` requires M2 version 264 (0x108), same in our client. Vertex colours 0xff32859d / 0xffe8dbb8 are Skate mesh markers, not game data |
| movement-phase, control-ownership | VERBATIM, check constants | hard-coded 3.3.5a movement flag bits (0x100, 0x200, 0x400, 0x800, 0x1000, 0x100000, ...) and the CInputControl bit 0x10. Same game build so they match; swap for our enum names when touched |
| local-player | VERBATIM, check rule | `guid>>32==0` assumes a plain 3.3.5 player GUID; confirm for our custom races/characters |
| camera-follow, dynamic-world, ride-frame, peer-visual, world-map, background-input, skate-audio | VERBATIM | pure. world-map.h includes the generated `world-map-catalog.h` (gitignored, built by `generate-world-map-catalog.py`); its known-map list must be removed or made to accept CoA maps and 1820 |
| skate-pipe | VERBATIM with one fix | Win32 CreateProcess/job-object/pipe wrapper, x64-clean. Line 191 hard-codes the helper's working directory `D:\world of skatecraft 3.3.5\native-probe`; make it a parameter |
| xbox-input | VERBATIM | `<windows.h>` + `LoadLibraryEx("xinput1_4.dll")`, no game dependency; the `static_assert`s are the XInput packet size, fine on x64 |
| native-floor-probe | VERBATIM | gate object only; useful only if the Phase 3 seam keeps the "ignore Source overrides during the stock step-down probe" behaviour |
| character-pose-scope | REWRITE (partly salvageable) | `Record` mirrors the raw 0xAC bone record with `static_assert(offsetof(Record,flags)==0x8C)`; `Begin`/`End` clear bone flag 0x80 in place; `Slot()` does pointer arithmetic from the quaternion temporary (`base+0x1C`, stride `sizeof(Record)`); `TranslationStageMatches` assumes a `+0xC8` stack temporary. That is the hook technique and breaks on x64. `CopyRotation`, `AddRootOffset`, `AddRagdollRootOffset`, `RemoveAnimatedTranslation`, `BodyBones` are reusable. 6 tests include it |
| rig-nested-update, rig-translation-dispatch | REWRITE (drop) | `__declspec(naked)` x86 asm, `__try/__finally`, `gRigModelUpdate` hook state |
| character-rig-observer | REWRITE | addresses 0x73E19A/0x833026/0x8272F0/0x8265E0, raw model offsets, `GetTickCount`, `LocalWorldConnection` |
| camera-follow-native | REWRITE | 0x604490 detour, 0x5FE5F0, 0x607BD0, 0x604B80/0x605510/0x6071DE, 0xB7436C, frame+0x7E20, camera+0x9C |
| character-pose-native | REWRITE | the three rig detours, 0x82F2F5, 0xB2F00C, 0xAB63BC, 0xD3F78C, 0x819210, model offsets, `D:\` request-file paths |
| skate-vertical-native | REWRITE | the seven fall/step detours, 0xADBA38, `_ReturnAddress` gates, `_fxsave` |
| skate-ground-native | REWRITE | movement+0x44/0x80/0xBC, 0xCD7768, 0x72EA50, 0x86AE20 |
| rolling-native | REWRITE | 0x98A0B0, 0x5FBE10, 0xC24954, 0xB499A4, player+0xD8, movement+0x28/0x44/0x8C/0x94/0xBC |
| ollie-native | REWRITE | 0x5FBF80, 0x72EB80, 0x6ECC20, 0x988490, 0xCD7768, 0xB499A4, movement offsets |
| dynamic-world-native, platform-observer-native, ride-frame-native | REWRITE | gameobject scan via 0x4D4B30/0x4D4DB0, vtable 0xA34640/0xA34670, object +0xB4/+0xD0/+0x1A8/+0x218, GameObject accessor addresses |
| world-board-native | REWRITE | 0x4F5960, 0x4F6D20, 0x4C2270, 0x47BFF0, 0xB7436C, frame+0x7E20/0x340 |
| peer-visual-native | REWRITE | 0x4D4DB0, lua API addresses, 0xAB63BC, 0x819210 |
| skate-utilities-native | REWRITE | 0x4D4DB0, 0xAB63BC, player vtable slots, lua API addresses |
| xbox-native | REWRITE | 0x819210 focus Lua strings, `LocalWorldConnection`, `GetForegroundWindow` polling |
| board-terrain-native | REWRITE | thin wrapper over native reads (the pure part is board-terrain.h) |
| skate-audio-native | REWRITE (light) | XAudio2 player with `<xaudio2.h>`, `<objbase.h>`, file reads, one `D:\` path. No game addresses, so it ports nearly as-is, but it is not a pure header and its test includes it |
| skate-shadow | REWRITE | 1402-line orchestrator: 0x819210, 0xAB63BC, 0xD3F78C and the lua API addresses, `GetTickCount`, `ShadowWorker` thread, `CreateFileW` dumps, 24 `D:\` paths, includes six `*-native.h` |
| skate-render, world-depth, native-depth-capture, world-board | REWRITE (drop) | `<d3d9.h>` draw/depth hooks, replaced by the board `CM2Model` in Phase 5. world-board.h includes skate-render.h and world-depth.h |
| *-fingerprint (12): air-root, camera-follow, character-rig, client, dynamic-world, equipment-stage, flight-handoff, model-board-size, ollie, ride-frame, vertical, world-camera | DROP | byte signatures of the stock exe |

#### Tests that come along

76 test sources. They are standalone `main()` programs built by per-feature `build-*.ps1` scripts (32-bit `cl
/W4 /WX`), not by CMake, so the Testing section's "test target" is new work.

- 42 are clean: they include only VERBATIM headers and run unchanged (for example ollie-controller via
  trick/natural-momentum, ground-transition, ground-heading, ground-grid, bail-recovery, bail-entry,
  flight-handoff, ledge-authority, contact-recovery, checkpoint-handoff, controller (run-speed-lease,
  rolling-controller), trick-score, rig-binding, rough-contact, start-surface, riding-contact, steep-bank,
  wall-jump, ragdoll-root-handoff, movement-stop, onfoot-terrain, passenger-input, vanilla-reference).
- 16 need gitignored fixtures that cannot be committed: `tests/human-male-observed.h` (an observed stock human
  male rig, so derived from game data; used by character-alignment, character-foot-contact, character-retarget,
  grab, grind, model-board-size, ollie, onfoot-pose, onfoot, powerslide, ragdoll-surface, trick and
  xbox-controller tests), `peer-board-template.h` (peer-visual-tests, Skate asset derived) and the generated
  `world-map-catalog.h` (continent-tests, instance-map-tests). Either regenerate them in a local-only step or
  replace them with a synthetic humanoid rig fixture.
- 18 cannot come along as is: 4 call `MH_CreateHook` on test fixtures (air-root, camera-follow, character-pose,
  skate-vertical tests; they test the detour calling conventions), 6 include `character-pose-scope.h`, 5 include
  the D3D9 headers, 2 include `dynamic-world-native.h` / `skate-audio-native.h`, and some overlap.

Header-to-test pairing for the guide's list: ollie-controller (ollie-tests, flight-handoff-tests,
ground-transition-tests, trick-tests), ground-transition (ground-transition-tests, steep-bank-tests),
ground-heading (ground-heading-tests), grind-motion (grind-tests, fixture), bail-recovery (bail-recovery-tests,
bail-entry-tests, onfoot-tests), onfoot-state (onfoot-tests, onfoot-pose-tests, onfoot-remount-tests, fixture),
character-retarget (character-retarget-tests, fixture), model-board-size (model-board-size-tests, fixture),
run-speed-lease (controller-tests), rider-air-alignment/legs (only trick-tests, fixture), rider-air-feet (no
test), contact-recovery (contact-recovery-tests), ledge-authority (ledge-authority-tests), checkpoint-handoff
(checkpoint-handoff-tests). The helper's own `cargo test` and `test-bridge.py` are unaffected.

#### Helper collision (rule 2 in detail)

- `world_collision.rs:15` `DATA` is also used by `shadow_wire.rs:1185` (ops 16/46), `map_catalog.rs`,
  `instance_probe.rs` and `ledge_probe.rs`. `shadow_wire.rs` stores a `WorldCollision` in the session and uses it
  for ground refresh (ops 17/47), the ride frame and dynamic-world (`dynamic_world.rs` imports `HALF`,
  `MAX_TRIANGLES`, `Tri`).
- In-world init is ops 16/46 (`origin`, `centre`, heading, optional ride frame): the helper loads the map itself.
  Only ops 1/30 take triangles from the parent, capped at 8192 (`protocol.rs:10`, `shadow_wire.rs:721`); the
  helper's own limit is `MAX_TRIANGLES` 49,152 (`world_collision.rs:19`). Phase 2 therefore needs a new or changed
  op that accepts triangles for init and for refresh, and a larger cap, not just "use the init op".
- `world_collision::map_name(map)` (shadow_wire.rs:1173) rejects maps outside the catalog, so CoA maps and 1820
  fail in the helper too, not only in `world-map.h`.
- The helper depends on `benilla-formats` (MPQ/WMO/ADT reader) by path from `../../skatecraft-wrath`, and its dev
  build on a git dependency, so building it standalone needs that whole tree.

#### Corrections to this guide

1. `0x7A3B70` is `CMap::Intersect`, not "CWorld::Intersect"; the DLL calls `World::Intersect` at 0x77F310
   (World.cpp:4251). The own-body filter reads the hit-model GUID that `CMap::Intersect` leaves in
   `s_lastCollisionGUID` (0xCD7768); that file-static is not exposed, so Phase 2/3 needs an accessor or a new out
   parameter. The guide's table row omits this side channel.
2. `0xCA1238` is `CGUnit_C::s_activeMover`, not the local player GUID. Mounted/possessed/vehicle states differ
   from the player; decide which one "local player only" means.
3. `OnCameraUpdated` (WorldFrame.cpp:2060) fires after `CGCamera::UpdateCallback`, when yaw/pitch and the boom are
   already composed. The original edits the yaw/pitch/roll locals inside `UpdateTargetFacing` before the basis and
   collision sweep. Following from `OnCameraUpdated` would lag a frame or need a rebuild; add a seam in
   `UpdateTargetFacing` (three callers) instead.
4. The rig technique needs `M2UseThreads` = 0: the DLL refuses to arm otherwise (`require_single_threaded_animation`,
   reads 0xB2F00C+0x30), and our CVar defaults to 1 (Client_CVar.cpp:774). The Phase 4 per-bone seam avoids this
   only if it reads a snapshot, as the guide says.
5. The pose hooks clear bone flag 0x80 on 23 body bones for the duration of `AnimateMT` and write 4x4 float
   matrices (translation at [12..14]) in place. A per-bone seam replaces this, but our `M2ModelBone` is not the
   0xAC-byte record on x64 (pointer member), so code ported from `character-pose-scope.h` must use members.
6. Phase 1 says "op codes 1-6/10". The in-world path also uses 16/46, 17/47, 30 and 44/45 (see Helper collision).
7. Reuse section: the list of "logic" headers is incomplete and checking for `windows.h` is not sufficient.
   The real checks are transitive `skate-pipe.h` includes, the mutable static in `ground-tuning.h`, three
   gitignored generated headers and hard-coded flag constants (see Header verdicts).
8. Phase 3 table: the function mapping for 0x7612B0/0x7618B0/0x7619C0 is right, but the callers listed above
   decide whether the override applies; the seam arguments must carry that context.
9. Not mentioned in the guide: 39 hard-coded `D:\world of skatecraft 3.3.5` strings across 9 DLL headers/sources
   (request files, helper working directory, logs); all go with the DLL scaffolding.

### Phase 1: helper link and input (board simulates, player doesn't move)

- `SkateHelper`: spawn `wrath-skate-bridge.exe --shadow <assets>` with redirected pipes on `OnEnterWorld`, kill on
  `OnLeaveWorld`/`OnShutdown`. Port the `WSB1` header, op codes 1–6/10, epoch and stale-reply rejection from
  `skate-pipe.h`. Pipe I/O on a worker thread; the main thread only swaps the latest pose
  (the README's "keep pipe I/O off the render callback" rule).
- `SkateInput`: port `xbox-input.h` (XInput poll, `Toggle`, `ResumeGate`) and the keyboard fallback. Neutral input
  when the window is unfocused (`BACKGROUND_PHYSICS_BUILD` behaviour).
- Register `WrathSkate*` Lua functions (`WrathSkateIsLocalPlayer`, focus/pad events) through `RegisterLuaFunction`
  instead of executing Lua strings.
- **Done when:** with J held in skate mode, the debug text shows engine position/velocity changing from pad input
  on a flat test triangle pair.

### Phase 2: collision from the client's own world

The helper's init op already accepts up to 8192 triangles from the parent; use that, not its MPQ reader.

- `SkateCollision`: around the player (the helper uses a 24×24×45 yd box, `HALF` in `world_collision.rs`), pull
  triangles from loaded `CMapChunk` terrain, `CMapObjGroup` collision (BSP faces) and M2 collision meshes of
  doodads/gameobjects. Convert with the documented mapping: subtract origin, `(x, z, -y) * 0.9144`.
- Refresh with `OnTerrainChunkCreated`/`OnMapObjGroupLoaded` and when the player nears the box edge, on a worker,
  bounded (the existing "18 m hard edge / 12 m pose continuation" rules in `POSE-CONTINUATION-STATUS.md`).
- This is the big win over the stock-exe build: CoA maps, map 1820, Retail WMOs/ADTs and housing plots all work
  because we read what the client actually loaded, not the 3.3.5 MPQ chain.
- Port the rail/edge detection inputs (`rail_registry.rs` expects edges from the same triangle set).
- **Done when:** the debug overlay shows grounded state and ollies landing on real terrain and on a WMO floor.

#### Wired (2026-11-15, task t-19d1ba05 item 2)

Uses ops 48/49/50 ([SKATE_HELPER_PROTOCOL.md](SKATE_HELPER_PROTOCOL.md)), not op 1 and not the helper's MPQ reader.

- `Extensions/Skate/src/SkateCollision.h`: packet encoder (44-byte prefix + `count*9` floats, init generation 1 with
  centre == origin, refresh heading word 0, status 0, 1..49,152 triangles), `Keep` (drops non-finite, > 10,000 m from
  origin, zero-area triangles the helper would reject) and `Generations` (strictly increasing; only the latest accepted
  generation is awaited, superseded ones are ignored; a failure keeps the old world).
- `Skate.cpp` `UpdateCollision`, from a new `WorldScript::OnWorldUpdate` (main thread): gathers the helper's own box
  (half extents 24 x 24 x 45 yd, `HALF` in `world_collision.rs`) with `GatherCollisionTriangles(..., 0x80100111)`,
  drops triangles tagged with the local player's or `CGUnit_C::s_activeMover`'s GUID, and hands the copy to the
  worker. Init (op 48) when there is no session, the map changed (transfers do not fire `OnEnterWorld`), or the board
  is > 5,000 yd from the origin; otherwise refresh (op 49) once the board is 8 yd (XY) / 20 yd (Z) from the last gather
  centre, at most every 0.5 s. The box follows the board pose (`origin + (x, -z, y) / 0.9144`), since Phase 2 does
  not move the player. A TRUNCATED gather retries with the horizontal half extent halved (24 -> 12 -> 6 yd,
  `skatecollision::Shrink`) and sends the largest box that fits, logging `Skate: collision box W x W x 90 yd`; the
  refresh distance is a third of the half extent used. If even 6 yd is truncated, or the gather is INCOMPLETE,
  nothing is sent and the installed world stays; it is retried. Heading = player facing (the
  helper's heading uses the same convention, `velocity = (cos h, -sin h)` in engine x/z).
- `SkateHelper.cpp` worker: op 48 with a fresh session epoch (the cancel epoch stays separate), op 49 with the next
  generation, op 50 polled while a generation is outstanding. Rejections and async failures keep the old session and
  ask the main thread to regather. Steps (ops 5/6) start only after op 48 installs the client world; the synthetic
  op 1 square is gone. `prepare_transport.py` gives ops 48 and 7 the 20 s init deadline in `SkateTransport.h`.
- Ollie: what blocked it was client-side. Op 18 (the only step op that accepts a pop, right stick Y > 0) requires the
  board preview from op 7, and op 48 resets the preview, as every session init does. Phase 1 never sent op 7. The
  worker now sends op 7 after each op 48 and then steps with op 18 (`OllieStep`), using the first 60 bytes of the
  reply (the board frame follows). If op 7 is refused, it falls back to ops 5/6 without ollie. Input: pad right
  stick (pull down, flick up), or keyboard Space (hold to crouch, release to pop). No helper change was needed. A
  smoke run against the real Phase 2 helper with the real assets (op 48 flat floor, op 7, op 18) went airborne
  13 ticks after the pop and landed `ground 1` 44 ticks later. Op 7 replied 1.25 MB in 0.06 s; op 18 replies were
  at most 27,780 bytes (limit 65,536).
- Right-stick deadzone (live test fix, 2026-11-15): a right stick past roughly ±9,000 reads as crouch or pop to the
  engine, and then every push is ignored. Measured against the real helper: ry -3000 pushes to 8 m/s, while ry -9000
  stays at 0.0 m/s, matching the live log (about 0.1 m/s with the pad). Phase 1 never sent the right stick. `skate::Pop`
  now zeroes |ry| <= 8,689 (XInput's right-thumb deadzone). The `Skate: J=` log line now prints `buttons`, `ry` and
  `pop`. Regression in `worker-tests`: real `InputGate` with a resting stick, then push, then the board moves.
  Pushing is a tap on A (or W) for each kick. The pad has priority: keyboard W/S/A/D only apply with no pad connected.
- Overlay (hold J): `GROUNDED`/`AIRBORNE`/`NO CONTACT`, raw `ground`/`air` from the pose summary and the installed
  collision generation; the `Logs\Extensions.log` line `Skate: J=...` also prints `gather=<count>/<status>`.
  `WrathSkateGetPose()` returns `..., airborne, ground, generation` after the Phase 1 values.
- Tests: `collision-wire-tests` (prefix offsets, init/refresh rules, cap 1,769,516 bytes, `Keep`, generations);
  `worker-tests` drives the real worker against the fake child, which enforces the doc's epoch/map/origin/generation
  checks (init, two refreshes, local refusal, rejected refresh, failed build, op 7 + op 18 pop, no-preview fallback).
  Shrink steps are in `collision-wire-tests`. 27/27 pass.

Deployed: `G:\3.3.5a - Dev\WowSkatePhase2.exe` (`build/skate-live`, `WOWCLIENT_OUTPUT_OVERRIDE=WowSkatePhase2`, Retail
and ASECM3 on) and the Phase 2 debug helper at `G:\3.3.5a - Dev\Skate\wrath-skate-bridge.exe`; `skateHelperPath` in
`WTF\Config.wtf` now points there (backup in `_backups\skate-phase2-20261115-012054\`; the first Phase 2 exe is in
`_backups\skate-phase2-20261115-012526\`). `Wow.exe` and
`WowSkatePhase1.exe` are unchanged.

In-game check:

1. Start `G:\3.3.5a - Dev\WowSkatePhase2.exe` and enter the world in Stormwind.
2. Wait about 15 s (helper asset load). `Logs\Extensions.log` should show `Skate: collision init map 0, N of N triangles`.
3. Open terrain (e.g. Elwynn just outside the gate): hold J, release all controls to arm, push with pad A or W. The
   overlay at the top of the screen should read `GROUNDED | ground 1 ... | collision gen 1` and the generation should
   rise as the board travels. Ollie: pull the right stick down and flick it up (keyboard: hold Space, release).
   The overlay should go `AIRBORNE` and then back to `GROUNDED` on the terrain. Rolling off a ledge should do the same.
4. WMO floor: stand inside a Stormwind building (e.g. the Trade District bank) or on a canal bridge, log out and
   back in there (`/reload` does not restart the helper) so the session starts on the floor, then repeat step 3,
   ollie included. It passes if the board stays `GROUNDED` on the floor and lands back on it after an ollie, without
   falling through to the terrain below. `Logs\Extensions.log` shows which box size was used (`collision box`).
5. If the overlay says "waiting for world collision" for more than 30 s, send `Logs\SkateHelper.log` and the
   `Skate:` lines from `Logs\Extensions.log`.

### Phase 3: drive the player (movement seams)

Add explicit hook points in our movement code instead of detours, the same way the existing `UnitScript` seams
are wired. Proposed new `UnitScript` virtuals (or a dedicated `MovementScript`), each called only for the active
local mover:

| Seam | Placed in | Replaces |
|---|---|---|
| `OnFallStart(CMovementShared&, float& velocity)` | `CMovementShared::StartFalling` | `VerticalStartHook` |
| `OnFallDistance(const CMovementShared&, uint32_t t, float& distance)` / inverse | `RelDistanceFallen`, `CalcTimeFallen` | `VerticalDeltaHook`, `VerticalInverseHook` |
| `OnFallDown(CMovement_C&, ..., C3Vector& move)` | `CMovement_C::FallDown` | `AirRootSweepHook` |
| `OnFall(...)`, `OnFloorProbe(...)` | `CMovement_C::Fall`, `WillPassObstacle` | `AirRootFallHook`, `NativeFloorProbeHook` |

Then port `skate-ground-native.h`, `skate-vertical-native.h`, `rolling-native.h`, `ollie-native.h`,
`flight-handoff.h` onto them, swapping raw offsets for members (`m_flags`, `m_runSpeed`, `m_fallTimeMs`, …).
Steering stays as the original does it: `ToggleControlBit` + facing + a `RunSpeedLease`-guarded run speed. Never
restore over a newer server speed (the lease already enforces this).

Keep the Win32 build compiling; it is how struct layouts get checked against the original.

- **Done when:** pushing, coasting, braking, downhill momentum and ollies move the real character, other clients
  see it move normally, and J exits back to stock movement with speed restored.

#### Wired (2026-11-15, task t-6d006d04)

No WoW/Source change was needed: the existing `OnMoverStartFalling` seam is the only fall hook used. The approach is
the DLL's default path (`rolling-native.h`, `skate-ground-native.h`, `ollie-native.h` without the root-driven
vertical motor): steer WoW's own movement and let stock collision and packets do the rest. No position writes.

- `Extensions/Skate/src/SkateDrive.h` (pure, tested): board pose -> command. Speed and heading come from the board's
  horizontal velocity through the donor `skatemotion::Ground` + `Synchronize` (yards, capped by the server allowance
  and 14 yd/s, small catch-up along travel). Within 3 yd it catches up; beyond that it follows the board's velocity alone,
  and asks for a resync only past 20 yd XY or 10 yd Z (a re-init costs about 9 s with real assets). A pose whose
  tick has not changed for 250 ms (helper busy) stops the character. Fixed after the second live test: a 3 yd
  threshold fired as soon as the 7 yd/s character fell behind the board, and resyncs queued during the 9 s init
  looped forever, freezing the board. An ollie is one jump on the board's takeoff edge within 600 ms of a pop, with
  launch velocity `-(vy / 0.9144)` yd/s (stock -7.9555 below 1 yd/s, clamped to 20); stock gravity owns the arc, as
  the DLL's `skatevertical::Motor` did with `ArcGravity`. `Lease` tracks the server's run speed (the last value we did
  not write) so exit never restores over a newer server value.
- `Skate.cpp` `Drive` (from `OnUpdate`, main thread), only for the local player as the active mover, not in walk mode,
  swimming, flying, rooted, on a transport or a spline: `CMovementShared::ChangeRunSpeed(speed)`,
  `CGUnit_C::OnSetRawFacingLocal(now, heading)` when it differs by > 0.02 rad, and the forward control bit through
  `CGInputControl::ToggleControlBit` (released when the board stops, the pose is missing or a resync is pending).
  Ollie: `CGUnit_C::TryJumpOrAscend(now)`; `SkateUnit::OnMoverStartFalling` swaps in the launch velocity. Rolling off
  a ledge is a normal stock fall. A resync re-inits the helper session (op 48) at the player, which puts the board
  under it (at most every 3 s); the first J press after walking around does this, so expect a short pause.
- Packets: all movement goes through the stock event queue, so the client sends its usual
  `MSG_MOVE_START_FORWARD` / `SET_FACING` / `HEARTBEAT` / `JUMP` / `FALL_LAND` / `STOP`.
- **Speed allowance, GM only (Phase 6 replaces it):** `SpeedAllowance(bool)` in `Skate.cpp` is the single place. It
  does what the DLL's addon did: chat `.modify speed walk 2` on drive start and `.modify speed walk 1` on exit (on
  AzerothCore/Esteria `walk` sets the run rate, `cs_modify.cpp`: 2 = 14 yd/s). Skipped when another player is
  targeted (the command acts on the selected player); on exit it then prints a reminder. A non-GM account gets
  7 yd/s, and the command text is said in /say. Phase 6 swaps the body of this one function for the server opcode.
- Keyboard: while driving, W/S/A/D/Q/E/arrows/Space get override bindings to a no-op button (`WrathSkateKeys`) so the
  stock bindings do not also move, turn or jump (the skate code reads those keys itself). Not applied in combat.
- Exit (J released, focus lost, unsafe state): every move/strafe/turn control bit is released (also ones whose
  key-up went to an override), run speed restored to the server's value, `.modify speed walk 1`, overrides cleared
  (after combat if needed). A jump in flight just finishes on stock gravity, so there is no owned fall state to get
  stuck in. Logged as `Skate: drive on` / `Skate: drive off (<reason>), run speed restored to N`.
- Tests: `tests/drive-tests.cpp` (world mapping, heading in all directions, allowance cap, stop, resync, ollie edge,
  stale pop, ledge without pop, takeoff velocity, facing wrap, lease). Skate suite 28/28 pass.

Known limits / risks:
- Other clients extrapolate at the server run speed (14 yd/s after the GM command) between heartbeats (every 500 ms),
  so a slower board shows small forward overshoot and snap-back for them. Phase 6 can send the real speed.
- AzerothCore core has no speed check and Esteria runs no anticheat module, so no server rubber-banding is expected;
  an anticheat module would flag 14 yd/s without the GM rate.
- The character uses its own capsule against the same collision the board uses; where they disagree (a gap the board
  fits through), the resync resets the board to the character.
- The ollie height follows stock gravity, not the board, so the character lands before the board does on big pops.

In-game check (`G:\3.3.5a - Dev\WowSkatePhase3.exe`, GM account, no player targeted, controller connected):

1. Enter the world, wait about 15 s for `Skate: collision init` in `Logs\Extensions.log`.
2. Elwynn terrain: hold J, neutral controls, then push with A. The character must run forward with the board's
   speed (overlay `vel`), coast down when you stop pushing, slow with B, steer with the left stick, and stop at 0.
   Chat should show the `.modify speed walk 2` acknowledgement. Roll down a slope: speed rises without pushing.
3. Ollie (right stick down, flick up): the character jumps and lands on the terrain; the overlay goes AIRBORNE ->
   GROUNDED.
4. WMO floor (Stormwind bank or a canal bridge): repeat 2-3. The character and board must stay on the floor and land
   back on it, not drop to the terrain below.
5. Release J while moving and while in the air: the character stops (or lands) and walks normally with W/A/S/D at
   normal speed; `drive off ... restored to 7.00` (or the server's speed) in the log, no stuck falling, no snapping back.
6. Optional: a second client nearby should see the character run, turn and jump.

### Phase 4: rig pose (character rides the board)

`OnModelAnimated` fires after `AnimateST`, when attached children (weapons, cloaks, backpacks) are already
animated, so overriding bones there leaves attachments a frame behind. Add a per-bone override seam inside
`CM2Model::AnimateMT`, right where each bone's local rotation/translation is composed (the spot the adapter
reached by detouring `C44Matrix(quat)` and `C44Matrix::Translate`):

`ModelScript::OnBoneLocal(CM2Model&, uint32_t bone, C4Quaternion& rot, C3Vector& trans)`, gated so the dispatcher
costs nothing unless a model has opted in (a flag on `CM2Model` set by the skate extension for the local player
only; AnimateMT runs multithreaded, so the callback must only read a pose snapshot).

Port `character-pose-native.h`, `character-retarget.h`, `rig-binding.h`, `character-alignment.h`,
`character-foot-contact.h`, `character-height.h`, `character-ragdoll.h`. Check retargeting against our custom
and Retail races (30+ rigs, Retail-model races via `RetailRaceGeometry`), which the original never saw; non-humanoids
keep the original's fallback (stock animation, board/physics only).

- **Done when:** the skater pose, flip tricks and ragdoll show on a human, a Retail-model race and a custom race.

#### Wired (2026-11-15, task t-d4372afd)

No engine change was needed: it uses the existing `OnModelBoneLocal` seam and `m_boneHook`.

- **Helper (worker thread, `SkateHelper.cpp`):** steps now use op 22 (op 18 plus the right stick X, one 8-byte
  sample per step) instead of op 18. Its reply is the 60-byte pose followed by the skater frame: 24 Source body
  anchors and the board vertices, which the donor drew from with `skatevisual::Decode`. The frame decodes against op 7's
  mesh schema, which is now kept instead of thrown away. Op 20 then returns the source state at the same tick (300
  wipeout and 702 checkpoint are drawn as ragdoll, as in the donor). The pose, frame, mesh and state are published
  together under the worker lock (`Helper::Visual`). The game thread still does no pipe I/O.
- **Pose (main thread, `Skate.cpp` `UpdateRig`, called from `OnWorldUpdate`, before `CM2Scene::Animate`):** only
  while driving. The skeleton is read once per model from `M2Data` bones and `m_boneIndicesById`
  (`SkateRig.h` `Skeleton`). The donor's `characterrig::Discover` maps its 23 body roles by key bones and tree
  shape, never by race ID. The solve follows the donor's `RigPoseFrame`: `characterretarget::Solve` (riding) or
  `characterragdoll::Solve` (bail). The root offset comes from deck alignment with `characterheight`, the
  `rider-air-alignment` hips anchor in the air (flips) and `characterragdoll::Anchor` for the bail. `Fill` stores
  each body bone's local 3x3 plus its pivot, and the root offset goes on the root bone.
- **Bone hook (`SkateModel::OnModelBoneLocal`, either thread):** for `rigModel` only, `skaterig::Apply` overwrites
  the 3x3 and `d0..d2` of the 23 body bones. This is the donor's `CopyRotation` plus `RemoveAnimatedTranslation` plus
  the root offset. Every other bone (fingers, face, Retail helper bones) keeps its stock animation. Attachments
  (weapons, cloak, backpack) animate after their parent bones in the same `AnimateMT`, so they follow in the same
  frame. `m_boneHook` is set and cleared only in `UpdateRig` on the main thread. `OnModelDestroy` drops the pointer.
- **Threading: `M2UseThreads` = 1 kept.** The snapshot rule holds. The pose is written on the main thread before the
  scene animates, the M2 worker only runs inside `CM2Scene::Animate` while the main thread waits, and the hook only
  reads the snapshot and calls no engine code. Nothing forces 0.
- **Other skeletons, with fallback:** Retail-model races load as version 264 M2s with the standard key bones, so
  they map like Wrath rigs. Rigs over 256 bones (Retail Dracthyr) keep their first 256 bones whose parent is kept, and
  key ids of 128 and up are dropped. Each of these fallbacks leaves the model on stock animation and never writes
  bones: no humanoid key bones (creature/ASEC models), a body bone with billboard/ignore-parent flags, a body bone
  past the 256 cut, a rejected frame, or no preview mesh. The status line `Skate: rig <model>, N bones: ...` names
  the outcome per model, and `Skate: rig skater pose / skater pose (air) / ragdoll / ... (stock animation)` shows the
  per-frame result.
- **Flips:** pad right stick: pull down, then flick up and to the side (kickflip/heelflip). Keyboard: hold Q or E
  while releasing Space. Both use `Pop()`'s deadzone on X.
- Not ported (Phase 5 or later): foot IK (`character-foot-contact`), the displayed-ground fit, equipment
  compensation (`character-equipment-riders`) and on-foot (op 34/35). Overwriting the 3x3 drops body-shape
  procedural scale on the 23 body bones while skating, as the donor did by clearing flag 0x80.
- Tests: `tests/rig-tests.cpp` (synthetic humanoid: mapping, bind pose to identity, bent elbow, ragdoll root, bound
  rejection, collapsed frame, non-humanoid, flagged bone, over 256 bones and a cut-away body, malformed parent order,
  unmapped bones untouched). `worker-tests` now covers the op 22 frame, mesh and state handoff, the wipeout state and
  no frame without a preview (the fake bridge serves ops 7/20/22). Skate suite 29/29 pass.

Fix after the first live test (Retail body stayed on the run cycle, no rig line in the log). The client enables Storm's
ordered printf (`Client.cpp:1851`), which rejects `%.*s`, so the rig line came out empty. `UpdateRig` also returned
silently when `RigBind` failed. `RigBind` had failed because it applied the donor's Human-only rules. Now:
`Skeleton` hands Discover the flags the engine animates (0x280, so Retail limbs carrying 0x80 count). `Bind` poses
the 23 body bones plus every bone between them and the root, so animated Retail helpers cannot add the run cycle
back. Bones the engine does not animate (a Retail root) are skipped, and their children are posed relative to the
nearest animated ancestor, which also carries the yaw and root offset. Every early return now logs a reason. A rig
that is not mapped logs why (missing key bone, flags, tree) and writes its skeleton to `Logs\SkateRig.txt`.

Second live test: the user's Retail body has 216 bones and 291 key bones, and no Waist key (5). The pelvis is bone 2
(key 51) and forks the legs. `SkateRig.h` `Plan` now tries the donor's Discover first, which still covers Wrath rigs,
and falls back to `Structural`. That planner uses the head (6), root (26), arm/shoulder (0..3) and spine (4) keys as
hints. The chest is where the clavicles meet and the neck sits between chest and head. The pelvis is the bone whose
direct children split left and right and reach the feet. Each limb's next joint is the largest-subtree descendant at
least 3 cm away, looking through zero-length helpers and never a helper parked at the origin. Every plan must pass
`Check`: limbs on their side, legs going down. `Reduce` builds a 23-bone role skeleton that the unchanged donor
solvers run on, and `Bind` maps it back onto the model. `tests/fixtures/retail-rig.txt` is that live dump; it is
game-derived, so do not commit it. `rig-tests` asserts every role on it. The rig log line now names the model
file.

Third live test, freeze after an ollie: the board rolled downhill at 13 m/s while the character was held to 7 yd/s.
The lease stayed at 7.00 because the GM speed was not granted. Mid-ollie the gap passed 20 yd. The resync then
re-ran op 48, which blocked the helper for 9.7 s: the tick stayed at 727, the character was held, and the rig showed
the last air frame. Fixes:
(1) `Helper::Cap` sends op 11 with the run allowance in m/s, as the donor did, and re-sends it after every op 48.
The board can no longer outrun the character.
(2) `Driver::Step` never resyncs while airborne; it keeps flying on the board's velocity and resyncs after landing.
(3) `UpdateRig` drops to stock animation when the frame tick stops advancing for 250 ms.
Tests: `drive-tests` (airborne split keeps driving, resync on landing) and `worker-tests` (cap applied, and kept
across a re-init).

Fourth live test, no bail pose: `SkateHelper.log` shows real wipeouts after flips that landed badly (`KnownAir ->
WipeoutGround`, code 300, 150-230 ticks, then 702 -> 100). Op 20 reported them, but every bail frame was refused:
the donor's ragdoll root anchor rejects a Source body more than 4.5 yd from the character, and a tumbling bail always
gets that far. `RagdollRoot` now keeps the ragdoll on the character at the Source hip height in that case. Tipped
air frames, where the riding solver needs an upright chest, now pose through the full-3D ragdoll solver instead of
going stock. Every rejection logs a reason (`bail frame did not solve`, `air frame did not solve`, ...).
The helper has no op that forces a wipeout (`request_wipeout` is internal to skate-host physics). So `skateBail
[seconds]` (console, category game) and `/run WrathSkateBail(seconds)` draw the live frame through the bail path
while skating, logged as `Skate: rig ragdoll (skateBail)`. A real bail is logged as `ragdoll (bail)`.

In-game check (`G:\3.3.5a - Dev\WowSkatePhase4.exe`, same setup as Phase 3: GM account, no player targeted,
controller connected). The board itself is still invisible (Phase 5), so the character rides an invisible deck
slightly above the ground.

1. Human: enter the world and wait for `Skate: collision init`. Hold J and push. The character must switch from the
   run animation to a sideways skater stance (`Skate: rig skater pose` in `Logs\Extensions.log`, and earlier
   `Skate: rig <model>, N bones ...: skater pose on 23 body bones`). Pushing should show the kicking leg. Weapon,
   cloak and backpack stay on the body with no lag.
2. Flip: pull the right stick down, then flick it up and to the side (keyboard: hold Space, hold Q or E, release
   Space). The body should do the flip motion in the air (`skater pose (air)`) and land back in stance.
3. Ragdoll: bail by landing badly or hitting a wall at speed. The body should go limp (`Skate: rig ragdoll`) and
   return to the stance when the engine recovers.
4. Repeat 1-3 on a Retail-model race character and on a custom race. If a model logs `not a supported humanoid rig`,
   it keeps running stock, which is the fallback. Send that line and the model name.
5. WMO floor (Stormwind bank or a canal bridge), carried over from Phases 2-3: log in there and repeat 1-2. The pose
   must show and the character must stay on the floor.
6. Release J: the character returns to stock animation at once (`Skate: rig off`) and walks normally.

### Phase 5: board, camera, audio, peers

Plan (2026-11-14): the board as a world `CM2Model`, the donor's camera follow, XAudio2 audio, peers by addon message
first and a custom opcode later. What was built is below.

#### Wired (2026-11-15, task t-9586b726)

**Board: a real M2, the real Skate 3 board (not a placeholder).** Nothing is converted offline and nothing is
stored: the helper already streams everything the donor drew from. Op 7 returns the board's UVs, per-triangle material
and three RGBA textures (512 + 128 + 128), op 22 returns the board's 2,285 vertices every step (source metres, already
skinned). `SkateBoard.h` (pure, tested) turns those into a client-only model:

- `Build()`: M2 (MD20 v264, one rigid bone, one submesh and batch per texture), skin and three BLP2 (ARGB8888 with a
  full mip chain; Skate keeps gloss in alpha, so alpha is forced opaque) built in memory from the mesh and the first
  frame's vertices as the rest shape (local yards, `(dx, -dz, dy) / 0.9144` around the centre). Normals come from the
  triangles, flipped outward by the signed volume; the material is two-sided as a safety. The layout is the one
  `Tools/FortniteCosmetics/convert_backpacks.py` writes for the live cosmetics: the test output compares byte for byte
  with that writer except for the two-sided flag. Real helper data builds a 110,502-byte M2, 29,720-byte skin, extent
  0.27 x 0.99 x 0.16 yd.
- Served by name (`Skate\Board\board<N>.m2`, `board<N>00.skin`, `tex<N>_<i>.blp`) through the existing
  `VirtualAssets::reader` hook (SFile2-Core.cpp), installed in `OnClientInitialized` and chained to any earlier reader
  (`ReadAsset`, Skate.cpp:368). `N` increases per helper session, because the model cache keeps a model of the same
  name. So no Skate data is in git, an MPQ or a loose file; the converted assets stay where the user keeps them.
- Spawned the way Housing draws its ghost: `World::s_m2Scene->CreateModel` + `CMap::ObjectCreate`, moved every frame
  with `CMap::ObjectUpdate` (`UpdateBoard`, Skate.cpp:416, from `OnWorldUpdate` after `UpdateRig`). It is not a server
  object. It renders on Vulkan, DX11 and D3D9 with world lighting, and replaces the donor's D3D9 overlay.
- Hidden when not skating (the model is destroyed on J release, on `OnMapUnload`, `OnLogoutComplete` and
  `OnLeaveWorld`) and hidden while the helper's frame is stalled or the frame marks the board far away.
- `Place()`: a rigid (polar-decomposition) fit of the streamed vertices to the rest shape, composed with the donor's
  own `boardplacement::WorldPoint`, so the M2 lands where the donor drew each vertex, including full orientation
  (flips spin it, tilts and grinds tilt it). Fit choice follows the donor (`ChooseFit`): the grounded stance fit while
  riding, the root-relative fit in the air (so a kickflip spins around the rider, not the floor), the board's own free
  trajectory in a bail (`ragdoll (bail)` and `skateBail`), model scale from the player's world matrix
  (`modelboardsize`). With a rig that is not mapped, it is a plain board under the feet.
- Verified against a capture of the real helper (564 frames of push, steer, ollie, kickflip and heelflip; up to 180
  degrees of rotation): the worst residual of the rigid fit is 7.5 cm (trucks tilt, wheels spin), and the M2 matches the
  donor's vertex positions within 0.17 yd summed over axes. Frames above 20 cm hide the board and log it.
- Known limits: trucks and wheels do not tilt or spin separately (one rigid bone); the rest shape is the first frame
  (a board mid-tilt at that moment keeps that tilt); vertex colour tint is not used (the textures carry the look). Not
  seen in the game yet: if the board is invisible, `Logs\Extensions.log` has `Skate: board ...` lines (M2 built, model
  created, shown or the reason it is hidden).

**Camera.** `SkateWorld::OnCameraTargetFacing` (Skate.cpp:619) uses the donor's `camera-follow.h` `ViewYaw` through
`SkateCamera.h`: while driving, the view yaw eases toward the board's travel heading at 2.5/s above 0.5 yd/s, holds when
the board stops, yields to either mouse button (WoW-style mouse look, resumes from where you left it) and only touches
the local player's camera. `Drive` publishes the heading and speed (Skate.cpp:219). The stock camera returns the moment
the board stops being driven (J released, focus lost, drive refused); there can be one frame of snap back to the stock
yaw.

**Audio: not done.** The donor's 48 WAVs (`native-probe/audio`, from the installed Vanilla Skate sound bank) are not on
this machine: the path was `D:\world of skatecraft 3.3.5\native-probe\audio`, which does not exist, and neither the
repo nor the Skate staging folder holds any. The phase tracker (`skate-audio.h`) is already imported unchanged. To
finish: copy the WAVs to a folder, add a `skateAudioPath` CVar, port `skate-audio-native.h` (XAudio2, own thread) and
feed `Tracker::Observe` from the pose and `Visual::state` already available in `UpdateRig`.

**Peers: Phase 5 party/raid transport is implemented.** The donor's original `SendAddonMessage` route requires no
server opcode. The UI-ready bootstrap authenticates senders against the current group roster, and native code
renders private client-only M2 boards on those resolved group units. Full contract and two-client test:
[SKATE_GROUP_PEERS.md](SKATE_GROUP_PEERS.md). `SkatePeerWire.h` remains the 19-byte binary state:

| Offset | Type | Meaning |
|---:|---|---|
| 0 | u8 | version = 1 |
| 1 | u8 | flags: 1 active, 2 airborne, 4 free (bail tumble), 8 hidden |
| 2 | u16 | sequence (wraps; the receiver drops anything that is not `Newer(seq, last)`) |
| 4 | i16[3] | board centre minus the skater's position, world yards, steps of 1/512 yd (+-64 yd) |
| 10 | i16[4] | board rotation quaternion x y z w, scale 32767, sent with w >= 0 (`Placed::rotation` -> `Quaternion`) |
| 18 | u8 | ground speed, yd/s x 4 |

Carriage is now prefix `WRSP5` on PARTY/RAID at 10 Hz, with an off message and 1.5-second expiry. A fixed 70-character
hex envelope adds map, epoch, compatible-mesh checksum and actual displayed scale. A deterministic deck basis handles
different helper init headings. Viewers load their private op7/op9 mesh even with J off. Both clients must have
matching owned assets; messages carry no geometry or textures. A paired `WRRP5` packet now sends 23 solved semantic
role rotations and a bounded visual root offset for matching supported remote rigs, so the rider skates instead of
running on the board. Full 122-byte/244-hex contract, model-facing compensation, hook ownership and live retest are
in [SKATE_GROUP_PEERS.md](SKATE_GROUP_PEERS.md). The user confirmed board visibility; new proper-rider/off acceptance
remains pending. The later Phase 6 area-of-interest opcode is still separate for non-group nearby players.

Tests: `board-tests` (rigid fit for six rotations, M2 lands on the donor's `WorldPoint` for plain, stance, air and free
fits, orthonormal rotation, rejections, M2/skin/BLP layout and outward normals, fit choice, quaternion),
`board-real-tests` (runs on a captured helper stream when `SKATE_BOARD_CAPTURE` is set, otherwise skips),
`camera-tests`, `peer-wire-tests`. Skate suite 33/33 pass.

Deployed: `G:\3.3.5a - Dev\WowSkatePhase5.exe` (`build/skate-live`, `WOWCLIENT_OUTPUT_OVERRIDE=WowSkatePhase5`, Retail and
ASECM3 on, SHA-256 `f97fb955a8349279e0f7c1ad27ec7df71222239e03f36479d2fd9a17bc228a58`). No client data file was
replaced, so there is no backup; `Wow.exe` and the earlier Skate exes are unchanged. `WTF\Config.wtf` still points at
the Phase 2 helper.

Fix after the first live test (board invisible or solid green; the log showed the model created and shown). Green is the
engine's default texture (`s_defaultColor`, Texture.cpp), so the files were opened but not read: the committed
`VirtualAssets` branch of `SFile::OpenEx` (SFile2-Core.cpp) never set `m_position`, which `SFileImpl`'s constructor
leaves uninitialised, so reads started at `m_data + garbage` (0 bytes or junk for the M2, skin and BLPs). It now sets
`m_position = 0`, like the embedded and Retail branches. The generated files were already sound; `board-tests` now also
asserts the bounds contain the mesh, every file the M2 names (and the derived skin name) resolves through the registry
in any case and with either slash, and each BLP's header/levels/sizes decode as `CBLPFile` reads them. New diagnostics in
`Logs\Extensions.log`: `Skate: board file served|NOT FOUND <name> (<bytes>)` the first time the loaders ask for each
name; and CVar `skateBoardDump` (a folder, e.g. `SET skateBoardDump "C:\Temp\board"` in Config.wtf) writes the generated
M2/skin/BLPs there when they are built. The registry moved to `SkateAssets.h`.

In-game check (`WowSkatePhase5.exe`, same setup as Phase 3/4):

1. Enter the world, wait for `Skate: collision init`, hold J and push. A skateboard (deck, trucks, wheels, textured)
   must appear under the feet, follow the character, turn with the board, and stay under the character at speed.
   `Skate: board M2 built ...` and `Skate: board shown` are in `Logs\Extensions.log`.
2. Ollie and kickflip/heelflip: the board pops with the character, the flip spins the board under the rider, and it is
   under the feet again on landing.
3. Bail (land badly, or `/run WrathSkateBail(3)`): the board tumbles on its own trajectory (`board free (bail)`).
4. Camera: while pushing and turning, the view should lag the character and swing around smoothly rather than rigidly
   with the turn; hold either mouse button to look around, release to ease back; stop and the view holds.
5. Release J: the board vanishes at once and the stock camera is back.
6. Carried over: skate on a WMO floor (Stormwind bank or a canal bridge; log in there, repeat 1-2, the board and the
   character stay on the floor) and release J while moving and in the air (the character stops or lands and walks
   normally, the board is gone, `drive off ... restored to` is in the log).
7. If no board appears, or it is the wrong shape, dark or inside out, send the `Skate: board` lines and a screenshot.
8. Peers and audio: not testable (see above).

### Phase 6: server and addon cleanup

Normal Esteria players are not GMs, so the chat commands must go:

| Today | Replace with |
|---|---|
| `.modify speed walk N` | Server-side handler (custom opcode) that grants/revokes the skate speed tier while skate mode is on; client sends enter/exit |
| `.cast 90001/90002 triggered` + `skate-fall-protection.sql` | Same handler applies/removes the fall-immunity aura (keep spell 90001/90002 IDs only if free in our `spell_dbc`) |
| `.go xyz` return point | Server-validated return (same map, distance-limited) or drop F12 for non-GMs |

Check the server's movement checks accept the skate speed tier and the vertical curve (the stock-exe build ran on a
plain local AzerothCore with no anticheat module; Esteria's realm also has none per `MANASTORM_INTEGRATION.md`, but
verify speed validation in core).

Trim the addon: bindings, J/Y toggles, F10/F11/F12, score display. Everything that existed only to talk to the
injected DLL through globals (`WrathSkateWindowFocused`, polling) becomes registered functions/events. Ship it in the
Esteria interface MPQ or as a loose addon.

### Phase 7: later / optional

- Tram and lift riding (`ride-frame*.h`, `platform-observer-native.h`, `passenger-input.h`): live acceptance was
  still pending in the original, so port it last.
- Dynamic doors/objects collision (`dynamic-world*.h`).
- In-process engine (skate-host as `staticlib`).

## Engine seams

Added 2026-11-15 for Phases 2-5 (task t-65874f6c). Each is a no-op unless a script registers: the movement
dispatchers return before the loop unless a `UnitScript` exists and the movement is the active mover's, the bone
hook is behind a per-model flag, the camera hook is one empty loop. Stock behaviour is unchanged. All live in the
existing script types, so `Extensions/Skate` derives from `UnitScript` / `ModelScript` / `WorldScript` as usual;
the full comments are on the virtuals in `WoW/Source/Extension/ScriptDefines/*.h`.

### Movement (`UnitScript`, main thread)

Fire only for the active mover, `CGUnit_C::s_activeMover` (0xCA1238). That is the unit this client moves: a vehicle
or possessed unit while one is controlled, not always the player. `mover` is passed explicitly; for "local player
only" compare `mover.m_obj->m_guid` with `ClntObjMgrGetActivePlayer()`. `movement` is `mover.m_movementData`.
Every value passed by non-const reference holds the stock value and may be replaced. They are placed at the DLL's
gated call sites, and `MoverFallSite` names the caller instead of `_ReturnAddress()`:

| Site | Caller (12340 return address) |
|---|---|
| `MOVER_FALL_SITE_MOVE_FACETS` | `GetMoveFacets`, sizing the query box (0x760154) |
| `MOVER_FALL_SITE_FALL` | `Fall`, the real fall step (0x7618F6, 0x7619A9) |
| `MOVER_FALL_SITE_FLOOR_PROBE` | `WillPassObstacle`, the throwaway step-down probe (0x761A20, 0x761A66) |
| `MOVER_FALL_SITE_HOVER_MOVE` | `HoverMove` -> `Fall` (0x7629F5) |
| `MOVER_FALL_SITE_COLLIDE_REQUEST_MOVE` | `CollideRequestMove` -> `Fall` (0x763008) |

| Hook | Where | Replaces |
|---|---|---|
| `OnMoverStartFalling(CGUnit_C& mover, CMovement_C& movement, float& velocity)` | `CMovementShared::Jump`, before its `StartFalling` (0x98846D) only. `velocity` is the initial fall velocity, negative is up (-7.9555 land, -9.0967 swimming) | `VerticalStartHook` velocity override |
| `OnMoverFallDistance(mover, movement, MoverFallSite site, uint32_t time, float& distance)` | after `RelDistanceFallen(time)` at MOVE_FACETS, FALL, FLOOR_PROBE. `distance` is yards fallen after `time` ms of fall, positive down | `VerticalDeltaHook` |
| `OnMoverFallTime(mover, movement, float distance, int32_t upward, float& seconds)` | after `CalcTimeFallen` in `GetTimeJustFallen` (0x75EB78) | `VerticalInverseHook` |
| `OnMoverFall(mover, movement, site, uint32_t time, uint32_t elapsed, float& distance, C2Vector& direction)` | before `Fall` from HOVER_MOVE / COLLIDE_REQUEST_MOVE. Copies: a change affects only that call | `AirRootFallHook` |
| `OnMoverFallDown(mover, movement, site, time, elapsed, C3Vector& move)` | before `FallDown` from FALL / FLOOR_PROBE. `move` is the step's displacement | `AirRootSweepHook` (before) |
| `OnMoverFallDownDone(mover, movement, site, uint32_t consumed)` | after that `FallDown`. `consumed` = its return; position, flags and fall clock are committed (it may have reset `m_fallTimeMs`/`m_fallStartZ` on contact) | `AirRootSweepHook` (after, `SweepRestart`) |
| `OnMoverFloorProbe(mover, movement, const C2Vector& direction, const C3Vector& position, float height, int32_t& result)` | `AttemptStepUp` after `WillPassObstacle` (0x76207F). `result` nonzero = clears the obstacle | `NativeFloorProbeHook` |

Notes:
- The DLL's `nativefloorprobe::Scope` gate is not needed: the falls inside the probe arrive with site FLOOR_PROBE,
  and `RestoreMoveState` undoes them.
- `VerticalStartHook` also re-based its clock on every `StartFalling` caller. Only the Jump site has a seam. Watch
  `movement.m_fallTimeMs` dropping to 0 in the next `OnMoverFallDistance` to detect other restarts.
- `s_moveFacets` (0xADBA34, the "plane count" the DLL read at 0xADBA38) is still file-static in Movement.cpp. Not
  exposed because nothing in the seams needs it. Ask if the port does.

### Rig pose (`ModelScript`, either thread)

`OnModelBoneLocal(CM2Model& model, uint32_t boneIndex, C44Matrix& local)` fires in `CM2Model::AnimateMT` (0x82F0F0,
Engine/Source/Model2/M2Model.cpp, in the bone loop just before `local.Translate(-pivot)`), once per animated bone
(flags 0x280), parent first, only for a model whose `model.m_boneHook` is true (new member, default false). `local`
is the bone's transform about its pivot: rotation x scale x procedural in the 3x3, pivot + animated translation in
`d0..d2`. These are the values the DLL caught at `C44Matrix(C4Quaternion)` and `Translate`. Overwrite the 3x3 to
set the rotation, or `d0..d2` (keep `+ pivot`) to set the translation. The engine then un-pivots, multiplies by
the parent matrix and billboards. Attachments (weapons, cloaks, backpacks) animate from `AnimateAttachmentsMT` at
the end of the same `AnimateMT`, so they follow in the same frame.

Threading: **works with `M2UseThreads` = 1, no need to force 0.** `CM2Scene::Animate` splits root models between
the main thread and one M2 worker. The main thread waits in `WaitThread` until both are done, so nothing else runs
during the pass. The rules:
- Set or clear `m_boneHook` on the main thread, outside `CM2Scene::Animate`. Any other hook works, e.g.
  `OnModelLoaded` / `OnUnitDisplayInfoUpdated` for the local player's model.
- In the callback, read a pose snapshot that was written on the main thread before the scene animates, for example
  in `WorldScript::OnWorldUpdate`. Write only `local`. Call no engine functions.
- Only the opted-in model calls the hook, so for one player model it runs on one thread per frame.
- Bone flag 0x80 (procedural, used by body shape) is already folded into `local`. Overwriting the 3x3 drops it,
  which is what the DLL's clearing of 0x80 did.

### Camera (`WorldScript`, main thread)

`OnCameraTargetFacing(CGCamera& camera, CGObject_C& target, float& yaw, float& pitch, float& roll)` fires at the end
of `CGCamera::UpdateTargetFacing` (0x604490, WoW/Source/UIUtil/Camera.cpp), the same point the DLL's post-call
`CameraAnglesHook` edited. The angles are composed (target facing, yaw offset, ground tilt, pitch clamp) and stored
with `SetFacing`. They are the caller's locals, used right away for the basis, eye and collision sweep, so a change
applies this frame. Its three callers are `MakeRelativeTo`, `SetTelescope` and `CalcTargetCamera` (all of them),
so it fires several times per frame. Filter by `&camera == CGWorldFrame::s_currentWorldFrame->m_camera` and by
target, as the DLL did. It does not fire without a target.

### Ray hit GUID

`WOWGUID World::GetLastIntersectGUID()` (WoW/Source/WorldClient/World.h) returns `s_lastCollisionGUID` (0xCD7768),
which `CMap::Intersect` sets under `World::Intersect` (0x77F310). Read it right after an `Intersect` that hit. It
holds the WMO's or collidable doodad's object GUID, or 0 for terrain or an untagged model. It is reset only when
`flags` include a WMO/M2 bit (0x40F300FF), so with terrain-only flags it is stale. Main thread only.

### Collision triangles (Phase 2)

```cpp
uint32_t World::GatherCollisionTriangles(const CAaBox& box, C3Vector* out, uint32_t maxTris,
    uint64_t* outIds = nullptr, uint32_t flags = 0x80100111, uint32_t* status = nullptr);
```

Declared in WoW/Source/WorldClient/World.h:502, defined in World.cpp:4261 (task t-19d1ba05). It runs the client's own
`World::GetFacets(box, flags)` (0x783A40 -> `CMap::GetFacets` 0x7A5F20), the query `CMovement_C` collides with, and
copies the result out. No new spatial walk, so anything the client has loaded as ADT chunks, WMO groups or M2
doodads is covered: CoA maps, map 1820, Retail terrain/WMOs (RetailWmo converts to legacy groups with MOPY/MOBN) and
housing objects.

- **Coordinates:** world space, yards, WoW axes (x north, y west, z up), the same space as `box` and
  `World::Intersect`. Not origin-relative and not converted; the helper does `(p - origin)` then `(x, z, -y) * 0.9144`.
- **Output:** `out` gets 3 `C3Vector` per triangle (`maxTris * 3` entries). Winding is counter-clockwise about the
  facet's collision normal (`(v1 - v0) x (v2 - v0)` points to the open side). The function enforces this because
  terrain, WMO BSP and M2 facets do not agree on it.
- **Sources, by flag** (default = the local player's ground movement, `GetFacetQueryFlags` 0x100111 + 0x80000000):
  `0x100` terrain (holes skipped, impassable chunks add their walls), `0x10` WMO group collision (the MOBN BSP faces
  `CMapObjGroup::GetTris(CAaBox)` returns, with the same face-ignore filter stock movement uses, so polys movement
  passes through are not included), `0x1` untagged M2 doodads,
  `0x100000` tagged doodads and gameobjects (through `World::s_objectCollisionCallback`, i.e. doors/transports as
  placed now). `0x200` flight bounds, `0x10000`/`0x20000` liquid surfaces and `0x4000` walkable-only (n.z >= 0.643)
  also work as in stock.
- **Placeholders:** with 0x80000000, ADT cells, WMOs and WMO groups that are not loaded yet come back as their AABB
  (12 triangles, id 0), and an M2 doodad whose collision is not loaded comes back as its bounding box, exactly as
  stock movement sees them. Without the bit, the query stops at the first unloaded piece (partial set, INCOMPLETE).
- **`outIds`** (optional, `maxTris` entries): the object GUID per triangle: the WMO's or tagged doodad/gameobject's
  GUID, 0 for terrain, untagged doodads, liquid and placeholders. There is no separate source enum; terrain and
  untagged doodads both read 0.
- **Return / `status`:** the triangle count written. `COLLISION_GATHER_TRUNCATED` (1): more than `maxTris` existed,
  the first `maxTris` are returned (order: WMOs with their doodads first, then per ADT chunk terrain, then its
  doodads). `COLLISION_GATHER_INCOMPLETE` (2): `GetFacets` returned false (box outside the map grid, a destructible
  proxy (siege damage) overlaps it, or an unloaded piece without 0x80000000); the triangles found so far are still
  returned.
- **Cost / threading:** one stock facet query; triangles are clipped by outcode to `box`, not cut, so they can stick out
  of it. A function-static facet buffer is reused, so after warm-up there is no allocation beyond the caller's arrays.
  Main thread only (CMap query tags and the M2 collision scratch arrays are global), so call it from
  `WorldScript::OnWorldUpdate` and hand the copy to the pipe worker.
- **Self-check:** console command `collisionTris [half]` (debug category, World_Console.cpp) gathers a
  `2*half x 2*half x 45` yd box (default half 12, the helper's 24x24x45) around `World::s_currentWorldPos` and prints
  the count, how many carry a GUID, status, and how many triangles miss the box in XY (must be 0; the command fails
  otherwise). It needs a loaded world, so there is no offline test.

## Testing

- Bring every `*-tests.cpp` for copied headers into a test target; they are engine-independent.
- Keep the helper's own `cargo test` and `test-bridge.py`.
- Per phase, the "Done when" line is a live check in the dev client
  ([deploy-to-dev-client](../README.md#running)); offline tests alone never count as acceptance, the same rule the
  original project used.

## Risks

- **Assets and licensing.** Skate 3 data and the benilla/skate-host code have their own licences
  (`skatecraft-wrath/NOTICE`, MIT/Apache). Nothing from Skate 3 may be committed or packed into a shipped MPQ; the
  helper and converted assets stay user-supplied. Get the licensing review the original README says is still owed
  before distributing the helper binary.
- **Retargeting** across our extra races is untested territory and the most likely place for visual bugs.
- **AnimateMT is multithreaded.** The bone seam must read an immutable per-frame snapshot.
- **Speed allowance** is a server change; without Phase 6 nothing works for non-GM players.
- **Controller required.** The original is pad-first; keyboard is a fallback, not parity.

## Client-owned helper wire contract (Phase 2, Codex)

The additive WSB1 operations 48/49/50, payload layout, limits, coordinate conversion, generation/failure semantics and required op48 transport deadline are specified in [SKATE_HELPER_PROTOCOL.md](SKATE_HELPER_PROTOCOL.md). Client gather implementation details above remain authoritative.

