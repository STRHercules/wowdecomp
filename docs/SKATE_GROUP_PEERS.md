# Phase 5 group board replication

The original Phase 5 plan keeps the donor's party/raid addon-message route before
the later server opcode. That route is now built into the existing UI-ready Lua
bootstrap; no extra addon installation or server modification is required.
`SkatePeers.h` contains the transport, envelope and transform helpers.

Both players run the same client build and supply matching owned Skate assets and
helper paths. A viewer need not hold J or skate first: after collision init, the
worker loads schema op7 and samples op9/op20 once, without advancing physics, to
generate its private board M2/skin/BLPs. No private meshes/textures travel over the
network or are written into a shipped MPQ. Viewers without matching assets see no
board. Matching supported rider rigs also receive the semantic rider pose through
the paired message below. Unsupported or mismatched rigs retain stock animation.

## Transport and identity

Prefix `WRSP5`, channel `PARTY` or `RAID`, with a 10 Hz paired-update cadence while active and
one off message after exit. No guild, public chat, whisper, arbitrary recipient or
non-group relay. A Lua roster resolves the server-supplied sender name to the
current party/raid unit token; ambiguous/unknown names, self, wrong channels and
over-rate input are rejected. Native code independently permits only `party1..4`
and `raid1..40`, resolves the current player object, and checks map and asset
identity. The wire supplies no GUID that could select an unrelated world object.

The sender retains fractional timer time rather than accumulating display-frame
rounding delay. Authorized incoming bursts coalesce to the newest board/body
pair once per display frame, in board-then-pose order. Native acceptance retains
sequence/epoch checks and a bounded 20/s, four-message burst budget; ordinary
10 Hz arrivals are not rejected merely for being less than 90 ms apart. Pending
state is cleared with roster changes. No extra message type or higher intended
send rate was added.

The base 70-character ASCII payload is:

`EEEEEEEE:MMMMMMMM:HHHHHHHH:SSSS:BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB`

* `E`: nonzero 32-bit sender session epoch, hexadecimal. Process startup seeds it;
  map/UI/helper-asset resets advance it.
* `M`: 32-bit map ID, hexadecimal; opaque, including custom maps.
* `H`: 32-bit checksum of local mesh UV/index/material/color/texture content. This
  is asset compatibility detection, not authentication or transmitted game data.
* `S`: unsigned 16-bit uniform scale, units 1/1024, bounded to 63.9.
* `B`: the existing 19-byte `SkatePeerWire.h` state encoded as 38 hex characters:
  version/flags, wrapping sequence, relative world-yard centre, quaternion, speed.

Current active senders append `:XXXXXXXXYYYYYYYYZZZZZZZZ`, making a 95-character
envelope. Each eight-digit word is the IEEE-754 float bit pattern of the sender's
actual native actor position in absolute world yards (XYZ). Positions must be
finite, within one million yards per axis, and within 16 yards of the currently
resolved group actor. Legacy 70-character envelopes remain readable. No new
message or packet frequency is added.

The quaternion maps the deck's deterministic geometric basis to world axes. It
does not assume sender/viewer first helper poses have the same heading. Each
viewer computes that basis from its own matching private mesh, then converts the
packet to its M2's local basis. The sender transmits actual displayed scale and
centre offset, so differing player model sizes do not silently resize a peer's
board.

There are at most 39 peer records and eight simultaneously drawn boards within
100 yards. Same-epoch sequences must advance (including 16-bit wrapping). Four
retired epochs are remembered per current group peer. Invalid packets cannot
renew expiry. Boards expire after 1.5 seconds without an accepted update; inactive
packets hide them sooner. Roster/token changes, object removal/destruction, map
unload, Lua rebuild, logout and shutdown release the client-only board objects.
No remote player movement, skeleton structure, GM privileges or server state are changed.

Helpers use `Logs/SkateHelper-<PID>-<instance>.log`, with the chosen filename logged
on world entry. This permits two instances of the same client folder; a shared
stderr write handle must not prevent the second helper starting. Peer acceptance
and board-creation diagnostics include the client PID in `Extensions.log`.

## Validation and live gate

### Paired rider pose (2026-11-16)

The user's first two-client test confirmed board visibility but showed the rider
running on it. A second prefix, `WRRP5`, now carries the solved rider pose, paired
with the immediately preceding `WRSP5` board epoch/map/mesh/sequence. Board and
body presentation advance together; a missing pose briefly retains the last
matched pair, rather than advancing the board independently. No independent
body-only smoothing is introduced.

Remote-quality follow-up: the first pose build used the receiver's model matrix
from `OnWorldUpdate`, but later `CGUnit_C::Animate -> UpdateWorldTransform` replaced
that basis/origin before M2 bone animation consumed it. The adapter now rebuilds
the finished remote locals from the existing `OnUnitWorldTransformUpdated` seam,
after the current render transform is placed. No engine seam was added, and the
MT callback still only reads prepared poses. It never changes the unit/model
world transform, movement queue, speed or network traffic.

The paired presentation now transitions for 100 ms from its currently displayed
pair to each new matched board/pose pair, using one shared phase for board
translation/scale/shortest-arc rotation and rider root/model/global-role rotations.
Jittered early arrivals continue from the displayed pair rather than restarting
at an old endpoint. There is no extrapolation; loss holds the endpoint until
normal expiry. Off, epoch/map/asset/rig changes, lifecycle cleanup and TTL retain
their immediate clear/reset behavior. This does not interpolate server movement.

The payload is exactly **122 binary bytes / 244 hex characters** (250 including
the five-character prefix and separator, below the 254-byte limit):

| Binary offset | Type | Value |
|---:|---|---|
| 0 | u8 | version 1 |
| 1 | u32 | nonzero board epoch |
| 5 | u16 | board sequence |
| 7 | u32 | map |
| 11 | u32 | compatible private board mesh checksum |
| 15 | u32 | rider rig fingerprint |
| 19 | u32 | actual sender model rotation quaternion |
| 23 | i16[3] | world visual-root offset relative to unit, 1/2048 yard, bounded +/-8 yards |
| 29 | u8 | semantic role count, exactly 23 |
| 30 | u32[23] | solved global role quaternions in `skaterig::BodyRoles` order |

Each quaternion uses a two-bit largest-component selector and three signed
10-bit components scaled by `511*sqrt(2)`. Encoding normalizes near-unit finite
quaternions and stores the largest component positive. Decoding rejects reserved
-512 values, impossible retained squared lengths, or a non-largest omitted
component. No raw model bone indices, asset contents or animation files travel.
The fingerprint hashes explicit bone count/key/parent/flags/pivots/rotation-track
coverage and semantic mapping fields, without struct padding or instance scale.
Only the same supported remote avatar rig is posed; mismatches log and stay stock.

The receiver reads its own copy of that unit's loaded model, uses the existing
`Plan/Bind/Fill` path (including Retail ankle metadata), and transforms both
rotations and visual root through the actual sender/receiver model matrices.
This handles different network-render facing, ground tilt and model-vs-unit
origins. It never drives or relocates the remote unit. The main thread prepares
at most eight immutable model-pose views before scene animation; the MT bone hook
only reads those views. Expiry, off, roster/object/map/Lua/model lifecycle changes
restore the model's previous `m_boneHook` ownership without releasing the
engine-owned avatar model.

Offline validation: **39/39** tests pass with actual skin/pose/board captures and
the WAV bank. All 604 recorded frames pass quantized remote reconstruction at
0/90/wrap yaw plus unequal model/unit origins and tilt: maximum world joint error
0.003234 yard, actual weighted lower-leg skin error 0.002515 yard, and foot/deck
relative error 0.003157 yard. The actual embedded Lua tests cover both prefixes,
independent throttling, roster authorization and off suppression. Hook tests
cover prior-true/prior-false restoration and destroyed-model handling.
`WowExtensions.lib` compiles. Proper remote riding and off cleanup still need
the next live two-client retest.

The user subsequently confirmed proper remote riding but reported severe jitter.
A 604-frame regression now simulates changing native render transforms *after*
the old preparation phase: its reproduced error is 0.432516 yard, versus 0.003234
yard after final-transform preparation. A 625-display-frame simulation uses 10 Hz
source pairs, jitter and periodic packet loss: maximum ankle display step falls
from 0.450491 to 0.090928 yard; deck step from 0.259450 to 0.041512 yard. These
figures demonstrate the presentation fixes, not a measurement of live server
position rubberbanding. All 39 tests pass with the actual private fixtures;
live remote smoothness and off cleanup remain acceptance gates.

The source tests execute the actual embedded Lua with mocked game functions and
no real messages. They cover group authorization, send/receive throttling,
party/raid selection, the off message, stale/wrapped sequences, retired epochs,
expiry, malformed envelope/codec data and the viewer-rest transform. Worker tests
check preview availability with J off and two simultaneous helpers in one working
folder. The real-helper timing probe also builds the idle viewer's actual M2 and
deck basis from op9.

Offline result: full 38/38 tests pass with the existing private skin/pose/board
captures and WAV bank. The real helper bootstrapped a 2,285-vertex viewer board
with three textures and a valid deck basis while J was off; subsequent cadence
remained 59.992 Hz. `WowExtensions.lib` compiles. These checks do not substitute
for the live two-client rendering test.

Live acceptance still requires two logged-in clients/accounts already grouped by
the user. The same `WowSkatePhase5.exe` and folder can be opened twice; no duplicate
client directory is required.

1. Enter the same map near each other and wait for collision/private board assets.
2. Leave J off on the viewer. Hold J and push, turn, ollie/flip, then bail on the
   rider. The viewer should see a textured board following the rider's position,
   rotation and scale. The rider must also skate/crouch/push/ollie/bail, rather
   than play the running animation on the board. Check `peer ... rider pose active`.
3. Release J. The peer board should hide on the off message or within 1.5 seconds.
4. Leave the group, change maps or move the rider out of streaming range; no stale
   board should remain. Repeat in the other direction.
5. Check PID-tagged `Skate: ... group peer ... accepted/board created` log lines.

The user confirmed the earlier board-only replication. No agent has joined a
group, sent a live addon message or declared the new rider-pose/off check passed.
The Phase 6 area-of-interest opcode remains a separate extension
for nearby players outside the party/raid; group visibility fulfills only the
original Phase 5 transport scope.

## Ground prediction speed

The board state speed byte is the sender's **actual applied native ground run
speed**, not the helper board velocity. Grounded ordinary movement packets do
not carry that changing speed. A fresh authenticated group peer can therefore
lease the reported value to the viewer's extrapolator, bounded by the latest
server-provided run allowance. It never changes a local mover, positions,
movement packet frequency or server authority. Unsafe/air/spline states and
off/expiry release it, and normal server speed messages take precedence.

The paired live traces established the speed mismatch; 41/41 tests and a full
client build pass with the correction. Actual two-client rubberbanding still
requires a new live check. Keep `skatePeerTrace` at 0 outside diagnostic runs.
