# Retail assets from Blizzard's CDN

For independent Forever/Skyborne acquisition, durable decoded caches, accurate
progress notices and Dracthyr Visage selection, see [CDN downloads](cdn-downloads.md).

For the proposed workflow that creates new client/server items and mounts while
preserving original assets, see [Native Retail imports](retail-native-importer.md).
The [first weapon import backend](retail-import-workflow.md) now implements original
asset packs with new client/server item records.

This is an opt-in first implementation of in-process Retail asset access. The client
can fetch a file from Blizzard's CDN, verify and decode it with CascLib, cache the
original data, and load a supported M2 model and its dependencies through SFile.
There is no export step and no generated MPQ. This does **not** yet support every
Retail creature or item. Shared skeletons and several modern rendering features
are explicitly rejected.

The client still needs its normal **3.3.5a Data folder and server**. Retail database
records, character customization, display IDs, and item appearances are not
automatically translated into the Wrath database/protocol.

## Build

```powershell
cmake -S . -B build/retail -G "Visual Studio 17 2022" -A x64 -DWOWDECOMP_RETAIL_ASSETS=ON
cmake --build build/retail --config Release --target WowClient RetailAssetProbe RetailAssetTests --parallel
ctest --test-dir build/retail -C Release --output-on-failure
```

Default builds do not download or link CascLib. Enabled builds fetch CascLib commit
`38a34665624b8775bb875274b36191b21c38d97b` from its upstream repository and verify
the archive's SHA-256. It is MIT licensed. To build offline, supply
`-DWOWDECOMP_CASCLIB_SOURCE_DIR=C:/sources/CascLib` with that revision checked out.

## Configure

Copy [RetailCasc.example.ini](../config/RetailCasc.example.ini) to `RetailCasc.ini`
in the client's `Data/Retail` directory, or set `WOW_RETAIL_CONFIG` to an absolute config
filename before launching. Relative paths in the config are resolved against the
config's directory, not the process's working directory.

```ini
mode=online
path=Cache/Retail
product=wow
region=us
build=88d5e8ef05ec5a8446cdec93d142d7c6
locale=2
```

`build` is a **build configuration key**, not a version number or CDN configuration
key. An explicit 32-digit hexadecimal key is required for online mode. The sample
key is the build used for development verification; available builds can be read
from `http://us.patch.battle.net:1119/wow/versions`. Use the `BuildConfig` column of
the desired region. Changing a build requires a client restart. Each online build
uses a separate `path/<product>-<build>/` cache directory.

The Wrath client's `Data/Retail/Cache` directory keeps acquired assets with the
game. Do not nest it beneath modern CASC installations or other caches with storage
manifests: CascLib searches ancestor directories. Encrypted files need
their matching key; an optional `keys=keys.txt` uses CascLib's key-import format.
Missing keys produce an error, rather than zero-filled data.

For an existing local Retail installation:

```ini
mode=local
path=C:/Games/World of Warcraft
product=wow
locale=2
```

Local mode uses the installed storage. It does not automatically enable CDN fallback.
`locale` is a hexadecimal CASC locale mask; `2` selects enUS.

## Select an asset

Use a virtual path such as `retail/916541.m2` in a model filename field or
`Model:SetModel`. Numeric references do not require a listfile. The optional
`listfile=community-listfile.csv` loads `FileDataID;path` rows and exposes names
under `retail/`, for example `retail/item/objectcomponents/weapon/example.m2`.
Names are case insensitive and accept either slash style.

To deliberately replace a stock asset, add `aliases=replacements.csv` to the
config. The file uses the same row format, but paths are the exact client requests:

```text
916541;Item/ObjectComponents/Weapon/MyCustomSword.m2
```

An alias overrides MPQ lookup for that path. Other stock paths continue to use the
normal file system. Skin filenames derived by the model loader are resolved from
the model's SFID chunk; AFID entries resolve raw external animations; TXID entries
provide fixed textures. Dependencies are published only after every normal skin
profile passes validation. LOD-only skins are not exposed.

Replaceable creature/item textures still come from display data. Supply compatible
texture paths in your Wrath display records, or use the preview's texture override.
Simply assigning a Retail display ID to a Wrath creature is insufficient.

## Equipped weapons and shields

Add `equipment=equipment.csv` to the config to replace a Wrath hand item's appearance.
Rows contain **Wrath ItemDisplayInfo ID;Retail model FileDataID;Retail texture FileDataID**:

```text
# Replace display 12345 with the blue Arakkoa sword (example target ID).
12345;916541;916543
```

Use the target item's actual display ID, which is different from its item ID.
The model passes through `CCharacterComponent::AddHandItem` and the existing
`AddLink` attachment path. It replaces the equipped model and binds texture type 2;
hand/sheath selection, finger pose, enchant visuals, parent lighting, transforms,
and character animation remain under the normal engine. The same mapping applies
in character selection, dressing rooms and in-world calls to that path, for every
item using the mapped display. No DBC or server record is changed. This mapping
currently covers hand items, including shields; body armor and shoulder/helm
geosets require their separate appearance paths.

For a character-selection smoke test without discovering the display ID first,
use `retailweapon 916541 916543` at character selection. Once the selected character
has a loaded model and a mainhand item, the command replaces that display's
appearance and records its exact mapping row in `Logs/Extensions.log`. The override
lasts for that process and also applies when that display is subsequently equipped
in-world. Persist the logged row in the equipment CSV for future launches.

For a repeatable test across character changes, launch `Preview.ps1` with
`-Equipped` (sets `WOW_RETAIL_EQUIP_PREVIEW="916541 916543"`). This deliberately
replaces **every equipped mainhand** in that test process, from initial character
creation/loading onward, including world equipment. Offhands keep their existing
appearance unless explicitly mapped in the equipment CSV. A character must have
a mainhand item equipped; sheathed weapons use the normal sheath attachment.
The animated parent and attachment ID are logged once when an attached Retail
model reaches the animation pass. The test does not change inventory, item stats,
server appearance, or login state. Normal launches should use specific display
mappings instead of the broad smoke-test environment variable.

## Probe and preview

Before launching the client, use the built `RetailAssetProbe.exe`:

```powershell
RetailAssetProbe.exe C:/config/RetailCasc.ini retail/916541.m2 --inspect
RetailAssetProbe.exe C:/config/RetailCasc.ini retail/916541.m2
RetailAssetProbe.exe C:/config/RetailCasc.ini retail/916543.blp
```

`--inspect` reads original data and reports the model's dependencies and first
unsupported layout feature. Without it, the probe exercises preparation and skin
validation used by the client. Probe success alone does not prove correct rendering.

With the RetailPreview extension enabled (the default), the client console command
is `retailmodel <modelID> [textureID [textureType]]`. Texture type defaults to `2`,
the object-skin slot. For example:

```text
retailmodel 916541 916543 2
retailmodel hide
```

The first example requests the Arakkoa sword and its blue texture directly from the
pinned Retail build. The preview logs successful native model loading to
`Logs/Extensions.log`. `WOW_RETAIL_PREVIEW="916541 916543 2"` opens the same preview
two seconds after client initialization, for a repeatable smoke test. It does not
log in to a server. `WOWDECOMP_EXTENSION_RETAILPREVIEW=OFF` excludes the preview.

Runtime failures and unsupported features are written to `Logs/RetailAssets.log`.
For a development Data folder with modified GlueXML/FrameXML, build with the existing
`-DWOWDECOMP_DISABLE_SIGNATURE_CHECKS=ON` option as well. This option is independent
of CASC support and remains off by default.

`Tools/RetailAssets/Preview.ps1` supplies the config and preview environment to one
client process without changing your persistent environment. Place the built
executable alongside the development client's `Data` folder, then run:

```powershell
./Tools/RetailAssets/Preview.ps1 -ClientDirectory 'C:/WoW-Dev' -Executable WowClient64.exe -Config 'C:/config/RetailCasc.ini'
# Use the real character hand attachment instead of a floating Model frame:
./Tools/RetailAssets/Preview.ps1 -ClientDirectory 'C:/WoW-Dev' -Executable WowClient64.exe -Config 'C:/config/RetailCasc.ini' -Equipped
```

## Race customization tables

`Engine/Source/Retail/Db2.*` reads WDC5 tables straight from the pinned CDN build
(by FileDataID; DB2s carry no CASC names), and `Appearance.*` resolves a retail
race/sex plus a list of `ChrCustomizationChoice` IDs into geosets, texture layers
and attached models. Both are adapted from WarcraftXL's GPL-3.0 wxl-db2; keep that
licence in mind when distributing. Table layouts are checked by layout hash, and all
tables used match build 12.1.5.70448. `ReadTable` zero-fills sections encrypted for
unreleased content, as the stock client does.

`RetailRaceProbe <RetailCasc.ini> [raceId sex]` lists every race/sex model and, with a
race, its options, choice counts and the recipe for the first choice of each option.
Not yet implemented: applying a recipe in `CharacterComponent`, SKEL/chunked ANIM,
the creation UI, and server-side storage of choices.

## Current boundary

| Area | Supported in this implementation |
| --- | --- |
| Storage | Local CASC or online CDN; pinned online build; persistent original-byte cache; strict data checking |
| M2 | MD21 payload versions 272–274 with embedded bones; validated legacy-compatible arrays and tracks |
| Skin | SFID resolution, vertex/index/bone range checks, reconstruction of unused influence counts |
| Material | One/two texture batches; supported simple combiners; first three explicit Retail T1/Env shaders mapped to their legacy equivalents |
| Texture | Native BLP2 format version 1; fixed TXID textures; replaceable object-skin textures for mapped hand equipment and preview |
| Animation | Embedded tracks and raw AFID external tracks; incoming blend duration adapted to the legacy mixer |
| Shared SKEL, chunked ANIM | Not implemented; rejected |
| Cameras, ribbons, particles | Modern layouts not implemented; rejected |
| Modern shaders with more stages | Not implemented; rejected |
| WMO | Root and groups (v17, Legion+ chunk set) rewritten in memory to the 3.3.5 positional layout (`RetailWmo.cpp`): MOMT texture FDIDs and MODI doodads become `retail\<id>` names, GFID groups are served as `<root>_NNN.wmo`, MPY2/MOVX narrowed, post-Wrath chunks and LODs dropped, collision BSP kept. No group splitting (groups past 16-bit indices are refused) and no BSP rebuild |
| WDT/ADT | Terrain WDTs (MAID) and split tiles (root/_obj0/_tex0) merged in memory into 3.3.5 monolithic ADTs (`RetailTerrain.cpp`): MCIN, name tables from FDIDs, MCRF, folded holes, MTXF 0x1. The WDT needs an `aliases` row for `World\Maps\<dir>\<dir>.wdt`. Not done: WMO-only WDTs, MH2O liquid, MCSE, WDL, more than four layers |
| Retail DB2/appearance/customization pipeline | Not implemented |

The adapter changes only decoded memory: it unwraps MD21, builds virtual texture
paths, provides a real constant-opacity channel where Retail uses a sentinel, and
prepares a layout the existing engine can relocate. It does not weaken the normal
Wrath model version check. Unsupported data fails before reaching the renderer.

Reads currently fetch and prepare an entire asset at open. Cold storage startup
can download substantial metadata and block the calling thread. The existing async
reader then reads the prepared memory file; background CDN scheduling, cancellation,
disk-budget eviction, and streaming large files are future work. Individual decoded
files are limited to 128 MiB. Prefetching with the probe reduces cold gameplay stalls.

No game data, installation paths, encryption keys, or CDN cache is committed.

## Verification

The x64 Release client and standalone probe/tests were built on 2026-10-16.
The parser/configuration tests cover truncation, invalid offsets and indices,
unsupported features, opacity defaults, skin influence reconstruction and routing.
The disabled configuration was also generated without fetching CascLib.

Live CDN verification used model **916541**, skin **916542**, fixed texture
**775194** and blue object-skin texture **916543**. The native client loaded
925 vertices, eight bones, two texture slots and one skin section. The lit,
bounds-fitted preview was visually verified with the sword textured correctly.
The hand-equipment path was then built and visually verified in character selection
on a Vulpera character: the Retail sword replaced the equipped mainhand at character
scale while the stock offhand shield remained. The floating preview was disabled.
In-world movement, combat and sheathing of this appearance remain live acceptance
checks; the earlier floating-preview screenshot does not establish those behaviors.
This verifies that example, not the whole Retail asset corpus or animation system.
