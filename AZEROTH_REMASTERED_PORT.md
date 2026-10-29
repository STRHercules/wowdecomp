# Azeroth Remastered: visual packs and the Visuals switch

Updated: **2026-10-29**. Donor: `R:\Users\Zach\Documents\GitHub\Azeroth-Remastered` (one squashed commit, `4929a78`).

## Done: the switch

- `visuals` cvar (saved in Config.wtf): 0 Stock (Vulkan), 1 Lumenveil (default), 2 Azeroth Remastered. `WoW/Source/Ui/Visuals.h`;
  registered in `Lumenveil::Startup`.
- `Lumenveil::Enabled()` also needs `visuals == 1`, so Stock and Azeroth Remastered turn Lumenveil off whole (its own
  `lumenveil` check keeps working inside that mode).
- The dropdown is `WowDecompVisualsDropDown` on the stock Effects page (`WoW/Source/UIUtil/DistanceOptions.cpp`,
  `visualsScript`), in the empty cells left of Player Textures. It is a stock-style control: Okay applies, Cancel reverts,
  Defaults sets 1.
- Not checked in game (build only): the dropdown's position and its Apply/Cancel behaviour.

## Not done: the Azeroth Remastered renderer

Mode 2 draws the stock picture today. The donor is not a patch on this tree; it is a different Vulkan backend with the
effects written against it:

| Donor part | Size | Why it does not drop in |
|---|---|---|
| `Engine/Source/Gx/CGxDeviceVulkan/wxl/` | ~170 files | its own Vulkan renderer (stage chain, present, pipelines); ours is `Vulkan/` + `GxEffects` |
| `.../hd/HdVulkan{Bloom,AO,Fog,Grade,Shadows,ModelLights}.cpp` | ~1.7k lines | **compute** shaders on storage images, registered as wxl stages; `GxEffects` is fragment-only |
| `.../hd/Cata*.cpp`, `hd/optim/` | ~3k lines + ~3.6k shader files | Cata water and mirror, grass, terrain4: replace the client's own draws |
| `WoW/Source/WorldClient/HD/*` | ~11k lines | HD logic (fog, sky, water, GI, lights, time) that feeds the above; touches `Game/`, `Object/`, `Ui/` files |
| `Interface/AddOns/WaystoneHD`, `Tools/Pack/Reglages-HD.txt` | small | the cvar names (`hdBloom`, `hdFog`, `hdAO`, `hdGrade`, `hdWater`, ...) and their tuned defaults |

## Two routes to run the donor's effects

Asked 2026-10-29: could the donor's Vulkan backend simply replace ours (for Stock and Lumenveil too)? Not wholesale:
`GxEffects` (so all of Lumenveil) sits on our backend's internals (frame, depth copies, `pipelines::ForgetBound`), our
backend carries Retail work the donor lacks (`VulkanShadersRetail`, Retail model and mount fixes, unit outline, loot
beam), and about 277 shared files differ (core Gx device files included). Two steps instead, in this order:

1. **Add compute to our `GxEffects`** (recommended first). A compute pass API beside the fragment one: storage images,
   compute pipelines, push constants, barriers between passes, `Format` and usage flags for storage targets, and a
   format check like the donor's (`SAMPLED | STORAGE | LINEAR`). The donor's `HdVulkan*` compute shaders and `HD::*`
   formulas then port nearly as they are. Stock and Lumenveil are untouched.
   **Landed 2026-10-29; runs in game (AMD, driver 26.10.1, through Grade's `hdGradeDebug` divider):** `GxEffects::Compute` + `Device::Dispatch`,
   `CreateStorage` (a target with STORAGE usage), `CanStore`. Interface in `GxEffects.h`: samplers 0..15, constants 16,
   storage images 17..24, push constants up to 128 bytes. Porting a donor shader means renumbering its bindings and
   naming the format of each image. Not covered: writing the world target directly (BGRA8 has no GLSL image format), so
   an effect copies it with `CopyColor` into an RGBA16F storage texture, runs, and draws the result back with a pass.
2. **Spike the donor's backend alongside ours.** A CMake option (default off) that builds `wxl/` and `hd/` in place of
   `Vulkan/`, run Stock only, to see whether it renders at all and what breaks. Not read yet: how wxl plugs into the
   donor's device layer (`GxSeam`, `VkHostLinkApi`), so the size of this spike is a guess. Decide on a swap only after it,
   and only if its performance work (depth pyramid, doodad batching) is worth re-fitting Lumenveil and the Retail work.

## HD world logic

- **HdTime (2026-10-29, works in game):** `HD/HdTime.cpp`, all gated on visuals 2, the donor pack's defaults.
  `hdTime` (-1, or 0..24 to hold the hour on this screen: the time override that works in mode 2, where
  `lumenveilTime` does not) in `CGWorldFrame::UpdateDayNight` and the skybox animation minutes; `hdSunPath` (1, the sun
  where the world light comes from by day) in `DayNight` `SetPlanets`; `hdShadowSun` (1, shadows at the light's real
  height) in `CShadowQuery::Update`; `hdShadowBillboard` (1, tree leaf cards face the sun in the shadow pass) with the
  donor's billboard block in `ShadowQuery.cpp`.
- **Custom lights (2026-10-29):** the donor fades fog and grading out inside a light Forever lacks. This server's own
  lights (IDs 5000+, from `patch-E.mpq`) are such lights, and where one overlaps a Forever light at full weight (L5000
  and Stormwind's L77 near the gate) the result flipped with their nearest-first order: no fog, then full fog, a step
  apart. Nearby lights Forever does not have at all are now skipped (`HD::ForeverHasLight`); lights it has without fog
  or grading still fade them out, as the donor's. Checked at the gate: the fog stays. `hdFogInfo` lists the lights, settings and weights the fog came from.
- Next, as surveyed: contact shadows and GI as GxEffects passes (D3D9-only in the donor); then the material shaders
  (terrain, WMO, grass, sky, grass wind, point lights: the donor injects HLSL into the engine's shaders and runs it on
  Vulkan through offline-translated modules, `HdGxShader`); water last (Forever PBR or Cata water, its mirror replays the
  frame inside the donor's backend, and Cata water needs its `patch-w.mpq`).

## forever_data

The donor's `forever_data` and the scripts that made it are untracked in its repo (`/forever_data/`, `/forever_tools/` in
its `.gitignore`). `Tools/ForeverData/export_forever_data.py` rebuilds it from an installed Forever client
(`wow_classic_beta`, read only, through `RetailAssetProbe` in CASC local mode):

    python Tools/ForeverData/export_forever_data.py "D:/Blizzard/World of Warcraft" "G:/3.3.5a - Dev/forever_data"

From build 1.60.1.70124 (2026-10-29): 626 lights, 282 grading keys over 8 LUTs, 9413 fog layers; 229 lights have fog and
163 grading (the Eastern Kingdoms map light, ID 1, has both). With it in the dev client the picture changes in game
(visuals 2, 2026-10-29): Grade and Fog draw; not yet compared against the donor's own data or screenshots. Tables by FileDataID: Light 1375579, LightData 1375580
(`ColorGradingFileDataID`), LightDataGlobalVolumeFog 7492879 (unnamed fields; the donor's "column N" is
`Field_1_60_1_69876_N`, flags 22, layer 23, and the value ranges agree). Lights match by ID and map: of the 284 IDs both
clients have, all are the same lights (50 with retuned falloff radii). Layouts in `Tools/ForeverData/definitions`
(WoWDBDefs); a newer Forever build may need newer ones.

## Suggested order

1. Post stack over `GxEffects`: Grade, then Bloom + sun rays, then Fog, then AO. Each stays a compute shader (`Dispatch`), with
   its bindings renumbered; the `HD::*` formulas and the cvars come over unchanged. Gate every one on `Visuals::Current() == AzerothRemastered`.
   **Grade landed 2026-10-29; its whole path runs in game (the `hdGradeDebug` divider shows), real grading not seen for lack of data:** `WoW/Source/WorldClient/HD/HdGrade.cpp`, called from
   `CGWorldFrame` after `PlayerNameUpdateLate` (the donor's spot). Shaders in `HD/Shaders` (`python Compile.py
   <glslang.exe>` regenerates the committed `Spirv.inc`; glslang 16.6.0 from the KhronosGroup release works).
   `HdForever.cpp` holds only the light table and grading keys so far. cvars: `hdGrade` (1), `hdGradeStrength`,
   `hdGradeDebug`, `hdGradeLut`, `hdGradeInfo`, `hdForeverForce`, `hdForeverInfo`. **No grading data on this machine:**
   the donor ships `forever_data\hd_forever_light.txt`, `hd_forever_grading.txt` and `grading\<id>.blp` (1024x32
   uncompressed BLP2) beside the exe, untracked in its repo. Without them every light gets the identity LUT and the
   picture is unchanged; `hdGradeDebug 1` still runs the whole path and shows a white divider down the centre.
   **Bloom + sun rays landed 2026-10-29; halo and rays work in game (the `hdBloomDebug` sun dot lands on the sun disc, a few pixels low and right):** `HD/HdBloom.cpp`, before Grade, with
   `HD::Bloom::OpaqueDone()` beside Lumenveil's to take the projection and viewport for the rays. `Bloom.comp` makes six
   modules (`BLOOM_PASS` 1..6 via `Compile.py`'s third entry field); the donor's pass 0 (half-size copy) is a scaled
   `CopyColor`, its depth read is `CopyDepth`. cvars: `hdBloom` (1), `hdBloomStrength`, `hdBloomThreshold`, `hdRays` (1),
   `hdRaysStrength`, `hdBloomDebug` (1 ray mask, 2 rays, 3 halo; a red dot marks the sun), `hdBloomInfo`. Needs no data.
   The four effects share one RGBA16F frame (`HD/HdFrame.cpp`): bloom and grading, back to back, share one copy and one
   draw back (3 copies a frame instead of 4, one full-size texture instead of 4).
   **Fog landed 2026-10-29; its debug views (distance with the sky in red, raw depth) look right in game, the fog itself unseen for lack of data:** `HD/HdFog.cpp`, called before `SpellVisualsTick` (the donor's
   spot: after the liquids and weather, before spells and particles). `Fog.comp` is the donor's shader with bindings
   renumbered, as four modules (`FOG_PASS` 0 single pass for debug views 1/2/4; 1 march, 2 and 3 depth-aware blurs and
   composite). The camera (projection, view, viewport) now comes from `HD::CaptureCamera()` (`HdCamera.cpp`) beside
   `Lumenveil::OpaqueDone`, shared with Bloom. `HdForever.cpp` loads `hd_forever_fog.txt` too. cvars, the donor pack's
   defaults: `hdFog` (1), `hdFogDensity` (3), `hdFogSun` (1), `hdFogAmbient` (4.75), `hdFogDebug` (1 distance, 2 depth,
   3 fog alone, 4 sun shadow), `hdFogInfo`. **Left out:** the client's sun shadow maps in the fog (`hdFogShadow`, the
   light shafts) at first; since 2026-10-29 `GxEffects::Binding::engine` samples an engine texture as it is, and the fog
   binds the three sun shadow bands (R32F colour targets in this backend: the shader's raw-depth path, no comparison
   sampler needed) with `s_shadowMatrices[3..11]`, under the donor's conditions (`hdFogShadow` 1, shadow mode above
   Medium). Checked in game 2026-10-29 with the shared frame: all four effects still draw, the shafts work. The Cata water plane (stop the ray at the water) waits for
   the water port. Like Grade, no fog without `forever_data`; `hdFogDebug 1` and `2` run without it.
   **AO landed 2026-10-29; works in game (visuals 2, with the temporal pass):** `HD/HdAO.cpp`, Intel's XeGTAO as the donor ported it, called
   right after `HD::CaptureCamera()` (after the opaque models, the donor's AfterOpaqueWorld point). `AO.comp` + `XeGTAO.glsl`
   (MIT) make nine modules: prefilter, mip, the main pass at four qualities, blur, temporal, apply. Each pass binds only
   the depth levels it reads (GxEffects refuses an image that is also sampled); the AO/edges images are RGBA16F (no RGBA8
   storage). The temporal pass reprojects with an absolute view made from the engine's camera-relative one and
   `DayNight::g_dnInfo.m_cameraPos`, as Lumenveil does. cvars, the donor pack's: `hdAO` (1), `hdAORadius` (1),
   `hdAOStrength` (0.85), `hdAOQuality` (1), `hdAOTemporal` (1), `hdAOHalfRes` (0), `hdAODebug`, `hdAOInfo`. Left out:
   G-buffer normals (`hdAONormals`; there is no G-buffer here), so normals come from the depth. Needs no data.
2. The `WorldClient/HD` logic those passes read (sun, fog, time), without the parts that need Cata water.
3. Water (`HdWater*`, `Cata*`): a hook point in `Liquid::CRenderer::Flush`, as Lumenveil's water uses.
4. Shadows, GI, grass and terrain: only if 1-3 hold up.

Licence: the donor files carry `GPL-3.0-or-later`; Lumenveil's `Core` is GPL-2.0. Decide before shipping both.
