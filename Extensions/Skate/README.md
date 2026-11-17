# Skate extension

Normal auto-discovered extension; disable with `WOWDECOMP_EXTENSION_SKATE=OFF`.
The isolated helper supplies physics and poses. Client collision, movement,
rig, board, camera, group peers and optional audio adapters are integrated; server authority and a nearby non-group relay remain separate.

Set `skateHelperPath` to the user-supplied x64 `wrath-skate-bridge.exe` and
`skateAssetPath` to the user's converted asset directory containing `rig.json`.
Set these CVars before entering the world; re-enter after changing them. Relative
paths resolve from the client working directory. Nothing is downloaded or shipped.
The helper runs in its executable directory; stderr goes to `Logs/SkateHelper-<PID>-<instance>.log` (the filename is logged on world entry).

Hold J, release all controls to arm, then use pad A/B and left stick, or keyboard
W/S and A/D (arrow keys also work). Ollie: right stick down then up, or hold/release Space. Y toggles simulation mode. Releasing J or
losing window focus suspends helper input. Resume requires neutral controls.
The on-screen text reports grounded state, engine position and velocity on the client's own
collision (Phase 2: ops 48/49/50, see `docs/SKATE_INTEGRATION.md`); the board waits until that world is installed.
Phase 3: while J is held the board drives the local player through stock movement calls
(`SkateDrive.h`, `Drive` in `Skate.cpp`); the 14 yd/s run allowance uses the GM command
`.modify speed walk 2/1` until Phase 6 (see `docs/SKATE_INTEGRATION.md`).
Phase 5: the board is drawn as a real client-only M2 (`SkateBoard.h`), generated in memory at run time from the
helper's own op 7 mesh and first frame and served through `VirtualAssets`; no Skate data is stored in git, an MPQ or a
loose file. The camera follows the board's heading through `WorldScript::OnCameraTargetFacing` (`SkateCamera.h`).
`SkatePeerWire.h` is carried by the Phase 5 PARTY/RAID addon-message transport (`SkatePeers.h`). Audio uses the optional
`skateAudioPath` user-owned WAV bank described below. See `docs/SKATE_INTEGRATION.md`.
No hooks, client offsets or proprietary geometry are used by the adapter.

`WrathSkateIsLocalPlayer("player")`, `WrathSkateWindowFocused()`,
`WrathSkatePadConnected()` and `WrathSkateGetPose()` are registered Lua functions.
The pose function returns valid, failed, position xyz, velocity xyz.
`WRATH_SKATE_FOCUS_CHANGED` and `WRATH_SKATE_PAD_CHANGED` carry a 0/1 state;
`WRATH_SKATE_DEBUG` carries whether J is held while focused.

The worker owns all pipe reads/writes and only publishes the latest snapshot.
Its job object and watchdog bound startup/request hangs; serial and epoch checks
reject stale replies. Leave, client destruction and shutdown cancel the epoch,
kill the owned child and join the worker. The game thread may wait briefly during
teardown, but never reads/writes helper pipes.

## Imported donor files

Source: `wotlkskate` checkpoint `afa45f0`. `donor-sha256.json` covers exactly
63 verbatim headers and 41 test sources. The Phase 0 guide claims 42 clean tests,
but only 41 test sources have a transitive include closure within its 63 headers.
No donor source or proprietary fixtures were modified or copied.

The original `SkateLogic/skate-pipe.h` remains byte-identical; runtime uses the
separate `SkateTransport.h` adapter with a configurable working directory.
`prepare_transport.py` reproduces that adapter. Imported headers that mention
generated fixtures, stock-client paths or game constants are retained for later
porting and are not used by the Phase 1 runtime. In particular, the mutable
`ground-tuning.h` static remains unchanged and is not shared by runtime TUs.

Verify without rewriting files:

```powershell
rtk proxy python Extensions/Skate/import_donor.py --verify
```

Reimport only when deliberately restoring this checkpoint:

```powershell
rtk proxy python Extensions/Skate/import_donor.py R:/Users/Zach/Documents/GitHub/wotlkskate
```

## Verification

```powershell
rtk cmake -S . -B build/skate-phase1 -G "Visual Studio 17 2022" -A x64 -DWOWDECOMP_SKATE_TESTS=ON -DWOWDECOMP_ENABLE_ASEC_M3=OFF -DWOWDECOMP_EXTENSION_ECLIPSE=OFF
rtk cmake --build build/skate-phase1 --config Release --target SkateTests --parallel 6
rtk proxy ctest --test-dir build/skate-phase1/Extensions -C Release --output-on-failure
rtk cmake --build build/skate-phase1 --config Release --target WowExtensions --parallel 6 -- /p:BuildProjectReferences=false
```

2026-11-15: all 25 configured tests pass (23 unchanged donor tests and two new
input/worker tests), with assertions enabled in Release. The fake child verifies
init/step/suspend, stale epoch/serial, bad magic, oversized and truncated replies,
and cancellation of blocked startup/request. It does not emulate Skate physics.
`WowExtensions.lib` compiles with MSVC x64. A normal dependency build is blocked
by pre-existing duplicate definitions in `ASECM3/client/Disabled.cpp`.

The explicitly excluded, unchanged donor tests are:

* `air-root-engine`, `air-sync`, `authored-collision`, `engine-readiness`,
  `ground-movement`, `motion-feedback`, `natural-momentum`, `onfoot-terrain`,
  `passenger-input`, `pose-continuation`, `shadow`, `steering-strength`,
  `vanilla-reference`: original helper/assets at hard-coded D: paths; several also
  assert x86 pointer width.
* `bail-entry`, `bail-recovery`: original captured trajectory/pose recordings.
* `air-board-validation`, `wall-jump`: original captured body/board binary fixtures.
* `flight-handoff` on x64: deliberately casts incompatible x86 calling conventions.

All names above have the suffix `-tests.cpp`. They remain hash-preserved; changing
them to pass or importing game-derived recordings is outside this phase.

Helper tests were run with:

```powershell
rtk cargo test --manifest-path R:/Users/Zach/Documents/GitHub/wotlkskate/native-probe/skate-bridge/Cargo.toml --target-dir R:/Users/Zach/Documents/GitHub/wowdecomp/build/skate-phase1/helper
```

Result: 62 passed, 3 failed, 12 ignored. The three failures require the absent
hard-coded `common.MPQ` donor installation. No Rust sources were changed.
The same command with `build` instead of `test` builds the local development helper
at `build/skate-phase1/helper/debug/wrath-skate-bridge.exe`.

### Owned-disc asset verification (2026-11-15)

The user-authorized ISO was extracted locally with XboxDev `extract-xiso`
revision `3f5b62cfe68f000b0e3c8a30104973f3a297948e`, built from reviewed source.
All 103 extracted file sizes match the game-partition listing, totaling
6,404,940,920 bytes. The source ISO remains 7,838,695,424 bytes with its original
last-write time. Extracted `default.xex` SHA-256:
`1db39496585c521d17a2137804f42cf73ebed2b32cac166ec42dbf772f4dcf7f`.

Only the asset conversion procedure was used: engine revision
`cb7968930f14dad38457e98720d1a274e469eec2`, converter revision
`adb684e78ec07654f00e2a9da88d32ae02cc68bd`, and its locally built RefPack decoder.
The converter successfully verified/extracted the default skater and wrote the
animation banks, graphs, physics collections and skater GLB. It does not export
root-level `rig.json`/`board.json`, so the first helper smoke correctly failed.
Running the unchanged `crates/assets/src/skate_board.rs` exporter from the
helper's pinned upstream `099b1948e9c08394b20c760ef123da38ef347278` completed that
step: 35 rig joints, three board surfaces/textures, 2,138 converted files.

The actual helper's `--smoke` now passes: 600 steps, 9.9999 simulated seconds,
38.0001 metres pushed, maximum horizontal deck speed 8.50189 m/s, coordinate
round-trip passed. A separate real WSB1 session passed flat-triangle init,
neutral/push/steer/brake input, suspension, stale-epoch rejection and valid-epoch
recovery. Its init took about 12 seconds, inside the adapter's 20-second deadline.
These checks used real converted assets and the real physics engine.

Private assets and verification artifacts are staged outside Git at
`G:/Downloads/HDWoWModels/Skate 3/phase1-assets-20261115/`:
`converted/assets/`, `provenance.json`, `asset-verification.json`, `smoke.json`,
`wsb1-result.json`, and the local verification scripts/logs. Extracted disc files
are in the adjacent new `Skate 3/` directory. Nothing was mounted or executed from
the console executable; no game data was committed or packed into an MPQ.

Phase 1 live acceptance passed on 2026-11-15: the user confirmed that holding J
shows position/velocity values that change with controller input. The dedicated
Dev client was built with Retail and ASECM3 enabled and deployed as
`WowSkatePhase1.exe`; the existing client executable was preserved. An initial
overlay failure was fixed by waiting for FrameXML and its event table before
creating the display. A regression test executes the actual Lua bootstrap.
Real-world collision, player movement, rig/board rendering, and a release helper
build remain unverified; they are outside this Phase 1 acceptance.
Helper redistribution still requires the separate human licensing review.

### Phase 5 body / ollie takeover (2026-11-15)

Completed the saved body-contact work and corrected the Retail parallel-joint
binding: a helper at the same pivot and parent role as a knee/elbow gets that
joint's solved rotation. Limb helpers follow the pose; face/fingers, unrelated
root branches, ignore-parent branches and origin-only attachment helpers stay
stock. Ground contact uses the donor support-foot solver and air frames use its
hip/leg correction with the grounded deck calibration.

The ollie trajectory starts only when the stock jump accepts it. Its samples use
the mover's fall clock. Fall queries receive the remaining step displacement,
including `currentZ - fallStartZ`, and the swept collision gets a matching inverse
contact time. Stock floor probes are unchanged. Missing helper samples extrapolate
under stock gravity; they do not freeze the character. Exit, focus loss, bail,
map change and stock contact resets clear the arc.

Verification: `SkateTests` builds and **34/34** tests pass with assertions enabled,
including the two real-capture tests. The new `rig-real-tests` uses
`SKATE_OLLIE_CAPTURE` (mesh.bin and length/frame/state records); the existing
`board-real-tests` uses `SKATE_BOARD_CAPTURE` (length/frame records). Captures remain
private; conversion of framing only is in ignored
`build/skate-phase5-takeover/adapt_capture.py` and its `board-capture/` output.
The 604-frame real helper recording includes 516 planted frames and 88 airborne
leg solves, with no pose refusals. Maximum mathematical sole residual is 0.000001
yard, air ankle target residual 0.017910 yard, and composed native-joint palette
error 0.000005 yard. These are offline math checks, not visual game acceptance.
The synthetic tests also check parallel joints, preserved fallback branches,
incremental fall displacement, contact inverse, invalid samples, extrapolation
and sample-buffer rollover.

`WowExtensions.lib` compiles with MSVC x64. No client/helper executable was
replaced in this batch. Ankles, shins, deck clipping, ollie height and release on
real terrain/WMO floors still need the user's live retest. Camera/audio/peers
were not changed by this body batch.

### Phase 5 camera adapter (2026-11-15)

The follow callback now applies the donor's native yaw/basis synchronization:
`UpdateYaw(desired - composed)` persists the offset in native yaw/smoothing state,
then `SetFacing(desired, pitch, roll)` updates the basis that the camera boom and
collision sweep consume. Previously only the returned yaw changed, while
`Forward()` still described the stock basis. No engine source change was needed.
The original donor easing law remains unchanged. Mouse look yields without native
camera writes, repeated calls at the same timestamp do not ease again, and J
release keeps the last displayed native offset instead of restoring an old one.
Camera follow also refuses a collision session belonging to another map.

The actual adapter is exercised against simulated native offset/smoothing state
and a separately consumed view basis in `camera-adapter-tests`, including body
turns, repeated composition calls, stopped motion, mouse drag/release, J release,
stale-state handoff, angle wrapping and clock wraparound. Both focused camera
tests and the complete **35/35** suite pass, including the private body/board
captures. `WowExtensions.lib` compiles. Camera smoothness and release handoff
remain pending live verification; this batch did not deploy a client.

### Phase 5 audio adapter and deferred-jump correction (2026-11-15)

Set `skateAudioPath` to the user's exported 48-WAV bank before entering the world;
empty disables audio. `SkateAudio.h` reuses the donor's Wave/Output and unchanged
`SkateLogic/skate-audio.h` Tracker policy: rolling, wind, board/truck grinds,
pop/flip, land/hard-land, bail and skid sounds. Unknown support uses the donor's
concrete fallback. There are no extra helper requests or collision queries.

An owned worker performs all file reads, COM, XAudio2 device/voice creation and
playback. Its 64-event queue retains short cues without unbounded growth; duplicate
ticks and new epochs cannot replay old events. Muting and suspension cancel queued
cues, including events captured before a slow device open. J exit, focus loss,
map change and stale source frames silence audio; leave/shutdown stop and join the
worker. Gain honors `Sound_EnableAllSound`, `Sound_EnableSFX`, master times SFX
volume and the background-sound CVar (skating itself still suspends without focus).
Bank/device failures log once per state change on the game thread and do not feed
back into movement. Retry a changed/missing bank by re-entering the world.

The parser validates complete RIFF/chunk bounds, PCM16 mono format/rates and clears
state on malformed reuse. One-shot pool reuse stops an idle voice before changing
its sample rate. `audio-tests` covers policy, gain/mute, malformed chunks, missing
banks, epoch/duplicate handling, queue capacity and repeated stop. With
`SKATE_AUDIO_BANK` set it validates all 48 real WAVs. Its explicit `--device-smoke`
also passed actual XAudio2 initialization and 32 reused one-shot submissions with
the mastering voice held at **zero gain throughout**. This verifies device/buffer
lifecycle, not audibility.

The parent review caught a deferred movement detail: `TryJumpOrAscend` queues a
jump rather than accepting it synchronously. `PendingJump` now retains the request
until the later `OnMoverStartFalling` callback, consumes once, expires after 500 ms,
and is cleared on reset. Its deferred acceptance/refusal/expiry tests pass.

Final verification: **36/36** tests pass, including both real pose/board captures
and the 48-WAV bank. `WowExtensions.lib` compiles. The tests ran with:

```powershell
$env:SKATE_AUDIO_BANK = 'G:/Downloads/HDWoWModels/Skate 3/phase1-assets-20261115/audio'
$env:SKATE_OLLIE_CAPTURE = 'C:/Users/Zach/AppData/Local/Temp/claude/17eb29d68b778558/d01caaae-025a-480d-9b34-3c0c4ca984b4/scratchpad/rec'
$env:SKATE_BOARD_CAPTURE = 'R:/Users/Zach/Documents/GitHub/wowdecomp/build/skate-phase5-takeover/board-capture'
rtk cmake --build build/skate-phase1 --config Release --target SkateTests --parallel 6
rtk proxy ctest --test-dir build/skate-phase1/Extensions -C Release --output-on-failure
rtk proxy build/skate-phase1/bin/Release/skate-audio-tests.exe --device-smoke
rtk cmake --build build/skate-phase1 --config Release --target WowExtensions --parallel 6 -- /p:BuildProjectReferences=false
```

The private bank is outside Git; no WAV data is committed. These body/camera/audio
batches have not deployed a client. Live sound balance/cues, mute/focus/exit behavior,
body contact, ollie height, camera smoothness and WMO floors still need retesting.

### Second live feedback: Retail ankle skinning (2026-11-15)

The user confirmed the higher ollie, but still saw shin/ankle bending and rough
animation. The previous joint-position tests did not cover the actual mesh's
skinning influences. The cached player model was `retail/1011653.m2`, with SKEL
2138400. Its co-located ankle wrapper has rotation keys in only two sequences;
the actual foot-rotation child has keys in 399. The wrapper influences thousands
of lower-shin vertices, so assigning the foot's rotation there bends the shin.

The structural planner now selects the co-located direct child with greater
rotation-track coverage for the ankle role. The passive wrapper retains the
shin's palette; binding also refuses to copy its descendant foot's rotation back
onto that wrapper. Selection uses loaded track metadata, not model/race/bone IDs.
Ordinary donor mapping and synthetic/default metadata retain their previous path.

The private skin fixture contains 27,365 real lower-leg vertices; 13,588 have
wrapper influences. A controlled 90-degree foot-flex regression measures the
old incorrect wrapper contribution moving shin vertices by up to 0.495386 yard;
the corrected wrapper ownership removes that contribution (0.000000 yard).
This is an isolated deformation test, not a claim that every live frame had that
error. Native parallel-shin corrective tracks also differ; no speculative blend
ratio was introduced, and their remaining appearance still needs live review.

Enable the additional real-skin check with
`SKATE_RETAIL_SKIN=build/skate-phase5-feedback2/retail-leg-skin.bin`. The private
fixture was extracted read-only from the existing decoded cache by the ignored
`build/skate-phase5-feedback2/export_skin_fixture.py`; neither mesh nor animation
data is committed. All 604 recorded helper frames still solve without refusal.
The helper cadence/render stutter is a separate pending timing batch; this ankle
batch changes no helper pacing, camera, board placement, audio or ollie height.

### Helper cadence correction (2026-11-15)

Live logs showed only 46–48 helper ticks per wall second even though its period
was 1/60 s. The worker slept 16 ms **plus** step/state pipe and decode work on
every iteration. That drift let the normally moving character separate from the
slower-simulated board and eventually trigger a multi-second collision re-init.

The worker now waits on an absolute `steady_clock` deadline from the decoded
helper period, including all work inside that period. One native tick and one
matching op22 input sample are still sent per step. Init/preview and inactive
periods reset the deadline. An early collision wake services the snapshot without
advancing physics early; a large overrun drops backlog instead of an unbounded
catch-up burst. No physics speeds, camera law or presentation interpolation were
changed.

`timing-tests` runs the actual worker against a child that delays each step and
state reply by 3 ms, with the game's 1 ms timer resolution. The old worker measured
39.496 Hz; the corrected worker measured **59.996 Hz** (120 ticks in 2.000133 s).
The same worker against the real asset-backed helper measured **59.989 Hz** (120
ticks in 2.000366 s). Suspension stops ticks, resumption does not replay paused
time, and stop/cancellation remains bounded. The complete **37/37** suite passes
with actual skin, pose/board captures and WAV bank enabled; `WowExtensions.lib`
compiles.

Real-helper timing can be repeated with `SKATE_TIMING_HELPER` pointing to the
isolated helper executable and `SKATE_TIMING_ASSETS` to the owned converted tree,
then running `skate-timing-tests.exe <bridge-fixture-exe>` (the fixture argument is
unused when the real helper variables are set). No running client/helper was
replaced. Live camera/animation smoothness and remaining native shin correctives
still require a retest; stable offline cadence alone is not visual acceptance.

### Phase 5 group peers (2026-11-15)

The original donor-style party/raid transport is implemented without a server
change or separate addon install. Both users run the same client with matching
private assets/helper configuration; the viewer can leave J off. One op9/op20
preview read after init bootstraps the viewer's private board M2 without advancing
simulation. Packets carry no model/texture data. Matching supported rider rigs
also receive the paired semantic pose described below.

`SkatePeers.h` sends the existing 19-byte state as a fixed 70-character hex
envelope with map, epoch, compatible-mesh checksum and actual scale. A deck-local
geometric basis makes different helper/rest headings produce the same world
board. Lua authenticates the server-supplied sender name against the current
party/raid roster. Native code independently restricts and resolves group unit
tokens; packet data cannot nominate an arbitrary GUID or file. Packets are
throttled to 10 Hz, stale/retired sequences/epochs are rejected, and boards expire
after 1.5 seconds. At most 39 records/eight nearby boards exist. Group, object,
map, Lua and client lifecycle changes clean them up.

Helpers now use independent PID/instance stderr logs, permitting two instances
of the same client directory. No duplicate viewer folder is needed. Native peer
accept/create diagnostics include the client PID. The full contract and manual
two-client gate are in [SKATE_GROUP_PEERS.md](../../docs/SKATE_GROUP_PEERS.md).

Tests execute the actual embedded Lua transport with mocked game functions
(no live account/message use), validate envelopes/leases, reconstruct matching
world geometry from differently oriented rest meshes, and bootstrap viewers
with J off. Worker tests start two simultaneous helpers in one working folder.
The complete **38/38** suite passes with real skin/pose/board captures and the WAV
bank enabled, and `WowExtensions.lib` compiles. The real helper's idle viewer
bootstrap produced a valid 2,285-vertex M2 with three textures and a valid deck
basis, while subsequent active cadence remained 59.992 Hz. Actual two-client
board visibility remains a live gate. Party/raid-only visibility follows the
original Phase 5 scope; Phase 6 still owns the later nearby non-group relay.

### Remote rider pose (2026-11-16)

The user verified remote board visibility but saw the character running on it.
`SkatePeerPose.h` adds prefix `WRRP5`, exactly 122 bytes/244 hex characters, paired
with the existing board packet's epoch/map/mesh/sequence. It carries 23 solved
semantic role quaternions, explicit same-rig fingerprint, full model rotation and
bounded world visual-root offset. The receiver poses its own loaded copy of the
remote avatar through existing `Plan/Bind/Fill`, with model-facing/tilt/root-origin
compensation. Avatar movement and engine ownership stay unchanged.

Board and rider advance as a matched pair; missing/reordered packets cannot mix
states. Off/stale/group/map/object/model/Lua cleanup releases the published pose
and restores prior bone-hook ownership. The MT callback reads a fixed eight-entry
immutable view list; it does no engine work. Unsupported/mismatched rigs retain
stock animation and log the reason. No body-only interpolation was added that
could drag feet behind the deck.

Full **39/39** tests pass with private assets, including actual Lua pairing/auth
and quaternion/lease/hook guards. Across all 604 recorded frames and three viewer
headings with different origins/tilt, remote world joint/skin/foot-deck errors are
at most 0.003234/0.002515/0.003157 yard. `WowExtensions.lib` compiles. Contract and
manual test: [SKATE_GROUP_PEERS.md](../../docs/SKATE_GROUP_PEERS.md).
Proper remote rider animation and release-J cleanup still require live acceptance;
no client was deployed by this source batch.

### Remote rider quality follow-up (2026-11-16)

The user confirmed proper remote animations but reported strong jitter. Code
inspection found that the world-update pose used the receiver's previous model
matrix, then native `Animate/UpdateWorldTransform` changed that matrix before the
bone hook used it. The existing `OnUnitWorldTransformUpdated` callback now rebuilds
remote pose locals against the final current matrix. No engine or movement code
was modified.

A shared 100 ms presentation transition handles the 10 Hz paired board/body
snapshots. Both use the same phase and shortest-arc rotations; jittered arrivals
start from the displayed pair, losses hold without extrapolating, and off/TTL or
identity changes clear/reset as before. It does not move the remote actor or alter
physics, server positions, local input or the accepted local skater presentation.

Real-fixture tests reproduce 0.432516-yard stale-matrix error and reduce it to the
0.003234-yard codec bound after final-transform preparation. With 10 Hz jittered
and dropped source pairs, 625 display frames show maximum ankle/deck steps reduced
from 0.450491/0.259450 to 0.090928/0.041512 yard. All 39 tests pass with private
skin/pose/board/audio fixtures; `WowExtensions.lib` compiles. Live server-position
rubberbanding was not directly measured, and remote smoothness/off cleanup still
need retesting. No client was deployed by this source batch.

### Remote rubberband diagnostics (2026-11-16)

The user accepted remote pose smoothness and J-release stock-animation cleanup,
but still reports whole-character rubberbanding. Source review found a plausible
ground-speed prediction mismatch: local `ChangeRunSpeed(c.speed)` updates only
the local mover; ordinary ground movement packets contain no run-speed value.
Remote run speed comes from server speed messages, while positional heartbeats
can correct a different prediction. This is source evidence, not yet a measured
live diagnosis. No speed, position, packet frequency or presentation behavior was
changed in the diagnostic batch.

`skatePeerTrace` defaults to 0. Set it to 1 before the diagnostic run in both
instances. The first local skating or paired-peer activity starts
`Logs/SkatePeerTrace-<PID>.csv`, capped at 60 seconds/10,000 rows, 20 Hz per unit and
stage, and eight remote peers. A new world entry permits one new capture; changing
the CVar to 0 or leaving/closing flushes and closes it. No auth/chat content or
packet payloads are logged. Main-thread buffered CSV writes are diagnostic-only;
the MT bone callback does no file work.

CSV stages: 0 local frame, 1 peer frame, 2 accepted board message, 3 local outgoing
movement event, 4 final native model-transform callback. Monotonic `ms`, PID and
unit GUID correlate the two processes. Columns include actor/movement/model
positions, model basis, run/current speed and flags, peer epoch/sequence/age,
board/pose root offsets and reported speed, local allowance, movement event count,
last opcode and timestamp. The event hook observes only metadata and never reads
or changes the packet cursor.

Test plan: both clients grouped/nearby, one skates for 10–15 seconds with a few
pushes followed by coasting while the other watches; then close normally or set
the trace CVar to 0. Preserve both PID files and restore `skatePeerTrace` to 0.
The trace will distinguish native actor corrections from model/pose offsets
before any behavioral fix or server work is proposed. The focused logger test
validates all 40 CSV columns, per-series rate, duration/row/key caps, one-capture
ownership, repeated close and disabled writes.

### Measured remote prediction correction (2026-11-16)

The paired PID traces confirmed the mismatch: the sender varied its applied
ground run speed while the viewer retained 7 yd/s. The remote native actor and
model XY positions matched, separating this from the already-corrected pose
offset jitter. Private captures and analysis are preserved under
`build/skate-phase5-rubberband/`; tracing was disabled after collection.

The existing peer speed byte now reports the sender's actual applied mover run
speed. A fresh, authenticated ground peer temporarily leases that speed to the
viewer's native extrapolator, clamped to its latest server-provided allowance.
The 0.125 yd/s quantization tolerance never permits exceeding that allowance.
Local/active movers, walking/backward/air/swim/fly/transport/root/spline states,
stale peers and invalid reports are excluded. Off, expiry and lifecycle cleanup
restore only an owned value. A read-only observer releases the lease before
ordinary server run-speed handlers, including a server value equal to the
temporary value. No positions, outgoing movement cadence or server state change.

Final verification: **41/41 tests pass** with the real skin/pose/board/audio and
speed-trace inputs enabled; the full `WowClient` build passes. Synthetic coast
prediction error decreased from 2.872500 to 0.072500 yd. A recorded-speed replay
with modeled 500 ms position corrections, 100 ms addon delay and the existing
quarter-yd/s speed encoding decreased from 3.690846 to 0.962138 yd. Those are
controlled replay results, not live acceptance. Run the trace test with
`SKATE_SPEED_TRACE` pointing to the captured sender CSV. The two-client
rubberbanding retest remains outstanding.

### Airborne peer trajectory (2026-11-16)

The user reported nearly removed ground rubberbanding but laggy/choppy remote
ollies. Ground-speed replication cannot transmit the local sampled Skate jump
arc. Active board envelopes now include the actual actor's world position in a
95-character envelope (legacy 70-character input remains supported). The existing
epoch/map/mesh/sequence pairing and roster checks still apply; finite/world and
16-yard actor-relative limits reject invalid anchors.

During airborne presentation, board and rider use the same bounded interpolated
world anchor and existing 100 ms pose phase instead of the observer's stock jump
position. Takeoff starts from the displayed native anchor; landing blends back
to native presentation. Ground-only motion keeps the accepted native prediction
path. Off/expiry/model/map changes keep the existing cleanup. No remote actor,
movement state, fall curve or server position is written by this visual path.
Sender pose capture is refreshed after the final model transform, preventing a
previous-frame basis/origin from contaminating the outgoing visual root offset.

Verification: **42/42** tests and the full client build pass with all private
skin/pose/board/audio/speed inputs enabled. The new test covers 95-character Lua
transport/codec bounds, invalid/far coordinates, takeoff and landing continuity,
epoch/reset behavior, legacy input and shared board/body anchoring without
native-model writes. A 604-frame real-helper motion replay with controlled
observer position corrections covers 72 interior airborne display samples:
maximum native/injected step 1.011833 yd versus paired visual step 0.158543 yd.
These are controlled replay results; live two-client ollie smoothness still
requires acceptance. Both clients must restart on the new build.

### Single remote landing handoff (2026-11-16)

The user reported a second visual landing after the airborne sync improvement.
The handoff could return the landed source pose to a still-airborne observer's
native position. A regression reproduced that return to a native anchor 1.5 yd
above the source floor after the first touchdown.

The paired presentation now waits for the observer's falling flags to clear,
retaining the source landing anchor meanwhile. It then begins a fresh bounded
blend from the displayed landing to native presentation. Repeated source-ground
updates cannot discard that wait; epoch/off/expiry cleanup clears it. No wire,
physics or position-state changes were needed. The regression failed before the
fix and passes afterward, including delayed observer landing, continuous handoff,
ground-path restoration and cleanup. All **42/42** tests with private inputs and
the full client build pass. The user subsequently confirmed one landing.

### Peer delivery pacing (2026-11-16)

The user accepted the single landing but reported visible peer lag and a small
amount of rubberbanding. The earlier receiver trace recorded 209 accepted board
events over 31.633 seconds (6.575 Hz), with 64 two-sequence skips and one
three-sequence skip. This is earlier live evidence, not a new network capture.
Current source still discarded fractional sender timer time and rejected new
arrivals within 90 ms at both Lua and native layers.

The sender now retains timer remainder while emitting at most one pair per
frame, preserving the intended 10 Hz cadence without catch-up bursts. Incoming
authorized messages coalesce to the latest board and pose per sender each
display frame, dispatched board first; identical deliveries are deduplicated.
Native stale/retired sequence checks remain, with a bounded 20/s, four-message
burst allowance rather than the fixed spacing floor. Roster changes clear the
pending queue. Payload formats, simulation, speed authority and the existing
paired visual transition are unchanged.

The delivery regression failed against the old 90 ms rejection. It now accepts
a complete 10 Hz sequence with 70/130 ms arrival gaps, tests the burst bound and
expiry, executes the actual embedded Lua for newest-pair/auth/order/cleanup, and
checks nine pairs in 0.99 seconds at 33 ms frames (the old sender emitted seven).
All **43/43** tests pass with private inputs and the full client build passes.
Actual responsiveness and remaining rubberbanding still require the two-client
live retest; no end-to-end latency measurement is claimed from these tests.
