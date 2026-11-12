# Body Slider 0.2.2 prototype verification

## Transport correction after live testing

The user's two-client screenshots confirm successful world entry and visible
local body changes after the bridge correction. They also show the server's
level-ten whisper restriction and repeated attempts to contact the unresolved
name `Unknown`. Remote slider delivery was not established.

0.2.2 is an addon-only change. Channel transport is now the default; explicit
party/raid and legacy whisper modes are also available. Unknown/localized
placeholder names are rejected, joining/sending is bounded, and a rejected
transport pauses instead of retrying continuously. Native DLL and executable
patch formats are unchanged. Channel payloads use a printable envelope around
the existing experimental MSP `BS` field, not a new stock addon-message type.

Two isolated Lua clients simulate channel and group delivery, including changes,
sender/GUID binding and mode restrictions. Live channel/group delivery and actual
ChromieCraft permissions still require verification. The original server errors
are not hidden, and no attempt is made to change server-side restrictions.
Additional cases cover unknown/localized names, bounded channel joining,
coalesced updates, rejection pause, exact missing-player backoff, transport
switches and narrowly removing the addon's published MSP field. The native DLL
and existing patched executable are not rebuilt or replaced for this update.

## Character-login crash correction

The user-provided 0.2 crash report identifies `BodySlider_Supported` at address
`0x69127790`, exactly DLL base `0x69120000` plus the original callback RVA
`0x7790`. Stock Lua calls `OsValidateFunctionPointer`, which accepts only original
executable `.text` addresses `[0x401000, 0x9DE3B3)`. Directly registering DLL
callbacks was incorrect. Previous tests exercised callback implementations but
did not exercise stock Lua's C-call validator; startup did not call the addon.

0.2.1 registers six verified executable-resident bridges and tail-jumps to the
specific DLL callbacks. The validator and stock check code remain unchanged.
Executable patcher/output format is unchanged; this is a DLL-only correction.
Hook page protections now restore in reverse preparation order, including
rollback, so bridges sharing a page do not leave it writable.

Seven new offline bridge tests and the four native CTest executables pass.
The native integration test also verifies same-page hook installation/rollback
restores exact bytes and execute/read protection. In the actual isolated client,
a new startup Lua call to `BodySlider_Supported()` passes the real C-call
validation path. The process remains responsive. Read-only live-memory checks
verify all six bridge targets, execute/read protection, expected text bounds,
and byte-identical stock validator/check code. The original user crash address
is still rejected directly, as expected; only the named bridges are registered.

The user's subsequent screenshots establish successful world entry and local
slider behavior after this correction. Other rendering/gameplay acceptance gates
below remain open.

Input: `D:/Wow.exe`, PE32 x86, version 3.3.5.12340.
Original SHA-256 remains
`aa63a5750d60ef16746c686b3d5e26876d98953eab08b1c026cd0faf78e88cb8`.
Patched SHA-256:
`f1dd0703eaf82022f07d69ebb3cf28ca4d99ee815dbd90a29ac3dde86ad9ac82`.

The loader patch remains unchanged: 26 original bytes differ, all in PE headers.
Original code, resources and imports remain intact. The original executable hash
was checked again after testing and is unchanged.

## Passed checks

All 48 automated checks passed:

- Twenty-five Python checks: eight reference patcher checks, seven stock rig
  checks, five deformation audit checks and five package checks.
- Ten Lua 5.1 checks: all 37 controls, version 2 MSP framing, legacy version 1
  migration, complete payload validation, sender binding, simulated roleplay
  addon coexistence, fragmentation bounds, throttling, in-game UI lifecycle,
  page navigation and model support handling.
- Nine native patcher checks, including conversion of the supplied executable,
  unsupported input and overwrite rejection, and byte-identical restoration.
- Four Win32 Release CTest executables: shape math, native integration,
  deformation math and mesh runtime integration. These exercise the actual
  implementation against controlled stock API substitutes; they do not replace
  a live in-world test.

Additional offline checks ran the original vertex arrays for all twenty race/sex
profiles through 500 profile/slider cases. Results remained finite, normals stayed
unit length, weights and UVs were unchanged, and neutral settings reproduced the
original vertices exactly. The profile audit records 83 raw and software-renderer
geometry fingerprints. The profiles provide female breast deformation, butt
deformation for both sexes and 23 facial controls.

`BodySliderPatch.exe` is 120,832 bytes. Its file picker opened and cancelled
without writing files. Command-line patch and restore operations completed
successfully against the supplied executable.

The DLL builds as x86, exports `BodySlider_Initialize` at ordinal 1, imports only
`KERNEL32.dll` and statically links its C++ runtime.

The actual patched client started in an isolated folder using existing client
data. The DLL installed eleven hooks, the Lua API registered twice, the process
remained responsive and no client error report was found. No credentials were
entered and no realm login was attempted. The hidden smoke-test process was
terminated afterward; this is **not** a clean-exit acceptance test.

Startup log:

```text
BodySlider 0.2: eleven rendering/Lua hooks installed; server protocol unchanged
Lua API registered (operations require an in-world player)
Lua API registered (operations require an in-world player)
```

## Known limits and remaining live acceptance

One Human Female software-renderer LOD 2 geometry variant is excluded because its
bone palette contains an out-of-range index. Scourge eye regions use estimated
geometry quantiles; other eye regions use original eye-bone CRC landmarks.
Deformation regions are geometry estimates and have not been visually calibrated.
The projected-texture helper is not hooked, so projected decals may not align with
deformed surfaces.

In-world deformation, race/sex visual calibration, equipment and mount fit,
animation quality, shadow rendering, shapeshift recovery, graphics device reset,
crowded-area performance, clean exit, two-client delivery/reset/relog, real
TRP3/LibMSP coexistence and acceptance by individual realms remain unverified.
Simulated APIs and protocols do not establish those behaviors.

The MSP `BS` field is experimental and unregistered.
