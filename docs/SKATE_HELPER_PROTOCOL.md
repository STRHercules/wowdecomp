# Client-owned Skate collision protocol (WSB1 ops 48–50)

Helper-side Phase 2 contract, 2026-11-15. These additive operations in
`wotlkskate/native-probe/skate-bridge` consume geometry copied from
`World::GatherCollisionTriangles`. Existing operations retain their legacy
behaviour, including helper-owned MPQ loading for 16/46/17/47. **48/49/50 never
open a world archive or consult a map catalogue.** All `uint32_t` map IDs are
opaque identities, including 0, CoA maps and 1820.

## Framing and ownership

The unchanged 20-byte little-endian WSB1 header is five `uint32_t` fields:
magic `0x31425357`, operation, nonzero request serial, nonzero session epoch,
payload byte length. Maximum payload is 2,097,152 bytes. Reply header echoes
the serial and epoch, with operation `request | 0x80`; rejection uses `0xFF`
and up to 512 bytes of UTF-8 diagnostic text. Startup sends `(op,id,epoch,len)
all zero. Match all reply identity fields; discard old worker/session snapshots.

**Client transport change required:** op 48 must receive the same **20-second
initialization deadline** as ops 1/16/30. Existing `SkateTransport::Request`
otherwise assigns it a two-second deadline, shorter than real asset loading.
Keep all reads/writes off the game thread. Refresh acknowledgement is quick;
collision/rail compilation occurs on a bounded helper worker.

## Init 48 / refresh 49 payload

Both use this exact packed prefix, with no native padding:

| Offset | Type | Meaning |
|---:|---|---|
| 0 | u32 | Map ID; no whitelist |
| 4 | u32 | Generation: exactly 1 for init; strictly increasing for refresh |
| 8 | f32[3] | Fixed origin in absolute WoW world yards, XYZ |
| 20 | f32[3] | Gather centre in absolute WoW world yards, XYZ |
| 32 | f32 / u32 | Init: engine heading in radians, finite, abs <= 7. Refresh: reserved bits must be zero |
| 36 | u32 | Gather status, must be zero |
| 40 | u32 | Triangle count, 1..49,152 |
| 44 | f32[count][3][3] | Absolute WoW world-yard triangle vertices; original CCW winding |

Length must equal `44 + count * 36`; at the maximum it is **1,769,516 bytes**.
Object GUIDs are deliberately not serialized: adding eight bytes per triangle
would exceed the existing frame cap. The client owns any local-player/object
filtering using `outIds` before sending the triangle copy. Do not append GUIDs
or silently reduce the count cap.

The helper subtracts the fixed origin, then converts each point to engine metres
as `(dx, dz, -dy) * 0.9144`. This is a proper rotation; preserve vertex order.
Absolute coordinates must be finite and abs <= 1,000,000 yards on every axis;
converted coordinates and centre must be abs <= 10,000 metres on every axis.
Triangles must have nonzero area (engine cross-product squared > 1e-12).
Triangles may protrude beyond the gather box; they are not rejected for that.

Status 1 (TRUNCATED), 2 (INCOMPLETE), their combination, and unknown status bits
are rejected. Zero triangles are rejected. Retry after the client has a complete
bounded snapshot; never replace a valid world with a partial/empty query. With
the stock placeholder flag, client-provided AABB triangles are accepted as the
same collision stock movement sees; this wire adds no source interpretation.

Init requires centre == origin, generation == 1 and an epoch strictly greater
than the helper's current epoch. It builds a candidate session before replacing
the installed one. Any failure retains the old session. Successful init returns
the unchanged 60-byte pose summary (tick u64; position, velocity, forward f32[3];
period f32; bones, airborne, ground u32), and installs generation 1.

Refresh requires an existing client-owned session and exact epoch, map and origin
matches. Generation must exceed the highest accepted queued generation, even
while compilation is pending or after a failure. Queue-full rejection does not
consume a generation. A successful reply is empty and acknowledges **queueing**,
not installation. Origin/map changes require a new epoch/init. Legacy recenter,
triangle refresh and helper-world overlay operations (10/14/17/44/45/47) are
rejected within a client-owned session; use 49 for its complete replacement.

## Installation / status

Op **50** takes an empty payload and matching epoch. Its 16-byte response is four
u32 fields: `map, queuedGeneration, installedGeneration, lastFailedGeneration`.
The final field is zero before any asynchronous failure, and otherwise retains
the latest failed generation, including after a newer successful refresh.
Success is `installed == requestedGeneration`; failure is
`lastFailedGeneration == requestedGeneration`. Poll until one of those is true.
Track the latest accepted generation: if `queued > requestedGeneration`, the
older request was superseded and is not guaranteed to install or report failure.
Stop waiting for it and poll the latest generation (or keep one refresh pending).
Op **15** remains available and returns only `queued, installed` (eight bytes).
Both polls and ordinary simulation steps install ready collision before use.

Invalid packets fail synchronously without altering collision, identity or
generation. Asynchronous rail/collision build, installation or worker-channel
failures mark the failed generation and keep the old usable session/world.
Retry with a higher generation. Accepted newer snapshots supersede older queued
results; stale results cannot install. The worker has at most one queued job and
one queued result in addition to its current work and the latest retained packet.

Rails are detected from the same converted triangle set as physics. Normal
refreshes retire removed rails. While attached to a grind, retain installed rail
IDs; if attachment begins during a compact build, rebuild against those IDs
before installing it. The existing rail identity budget still applies and is
independent of the triangle cap.

Client integration and live terrain/WMO landing acceptance are separate from
this helper contract. No new operation extracts triangles, moves a WoW player,
or deploys anything into a running client.

## Helper verification

From the `wotlkskate` root, build to a separate target directory so the accepted
Phase 1 helper can remain running:

```powershell
rtk cargo build --manifest-path native-probe/skate-bridge/Cargo.toml --target-dir ../wowdecomp/build/skate-phase2/helper
rtk cargo test --manifest-path native-probe/skate-bridge/Cargo.toml --target-dir ../wowdecomp/build/skate-phase2/helper
rtk proxy python native-probe/test-client-collision.py ../wowdecomp/build/skate-phase2/helper/debug/wrath-skate-bridge.exe <owned-converted-assets> <proof-output-directory>
```

The source-only Python test uses the real helper, caller-supplied assets and
synthetic world-yard geometry. It covers map 1820/909, identity and stale-epoch
rejection, strict generations, partial-gather rejection, rail replacement,
asynchronous failure retention/recovery, and the 49,152-triangle frame path.
Unit tests additionally exercise parser bounds/conversion and deterministic
queue-full rejection without consuming a generation.

On 2026-11-15, full `cargo test` reports **65 passed, 3 failed, 12 ignored**;
the baseline was 62 passed, the same 3 failed, 12 ignored. The unchanged failures
all require the absent historical `DATA/common.MPQ` installation:
`world_collision::tests::map_selection_reads_owned_continents_and_instance_metadata`,
`world_collision::tests::authored_wrath_collision_produces_bounded_vanilla_rails`,
and `world_collision::tests::recorded_forward_guard_surface_profile`.
Those tests were neither disabled nor changed to manufacture a green suite.
