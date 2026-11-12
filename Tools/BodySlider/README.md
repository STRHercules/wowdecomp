# Body Slider for stock WotLK 3.3.5a

Standalone prototype for the supplied x86 build 12340 executable. It uses a small
executable loader patch, `BodySlider.dll`, and an in-game-only addon. No Esteria
client, server changes, custom packets, GlueXML edits, DBC edits, or model-file
replacement is needed. The original `D:/Wow.exe` is never modified.

## Current scope

Thirty-seven controls: twelve body proportions, breast/butt deformation, and
twenty-three facial controls for jaw, face, brows, eyes, nose, mouth, chin,
cheeks and ears. The window (Esteria Incarnations style) has Body, Face and Features
pages, a live preview of your own model, and a Presets page: share codes (the same
`BodySlider/2:` string the MSP field carries) and presets saved per race and sex in
`BodySliderDB.presets`. The preview calls `BodySlider_Set(previewFrame)` each frame; DLL
0.3.0 shapes that PlayerModel with the local player's values, older DLLs leave it stock. Breast deformation
is enabled on female profiles; butt and facial controls apply to both sexes.
There are audited skeletal profiles for the twenty original race/sex models.
Filename, bone count, key table, every parent and every bind pivot must match;
unrecognized skeletons, shapeshifts and HD replacements are left alone.

Breast/butt and face controls reshape each character's own vertices and normals.
Their smooth local regions are estimated from the original body mesh and skeleton
landmarks. They are not copied Retail mesh profiles. Recognized raw meshes and
software-remapped variants have recorded vertex fingerprints; unknown geometry
is left unchanged. One malformed Human Female software LOD variant is excluded.
These geometry checks do not establish visual quality for every animation,
outfit, mount or race. HD/replacement models require their own calibration.

## Install the built package

1. Close the client. Keep a copy of your original executable.
2. Run the included `BodySliderPatch.exe` and select your supported original
   `Wow.exe`. It creates `Wow-BodySlider.exe` beside it and leaves the original
   unchanged. It refuses unknown executables and existing output files.
3. Keep the original as a backup, rename the patched copy to `Wow.exe`, and put
   `BodySlider.dll` beside it. Copy `Interface/AddOns/BodySlider` into the same
   client folder's `Interface/AddOns`.
4. Enable Body Slider in the character-selection AddOns list. Log into the world
   and type `/bodyslider`. There is no slider UI before entering the world.

All viewers need the patch, DLL and addon to see shared shapes. Others see the
ordinary models. Appearance changes are local visual changes: server reach,
movement, combat and collision remain server-controlled.

Settings persist per realm and character in SavedVariables. Sharing defaults to
the custom `BodySliderMSP` channel. Both users need this addon version and must
use the same transport:

- `/bodyslider channel`: automatically joins `BodySliderMSP` and sends regular
  channel chat carrying the namespaced MSP `BS` field. The addon filters its own
  wire messages from the chat window. This channel is not private or encrypted.
- `/bodyslider group`: sends MSP addon messages over your party or raid instead
  of whispers. Join the same party/raid before testing this mode.
- `/bodyslider whisper`: explicitly enables the original MSP whisper transport
  for compatibility with earlier versions, where the server permits it.
- `/bodyslider status`: reports the selected transport, readiness or paused state.

Channel/group broadcasts coalesce changes to at most one send per three seconds,
with a thirty-second heartbeat. Channel joining has three bounded attempts;
explicit server rejection pauses the transport for the session. Unresolved names
such as `Unknown` never become whisper recipients. No transport automatically
falls back to whispers. Use a transport command to retry after resolving a block.
Servers can restrict channels and group messages independently; joining a channel
does not remove those restrictions. The original server warning remains visible.

The `BS` field and `BodySlider/2:` value schema are unchanged. `BS` remains an
experimental, unregistered MSP field; normal MSP whisper framing is used in
whisper mode. Group mode carries the same field over addon messages, and channel
mode wraps it in a printable Body Slider envelope rather than claiming standard
MSP whisper interoperability. The decoder still accepts twelve-control
`BodySlider/1:` values, treating new controls as zero. See
[MSP.md](Interface/AddOns/BodySlider/MSP.md) for framing and limits. Incoming
settings are bound to game-observed sender names and GUIDs, never a GUID from
the network payload.

Missing DLL: stock startup continues. Unknown executable or conflicting hook:
DLL refuses installation and logs why beside the executable in `BodySlider.log`.
This does not circumvent a realm's integrity checks or rules on modified clients;
launcher replacement, anti-cheat acceptance and message delivery depend on the
realm. There is no universal server compatibility claim.

## Build and patch

Building requires Windows, MSVC C++ with x86 tools/Windows SDK, CMake, and Python 3.
Users do not need Python or a compiler; the included patcher is a small native EXE.
The DLL statically links the C++ runtime and imports only Windows system APIs.

```powershell
cmake -S Tools/BodySlider/native -B build/body-slider -A Win32
cmake --build build/body-slider --config Release
ctest --test-dir build/body-slider -C Release --output-on-failure
cmake -S Tools/BodySlider/patcher-native -B build/body-slider-patcher -A Win32
cmake --build build/body-slider-patcher --config Release
build/body-slider-patcher/Release/BodySliderPatch.exe --patch D:/Wow.exe path/to/new/Wow.exe
python Tools/BodySlider/package.py path/to/new/BodySlider.zip
```

Copy `build/body-slider/Release/BodySlider.dll` beside the generated executable
and install the addon. The patcher accepts only SHA-256
`aa63a5750d60ef16746c686b3d5e26876d98953eab08b1c026cd0faf78e88cb8`.
It refuses to overwrite any existing output. Do not point it at a different
realm's customized executable.

The loader changes 26 original bytes, all PE header fields, and adds a 212-byte
bootstrap in a new section plus restoration metadata. Original code, resources,
and imports remain byte-identical. It loads only the `BodySlider.dll` beside the
executable and invokes initializer export ordinal 1 before stock startup, after
Windows has completed DLL initialization. The DLL installs eleven in-memory hooks:
the original four Lua/skeleton/lifecycle hooks plus batch vertex binding, five
CPU skinning routines and shadow batches. Hardware skinning uses a private vertex
buffer per shaped character; software skinning reads an immutable private vertex
snapshot through temporary model-data views. Shadow groups split when different
instances have different shapes. Engine-owned vertices and class layouts remain
unchanged. Mesh buffers rebuild after device invalidation and are released on
reset or model destruction.
Six Lua entry bridges use verified compiler padding inside the executable's
original `.text` section. Stock Lua rejects DLL callback addresses directly;
these bridges preserve its validator and check code unchanged. A harmless startup
Lua call verifies the callback path before a character login.
The separate projected-texture rendering helper still uses original geometry;
decal alignment may differ on reshaped surfaces.
Patching invalidates the original executable signature; restoration recovers it.

To produce a byte-identical original executable at a new path:

```powershell
BodySliderPatch.exe --restore path/to/patched/Wow.exe path/to/restored/Wow.exe
```

Keep the installed DLL until after closing the client. To uninstall, restore the
original executable and remove the addon and DLL. The normal WTF SavedVariables
file can be kept as a preset backup or removed while the client is closed.

## Verification and remaining acceptance

```powershell
python -m unittest discover -s Tools/BodySlider -p test_patcher.py -v
python -m unittest discover -s Tools/BodySlider -p test_model_audit.py -v
python -m unittest discover -s Tools/BodySlider -p test_deform_audit.py -v
python Tools/BodySlider/patcher-native/test_native_patcher.py
python -m unittest discover -s Tools/BodySlider -p test_package.py -v
python Tools/BodySlider/test_lua_bridge.py -v
python Tools/BodySlider/Interface/AddOns/BodySlider/test_protocol.py
```

Patcher optional disassembly checks use `pefile` and `capstone`; Lua tests use
`lupa.lua51`. Runtime dependencies are not shipped inside the addon. Model-audit
tests use the recorded JSON; recreating the audit reads local MPQs with StormLib.
See `evidence-native.txt`, `evidence-mesh.txt`, `stock_profiles.json` and
`deform_profiles.json` for source identities and offline ABI/model evidence.

Live acceptance still requires:

- Slider changes and reset on each supported race/sex; feet, equipment, mounts,
  movement/combat animations and shapeshift return must remain usable.
- Two clients seeing each other's settings, later arrival, departure, reset,
  UI reload and relog on an unchanged server.
- Actual TRP3/MSP-addon coexistence; protocol simulations alone do not prove it.
- Clean client exit, crowded-area performance and each target realm's acceptance.

The package is a prototype until those checks are recorded as passed.
