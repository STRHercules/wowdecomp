# Azeroth Remastered port: handoff

Updated: **2026-10-29**. Donor: `R:\Users\Zach\Documents\GitHub\Azeroth-Remastered` (one squashed commit, `4929a78`): a
3.3.5a client with its own Vulkan backend (`wxl/`) and HD effects written against it. We do not take its backend; we port
its effects onto ours through `GxEffects` (see "Why not swap the backend").

## Where things stand

| | State |
|---|---|
| Branch | `azeroth-remastered-hd` (last commit `89980403`: contact shadows, GI, full casters, terrain); **uncommitted**: buildings, grass (`HdMapObj`, `HdDetailDoodad`, `HdGrassWind`, `HdShaderSwap` hooks) |
| Dev client | `G:\3.3.5a - Dev\Wow.exe` = `bin\Win\WowClientRetailInventory.exe` from `build/retail-satchel`; every install backed up in `G:\3.3.5a - Dev\_backups\` |
| Works in game (visuals 2) | AO, Fog (with sun-shadow light shafts), Bloom + sun rays, Grade, HdTime, custom-light fix, Forever data |
| Built, not confirmed in game | Contact shadows (mask now built before the grass), GI, full shadow casters |
| Works in game (material shaders) | Terrain (352 shaders, ~6 s on 15 threads), buildings, grass (dark-in-shade fixed with `hdGrassShadow`; that last build installed, not yet seen in game) |
| Built, installed, not seen in game | Sky (`HdSky`) |
| Built, installed, not seen in game | Point lights (`hdLights`, terrain only, as in the donor) |
| Not started | Forever water, the donor's own shadow module |

Everything below is gated on `visuals 2`; Stock (0) and Lumenveil (1) are untouched.

## The pieces

**The switch.** `visuals` cvar (saved): 0 Stock, 1 Lumenveil (default), 2 Azeroth Remastered. `WoW/Source/Ui/Visuals.h`;
the dropdown `WowDecompVisualsDropDown` on the Effects page (`WoW/Source/UIUtil/DistanceOptions.cpp`). Lumenveil only
draws in mode 1.

**Compute in GxEffects** (`Engine/Source/Gx/GxEffects.h`, Vulkan side `.../Vulkan/Effects.cpp`): `Device::Dispatch` with
samplers 0..15, constants 16, storage images 17..24, push constants up to 128 bytes; `CreateStorage`, `CanStore`; and
`Binding::engine`, which samples one of the engine's own textures (a `CGxTex*`, e.g. a sun shadow band) as it is. A pass
may not sample a texture it also writes. GLSL has no BGRA image format, so the passes work on a copy of the world:
`HD/HdFrame.cpp` holds one RGBA16F frame that `Open` copies the world into (or returns, if already open) and `Close`
draws back.

**The effects** (`WoW/Source/WorldClient/HD/`), each the donor's formulas, cvars and tuned defaults (its
`Tools/Pack/Reglages-HD.txt`):

| File | What | cvars (default) |
|---|---|---|
| `HdAO.cpp` | XeGTAO (MIT), temporal | `hdAO` 1, `hdAORadius` 1, `hdAOStrength` .85, `hdAOQuality` 1, `hdAOTemporal` 1, `hdAOHalfRes` 0 |
| `HdContactShadow.cpp` (+`Units`) | screen-space contact shadows; D3D9-only in the donor, translated | `hdContactShadow` 1, `...Length` 1.5, `...Strength` 1, `...Units` 0 |
| `HdGI.cpp` | one-bounce screen-space GI; D3D9-only in the donor, translated | `hdGI` 1, `hdGIRadius` 4, `hdGIStrength` 1 |
| `HdFog.cpp` | Forever volumetric fog, 3 layers, sun-shadow shafts | `hdFog` 1, `hdFogDensity` 3, `hdFogSun` 1, `hdFogAmbient` 4.75, `hdFogShadow` 1 |
| `HdBloom.cpp` | halo + sun rays | `hdBloom` 1, `hdBloomStrength`, `hdBloomThreshold` .7, `hdRays` 1, `hdRaysStrength` |
| `HdGrade.cpp` | Forever colour grading (LUTs) | `hdGrade` 1, `hdGradeStrength` 1, `hdGradeLut` 0 |
| `HdTime.cpp` | time and sun | `hdTime` -1, `hdSunPath` 1, `hdShadowSun` 1, `hdShadowBillboard` 1 |
| `HdForever.cpp` | reads `forever_data` (lights, grading, fog) | `hdForeverForce` 0 |
| `HdCamera.cpp` | the camera the passes use, captured after the opaque models; `ScreenRows` | |

Debug views: `hdAODebug 1`, `hdContactShadowDebug 1|2`, `hdGIDebug 1|2`, `hdFogDebug 1..4`, `hdBloomDebug 1..3`
(red dot = the sun), `hdGradeDebug 1` (**the white line down the centre**: left ungraded, right graded; `0` removes
it). They are not saved, but stay on until set back to 0 or the game restarts.

Status lines: read them in chat with `/run print(GetCVar("hdFogInfo"))` (`/console hdFogInfo` prints nothing in chat).
Each effect has one: `hdAOInfo`, `hdContactShadowInfo`, `hdGIInfo`, `hdFogInfo` (lists the lights, Forever settings and
weights the fog came from), `hdBloomInfo`, `hdGradeInfo`, `hdForeverInfo`.

**Frame order** (`WoW/Source/Ui/WorldFrame.cpp`, `CGWorldFrame` render; the donor's points):

1. `HD::CaptureCamera()` and `HD::AO::World()` after the opaque models (`M2PASS_0`, beside `Lumenveil::OpaqueDone`).
2. `HD::ContactShadowPrepare()` before `CWorldScene::RenderDetailDoodads`: the contact-shadow mask from the depth
   *without* the grass (thin grass seen as a 1-yard slab cast shadows that turned with the camera).
3. After the grass: `HD::ContactShadowRenderOpaque()` applies the mask, `HD::GIRenderOpaque()` bounces, `HD::Frame::Close()`.
4. `HD::Fog::World()` after liquids and weather, before `SpellVisualsTick`.
5. `HD::Bloom::World()`, `HD::Grade::World()`, `HD::Frame::Close()` after `PlayerNameUpdateLate`.

Hooks elsewhere: `HD::ApplyTimeOverride` in `CGWorldFrame::UpdateDayNight`; `hdSunPath` in `DayNight.cpp` (sun position,
skybox minutes); `hdShadowSun`, `hdShadowBillboard` (the donor's leaf-card block) and `HD::ShadowFullCasters` in
`ShadowQuery.cpp`. Cvars are registered in `WoW/Source/Client_CVar.cpp` next to `Lumenveil::Startup`.

**Material shaders** (terrain works in game, 2026-10-29). The donor puts its own HLSL into the client's own shaders: compiled at
start-up with the system's `d3dcompiler_47`, swapped into the `CGxShader`, and on Vulkan used only when a module
translated for that exact bytecode exists. Our backend works the same way (manifest by md5, `Tools/VulkanShaders`), so:
- `HdShaderSwap.cpp`: `ShaderUsable` asks the device (`GxEffects::Device::ShaderKnown`) and, for unknown code, writes it
  to `<VulkanShaders*>\hd-pending\<name>.bin`; `ShaderSetCode` swaps it in.
- `Tools/HdMaterials/install_hd_shaders.py --client "G:/3.3.5a - Dev"` translates the pending blobs (`--extra-vertex/
  --extra-pixel`) into every `VulkanShaders*` folder's manifest. Flow: start the game in visuals 2 once (it writes what it
  lacks), run the tool, restart. The modules match that machine's d3dcompiler, so run it where the game runs.
- Pixel constants past c19 use the translator's existing `pext` block (c20..c115), so the donor's vertex-row trick for
  them is not needed.
- **Terrain** (`HdTerrain.cpp`, the donor's HLSL unchanged: 128 vertex, 224 pixel permutations, all translate): Forever's
  sky/horizon/ground ambient (`hd_forever_ambient.txt`, `HD::ForeverAmbientsAt`), hooked in `CMap::GetTerrainPixelShader`,
  `CMapRenderChunk::GetVertexShader` and after the chunk's vertex constants. cvars `hdTerrainLight` (1; 3 = test view: sun
  after shadow only), `hdLightAmbient`, `hdTerrainHemi`, `hdTerrainInfo`.
- **Buildings** (`HdMapObj.cpp`, works in game): the donor's HLSL and family tables unchanged (7 vertex
  families x 18 distinct codes, 5 pixel kinds x 16; 184 records after folding identical ones), Forever lighting on
  exterior-lit batches with the terrain's hemisphere ratios; the vertex and pixel shader of a batch are swapped as a pair
  (all or nothing), looked up by the shader's name. Hooked in `CMapObj::SelectWorldShaders` and `CMapObj::Initialize`.
  cvars `hdWmoLight` (1; 3 = test view), `hdWmoInfo`.
- **Grass** (`HdDetailDoodad.cpp` + `HdGrassWind.cpp`, works in game): the donor's HLSL (6 vertex, 8 pixel slots)
  with the terrain's Forever lighting, and grass wind and parting around the player (WarcraftXL's algorithm, GPLv3) in
  the vertex shader; all 14 swapped together once per frame. Hooked in `CDetailDoodad::Initialize`,
  `InitializeRenderState`, `SetupVertexShader` and `Destroy`. cvars `hdGrassLight` (1; 3 = test view), `hdGrassWind`,
  `hdGrassWindSpeed` (3), `hdGrassWindDirection` (45), `hdGrassWindStrength` (1), `hdGrassReact` (1),
  `hdGrassReactRadius` (2), `hdGrassInfo`. Not ported: the donor's `hdGrassBuffers` / `hdGrassKeep` (buffer filling, not
  the look). `hdGrassShadow` (0.5, ours): with Forever lighting the shadow cut the sun fully and left a scaled-down
  ambient, so grass under trees went near-black; shaded grass now keeps this share of the sun (0 = the donor's look).
- **Pixel c14/c15 are taken on Vulkan**: `CGxDeviceVulkanDraw.cpp` overwrites them with fixed-function fog on every draw.
  The donor's grass and building flags lived in c15 (the fog's green turned on the test view: black grass); both now use
  c16 like the terrain, which each sets before its own draws. Never give a ported shader a constant in pixel c14/c15.
- **Sky** (`HdSky.cpp`, built and installed 2026-10-29, **not seen in game yet**): Forever's dnsky / dnclouds as 4 new
  shaders (made in the device's shader list, bound with `GxRs_VertexShader/PixelShader` inside `DNSky::Render` /
  `DNClouds::Render`; `RsPop` unbinds). Differences from the donor: pixel constants at c20..c25 (not c200), and the dither's
  pixel position is rebuilt from the clip position (a TEXCOORD) because the translator has no `VPOS`; c25.xy = half the
  target size. cvars `hdSky` (1; 2 = 3.3.5 look, must equal 0; 3 = test view), `hdSkyGlow`, `hdSkyGlowHorizon`,
  `hdSkyGlowSize`, `hdSkyCloudSun`, `hdSkyDither`, `hdSkyInfo`. The 4 blobs are named `HdSky-SkyVS/SkyPS/CloudVS/CloudPS`;
  all four or none (a missing one goes to `hd-pending`, Blizzard sky kept).
- **Point lights** (`HdLights.cpp`, built and installed 2026-10-29, **not seen in game yet**): the client's own M2 point lights (lamps, fires)
  light the terrain per pixel with Forever's light buffer maths, up to 4 per chunk, in the terrain pixel shader (c16.z count,
  c17..c32 the lights, vertex c53 camera origin); the vertex shader then leaves its 3 per-vertex lights out. cvars `hdLights`
  (1; 2 = light path with no light, must equal 0; 3 = test view, lights only), `hdLightsRadius`, `hdLightsInfo`. Hooks:
  `HD::TerrainVertexConstants(sphere, scene, camera)` from `MapRenderChunk`, `HD::LightsForget` in three places in
  `M2Model.cpp`. Buildings and grass do not get them (the donor does not either).

**Fixes past the donor.**
- *Custom lights:* the donor fades fog and grading out inside any light Forever lacks. This server's own lights (IDs 5000+,
  `patch-E.mpq`) overlapping a Forever light at full weight flipped the fog a step apart (L5000 and Stormwind's L77 at the
  gate). Nearby lights Forever does not have at all are now skipped (`HD::ForeverHasLight`). Checked at the gate.
- *Full shadow casters:* with `hdShadowSun` 1 the shadows are long and reach the view from casters outside the camera's
  frustum, which the client culls; while long shadows are on, every caster of a band is drawn and the shadow scissor is
  off (the donor's `FullCasters`). Costs shadow-pass time.

## forever_data

Grading and fog read WoW Forever's lighting tables, which the donor keeps out of its repo. `Tools/ForeverData/
export_forever_data.py` rebuilds them, read only, from an installed Forever client (`wow_classic_beta`) through
`RetailAssetProbe` in CASC local mode:

    python Tools/ForeverData/export_forever_data.py "D:/Blizzard/World of Warcraft" "G:/3.3.5a - Dev/forever_data"

Build 1.60.1.70124: 626 lights, 282 grading keys over 8 LUTs, 9413 fog layers; 229 lights have fog and 163 grading.
Tables by FileDataID: Light 1375579, LightData 1375580 (`ColorGradingFileDataID`), LightDataGlobalVolumeFog 7492879
(unnamed fields: the donor's "column N" is `Field_1_60_1_69876_N`, flags 22, layer 23). Lights match by ID and map (the
284 IDs both clients have are the same lights). Layouts in `Tools/ForeverData/definitions` (WoWDBDefs); a newer Forever
build may need newer ones. Without `forever_data` beside `Wow.exe`, Grade and Fog do nothing.

## Building and shaders

- Material shaders: the client compiles the donor HLSL with the system `d3dcompiler_47` and swaps it into the engine's
  `CGxShader` (`HdShaderSwap`); unknown code goes to `<VulkanShadersRetail>\hd-pending\<name>.bin`. Then
  `python -I Tools/HdMaterials/install_hd_shaders.py --client "G:/3.3.5a - Dev" --glslang <abs path>/build/tools/glslang/bin/glslang.exe`
  (absolute glslang path, Windows form) translates them into both shader sets. Offline compiles are byte-identical, so
  blobs can be compiled ahead (scratch scripts `hd_compile*.py`: regex out the `R"HLSL(...)` pieces, D3DCompile via
  ctypes with the same macros and `D3DCOMPILE_OPTIMIZATION_LEVEL3`) and dropped into `hd-pending` before running it.
  Debug a translation with `Tools/VulkanShaders` `bls.translate.main([... '--names', 'X:0', '--extra-pixel', dir, '--print'])`.

- Client: `cmake --build build/retail-satchel --config Release --target WowClient`, then copy
  `bin\Win\WowClientRetailInventory.exe` over `G:\3.3.5a - Dev\Wow.exe` (back it up first, game closed).
- Shaders: GLSL in `HD/Shaders/` (`Grade`, `Copy`, `Bloom`, `Fog`, `AO` + `XeGTAO.glsl`, `ContactShadow`, `GI`), compiled
  into the committed `Spirv.inc` (28 modules) by `python Compile.py <glslang.exe>`; an entry's extra fields are `-D`
  defines (one source, several passes). glslang is not installed; 16.6.0 from the KhronosGroup GitHub release works.
- `RetailAssetProbe` (for the exporter): `cmake --build build/retail-satchel --config Release --target RetailAssetProbe`.

## Known limits

- Contact shadows are screen space: only what is on screen casts, and thin objects are taken as 1 yard thick.
- Not ported: G-buffer normals for AO (`hdAONormals`; no G-buffer here), the Cata water plane in the fog, the donor's
  D24 compare paths (this backend's shadow maps are R32F).
- `lumenveilTime` only applies in visuals 1; use `hdTime` in mode 2.
- Not compared against the donor's own screenshots: our `forever_data` is rebuilt, not the donor's files.
- Licence: the donor files carry `GPL-3.0-or-later`, Lumenveil's `Core` is GPL-2.0. Decide before shipping both.

## Next

1. Check `hdGrassShadow` in game (Goldshire road under the trees, `hdGrassLight 1`), then commit buildings + grass.
2. Check `hdLights` in game (a lamppost at night: `hdLights 3` shows only the lights; `hdLights 2` must equal `hdLights 0`).
3. **Water**: Forever PBR (`HdWater*`) or Cata water (`HdCata*`, `hd/Cata*`). Largest: the Cata mirror replays the frame
   inside the donor's backend, and Cata water needs its `patch-w.mpq`. Lumenveil's water hook in `Liquid::CRenderer::Flush`
   is the likely entry point.

## Why not swap the backend

Could the donor's `wxl/` backend replace ours for every mode? Not wholesale: `GxEffects` (so all of Lumenveil) sits on our
backend's internals (frame, depth copies, `pipelines::ForgetBound`), our backend carries Retail work the donor lacks
(`VulkanShadersRetail`, Retail model and mount fixes, unit outline, loot beam), and about 277 shared files differ. A spike
(a CMake option building `wxl/` and `hd/` instead of `Vulkan/`, Stock only) would size it; it is only worth it for the
donor's performance work (depth pyramid, doodad batching).
