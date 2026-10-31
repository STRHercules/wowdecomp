# Living Azeroth port: handoff

Status of the native port of `wxl-treewind` and `seyris-living-azeroth` (GPL-3.0) into this client, and what is left:
snow phase 3 first, then the rest of the donor. Branch `Esteria`.

- Donors: `R:\Users\Zach\Documents\GitHub\wxl-treewind`, `R:\Users\Zach\Documents\GitHub\seyris-living-azeroth`
  (its `src/features`, `src/env`, `docs/cdbc-tables.md`, `data/*.cdbc`).
- Commits: `96aaaa2b` (tree wind, world wind, kept grass and density), `bbaf9232` (snow phases 1-2, grass and wind gaps).
  The near-snow smoothing and trench fix in `WoW/Source/WorldClient/SurfaceCover.cpp` came after `bbaf9232`: check
  `git status` and commit it first.

## Done

| Feature | Where | Options page |
|---|---|---|
| Tree and bush wind (wxl-treewind), following the world wind | `Engine/Source/Model2/M2TreeWind.cpp`, hooked in `CM2Model::AnimateMT*` | Esteria Tree Wind |
| World wind: `WindProfile.cdbc`, gusts, shelter under roofs, lee behind walls | `WoW/Source/WorldClient/LivingWind.cpp` | Esteria World Wind |
| Climate: `AreaClimate.cdbc`, blended across area borders; air and ground temperature (lava) | `LivingWind.cpp` (`ResolveClimate`, `AirTemperature`, `GroundTemperature`) | - |
| Lua: `LivingAzeroth_GetVersion/GetWind/GetWeather/GetTemperature/GetHumidity` | `WoW/Source/Ui/GameUI_Scripts.cpp` | - |
| Grass wind and parting (player and 7 nearest units), per-doodad `GroundEffectDoodadWind.cdbc` | `WoW/Source/WorldClient/HD/HdGrassWind.cpp`, `HD/HdDetailDoodad.cpp` (grass shaders in all visuals) | Esteria World Wind |
| Kept grass (baked once, kept on the GPU), memory budget, `groundEffectDist` up to 1024 | `WoW/Source/WorldClient/DetailDoodad.cpp`, `Map.cpp`, `WorldParam_CVar.cpp` | Esteria World Wind |
| Grass density (`GroundEffectDoodadDensity.cdbc`) and far grass thinning | `WoW/Source/WorldClient/GrassDensity.cpp`, `MapChunk.cpp` (`CreateDetailDoodads`) | Esteria World Wind |
| Snow cover phases 1-2 (see below) | `WoW/Source/WorldClient/SurfaceCover.cpp`, `HD/HdSurfaceCover.cpp` | Esteria Snow Cover |

Custom tables are read by `WoW/Source/WorldClient/CustomTable.cpp` (WDBC with string block; archives, then loose files).
Installed in `G:\3.3.5a - Dev\DBFilesClient`: WindProfile, AreaClimate, GroundEffectDoodadDensity,
GroundEffectDoodadWind, GroundMaterial, GroundMaterialSelector, SurfaceCover (all `.cdbc`, from the donor's `data/`).

### Snow as it is now

- Tables: a terrain layer's material from `GroundMaterialSelector` (area chain, map, global; texture, ground effect,
  TerrainType), materials chained by `GroundMaterial.Parent`, the cover per material from `SurfaceCover` (inherit -1).
  Mud and wet ash (`MoistureMin` <= 1) are skipped: no moisture model yet.
- **Far** (`SurfaceCover::DrawAfter`, called right after each chunk in
  `CMapRenderChunk::RenderSinglePassVertexPixelShader`): the chunk drawn again from its own lifted vertices with its
  own index buffer, while the chunk's terrain constants, shadow maps and blend texture are still bound. A skin of at
  most 5 cm (`kFarDepth`): its edges follow the alpha maps per pixel, so a thicker one hangs over the ground. The full
  depth only tilts its normals. Per-vertex layer shares come from `CMapRenderChunk::CreateShaderTexture` read back from
  `s_defaultTex`. Vertex buffers in pools of 256 slots, grown to 4096.
- **Near** (`SurfaceCover::Update` from `CGWorldFrame::OnWorldUpdate`, `SurfaceCover::DrawNear` at the end of
  `CWorldScene::RenderChunksSinglePass`): 192 x 192 cells of 0.25 yd (48 yd) around the player, rebuilt and sent every
  frame (two buffers taken in turn). Per cell: terrain and far height (`OverTriangles` over each chunk's 145 vertices),
  cover share and depth from 65 x 65 nodes per chunk (`ChunkRec`), edges sloping to the ground, soft swells. The
  resting shape is box-blurred (5 cells, twice); trenches (`Stamp`, player and units, relax over `RelaxSeconds`) and
  rims go on after the blur. The outer 4 yd blend into the far mesh; the far pass clips inside the patch (`cNearRect`).
- Shaders: one vertex (SHADOW 0/1) and one pixel shader (SHADOW_MODE 0..3 x PCF) in `HD/HdSurfaceCover.cpp`, compiled
  at start-up, our own `CGxShader` objects (`ShaderCreate` on a name with no file, then `HD::ShaderSetCode`). Lighting,
  fog, shadow and HD lights code is HdTerrain's. Glints and a blue shadow tint on snow.
- CVars: `surfaceCover`, `surfaceCoverDepth`, `surfaceCoverOthers` (sand and ash), `surfaceCoverBrightness`,
  `surfaceCoverBreakup`, `surfaceCoverDistanceLift` (default 0.1), `surfaceCoverNear`, `surfaceCoverTrenches`,
  `surfaceCoverTrenchScale`; status `surfaceCoverInfo` (`/run print(GetCVar("surfaceCoverInfo"))`).

Known limits:
- Lumenveil lamps light by depth-buffer normals (`Engine/Source/Lumenveil/Core/shaders/vf_surface_normal.hlsli`), so
  every triangle shows under them on bumpy geometry. Keep the snow mesh smooth (no detail finer than about 2 yd), or
  blend those normals over a few pixels in Lumenveil (changes every lamp-lit surface).
- The near patch takes no HD point lights and no baked map shadows (no blend texture there).
- Snow only draws on the vertex-shader terrain path (`CMap::enableTerrainShaderVertex`), which the Vulkan client uses.
- Cells near the player under a chunk never drawn yet (behind the camera) get cover a frame or so after it is first
  drawn.

## Snow phase 3: done except deep far snow

Built in `WoW/Source/WorldClient/SurfaceCover.cpp` (plus `LivingWind` exports `GroundConditions`, `ZoneAir`,
`RoofOpenness`; `Camera.cpp` calls `SurfaceCover::LimitCameraDistance`), tested in Dun Morogh:

- **Fallen snow**: per zone (top of the area chain), builds while it snows below 0 C (air), sublimates, melts by
  degree-days. Near patch (`Cell::fAlpha/fRise/fMul`, `Cell::eShare/eLift` per frame) and the far mesh (bare layers
  are `Entry::fallLayer`, their `c33` cover scaled by the depth over 2 cm; the far skin is at most ~3 cm). Zones left
  behind evolve on their own climate (`ZoneAir`, no snowfall); entering a stale zone fast-forwards 30 s at 60x.
- **Painted snow melting**: `keep` per zone follows `MeltCap`; near patch redoes the edge (`Edge`) from `pShare`,
  far mesh scales the snow layers' `c33`. Donor default cap numbers, not SurfaceCover's MeltKeptShare/GoneTemperature.
- **Roofs**: near patch only (`UpdateOpen`: LivingWind's roof test on a 4 yd grid, blended); needs `worldWindShelter`.
- **Liquids**: `Dry()` per node and far vertex (terrain liquid, 0.3 yd band); SurfaceCover Flags 0x1 and 0x2.
- **Camera**: boom stays above the near cover at rest (trenches ignored).
- CVars: `surfaceCoverFallen`, `surfaceCoverFallenRate` (also melting), `surfaceCoverMelt`, `surfaceCoverNearRange`
  (leave at 48: wider cells kill trenches). The near patch blends into the far mesh over 12 yd.

Left:
1. **Deep far snow**: the far mesh is capped at `kFarDepth` (5 cm) because 4 yd vertices cannot follow the alpha-map
   edge; deep fallen snow looks thin in the distance and ramps up in the near patch. Needs the far lift to follow the
   fallen depth with a taper toward the edges (CPU re-upload of the lift part, or a vertex shader constant).
2. Known shortcuts: no local heat melting (lava) of painted snow; roofs not in the far mesh; one depth per zone.
3. Optional: ground decals on the cover; Base attachment models lifted onto it.

## After that: the rest of the donor

Not chosen in the first round (fog, lights, wetness, anti-aliasing were left out). Rough order:

1. **Fields: ground moisture** (`src/env/Fields.cpp`): a 2 yd grid around the player of moisture (rain, water
   nearby, melt water, drying by `GroundMaterial.RestMoisture` / `Absorbency`). Unlocks mud and wet ash covers (the
   rows skipped now), wetness and ground fog.
2. **Terrain wetness** (`src/features/TerrainWetness.cpp`): the terrain darkens and shines with moisture above its
   material's rest. Here that is a few lines in `HD/HdTerrain.cpp`'s pixel shader plus a moisture texture (Gx has no
   vertex texture fetch; pixel sampling is fine).
3. **Point lights by data** (`DoodadLightTable.cpp`, `DoodadLightProperties.cdbc`, `DoodadLightAssignment.cdbc`):
   lights added to placed models without editing them. The client already has HD lights per pixel on terrain
   (`HD/HdLights.cpp`) and Lumenveil lamps: decide which one takes the new lights before porting.
   `ModelLights.cpp` / `TerrainLights.cpp` are the donor's receivers.
4. **Ground fog** (`src/env/Fog.cpp`, `FogSettings.cdbc`, `features/GroundFog.cpp`, `FogFar.cpp`, `FogWake.cpp`): a
   ray-marched full-screen pass over a fog field, pushed aside by units. Needs the readable scene depth; Lumenveil
   already has a depth pass and fog model (`Engine/Source/Lumenveil/Core/fog_model.cpp`), so compare before porting
   another.
5. **Doodad fade** (`features/DoodadFade.cpp`): doodads fading in at view distance get fog while see-through.
6. **Anti-aliasing**: the donor's FXAA. Check what the Vulkan backend and Lumenveil already do first.
7. Small leftovers: grass per-doodad root/tip from the model geometry (`GrassDoodads.cpp`; now only from the table),
   an 8 x 8 wind sample grid for grass (`GrassMotion.cpp`; now one wind per chunk), `hdTime` working outside
   Azeroth Remastered (one check in `WoW/Source/WorldClient/HD/HdTime.cpp`).

## How to build, install and test

- Build: `cmake --build build/retail-satchel --config Release --target WowClient` gives
  `bin\Win\WowClientRetailInventory.exe`.
- Install (daily client `G:\3.3.5a - Dev`): check no `Wow.exe` runs, copy the old `Wow.exe` to
  `G:\3.3.5a - Dev\_backups\<name>-<yyyymmdd-hhmmss>\`, copy the new exe over `Wow.exe`, compare md5s.
- New or changed HD shaders (Vulkan draws DX9 code only through translated modules): either start the game once so it
  writes the missing code to `G:\3.3.5a - Dev\VulkanShaders*\hd-pending\`, or compile it offline with the same inputs
  as the client (same HLSL, name, macros in the same order, `vs_3_0`/`ps_3_0`, flags `1 << 15`, this machine's
  `d3dcompiler_47`), then run `python Tools/HdMaterials/install_hd_shaders.py --client "G:/3.3.5a - Dev"` and restart.
- Crashes: `G:\3.3.5a - Dev\WowClient_crash.log` and `Logs\gx.log`; symbolize against `bin\Win` with dbghelp.
- Memory: the client has run out of commit when WSL held over 20 GB; close other apps or cap WSL in `.wslconfig`.
- Editing tips: several sources mix CRLF and LF; keep each file's line endings. Long C++ blocks are easier to write to
  a scratch file and append than to pass through a shell command.
- Commit only when asked; end messages with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.
