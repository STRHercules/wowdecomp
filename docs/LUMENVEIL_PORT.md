# Lumenveil port to the Vulkan client: handoff

Updated: **2026-10-28**. Source mod: [Lumenveil](https://github.com/lumenveil/Lumenveil) 0.1.1, local checkout
`R:\Users\Zach\Documents\GitHub\Lumenveil` at `8e9a74ee2001390ec413097fa88ecff13832f391`. It is a D3D9 `version.dll`
mod for the stock 3.3.5a client; this port runs it natively in our source-built x64 client, on the Vulkan backend.

**Nothing is committed.** The working tree also has unrelated changes from other work (EsteriaInventory, SFile2-Core,
BusyNotice, CascSource, RetailAssets, Unit_C, MountOverrides, MountStudio, the other `*.md` plans). Commit only the
files listed in [Files](#files).

## Status

| Phase | What | State |
|---|---|---|
| 1 | Hooks, frame inputs from source, settings and zone looks, GxEffects pass layer, depth copy | **Done, verified in game** (depth view lines up) |
| 2 | Fog march, temporal, composite, sun/moon shafts, shaft memory, glow, sky reference, banks, clouds, terrain shadows | **Done, verified in game** (fog shows; shaft streaks with `DebugView=1` at 06:30) |
| — | `lumenveilTime` clock override (region panel's clock, without the panel) | **Done, verified** |
| 3a | Night lights, static part: lamps, windows, halos, surface light, visibility, blur, tiles, belt lantern, corpse lights; `lampdata.bin` | **Done, user reports it working in game** |
| 3b | Spell (effect) lights: baseline after the liquids, gather (two passes, not MRT), reduce, async readback, tracker. Water lamp records are Phase 4 | **Done, user reports it working in game** |
| 4 | Water: depth, colour, waves, reflections, rain rings, ripples, water memory, water lamps | **Done, user reports it working** (rain rings unseen) |
| 5 | Wet surfaces done (unconfirmed in game); relief (parallax) and terrain materials not started (they ship off in the mod) | **Partly done** |
| 6 | Settings as Video options pages: **done, user confirms**. Hotkeys, region panel, weather override: not started | **Partly done** |

Deployed: `G:\3.3.5a - Dev\Wow.exe` is the Phase 3b build. Backups of the exe before each step are in `G:\3.3.5a - Dev\_backups\lumenveil-*` (the Phase 3a build is in `lumenveil-spelllights-20261028-201223`, the Phase 2 one in `lumenveil-lamps-20261028-200328`).
`CoAVolFog.ini` and `CoAVolFog-regions.ini` (from `Lumenveil\package`) and `lampdata.bin` (76308 lamps, 12272 windows, made
with the mod's `lamp_extract.exe` built from its `engine/tools/lamp_extract` against the dev client's Data folder, 133 s:
`lamp_extract --data "G:/3.3.5a - Dev/Data" --out lampdata.bin --locale enUS --background`) are in the client folder.
To test: go to a lit town (Stormwind) at night (`lumenveilTime 22:00`); look for lamp halos, light pooling on the ground and
windows. Log lines `lamp halo targets`, `lamp light target`, `lamp tiles` appear in `Logs\Lumenveil.log` on first draw.

## How it fits together

```
WoW/Source/Ui/WorldFrame.cpp        CGWorldFrame::RenderWorld / OnWorldRender call Lumenveil:: at the six places
                                    the mod patched (FrameBegin/End around OnWorldRender, OpaqueBegin/Done around
                                    M2PASS_0, LiquidBegin/End around RenderLiquidPass, WorldDone before FFX::Finish)
WoW/Source/Ui/LumenveilInputs.cpp   engine::ReadClientWorld and the rest of the mod's client reads, from our own
                                    globals (DayNight::g_dnInfo, CWorldScene, World::s_weather, ObjMgr, units)
Engine/Source/Lumenveil/
  Lumenveil.h/.cpp                  the controller (the mod's hooks.cpp + dllmain.cpp): cvars, ini, regions,
                                    opaque state, depth copies, calls the renderer
  Renderer.h/.cpp                   the mod's renderer.cpp fog path, over a small D3D9-style State facade
  Core/                             the mod's platform-independent files, kept verbatim (GPL-2.0, see Core/README.md),
                                    plus Core/shaders = the mod's HLSL, unmodified
  Shaders/Compile.py                translates the mod's SM3 HLSL for glslang's HLSL front end, writes Spirv.inc
Engine/Source/Gx/GxEffects.h        backend-neutral full-screen/point pass API; GxEffects::Get() is null off Vulkan
Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Effects.cpp   its Vulkan implementation
```

### GxEffects (the pass layer)

Records into the frame's main command buffer *outside* the engine's render pass (`frame::Outside()`), in its own
colour-only render passes that load/store from `SHADER_READ_ONLY_OPTIMAL`, the layout every backend colour target
rests in. Per frame slot: a descriptor pool (512 sets) and a 1 MiB uniform ring (4 KiB per draw, so ~256 draws/frame).
Set 0: bindings 0–15 combined image samplers, 16 the constant block (`vec4 c[256]`), visible to vertex and fragment.
Supports: 2D targets (BGRA8, RGBA16F/32F, R32F, RG16F/32F, R8), mipmapped filled 2D and 3D textures, depth copies
(`Format::Depth`, the frame's depth format, D32_SFLOAT on the dev machine), six blend modes incl. MAX, write masks,
clear-on-begin, point lists with a vertex program (`points`), blocking `Read` (4-byte texels only), `CanBlend`.
Empty slots read (0,0,0,1) like D3D9. Destroyed objects retire through `frame::RetireRaw/RetireImage`;
`effects::Shutdown()` runs in `CGxDeviceVulkan::IDestroyVulkan`.

Backend changes for it: depth buffers gained `TRANSFER_SRC` (`Frame/Targets.cpp`); `frame::CopyTargetDepth`,
`CopyFromTarget`, `CopyFromImage`, `RetireRaw` (`Frame.hpp`, `Frame/Readback.cpp`, `Frame.cpp`);
`R32G32_SFLOAT` in `Texture/Registry.cpp`.

### Shader translation (Shaders/Compile.py)

The mod's HLSL is compiled by `build/vulkan-tools/glslang-16.6.0/bin/glslang.exe` with
`-D -V --hlsl-dx9-compatible --hlsl-iomap` after these rewrites:

- includes expanded (each file once);
- every `register(cN)` global moves into one cbuffer at binding 16 with `packoffset(cN)` (verified: c9 → byte 144,
  c32 → 512); `static const int` lines move above it (array sizes use them);
- `register(sN)` → binding N, set 0;
- `main(float2 x : VPOS)` → `SV_Position - 0.5` (D3D9 pixel centres are at integers);
- vertex shaders (point lists): grid position from `SV_VertexID` and `lvGrid` at **c255** (renderer sets it),
  `POSITION` → `SV_Position`, `PSIZE` → `[[vk::builtin("PointSize")]]`, `--invert-y`, and
  `MemoryTexelPosition` shifted half a texel (the script stops if that function changes upstream).

Run after changing a shader: `python Engine/Source/Lumenveil/Shaders/Compile.py "R:/Users/Zach/Documents/GitHub/wowdecomp/build/vulkan-tools/glslang-16.6.0/bin/glslang.exe"`
(forward slashes; it writes `Spirv.inc` and `DebugSpirv.inc`, which are committed). Inspect a module's bindings with
`glslang ... -H` and grep `Decorate`.

### Things learned the hard way

- The engine's projection is **GL-style** (z −1..1) and the device maps it onto the viewport's z range
  (`MapWorldDepth` in Renderer.cpp). View depth = `_43 / (d − _33)` with both terms negative; clamp the divisor
  negative, not positive (that was the flat-white depth view).
- Most of the screen is within ~100 yd of a 2000 yd far clip; linear depth views look white.
- Cvars registered with flag 0 are still saved to `Config.wtf` once set from the console; debug cvars are reset to
  their default in `Lumenveil::Startup`.
- Elwynn (zone 12, "green") gets fog ×0.2 from the shipped settings; daytime shafts there are faint by design. Test
  shafts with `lumenveilTime 06:30` facing the sun and `DebugView=1` in the ini.
- Installing needs the game closed; the agent may not kill the user's `Wow.exe`.
- In this environment long inline Python heredocs through Bash get mangled by the shell hook, and `grep` is rewritten
  to `rg` (escaped `\|` and `(` break). Write edit specs to the scratchpad and use the Grep tool.

## Settings and debug controls

- `CoAVolFog.ini` / `CoAVolFog-regions.ini` next to `Wow.exe`, reloaded within 1 s of a save. Every key the mod has is
  read; `FarClipMax` is ignored (our `World::ValidateFarClip` already reaches `World::kMaxFarClip`).
- Cvars: `lumenveil` (master, saved), `lumenveilDepthView` (debug; reset to 0 at start; also logs a 4×4 depth probe
  every 3 s), `lumenveilTime "HH:MM"` (hold the time of day on this screen; empty = server; not saved).
- `DebugView` in the ini: 1 fog radiance, 2 transmittance, 3 linear depth, 4 surface shadows (5–8 need later phases).
- Logs: `Logs\Lumenveil.log` (frame summary once a minute and on every sun/moon swap, with a `shafts:` line),
  `Logs\gx.log` (`vulkan: effects: ...`).

Build and install: `cmake --build build/retail-satchel --config Release --target WowClient`, then copy
`bin\Win\WowClientRetailInventory.exe` over `G:\3.3.5a - Dev\Wow.exe` after backing it up (see memory
`deploy-to-dev-client`).

## Known gaps in what is done

- **Client shadows, late-draw and sun-and-moon captures** (composite stages 9–11) are bound empty: the mod finds them
  from its D3D9 draw log (`draw_log.cpp`, `d3d9_wrap.cpp`). Natively these become direct captures at the right points
  in `OnWorldRender` (sun/moon in `DayNight::RenderGlares`/sky, late draws = after `WorldDone`).
- **Weather override** (region panel) is read-only: `GameWeather::Set` returns false (shortcut comment in
  `LumenveilInputs.cpp`).
- **Depth probe** (`DepthProbe.frag`) and `lumenveilDepthView` are port debugging aids; keep or drop.
- `liquiddata.bin` / `fogdata.bin` are not generated yet (`lampdata.bin` is; liquids: the same tool with `--liquids liquiddata.bin --maps 0,1,530,571`); banks treat all ground as land.
- Terrain shadow and bank floor maps (RG32F, RGBA32F) are sampled linearly; fine on desktop GPUs, not checked with
  `VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT`.
- No automated tests. The mod's `engine/tests/harness.cpp` drives the D3D9 DLL and does not port as is.

## Work left

### Phase 3: night lights (3a and 3b are done, see Status; the notes below are the original plan)
Done in 3a: `lamp_selection`, `lamp_tiles`, `lamp_catalog`, `corpse_lights`, `effect_lights.h` (vendored in `Core/`), shaders `vf_lamp_*`/`vf_lamps` (`Compile.py`), `LumenveilRenderer::RenderLamps` (Renderer.cpp, after the temporal pass), composite stages 6/7 and constants c17/c18+. Not checked: the shaders were only compiled and the build linked; nothing has been looked at in game. The belt lantern's pivot is now used for the shaft body.
The original Phase 3 notes follow; the parts for 3a are crossed out in practice.

Mod sources: `lamp_selection.*`, `lamp_tiles.*`, `lamp_catalog.*`, `lamp_data.h`, `belt_lantern.*` (vendored),
`effect_lights.*`, `corpse_lights.*`, `water_lamps.*`; renderer functions `EnsureLampTargets`,
`EnsureLampSurfaceTarget`, `UploadLampTable`, `UploadLampTiles`, `BlurLampLight`, `RenderLamps` (renderer.cpp
~1610–2070), effect lights `EnsureEffectShaders` … `UpdateEffects` (~2071–2330); shaders `vf_lamp_*`, `vf_lamps`,
`vf_effect_gather` (**two render targets, COLOR1**: GxEffects needs MRT), `vf_effect_reduce`.
- `lamp_selection.cpp` / `lamp_tiles.cpp` use D3D9 types; port their texture uploads to GxEffects.
- Effect lights compare the picture before/after the client's glowing effects: capture a baseline after the liquids
  (the mod's `CaptureOccluderDepth` + `EnsureEffectBaseline`) and gather after `WorldDone`; readbacks are async in the
  mod (`EffectReadback` ring with queries) — GxEffects only has a blocking `Read`, so add an async readback.
- Port `engine/tools/lamp_extract` (libmpq/zlib/bzip2, ~4k lines) to read our MPQs/CASC, or run the mod's own
  `lamp_extract.exe` once against the dev client's Data folder and ship `lampdata.bin`/`liquiddata.bin`.
- Fill composite stages 6/7 and the lamp constants (c17–c20) that Renderer.cpp currently zeroes.

### Phase 4: water
Hooks: liquid material draws (the mod patches the `CMaterial*Water` vtables and `Liquid::CRenderer::Flush` at
0x008A2240, our `WoW/Source/LiquidSystem/LiquidRenderer.cpp:111`); `BeginWater`/`EndWater`/`DrawWater`
(renderer.cpp ~3214–4300); shaders `vf_water*`. Needs: **stencil** (`kWaterStencilBit`, the client marks water
pixels) or an equivalent water mask, `tex2Dgrad` (fine in glslang), MRT for `vf_water_waves` (COLOR1), anisotropic
sampling.

### Phase 5: wet surfaces and relief
`RenderWetSurfaces` (~1703) is a post pass and ports like the fog. Relief and terrain materials **rewrite the client's
D3D9 pixel shaders** (`shader_splice.cpp`, `terrain_relief.cpp`, `runtime_heights.cpp`); our Vulkan client uses
offline-translated shaders (`Tools/VulkanShaders`), so these need a new design: either extra translated variants with
the relief code, or a G-buffer-ish post pass. Both ship off in the mod.

### Phase 6: windows and controls
`settings_panel.cpp`, `region_panel.cpp`, `overlay.cpp` are Dear ImGui over D3D9. Options: ImGui with a Vulkan
backend inside GxEffects, or rebuild them as FrameXML/Lua over cvars (fits the Esteria options pages). Also the
hotkeys (Ctrl+F12 toggle → `PressToggleKey`, Ctrl+F7, Ctrl+F3, Ctrl+F5 spots) and `GameWeather::Set` (our
`Weather` setter, the mod's 0x007846A0 path).

### Before committing
- Decide whether the debug cvars stay.
- Licence: `Engine/Source/Lumenveil/Core` is GPL-2.0; a distributed client with it must offer its source.

## Files

New: `Engine/Source/Gx/GxEffects.h`, `Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Effects.cpp`, `Effects.hpp`,
`Engine/Source/Lumenveil/**` incl. the lamp files in `Core/` (CMakeLists, Lumenveil.*, Renderer.*, Core/**, Shaders/**),
`WoW/Source/Ui/LumenveilInputs.cpp`, this file.

Changed: `CMakeLists.txt`, `Tools/WowClient/CMakeLists.txt`, `WoW/Source/CMakeLists.txt` (the Lumenveil library),
`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp` (effects shutdown),
`Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame.cpp`, `Frame.hpp`, `Frame/Readback.cpp`, `Frame/Targets.cpp`,
`Texture/Registry.cpp`, `WoW/Source/Ui/WorldFrame.cpp` (six call points), `WoW/Source/Client_CVar.cpp`
(`Lumenveil::Startup`), `WoW/Source/Game/ClientGameTime.cpp` (`ClientGameTimeChanged`).

## Phase 3b notes

- `LiquidEnd` (Lumenveil.cpp) calls `LumenveilRenderer::CaptureEffectBaseline` (colour copy of the viewport, only when
  `effectLight > 0`); `RenderLamps` compares it with the scene at fog time (`m_sceneCopy`), runs the gather and reduce,
  and `UpdateEffects` turns the readback from a frame or two earlier into lights (`Core/effect_lights.cpp`, verbatim).
- GxEffects has no MRT: the mod's gather writes COLOR0 and COLOR1, so `Compile.py` builds it twice (`EFFECT_OUTPUT` 0 and 1;
  a wrapper entry returns one half) and the renderer draws it once for each target. That doubles the gather's 64 taps per
  texel; real MRT would halve it.
- New `GxEffects::Device::BeginRead/PollRead/CancelRead` (Effects.cpp) over `frame::BeginImageRead/PollImageRead/
  CancelImageRead` (Frame/Readback.cpp): records the copy in the open frame, polls `frame::Completed()`.
- Not checked in game: cast spells at night and look for light on the ground and nearby surfaces; the log has `effect light:`
  lines on the first gather. There is no per-minute effects summary yet (the mod's `LogEffects`).

## Screen-space sun/moon shadow (not in the mod)

- Why: Lumenveil's surface shadows come from a terrain height map, so buildings, statues and characters cast nothing from the
  sun or moon; the mod left those to the client's shadow maps (client_shadows.cpp, not ported). With the client's shadows
  off (they fought the new lighting: hard blocks, double darkening) objects cast no moon shadow.
- What: `Core/shaders/vf_light_shadow.hlsl` marches the depth buffer toward the light (the lamp shadow test, 20 steps over
  30 yd, `kLightShadowYards`) into a viewport-sized RGBA16F target, smoothed with the lamp blur twice. The composite
  (`vf_composite.hlsl`, edited: c33 and s13) multiplies the scene shade by `SurfaceShadowShade`'s colour where the mask says
  shadow. `LumenveilRenderer::RenderLightShadow`; runs when `SurfaceShadows > 0` and the shade is below 1 (moon or sun high enough).
- Limits: only what is on screen casts; shadows reach 30 yd; where the terrain map also shadows (sun, hills) the two multiply,
  so those spots are darker than either alone. The client's own shadows should stay off.
- Debug cvar `lumenveilLampTiles 0` (lamp tiles off) showed no difference, so tiles were not the cause of the blocky light.

## Phase 4 (water), slice 1: depth through the liquid pass and the water colour pass

- Done: `GxEffects::ForceDepthWrite` (Effects.cpp; read in `CGxDeviceVulkan::IRenderStates`) makes the liquid pass write depth
  (`LiquidDepth=1`; the client draws liquids without depth writes). `Lumenveil::LiquidBegin` turns it on and copies the depth
  and scene before the water; `LiquidEnd` turns it off, copies the depth after (`occluder`), and runs `LumenveilRenderer::DrawWater`:
  the mod's `vf_water.hlsl` (absorption with depth, clarity, refraction by the slopes, deep tint) over the world target.
  Fog now sees the water at its surface, not the bottom.
- The mod only draws where the client marked water in the stencil (bit 0x80). Here `vf_water.hlsl` was edited to draw only where
  the depth after is nearer than before; the slope texture is empty, so there is no refraction by waves yet.
- Not done, in the order I'd do them: (1) waves: `vf_water_waves.hlsl` writes three targets (COLOR0..2, run it three times) into
  textures with 9 mip levels, read with `tex2Dgrad`; GxEffects cannot draw into a mip level or sample a mip chain you built, so
  it needs mipmapped render targets (or a downsample pass that fills each level) first; then `vf_water_slope.hlsl`;
  (2) reflections and glitter (`vf_water_reflect.hlsl`, shaft memory-like water memory `vf_water_memory*`); (3) rain rings,
  ripples (tables as textures, `water_rain/ripples/rings.cpp` are in the mod; copy them to Core); (4) water lamps.
- Only `cfg.waterClarity > 0` is checked as "water wanted"; the zoned config is not used in the water pass (the base config is).

### Phase 4, slice 2: flat-water reflections and glitter

- `vf_water_reflect.hlsl` (the mod's, edited to draw only where depth after is nearer than before) runs after the colour pass in
  `DrawWater`, blended premultiplied: screen-space reflection march over the scene before the water, sky from the screen or the
  zone's horizon-to-zenith gradient, Fresnel, sun glitter, terrain-shadowed glitter. Water memory is off (`cMemoryGrid.w` = 0),
  so what is off screen falls back to the sky gradient.
- Flat water: `m_waterSlopes` is a 1x1 RGBA32F texture, slope (0,0) and the wave field's total roughness (unresolved plus the
  resolved part the missing wave textures would have added), so glitter spreads as the waves would. `water_waves.h/.cpp` are
  in `Core/` now (D3D-free). The next step (wave textures with mips) replaces that texture with the real slope pass.
- Reflection distance is `kWaterReflectionYards` (400). `WaterReflect` and `WaterGlitter` in the ini control strength.

### Phase 4, slice 3: the wave pass

- `vf_water_waves.hlsl` (three outputs; built three times, `WAVE_OUTPUT` 0-2, a wrapper entry returning one) draws each layer's
  9 levels into an atlas texture a layer (256 x 511 RGBA16F, level n from row 512 - 512 / 2^n); the wave table is a 48 x 3
  RGBA32F texture uploaded each frame. `vf_water_slope.hlsl` (edited) reads the atlases with its own wrap and bilinear filtering
  in two levels, blended by the footprint's level of detail (isotropic from the longer axis, so grazing water is blurrier than
  the mod's anisotropic filtering) and draws only where the water surface is nearer than before. Its target is the world's size
  (RGBA16F); the colour and reflection passes read it. If the waves fail, the 1x1 flat-roughness texture is the fallback.
- 27 wave draws + the slope pass + colour + reflection = about 30 GxEffects draws a frame of the 256 the uniform ring allows.
- Still to do: rain rings and ripples (`water_rain/ripples/rings.cpp` and their table textures; the slope shader reads zero
  constants for them now), water memory (reflections of what was off screen), water lamps.
- The log line `water: drawn (clarity ..., waves|flat)` says which path ran.

### Phase 4, slice 4: water memory

- `vf_water_memory.hlsl` (unchanged) updates a pair of 1024 x 1024 RGBA32F octahedral maps (point and packed colour the camera saw in
  each direction; sky stored as negative codes), ping-pong, once a frame the water is drawn: from the scene before the water, the
  previous map moved by how far the camera went (`UpdateWaterMemory`; forgets on a start, map change, a 200 yd jump, a 30 s pause).
  The reflection pass marches it where the screen has nothing, so reflections of what is behind or above you stay when you look down.
- `WaterMemory=1` in the ini switches it on. The log line says `memory` or `screen reflections only`.
- Left: rain rings and ripples (their tables and constants c26-c29 are zero now), water lamps.

### Phase 4, slice 5: rain rings and ripples

- `water_rain`, `water_ripples`, `water_rings` (.h/.cpp, D3D-free) are in `Core/`. The rain drop table (256 x 256 BGRA8) is uploaded once;
  the ripple table (27 x 2 RGBA32F) every frame the player's feet make ripples. Constants c26-c29 and stages 3 (ripples) and 4
  (rain) feed the slope pass, whose shader already had the rings code. They need the wave pass (the slope pass only runs with
  `WaterWaves > 0`). Rain rings show while `weather` is rain (`WaterRain` in the ini); ripples while the player is at the water.
- Left of Phase 4: water lamps (`vf_water_lamps.hlsl`, `water_lamps.*`, needs the lamp table of the frame before; the mod draws them
  additively after the reflections).

### Phase 4, slice 6: water lamps (Phase 4 complete)

- `water_lamps.h/.cpp` are in `Core/`. `RenderLamps` records the lamps and lit windows it drew (`RecordWaterLamps`, spell and corpse
  lights left out); the next frame's `DrawWater` builds a 64 x 3 RGBA32F table from them (`WaterLampsCurrent`: at most 0.25 s old),
  and `vf_water_lamps.hlsl` (edited to draw only on water) adds a glint for each: a Beckmann lobe of the waves' roughness widened by
  the lamp's size, the lamp falloff, an 8-step depth test toward the lamp, gain `0.35 x WaterGlitter`. Added over the reflections.
- Phase 4 is done: depth through the liquid pass, colour, reflections and glitter, waves, water memory, rain rings, ripples, lamps.
  Known differences: no anisotropic filtering of the waves; water uses the base config, not the zoned one; the water pass is only
  drawn when the camera is not under water; lava and slime go through the same pass (the shader leaves them as the client draws
  them only when they do not write depth before and after).
- Next in the plan: Phase 5 (wet surfaces: `RenderWetSurfaces` is a post pass, ports like the fog; relief and terrain materials need
  a new design) and Phase 6 (settings window and region panel). Nothing from any phase is committed.

### Phase 4 gaps closed

- Lava and slime: `Liquid::CRenderer::Flush` tells `Lumenveil::LiquidInstance(magma)` which instances use material 2 (the bank's magma
  material); those are drawn with the client's own depth state, so they stay out of the depth-based water mask. (The mod instead
  draws them with forced depth and leaves them out of its stencil; here fog sees lava at the bottom, as stock.)
- Zoned config: `LiquidBegin`/`LiquidEnd` use `ZonedLook(...)` like `WorldDone` (regions, hours, weather override `WaterReflect` etc.).
- Waves: up to 4 taps along the longer footprint axis, level of detail from the shorter (anisotropic, like the mod's sampler).
- Log: a `water:` line a minute (frames drawn, with waves/memory/lamps/rain, ripples started, skips and the last reason, memory forgets).
- Still different: the mod's water pass is also off when the camera is under water (same here).

## Phase 5 (wet surfaces), slice 1: the wet surface pass

- `vf_wet_surfaces.hlsl` (the mod's, unchanged) draws into a viewport-sized RGBA16F target (`LumenveilRenderer::RenderWetSurfaces`, just
  before the composite, only with the linear scene copy): sheen toward the sun or moon, wetness and puddles from rain or snow
  (`SurfaceWetness`, `Core/wet_surfaces.cpp`), screen-space reflections, lamp light on wet ground, with the stages and the 20 constant
  rows c9-c28 as the mod sets them. The composite adds it (stage 12, `lamps.w` on). Water pixels are skipped through the depth before
  the water (`m_waterKnown`).
- Left out: the ground material tags (grass, soil, stone: they come from the relief and terrain material passes, slices 2 and 3, so the
  pass runs as the mod does with them off) and the scene before the late draws.
- Settings: `SurfaceSheen`, `WetSurfaces`, `Puddles` in the ini. Wetness builds only in rain or snow; the sheen works dry.
- Left of Phase 5: relief (parallax, `terrain_relief.cpp`, `shader_splice.cpp`: they rewrite the client's D3D9 pixel shaders, so this
  client's offline-translated Vulkan shaders need a new design) and the terrain materials; both ship off in the mod.

### Screen-space sun/moon shadow, now with the shaft memory

- The screen-space shadow vanished when its occluder left the screen. `vf_light_shadow.hlsl` now also reads the shaft memory (the
  2048 x 2048 shadow map from the light that remembers what the camera has seen; copies of `SunVisibilityInMemory` and its helpers
  from `vf_march.hlsl`, same registers c45-c52 and stages 3 and 4) at each surface, with a 0.2 yd bias so a surface does not shadow
  itself (`kMemorySurfaceBias`), and takes the darker of the two. Runs when the shaft memory does (`ShaftMemory=1`, light above the horizon).
- Limits: the memory's texels are 0.625 yd (with 4 x 4 sub-texel and slope codes), so remembered shadows are softer and coarser than
  the screen ones; thin things (lamp posts, railings) shadow weakly or not at all from memory. Acne or over-dark patches on surfaces
  are the first thing to look for: raise `kMemorySurfaceBias` if they show.

### Shaft-memory shadow dropped; edge fade and `LightShadow` added

- The shaft memory shadow looked bad in game (blocky, rounded, shadows running together: the map's 0.625 yd texels and 4-texel
  blending are too coarse for ground shadows). It is switched off in `Render` (`false && memory`); the shader code stays for now.
- `LightShadow` (ini, 0-1, default 1) scales how dark the screen-space sun and moon shadow goes; 0 turns it off (`SurfaceShadows`
  still controls the terrain shadows). Not in the mod; add `LightShadow=1` to `CoAVolFog.ini` to see it.
- The march fades an occluder's shadow out over its last 160 pixels before the screen edge (`EdgeFadedShadow`, `kEdgeFadePixels`),
  so a shadow eases away as its occluder leaves the screen instead of snapping off. It does not make the shadow stay.

### Fine shadow memory (replaces the shaft memory shadow)

- `LumenveilRenderer::UpdateFineShadowMemory` is a second shaft memory (same scatter, keep, realign and fill passes and shaders,
  duplicated with its own state in `m_fine`): 1024 x 1024 R32F, 0.15 yd texels over about 75 yd around the camera, filled from the
  depth the camera sees (every 2nd pixel, cycling 4 offsets) and kept as it turns and moves. `vf_light_shadow.hlsl` reads it
  (registers c60-c67, stages 3 and 4, 0.15 yd surface bias) and takes the darker of it and the screen march. The earlier try with
  the shaft memory itself (0.625 yd texels) was blocky and merged; this one is 4 times finer.
- Runs while the light is up and `LightShadow` and `SurfaceShadows` are above 0 (it needs the shaft memory's R32F blend check,
  so `ShaftMemory=1`). Limits: shadows only within 75 yd; what the camera never saw casts nothing; depth is kept in eighths of a yard.
- If it shows acne or dark patches on surfaces, raise `kMemorySurfaceBias` in `vf_light_shadow.hlsl`; if shadows have holes while
  moving, the 2-pixel scatter stride (`kFineStride`) is the knob. The log has `fine shadow memory:` on first run.

### Fine shadow memory: switched off (tried, did not work)

- In game it was blocky, dotted behind NPCs (the splat points do not cover every texel, so lit holes show inside shadows) and left
  shadow "footprints" where the player and NPCs had stood (a map that remembers cannot tell moving things from walls). Off in `Render`
  (`false && ...`); the code (`UpdateFineShadowMemory`, `m_fine`) and the shader's lookup stay for reference. The shaft memory
  shadow (0.625 yd) failed the same way, coarser. Sun and moon shadows of objects are the screen-space march again (with the edge
  fade and `LightShadow`).
- What would work: the client's own shadow maps, read in the composite (the mod's "client shadows" capture, unported), so that
  stock shadows shade the ground once, with the proper resolution and without remembering moving things.

### Stock shadows and the screen-space shadow no longer double up

- The client's own shadow maps (`CShadowCache`, three bands of 40, 160 and 640 yd; modes LOWEST none, LOW and MEDIUM characters only,
  MED-HIGH and HIGH buildings and environment too, VERY HIGH cascaded maps) are drawn into the surfaces by the stock shaders, so they
  persist off screen and at proper resolution. When the mode is MED-HIGH or above, `Lumenveil::RenderPasses` calls
  `SetStockObjectShadows(true)` and the screen-space sun and moon shadow of objects is not drawn. At LOWEST to MEDIUM it still is.
  Lumenveil's lamp shadows and the terrain height-map shadows are separate and stay. The log says which case ran (`shadows: ...`).
- Lumenveil's lamp light still scales with the surface colour, which carries the stock shadow, so a lamp lights a stock-shadowed
  stone less: the same behaviour as the screen shadow had.

### Thin occluders no longer cast lamp shadows

- `vf_lamp_shadow.hlsli` (edited, shared by the lamp surface light and the water lamps): when a lamp ray's first hit is an occluder
  narrower than about 0.4 yd on screen (a lamp post's crossbars, chains: the scene is already 0.25 yd farther a half-width out on both sides,
  or above and below), it is skipped and the march goes on (`ThinOccluder`, `kThinOccluderYards`, `kThinFarYards`; first 0.3 and 0.4, then 0.4 and 0.25 when 0.3 left faint stubs). The post itself and
  anything wider still cast. Raise `kThinOccluderYards` to drop thicker things such as railings; lower it to bring thin shadows back.

### Bug fixed: mid-frame post passes broke the engine's next draws

- Symptom: torch flames and weapon glow (transparent effects drawn after the liquid pass) vanished with `LiquidDepth=1`; gone with
  `lumenveil 0` or `LiquidDepth=0`.
- Cause: `pipelines::Record` keeps a `Bound` cache of what it last bound on the command buffer (pipeline, descriptor sets, vertex buffers,
  viewport, scissor, push constants) and skips binds that look unchanged. GxEffects passes bind their own state on the same buffer
  without telling it. The earlier passes all ran at the end of the frame, where the next draws (the UI) happened to rebind; the water
  pass runs mid-world, and the transparent M2 pass after it drew with stale state.
- Fix: `pipelines::ForgetBound()` (Pipelines.cpp), called at the end of every `GxEffects` `Draw`.
- Anything else that records into the frame's command buffer outside `pipelines::Record` has to call it too.

## Phase 6: Lumenveil in the Video options (the settings window, as pages)

- Instead of porting the mod's Dear ImGui settings window, every live setting of the ported config is a page control in the client's own
  Video options, next to the Esteria pages (`WoW/Source/UIUtil/DistanceOptions.cpp` builds those from built-in Lua; its `WowDecompBuildPage`
  helper makes a scroll page with checks and sliders that Okay, Cancel and Defaults handle).
- `Engine/Source/Lumenveil/OptionsPages.cpp`: a cvar per live setting (`lv<IniKey>`, e.g. `lvDensity`; not saved in Config.wtf, so
  `CoAVolFog.ini` stays the single store), with the ported config's own ranges and defaults. A cvar's callback applies the value to
  the running config (`ConfigStore::Apply`); the `lumenveilSave` trigger runs `ConfigStore::Save` (changed keys only, comments kept);
  `lumenveilSync` reloads every cvar from the config. The Lua hooks Okay and Apply to save and the window's OnShow to sync.
- The pages (Lumenveil Fog, Light, Colour, Weather, Water, Advanced) take their sections, labels, formats, help and choice names from
  the mod's settings window through `OptionsMeta.inc`, written by `Tools/LumenveilOptions/GenerateOptions.py` (run it again if the mod's
  window changes; settings it does not show appear under "Other" on the Advanced page with a label made from the key).
- The master switch `lumenveil` is the first control. The generated Lua is also written to `Logs\LumenveilOptions.lua` (a debugging aid).
- Not in the pages: the hotkeys (Ctrl+F12 toggle and the others), lists (zone density, dry and green zones, shader constant ranges) and the
  region panel (per zone, hour and weather looks); edit those in `CoAVolFog.ini` and `CoAVolFog-regions.ini`.
