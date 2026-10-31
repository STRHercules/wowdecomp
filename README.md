# Esteria client fork

A C++ reconstruction of the 3.3.5a client (build 12340, June 2010), maintained for the Esteria private realm.

Every upstream function is reconstructed from the original binary and keeps the address it came from in an
`// OFFSET:` tag, so each one can be compared against the original. The source tree follows the layout of the
original client (`Engine/Source/<Module>`, the game modules, original file names). Everything this fork
adds on top is described below, and none of it is in the original binary.

x64 is the main target. Win32 still builds and is used to check struct layouts against the 32-bit original.

Join the `Custom Race` discord for other clients and to show off what you build: https://discord.gg/W5YSWcM8PB

## What this fork adds

Most of this is opt-in at build time or needs a matching server. No game data is included in the repository, and
the client never modifies your original MPQs.

### Rendering

| Feature | What it does |
|---|---|
| Vulkan backend | Experimental renderer selected with `-vulkan` or the `gxApi` CVar. Uses retail assets by default, with a larger texture table and offline shader translation ([instructions](Tools/VulkanShaders/README.md)). D3D11 stays available with `-d3d11`. Offers the stock shadow qualities up to Very High (cascaded shadow maps), with per-frame descriptor and uniform budgets sized for them, and has the post-pass layer Lumenveil draws through (`GxEffects`, below) |
| [Lumenveil](#lumenveil-vulkan) | Volumetric fog and light shafts, night lights, water, wet surfaces and shadows for the Vulkan client, ported from the Lumenveil mod and set up from pages in the Video options. See [below](#lumenveil-vulkan) |
| Visuals switch | The `visuals` CVar (saved; the Effects page has a dropdown) picks the look of the Vulkan client: 0 Stock, 1 Lumenveil (default) or 2 [Azeroth Remastered](#azeroth-remastered-visuals-vulkan). Stock and Lumenveil are untouched by the Remastered code |
| [Azeroth Remastered visuals](#azeroth-remastered-visuals-vulkan) | Ambient occlusion, contact shadows, global illumination, volumetric fog, bloom, colour grading, a Forever sky, per-pixel lamp lights and PBR water with FFT waves, ported onto the Vulkan backend's post-pass layer. Includes `hdTime` to hold the time of day |
| [Living Azeroth](#living-azeroth-wind-grass-and-snow) | World wind, waving trees and grass, grass that parts around units, and snow that falls, lies, melts and takes trenches, all driven by optional data tables |
| Native M3 models | The [ASECM3](Extensions/ASECM3/README.md) extension loads and animates M3 models in-process: materials, skinning, particles, ribbons, cloth and rigid-body physics. Still in progress |
| Retail model adapter | Loads supported Retail M2 models (mounts, creatures such as Darkfallen and the Writhing Brood) straight from Blizzard's CDN, with no export step and no generated MPQ |

### Retail races and character creation

| Feature | What it does |
|---|---|
| Retail race appearance | Retail customization choices, skin colors, faces and hair drive the 3.3.5a creator and roster. Includes Kyrian and Venthyr appearances |
| Extra options without new DB2 | `Esteria\RetailExtras.txt` in an MPQ clones existing choices under new IDs, so custom faces and skins (for example extra Nightborne faces and Venthyr skins) show up beside the retail ones |
| Expanded creator | Restyled and expanded character creation, creator zoom, and polished character selection. Both creator panels slide out of the way on the customization pages, behind a collapsible tab |
| Faction race browser | Race selection with three factions (Alliance, Horde and the Freeborn): a sliding faction rail and a detail view for each race |
| Body sliders | A Body page in the creator, saved per character and synced to nearby players ([plan](docs/body-sliders-creator-plan.md)) |
| Eye lightning toggle | Per-character toggle for glowing eyes |
| Two forms | Worgen/Gilnean and Dracthyr/Visage, with both looks saved on the server ([design](TWOFORMS.md), [server](docs/two-forms-server.md), [test plan](docs/two-forms-e2e-test.md)). Work in progress |
| Character select | `GetCharacterInfo` returns a pending-rename flag as an 11th value, for a rename service icon beside the re-customize, race change and faction change ones |

Retail looks travel to the server and to other players in a 132-byte `RLK1` footer and the `0x524` opcode, so these
features need the matching Esteria server. Every custom race, with its IDs and what it offers, is listed under [Races](#races).

### Interface and gameplay

| Feature | What it does |
|---|---|
| DigitalInventory | Searchable five-column carried inventory with a server-side item index, via the [DigitalInventory](Extensions/DigitalInventory/README.md) extension and the `EsteriaInventory` addon in `Addons/`. With auto sort off, items stay where you put them in a saved free layout; the stock bags are held back until the realm answers, so they never flash before the replacement opens |
| Extended limits | Optional 1000-quest and 254-class layouts and larger containers ([details](docs/uncaps.md)) |
| Video and cursor options | Extra video controls and a native cursor selector, see [Running](#running). The Video options also hold the Esteria pages (view distance, horizon, distance fog and field of view; unit outline; loot beam; performance overlay) and, on Vulkan, [Lumenveil's settings](#lumenveil-vulkan). Each control is a CVar, so Okay, Cancel and Defaults treat the pages like any stock panel |
| Unit outline and loot beam | A coloured outline on your target, pet target, mouseover, quest givers and interactive objects (with a Vulkan outline pass), and a beam of light over lootable corpses tinted by the best item. Both are markers only and send nothing to the server |
| Performance overlay | A small movable overlay with frame rate, frame time, draw calls, triangles and CPU use (`showPerformance`) |
| Manastorm speedflying and transmog | The Manastorm speedflying controller, HUD, talent panel and flight camera effects, and the transmog addon with Vulkan-backed model snapshots (`Addons/Manastorm_Speedflying`, `Addons/Manastorm_Transmog`; [integration notes](docs/MANASTORM_INTEGRATION.md)). Replaces the earlier retail-style Skyriding, whose documents are archived in `docs/archive/` |
| Bundled addons | The body shape and view distance addons in `Addons/` |

### Retail content (optional)

Off by default. The retail assets build option (see [Options](#options)) enables local CASC/CDN access and the Retail model adapter, with durable
decoded caches and background downloads. See [support boundaries and setup](docs/retail-casc.md),
[CDN downloads](docs/cdn-downloads.md) and [native Retail imports](docs/retail-native-importer.md). Support is
partial: not every Retail creature or item works yet.

- **Packed originals.** Decoded Retail files can also be read from an MPQ (`EsteriaRetail\<product>-<build>\<locale>\<id>.bin`)
  before the client goes to the CDN or the cache, and the log says whether it is reading local files or the CDN.
- **Mount replacement.** An `EsteriaMounts\mounts.csv` in an MPQ replaces the model, scale and skins of a mount's *mounted*
  appearance with the Retail original (NPCs on the same display keep their own records). The
  [Mount Studio](Tools/MountStudio/README.md) desktop tool (Rust/Tauri) builds that catalog: it scans your client read-only,
  protects custom additions, downloads the original dependencies with checkpoints and validates them with the client's own loader
  in offline mode. No retroporting, resampling or conversion.
- **Tooling.** `RetailAssetProbe` can extract Retail `.db2` tables (`--extract-table`) and reports its capabilities
  (`--capabilities`); the server helper takes the database container's name.

### Lumenveil (Vulkan)

A port of [Lumenveil](https://github.com/lumenveil/Lumenveil) 0.1.1, a Direct3D 9 `version.dll` mod for the stock client (itself
a modified [CoAVolFog](https://github.com/jealous-sound/coa-vfog)), running natively in this client's Vulkan backend. It draws
through `GxEffects`, a small backend-neutral layer for full-screen and point passes over the world's target (`Engine/Source/Gx/GxEffects.h`;
Vulkan implements it, other backends have none and Lumenveil stays off). The code is in `Engine/Source/Lumenveil/`; the port's design,
the shader translation, what differs from the mod and what is left are in [LUMENVEIL_PORT.md](LUMENVEIL_PORT.md).

| Part | What it does |
|---|---|
| Fog | Volumetric fog marched per pixel: zone-light layers, drifting ground banks, morning mist, evening haze, dust and humid haze by climate, cloud shadows, a temporal filter and a per-zone colour grade with darker nights |
| Light shafts | Sun and moon shafts, including from a light outside the picture (a shadow map from the light that remembers what the camera has seen), glare and a light glow |
| Night lights | Lamps, torches, braziers and lit windows from `lampdata.bin`, the belt lantern, corpse lights and spell lights (found by comparing the scene before and after the client's glowing effects), as light on surfaces with screen-space shadows and as halos in the fog |
| Shadows | Terrain height-map shadows and cloud shadows, lamp shadows (thin things such as lamp-post crossbars cast none), and a screen-space sun and moon shadow of objects that stands down when the client's own shadows (MED-HIGH and above) already cover them |
| Water | The liquid pass writes depth, so fog sees the surface. Depth-based colour, a wave field of about 140 waves in three layers, screen-space and remembered reflections, sun glitter, rain rings, ripples around the player and lamps mirrored on the water. Lava and slime are left as the client draws them |
| Wet surfaces | Sheen toward the sun or moon, wetness and puddles from rain or snow, and reflections on the ground |
| Settings | CoAVolFog.ini and CoAVolFog-regions.ini beside the executable, reloaded within a second of a save (regions override any setting per zone, hour and weather). In game, **Esc → Video** has Lumenveil pages (Fog, Light, Colour, Weather, Water, Advanced) with every setting, labelled from the mod's own settings window; Okay writes the changed keys back to the ini and keeps its comments |

Not ported yet: relief (parallax) and terrain materials (both ship off in the mod and need a new design for the translated Vulkan
shaders), the mod's hotkeys, its region panel and the weather override.

Using it: build with the Vulkan backend, copy `CoAVolFog.ini` and `CoAVolFog-regions.ini` from the mod's `package` folder next to the
executable, and make `lampdata.bin` (and `liquiddata.bin` if wanted) with the mod's `lamp_extract` setup tool against your own client's
`Data` folder (see the port notes). `/console lumenveil 0` switches it off to compare with the plain game; `lumenveilTime HH:MM` holds the
time of day on your screen. Logs are in `Logs\Lumenveil.log`. The shaders are compiled offline (`Engine/Source/Lumenveil/Shaders/Compile.py`,
needs glslang) and the compiled modules are committed, so a build does not need it.

**Licence.** `Engine/Source/Lumenveil/Core/` is Lumenveil's own code under the **GNU General Public License, version 2** (see
`Core/LICENSE` and `Core/README.md`). A client built with it is a work based on it: whoever receives that binary must be able to get its
source under the same licence.

### Azeroth Remastered visuals (Vulkan)

Selected with `visuals 2`. These are the effects of the Azeroth Remastered client (a 3.3.5a client with its own Vulkan backend),
ported onto this client's `GxEffects` post-pass layer (now with compute passes) instead of taking its backend; the donor's
formulas, CVars and tuned defaults are kept. The code is in the `WorldClient/HD/` folder of the game modules; the port's design, status and what is left
are in [AZEROTH_REMASTERED_PORT.md](AZEROTH_REMASTERED_PORT.md). Stock (0) and Lumenveil (1) are untouched.

| Effect | What it does |
|---|---|
| Ambient occlusion, contact shadows, global illumination | XeGTAO with a temporal filter, screen-space contact shadows (also from units) and a one-bounce screen-space GI. `hdAO`, `hdContactShadow`, `hdGI` |
| Fog, bloom and grading | Forever's three-layer volumetric fog with sun-shadow light shafts, a halo and sun rays, and Forever's colour grading from lookup tables. `hdFog`, `hdBloom`, `hdGrade` |
| Time and sun | `hdTime` holds the hour on your screen (works in every visual set; -1 is real time), and `hdSunPath`, `hdShadowSun` and `hdShadowBillboard` put the sun and its shadows where the world light comes from: long shadows at dawn and dusk, leaf cards that face the sun |
| Sky | The Forever sky shaders. `hdSky` |
| Terrain, buildings and grass | The Forever material shaders, translated for Vulkan, with Forever's ambient and lighting. Grass also receives shadows (`hdGrassShadow`) |
| Lamp lights | Per-pixel point lights from the nearest lamps on terrain and, beyond the donor, on buildings. `hdLights`, `hdLightsStrength` |
| Water | Forever's PBR water: depth colour, FFT wave cascades, ripples, a soft shoreline and this client's own lake colours. `hdWater`, `hdWaterInfo` |
| Data | The `ForeverData` exporter (`Tools/ForeverData`) reads Forever's lights, grading, fog and water tables from your own Forever install |

Each effect has a status CVar (`hdFogInfo`, `hdAOInfo`, and so on) that you can read with `/run print(GetCVar("hdFogInfo"))`, and debug views
(`hdAODebug`, `hdFogDebug`, `hdGradeDebug`, ...). The material shaders are DX9 code that Vulkan draws through translated modules: after
a build that changes them, start the game once so it writes the missing code to `VulkanShaders*\hd-pending`, then run
`python Tools/HdMaterials/install_hd_shaders.py --client "<client folder>"` and restart.

### Living Azeroth: wind, grass and snow

Native ports of wxl-treewind and seyris-living-azeroth (both GPL-3.0), together with `wxl-grasswind`'s grass algorithm, so the world
moves and weathers. The data tables are optional `.cdbc` files (WDBC with a string block) that the client reads
from its archives or loose from `DBFilesClient`: `WindProfile`, `AreaClimate`, `GroundEffectDoodadDensity`, `GroundEffectDoodadWind`,
`GroundMaterial`, `GroundMaterialSelector` and `SurfaceCover`. Without them the features fall back to fixed defaults or stay off. The
Video options have pages for them (**Esteria Tree Wind**, **Esteria World Wind**, **Esteria Snow Cover**), and the work in progress and the
shortcuts taken are listed in [docs/living-azeroth-handoff.md](docs/living-azeroth-handoff.md).

| Feature | What it does |
|---|---|
| World wind | `WindProfile.cdbc` sets the wind per map and area: steady strength and heading, gusts that travel downwind, shelter under roofs and a lee behind walls. Bad weather pushes it up. Exposed to Lua as `LivingAzeroth_GetWind`, `GetWeather`, `GetTemperature`, `GetHumidity` and `GetVersion` |
| Climate | `AreaClimate.cdbc` sets temperature and humidity per area, blended across borders, with the time of day and the season, warmed near magma. Drives melting and snow |
| Tree and bush wind | Trees and bushes sway from the world wind (`Engine/Source/Model2/M2TreeWind.cpp`, from wxl-treewind), hooked into the M2 animation |
| Grass wind and parting | Grass bends with the wind and parts around you and the seven nearest units, each sized by its bounding radius and wider when mounted. A grid of 8 × 8 wind samples around you (gusts, roofs, lee) is read per blade, so the wind differs from place to place. A doodad's bend from its root to its tip comes from `GroundEffectDoodadWind.cdbc` or, where that is unset, from the model's own geometry |
| Kept grass and density | Grass is baked once and kept on the GPU, with a memory budget and `groundEffectDist` up to 1024 yards; `GroundEffectDoodadDensity.cdbc` sets its density, and far grass thins out |
| Snow cover | Snow (and sand and ash) lies on the ground as a layer of its own, from `SurfaceCover.cdbc`: deeper on flat ground, thinner on slopes, with ragged edges, lit and shadowed like the terrain. Each chunk is drawn again lifted by its cover, and around you a fine patch (48 yards across) is lumpy and takes **trenches** that you and nearby units press into it and that fill back in. Its depth follows the weather, below |
| Fallen snow | In a snowstorm on ground below 0 °C, snow builds up on bare ground (about 18 cm an hour at full intensity, up to 60 cm), sublimates when it stops and melts above 0 °C. Each zone keeps its own depth, and zones you have left evolve on their own climate; entering one you have not seen for a while fast-forwards it. It shows on the far terrain too, not only near you |
| Melting | Painted snow thins in warm weather, its edges retreating, down to the cover row's `MeltKeptShare` and `MeltGoneTemperature`, and grows back in the cold. Magma melts the snow around it |
| Where snow does not lie | Not under roofs and overhangs, and not under terrain water (thinning over 0.3 yards below the surface, from the cover's Flags); not on steep slopes |
| Camera | The third-person camera stays above the snow, as it is kept out of the terrain |

CVars include `surfaceCover`, `surfaceCoverNear` (the fine patch), `surfaceCoverTrenches`, `surfaceCoverFallen`, `surfaceCoverFallenRate`
(how fast snow builds up and melts: 1 is real time, 200 is a quick test), `surfaceCoverMelt`, `hdGrassWind`, `hdGrassWindFollowWorld`
and `worldWind`. `surfaceCoverInfo` reports what the snow is doing and what it costs: read it with
`/run print(GetCVar("surfaceCoverInfo"))`. For a quick test on a server that allows it, `.wchange 2 1` starts a snowstorm.

Not done yet: ground moisture and wet terrain (so mud and wet-ash covers stay off), lights, ground fog and anti-aliasing from the same
donor; the handoff lists them in order.

### Stability and fixes

- Fixes for guild bank packets, `IsTransport` on units, Lua `UnitAttackPower` return types and the console
- A crash logger in the main client target that writes a crash log beside the executable
- Extension hook system (upstream pull request #10) so features such as the
  ones above stay out of core files

## Races

The stock 3.3.5a client has ten playable races. This fork adds many more, in three families that differ in where their models and looks
come from. All of them use the creator, character select and in-world rendering described under
[Retail races and character creation](#retail-races-and-character-creation); their looks travel to the server and other players in the
`RLK1` footer, so they need the matching Esteria server (race IDs are the client's effective `ChrRaces` IDs, and the server's race,
class, start and language rows have to agree). Armor, helmets, emotes and animations of every race below work through the same
native pipeline, and helmet fitting follows the anatomy of the stock race named in each row.

### Races on Retail models

Their bodies, geosets and textures are the Retail originals, read from your installed Retail client or Blizzard's CDN and composed in
process (see [Retail content](#retail-content-optional)). They map onto race slots already present in the client's effective
`ChrRaces` table; the mapping is `Tools/RetailAssets/RetailRaces.csv` (target race ID, then the Retail `ChrRaces` ID) and, at run
time, the retail races environment variable. The stock race slots 1 to 11 can be mapped onto their Retail bodies the same way; Human and
Dwarf are the ones verified in game.

| Race | Target ID | Retail ID | Helmets follow | Notes |
|---|---:|---:|---|---|
| Worgen | 12 | 22 | its own | [Two forms](#retail-races-and-character-creation): Wolf and Human, each with its own saved look |
| Nightborne | 17 | 27 | Night Elf | Extra faces beside the Retail ones (`custom\nightborne\faces`) |
| Pandaren | 18 | 25 | its own | Alliance source; Retail's registry-only slot 26 has no models and is left out |
| Void Elf | 19 | 29 | Blood Elf | |
| Vulpera | 20 | 35 | Goblin | Extra textures from `EsteriaVulperaTextures.bin` |
| Lightforged Draenei | 21 | 30 | Draenei | |
| Zandalari Troll | 22 | 31 | Troll | |
| Dark Iron Dwarf | 23 | 34 | Dwarf | |
| Dracthyr | 28 | 70 | Blood Elf (Visage) | Two forms: Dragon (the race's native model) and Visage, each with its own saved look; the rig can exceed 256 bones |
| Kul Tiran | 29 | 32 | Human | |
| Mag'har Orc | 45 | 36 | Orc | |
| Highmountain Tauren | 46 | 28 | Tauren | Extra textures from the Esteria catalog |
| Mechagnome | 47 | 37 | Gnome | Inherited emotes work through recursive skeleton parents |
| Earthen (Alliance, Horde) | 48, 49 | 84, 85 | Dwarf | Both factions enabled; extra choices from the Esteria catalog |
| Haranir (Alliance, Horde) | 50, 51 | 86, 91 | Night Elf | Both factions enabled; extra textures from `EsteriaHaranirTextures.bin` |
| Skyborne (Alliance, Horde) | 52, 53 | Forever 95, 96 | Blood Elf | From the separate Forever source (`ForeverCasc.ini`); 10 faces and 9 skin palettes; adds no classes or spells |

Customization choices are the Retail ones (faces, skins, hair, tattoos, markings, eye colours), filtered per race, with Retail's
character-texture compositing (copy, multiply, overlay, screen and alpha layers), plus extra choices cloned from
`Esteria\RetailExtras.txt` and an optional Eye Lightning toggle on models that have the effect mesh.

### Esteria's own races

These are not Blizzard races. Some are built from Retail models with an Esteria profile in the client; the rest use appearance
catalogs the client reads from its own folder.

| Race | Race IDs | Looks | What it offers |
|---|---|---|---|
| Ethereal | 77, 78 | Retail bodies, Esteria profile | Two forms: the Darkweaver body and the mechanical Dreadnaught, which hides armor. Three bodies, six energy colours plus more custom ones (ten named, from Sunforged to Chartreuse), seven lightning palettes (Shadowguard, Violet, Amber, Teal, Lavender, Shifting, Crimson) with custom ones, and mech colours with matching glow. Its own hand and foot geosets swap under gloves and boots, equipment atlases repeat their body tile into the wrap gutter, and it has complete shoe surfaces despite the NPC barefoot flag. Equipment follows the Nightborne and Night Elf helmet family |
| Slime Giant | 71, 72 | Retail SlimeSkeleton body | One authored male body for both factions. Slime Color (Green, Amber, Mauve, Red, Gold, plus 30 more) and Slime Drip (off or on). Keeps the original 208-bone rig and animations; clothing is a skinned collection on Kul Tiran HD UVs. Helmets use the Human mask ([notes](docs/slime-giant.md)) |
| Centaur | 73, 74 | Retail Centaur bodies | Male and female, 16 skins each, head and beard options and a belt layer. Legacy helmets use the fitted Draenei family |
| Kyrian | 75 | Retail looks through an Esteria catalog (`EsteriaKyrian.bin`) | Male and female; five appearance fields saved in the character's extra bytes, high-resolution equipment, and native takeoff and flight |
| Venthyr | 76 | Retail looks through an Esteria catalog (`EsteriaVenthyr.bin`) | Male and female, with extra skins beside the Retail ones |
| Darkfallen | 43, 44 | Retail model adapter | Male and female; five appearance fields |
| Naga | 54 | Esteria catalog | Male and female |
| Tuskarr | 55 | Esteria catalog | Male and female |
| Vrykul | 56, 57 | Esteria catalog | Male and female; the female uses the stock player Vrykul materials; authored at full size, shown at 0.55 in the creator |
| Forgotten | 58, 59 | Esteria catalog (thin human) | Male only |
| Sethrak | 62 | Esteria catalog | Male and female |
| Gnoll | 63 | Esteria catalog | Male only, with fifteen controls |
| Saberon | 64 | Esteria catalog | Male only |
| Drakkin (Alliance, Horde) | 66, 67 | Esteria catalog | Male and female; the Alliance female and both Horde bodies use stock materials |
| Drakonid (Alliance, Horde) | 69, 70 | Esteria catalog | Male only |

The catalog races are a port of the original Esteria client DLL (`EsteriaAppearance.dll`): their looks are packed bytes, five per
character, whose choices come from per-race tables (`ComponentCore/CreatureAppearance.h` in the shared game code) and catalog files beside the
executable. The creator's art table also names Ogre (60, 61), Jinyu (65), Arakkoa (68) and Illidari (30, 31), but this client has no
appearance data for them.

## About the upstream reconstruction

The percentages below measure how much of the original binary has a counterpart in this source tree. They do
**not** say how much of the game works, and the missing part is not missing gameplay: the client builds into a
complete executable and the game is 100% playable. Bugs could still be hiding, though; a ported function can behave differently
from the original.

Measured against the 12340 binary on 2026-10-15. A function counts as ported when its address is
tagged in the source.

| | Ported | Total | Share of the original |
|---|---:|---:|---:|
| Functions | 24,515 | 27,157 | 90.3% |
| Code bytes | 5,270,186 | 5,656,573 | 93.2% |

The total leaves out 658 functions that IDA identifies as compiler library code and 132 import thunks.
Most of the untagged remainder is code the build gets elsewhere: compiler-generated code (static
initializers, template instances) that our compiler emits on its own, C runtime helpers, and third-party
libraries that are linked from their upstream sources instead of being reconstructed (FreeType, zlib, PCRE,
parts of FMOD Ex).

## Requirements

- Windows 10 or 11
- Visual Studio 2022 with the **Desktop development with C++** workload (MSVC v143 and a Windows SDK)
- CMake 3.10 or newer (the one that ships with Visual Studio works)
- For Win32 builds only: [NASM](https://www.nasm.us/), on `PATH` or passed as `-DNASM_EXECUTABLE=<path>`, and
  `DivxDecoder.dll` from your 3.3.5a client copied to `ThirdParty/DivxDecoder/` (the build copies it next to the
  executables; the x64 build uses its own decoder instead)
- A **3.3.5a (12340)** client. **No** game data is included in this repository.

## Building

From a Developer PowerShell or any shell with `cmake` on `PATH`:

```powershell
git clone <this repository's URL>
cd <the cloned folder>

# x64 (main target)
cmake -S . -B build/x64 -G "Visual Studio 17 2022" -A x64
cmake --build build/x64 --config Release --target <client target> --parallel

# Win32 (optional, needs NASM)
cmake -S . -B build/win32 -G "Visual Studio 17 2022" -A Win32
cmake --build build/win32 --config Release --target <client target> --parallel
```

The client targets and the project's CMake options are named in `CMakeLists.txt` and `Tools/`; the names below are described, not spelled out.

In-source builds are refused, so keep the build directory separate (`build/` above).

The client is written to `bin/Win/`:

| Target | x64 | Win32 |
|---|---|---|
| The client with a crash logger | `<client>64.exe` | `<client>32.exe` |
| The same client without it | `<name>64.exe` | `<name>32.exe` |

The first target is the client with a crash logger: it opens a console window and writes a crash log
when the client crashes. The second is the same client without it. You can also turn the logger off
with its CMake option.

### Options

Every option starts with the project's prefix (see `CMakeLists.txt`); only the part after it is shown.

| CMake option | Default | Effect |
|---|---|---|
| `<prefix>_DISABLE_SIGNATURE_CHECKS` | `OFF` | Skip the MPQ file and `.toc.sig` signature checks, so modified game data and interface files load |
| `<prefix>_RETAIL_ASSETS` | `OFF` | Enable local CASC/CDN access and the first Retail model adapter; see [support boundaries and setup](docs/retail-casc.md) |
| `<prefix>_EXTENDED_LIMITS` | `OFF` | Opt into custom player/container layouts; requires a matching server |
| `<prefix>_QUEST_LOG_SLOTS` | `25` | Active quest ceiling, up to 1000 with extended limits |
| `<prefix>_CONTAINER_SLOTS` | `36` | Container ceiling, up to 254 with extended limits; adds no bag content or art |
| `<prefix>_MAX_CLASSES` | `32` | Maximum class ID, up to 254 with extended limits; widens class masks and requires matching server/DBC formats |
| `<prefix>_MAX_UPDATE_MASKS` | derived (`42` stock) | Accepted mask blocks; 1000 quests require 194 |
| `<client>_CRASH_LOG` | `ON` | Build the client with the crash logger |
| `<client>_OUTPUT_OVERRIDE` | empty | Name the crash-logger client's executable something other than its default |
| `<prefix>_EXTENSION_HELLOWORLD` | `ON` | Compile the imported extension hook examples into the client |

See [extended limits](docs/uncaps.md) for the 1000-quest/254-class configuration, generated
server field contract, and pending server/UI acceptance work. Extended descriptor
generation requires Python 3.10 or newer; stock builds do not require Python.

## Running

1. Copy the `Data` folder of your 3.3.5a client into `bin/Win/`, so you have `bin/Win/Data/`.
2. Point `Data/<locale>/realmlist.wtf` at the server you want to connect to.
3. Start the 64-bit client from `bin/Win/`.

The built-in cursor selector is under **Escape → Interface → Mouse → Cursor appearance**.
Human (Default), Night Elf, Orc, Undead, Hearthstone, and animated Starcraft apply
immediately and save between sessions.
See [native cursor textures](Addons/Cursors/README.md) for conversion and behavior details.

### Extensions and Vulkan

The extension hook system from upstream pull request #10
discovers extensions under `Extensions/<Name>/src/`. Each has a
`<prefix>_EXTENSION_<UPPERCASE_NAME>` CMake option; the bundled `HelloWorld` extension
demonstrates the hooks and writes to `Logs/Extensions.log`.

The experimental Vulkan backend from upstream pull request #11
can be selected with `-vulkan` or the `gxApi` CVar set to `Vulkan`. D3D11 remains available
with `-d3d11`. Vulkan requires a driver providing `vulkan-1.dll`; building it uses the
bundled headers and requires a C++20 compiler, without requiring the Vulkan SDK.
For world and model shaders, follow the [offline Vulkan shader translation instructions](Tools/VulkanShaders/README.md)
using your own client data. Vulkan remains work in progress; build success does not establish
rendering or gameplay correctness.

## Layout

| Path | Contents |
|---|---|
| `Engine/Source/` | Engine modules: Storm, Gx (Direct3D 9 / Direct3D 11 / OpenGL / Vulkan), Model2, Frame/FrameXML UI, SoundEngine, Lua, ... |
| The game modules' `Source/` | Game modules: objects, world rendering, UI, networking, database, glue screens |
| The game modules' `Common/` | Code shared by the game modules |
| `ThirdParty/` | Libraries linked from their upstream sources |
| `Tools/<client>/` | The client executable target |
| `Extensions/` | Optional client extensions: ASECM3, DigitalInventory, BodyShapeSync, RetailPreview, HelloWorld |
| `Addons/` | Bundled Lua addons (EsteriaInventory, Manastorm speedflying and transmog, body shape, view distance) and the native cursor converter |
| `Engine/Source/Lumenveil/` | Lumenveil for the Vulkan client: the controller, renderer, the options pages, the shaders and `Core/` (the mod's own GPL-2.0 files) |
| `Tools/RetailAssets/` | Retail asset, race and glue-screen tooling |
| `Tools/MountStudio/` | Desktop tool that builds the mount replacement catalog from Retail originals |
| `Tools/LumenveilOptions/` | Generator for the options table the Lumenveil pages are built from |
| `Tools/XvidDecoder/` | x64 movie decoder that replaces the original 32-bit `DivxDecoder.dll` |

## Special thanks

- [binana](https://github.com/thunderbrewhq/binana)
- [thunderbrew](https://github.com/thunderbrewhq/thunderbrew)
- [whoa](https://github.com/whoahq/whoa)
- TOMRUS, for the initial IDB
- Everyone who has reported bugs so far
- Manatactical, for the unit outline, speedflying controller, HUD and talent panel, and the transmog addon and the README layout
- Morfium-G, for seyris-living-azeroth, the source of the world wind, climate, ground material and snow cover design and tables ported in Living Azeroth
- iThorgrim and [WarcraftXL](https://github.com/WarcraftXL), for wxl-treewind, wxl-grasswind and wxl-db2, which the tree wind, the grass wind and the Retail appearance resolver are ported from
- [Furioz420](https://github.com/Furioz420), for the native Direct3D 11 backend this client's DX11 work draws on
- [Lumenveil](https://github.com/lumenveil/Lumenveil) and [CoAVolFog](https://github.com/jealous-sound/coa-vfog), for the fog and lighting mod behind Lumenveil
- The Azeroth Remastered project, for the HD effects ported under `visuals 2`, and Intel's XeGTAO (MIT) for its ambient occlusion
- The modding community
- Saty
