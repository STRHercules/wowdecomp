# Phase 2 recon: retail housing maps and room WMOs (loaded natively)

Date 2026-11-05. Retail build pinned in `Data/Retail/RetailCasc.ini` (`build=88d5e8ef…`, 12.1.5.70448).
Client paths are relative to `wowdecomp`. Server paths (`server:`) are relative to `EsteriaWoW-housing`.

What each claim rests on:

- **Inspected**: read in source code.
- **Probed**: real files fetched from the pinned build through `RetailAssetProbe` and decoded in the scratchpad.
- **In game**: nothing. No client or server code was changed, so there is no in-game checklist for this phase.

## Summary

| Question | Answer |
|---|---|
| Neighborhood maps | **2735 Founder's Point** and **2736 Razorwind Shores**. Each is a terrain map with 55 plots. |
| House interior | **2783 Home Interior**: 25 empty tiles of flat terrain at z = -2, with no placed objects. Rooms are **assembled at runtime** from separate floor, wall and ceiling WMOs. |
| How a house is built | HouseRoom → RoomWmoData (bounds) → several RoomComponent rows. Each row is one WMO with an offset, a yaw, a Type (1 wall, 2 floor, 3 ceiling, 4 stairs, 7 prefab) and a ConnectionType. RoomComponentOption swaps in theme and doorway variants. |
| Native loading today | **None for WMO, WDT or ADT.** The Retail source rejects them (`CascSource.cpp:656`). The stock readers walk chunks in a fixed order, and retail files have different chunks. |
| What would fix it | An **in-memory adapter in `CascSource::Read`**, the same idea as `PrepareModel` for M2. WMO is small and well defined; the probe shows the transform keeps the collision tree. WDT/ADT is larger. |
| Showing a room | **Route (b): a type-14 GameObject whose display is `retail\<fdid>.wmo`.** Stock code already turns it into a map object (`World.cpp:5234`), with full rotation and client collision. No new map is needed. |
| Server | It runs a map with no `.map` or vmap files (debug log only). Retail WMOs give no server collision or LOS; that is acceptable, as it was for decor. |
| Smallest first build step | The WMO adapter, then spawn one Square Room (Small) as 10 GameObjects inside the existing map-35 instance. |

---

## 1. Retail housing maps (probed)

Source: `Map.db2` (FDID 1349477) and `NeighborhoodMap.db2` (5550624), decoded with WoWDBDefs layouts that list
build 12.1.5.70448.

| Map | Name | Retail InstanceType | WDT FDID | MPHD flags | Tiles (MAIN) | Files in listfile |
|---|---|---|---|---|---|---|
| 2735 | Founder's Point (Alliance) | 8 | 5660542 | 0x3CA | 210 (x 24–38, y 19–32) | 1,110 ADT parts |
| 2736 | Razorwind Shores (Horde) | 8 | 5660543 | 0x3CA | not counted | 4,305 ADT parts |
| 2783 | Home Interior | 7 | 5890928 | 0x3CA | 25 (x 31–35, y 31–35) | 125 ADT parts |
| 3026 / 3027 | "12.0 Warband – Founder's Point / Razorwind" | 0 | 7490322 / 7490323 | — | — | probably variants; not probed |

- **NeighborhoodMap** rows: ID 1 → map 2735, entry point (3807.8, -160.4, 194.1), facing 163.4°. ID 2 → 2736,
  entry (2053.6, 175.5, 175.1). IDs 4 and 7 point at maps 2640 (not in Map.db2) and 2783.
- **NeighborhoodPlot**: 114 rows. There are 55 plots each for neighborhood maps 1 and 2, and 4 for map 4. Each row
  has a house position and rotation, a cornerstone GameObject and position, a teleport position, a plot GameObject
  and a WorldState. In this build the DBD's `HousePosition`/`HouseRotation` arrive as `Field_12_0_0_63534_004/005`.
- **All three are terrain WDTs**, not WMO-only. MPHD 0x3CA means: 0x200 FileDataID-based (MAID), 0x80 height
  texturing, 0x4 big alpha, 0x2 vertex colours. MAID gives eight FDIDs per tile: root, obj0, obj1, tex0, lod,
  map texture, map texture normals, minimap.
- **Interior terrain**: tile 2783_31_31 (FDID 8211407) has 256 MCNKs, every height exactly **-2.00**. Its obj0
  (8211408) has **0 MDDF and 0 MODF** entries. So the interior is an empty flat plane, and every room is placed at
  runtime.
- **Neighborhood terrain**: tile 2735_32_24, which holds the entry point, has heights 140.7–597.5. Its obj0
  (6155610) has **1,051 doodads and 21 WMOs**, all referenced by FDID. Textures are FDIDs in tex0 MDID/MHID
  (6155612). Liquid is MH2O in the root ADT (6155609).

## 2. How retail assembles a house (probed; column names from WoWDBDefs)

The DBDs came from `github.com/wowdev/WoWDBDefs` master. Each table's layout hash matched a DBD layout that
lists 12.1.5.70448.

```
HouseRoom (37)          Name, Flags, RoomWmoDataID, WeightCost, ItemID
  └ RoomWmoData (40)    BoundingBox[6], Height
      └ RoomComponent (257)  RoomWmoDataID, ModelFileDataID (a .wmo), OffsetPos[3], OffsetRot[3],
                             Type, ConnectionType, MeshStyleFilterID, Flags
          · RoomComponentOption (651)  Type, SubType, Theme, MeshStyleFilterID → ModelFileDataID (.wmo)
RoomGridLine (23)       per RoomWmoData grid sizes (e.g. 11×11, 47×47)
HouseTheme (17)         Folk, Rugged, Generic, Bel'ameth, Silvermoon, …
HouseExteriorWmoData (8) Human/Night Elf/Blood Elf/Orc House, treehouses, Westfall barns
NeighborhoodMap / NeighborhoodPlot   see §1
```

Full per-component dump: [room-components.tsv](room-components.tsv) (249 rows, with listfile paths).

**Example: Square Room (Small)** (HouseRoom 1, RoomWmoData 5, box ±11.5 × ±11.5 × 0–11):

| Type | Count | WMO | Offset |
|---|---|---|---|
| 2 floor | 1 | 6431804 `12ph_square_room_small_floor_0_0_generic01.wmo` | 0, 0, 0 |
| 1 wall | 4 | 6431855 `12ph_generic_wall_0_0_multitheme01.wmo` | (±12, 0) and (0, ±12), yaw 0/90/-90/180 |
| 1 wall (corner) | 4 | 6431794 `12ph_generic_wall_0_0_multitheme06.wmo` | (±11.5, ±11.5) |
| 3 ceiling | 1 | 6431786 `12ph_square_room_small_ceiling_0_0_generic01.wmo` | 0, 0, 7 |

What the tables tell us:

- **Rotations are yaw-only.** Every OffsetRot seen is (0, 0, z).
- **Room sizes** come in 6-yard multiples: tiny 11, small 23, medium 35, large 47, hallway 23×11, closet 5×11.
  Octagon, T, L, cross and stairwell shapes follow the same pattern.
- **ConnectionType 0 walls often have ModelFileDataID 0.** That matches doorway sockets, where the WMO comes from
  RoomComponentOption by theme or style. The `wall_1_1` / `wall_1_2` / `wall_2_2` file names suggest doorway and
  half-wall variants. The exact selection rule is **not decoded**: RoomComponentOption's Type/SubType values and
  its unnamed `Field_12_0_0_63534_002` are still guesses.
- **HouseRoom Flags 8 rooms are prefabs** (Stormwind Kitchen, Orgrimmar Council Room, …). Their only Type 7
  component has FDID 0, so the model probably also comes from RoomComponentOption. Not decoded.
- **How rooms connect.** No socket table was found. Rooms seem to snap on the 6-yard grid (RoomGridLine) and
  join where wall components carry ConnectionType 0. **This is inferred, not proven.** Retail's room placement
  logic is client and server code, not data.
- **Exterior**: HouseExteriorWmoData has names, flags (0x2 Horde, 0x4 Alliance) and unlock items, but **no model
  FDID** in this layout. How an exterior WMO is chosen was **not found** in these tables. The DB2s in
  Patch-Housing.MPQ also have `housedecorthemeset` and `housedecor` ModelType=WMO rows (65; see
  existing-assets.md), which may be the link. Left open.

## 3. Native loading today (inspected + probed)

### Retail source

- `CascSource::Read` prepares only `.m2/.mdx/.skin/.anim/.blp`. Any other extension fails with "Retail runtime
  supports M2, SKIN, ANIM and BLP references only" (`Engine/Source/Retail/CascSource.cpp:562-658`).
  - Probe: `RetailAssetProbe <ini> retail/6431804.wmo` → exactly that error.
- `Inspect` and `--extract` return raw bytes for any FDID (`CascSource.cpp:540-548`). `Handles` already accepts
  any `retail/…` name, and aliases can map `World\Maps\…` paths to FDIDs (`:475-479`, `:399-416`). So routing
  works, and only preparation is missing.
- `docs/retail-casc.md:211` ("WMO/ADT: Not implemented") **is still true.**

### Stock WMO reader

- Root, `CMapObj::Load` (`WoW/Source/WorldClient/MapObjRead.cpp:59-150`): it walks chunks **by position**, with
  no tag checks: MOHD, MOTX, MOMT, MOGN, MOGI, MOSB, MOPV, MOPT, MOPR, MOVV, MOVB, MOLT, MODS, MODN, MODD, MFOG,
  then an optional MCVP.
- Groups: `CMapObj::ReadGroup` opens `<root>_%03d.wmo` (`:614-650`). `CreateDataPointers` /
  `CreateOptionalDataPointers` also walk sub-chunks by position, using the MOGP flags (`:286-411`).
- What the probe found in retail room WMOs (`chunks.py`, `wmo_probe.py`):

  | File | Retail chunks | What breaks the stock reader |
  |---|---|---|
  | Root 6431804 (v17, 824 B) | MOHD, MOMT, **MOMX**, MOGN, MOGI, **MGI2**, MOPV/PT/PR, **MOPE**, MOLT, MODS, **MODI**, MODD, **MDDI**, **MNLD**, MFOG, **MFED**, **MAVG**, **GFID** | No MOTX (textures are FDIDs in MOMT), no MOSB, no MODN. The stock reader would take MOMT as the texture names, MOMX as the materials, and so on down the list. New chunks sit in the fixed positions. Groups are named by GFID, not `_000`. |
  | Group 6432330 (v17, 59.5 KB) | MOGP: **MOGX**, **MPY2**, MOVI, MOVT, MONR, MOTV×4, MOBA, **MOBS**, MOBN, MOBR, **MOC2** | MPY2 sits where MOPY should be, MOGX is new, and MOBS/MOC2 shift the optional chunks. |

- **The fix is mechanical.** wotlkconv's converter (bundled with the `wow-retroport` skill,
  `vendor/wotlkconv/wmo/`, about 1,600 lines of Python) was run **in memory only, nothing written or deployed** on
  the same two files:
  - root 824 → 724 bytes, group 59,552 → 43,388 bytes;
  - texture FDIDs become a MOTX table;
  - MPY2 becomes MOPY (1,242 triangles);
  - MOBA bounds are recomputed, because retail keeps a wide material ID in those bytes;
  - UV layers go from 4 to 2;
  - group flags 0x80000009 become 0x02000009;
  - MOGX, MOBS, MOC2 and the root's LOD, ambient and light extras are dropped;
  - **MOBN/MOBR are kept, so the collision BSP survives**;
  - one material's newer shader falls back to diffuse.
- **Textures are fine.** `retail/7225255.blp`, `6243128.blp` and `7225256.blp` all prepare as client-ready
  BLP2 (probe output below).
- The shader clamp in commit `5714f6de` (`MapObj.cpp:992-1006`) only guards `UnifiedRender` against shader
  IDs above 6 in already-converted WMOs. It does not parse anything.
- `sExtensionMgr->OnMapObjLoaded` / `OnMapObjGroupLoaded` (`MapObjRead.cpp:433`, `:609`) fire **after** the
  positional parse, so they can't fix layout.

### Stock WDT/ADT readers

- WDT, `CMap::LoadWdt` (`WoW/Source/WorldClient/Map.cpp:2922-`): reads MVER, MPHD and MAIN **by position**, then
  for a WMO-only map MWMO into a **256-byte stack buffer** (`char fileName[256]`, read with the chunk's own size).
  Retail adds MAID (which the stock reader ignores), so a raw retail WDT would load. But no tile would open,
  because the client builds `World\Maps\<dir>\<dir>_x_y.adt` names. Those can be aliased to the root ADT FDIDs.
- ADT, `CMapArea::ProcessIffChunks` (`WoW/Source/WorldClient/MapArea.cpp:313-346`): needs **MHDR offsets** into
  one monolithic file with MCIN, MTEX, MMDX/MMID, MWMO/MWID, MDDF and MODF. Retail root ADTs have MHDR, MH2O,
  MFBO and MCNKs, but **no MCIN and no name tables**. Placements and textures are in obj0/tex0, by FDID. The
  per-MCNK walker is tag-based and tolerant (`MapChunk.cpp:178-247`).
- So the terrain adapter has to merge root + tex0 + obj0 into a legacy ADT. It must add MCIN, write the FDID
  textures as `retail\<id>.blp` names and the FDID placements as `retail\<id>.m2|.wmo` names, and convert MCAL
  alpha to the layout the MPHD flags call for. wotlkconv's `adt/` (about 1,700 lines with wdt/wdl/mh2o/layers) is
  the reference.
- `TerrainScript::OnTerrainTileLoaded` (`WoW/Source/Extension/ScriptDefines/TerrainScript.h`) also fires after
  parsing, so it can't fix layout either.
- **No other native map path exists.** HD/Forever (`WoW/Source/WorldClient/HD/*`) is rendering only: lights,
  water, terrain shading, fog. It never reads retail map files. A grep for WDT/ADT/split-file readers outside
  `Map.cpp`/`MapArea.cpp` found nothing.

## 4. Showing a room WMO in game (inspected)

| | (a) WMO placed in an empty map | **(b) GameObject with a `.wmo` display** |
|---|---|---|
| Client mechanism | WDT MODF or ADT MODF placement → `CMapObjDef` | `CGGameObject_C_Type_WorldObj::CreateWorldObj` → `CreateWorldObject` → `CWorld::ObjectCreate(name…)`. A `.wmo` extension makes a `CMapObjDef` (`World.cpp:5231-5249`, `GameObject_C.cpp:3199-3238`). |
| Which GO types | — | Types 11, 14, 15, 33 and 35 skip the M2 model (`GameObject_C.cpp:543-549`). **Type 14 (MAP_OBJECT)** takes the default branch with guid 0 (`:3234-3236`). That is the clean choice for static room pieces. |
| Transform | Fixed in the file | `UpdateWorldObject` builds Translate·Rotate·Scale from the GO quaternion and passes it to `CMap::ObjectUpdate` (`GameObject_C.cpp:2618-2649`). RoomComponent rotations are yaw-only anyway. |
| Client collision | Yes, the WMO's MOBN/MOBR BSP | The same CMapObjDef, so **probably yes**. This is the path elevators and destructible walls use. Not tested with an adapted retail WMO. |
| Server | Needs map_dbc and an instance for the map | Same as decor today: `gameobject_template` type 14 plus `gameobjectdisplayinfo_dbc` rows (feasibility §1) |
| Per-house layout | Can't vary per instance: placements are baked into the file | **Can**: the module spawns the components the owner picked, like decor |
| Needs | WMO adapter **plus** WDT/ADT adapter, a Map.dbc row and a map 2783 alias | WMO adapter only |

**Recommendation: (b) for rooms.** Retail assembles rooms at runtime too (§1, empty interior tiles), so (b) is
the faithful model, not a workaround.

**Server collision**: vmaps can't read retail WMOs, and GO collision comes from `temp_gameobject_models` keyed by
displayId (feasibility §1). Movement in 3.3.5 is client-authoritative, so walls and floors stop the player on the
client. Server LOS and pathing see nothing. That is acceptable inside a house. There are no creatures to path, and
spells cast through walls are the only visible effect.

## 5. Server side (inspected)

- **Missing terrain is tolerated.** `GridTerrainLoader::LoadMap` logs a missing `maps/%03u%02u%02u.map` at
  DEBUG and carries on with no terrain data (server:`src/server/game/Grids/GridTerrainLoader.cpp:30-45`). Vmaps
  and mmaps fail the same soft way (`:50-`). Heights then come back `INVALID_HEIGHT`
  (`GridTerrainData.cpp:232-253`). The Phase 2 prompt says map 35 runs this way in Phase 1. I didn't check the
  stage realm's `maps/` folder for `035*.map`.
- **There is a hook for synthetic terrain**: `sScriptMgr->OnLoadGridMap(map, terrainData, gx, gy)`
  (`GridTerrainLoader.cpp:47`). A module could set a flat height for 2783 (z = -2) or a heightfield exported from
  retail tiles, without the extractor.
- **The extractors can't read retail files.** AzerothCore's map/vmap/mmap extractors read 3.3.5 MPQs, and our
  probe shows the retail chunk layouts differ (§3). Server terrain for 2735/2736 would need our own exporter, or
  the adapter's output written to `.map` format. Not required for a client-authoritative MVP.
- **Rows a new map needs**:
  - Server: `map_dbc` (`IsValidMAP` needs a `MapEntry`; a dungeon type also needs `instance_template`,
    `MapMgr.cpp:313-326`).
  - Client: a Map.dbc row with Directory `2783`, plus an `aliases.csv` row mapping `World\Maps\2783\2783.wdt` to
    5890928 and one per tile root.
  - Retail InstanceType 7/8 don't exist in 3.3.5: use 1 (party) for the interior.
  - `make_slice.py` already does this pattern for map 35 (server:`modules/mod-esteria-housing/tools/make_slice.py:132-136`).
  - Whether the client accepts a Map ID of 2783 (above any stock ID) is **unverified**. Picking a free ID below
    1470 avoids the question.

## 6. Plan

| Work | Needs | Effort | Blockers / risks |
|---|---|---|---|
| **A. WMO adapter** in `CascSource::Read` (`.wmo`): root rewrite (MOTX from MOMT FDIDs, MODN from MODI as `retail\<id>.m2`, drop modern chunks, keep the stock order), groups registered as `retail/<id>_NNN.wmo` dependencies from GFID (the way skins come from SFID), group rewrite (MPY2→MOPY, MOVX→MOVI with splitting, MOBA bounds, chunk order), plus unit tests on the two probed files | Port wotlkconv `wmo/` logic to C++ | **Medium**: about 600–900 lines plus tests | Group splitting when a group passes 65,535 vertices (not hit by room pieces probed so far). Newer shaders fall back to diffuse. Doodads inside room WMOs use the M2 path, whose rejection rate for `12ph_*` is high (existing-assets.md). |
| **B. Room-kit placement** (route b): `gameobject_template` type 14 + `gameobjectdisplayinfo_dbc` rows pointing at `retail\<fdid>.wmo`, and the module spawning a room's RoomComponent rows at the room origin | A; Phase 1 decor plumbing | **Small** once A works | The doorway and prefab model choice (RoomComponentOption) isn't decoded; start with the fixed-model components. |
| **C. Interior map 2783** instead of map 35 | WDT/ADT adapter (D) **or** a WMO-only stand-in WDT | Small with a stand-in; medium with D | Map.dbc ID range; the flat plane is cosmetic, because rooms provide the floor |
| **D. Terrain adapter** (WDT MAID + split ADT → legacy monolithic, in memory) | Port wotlkconv `adt/` | **Large**: about 1,500+ lines; alpha and height-texture handling, MH2O, FDID name tables | Height texturing (MPHD 0x80) has no 3.3.5 equivalent, so terrain looks flatter. Performance with 1,051 doodads per tile, many probably rejected by PrepareModel. |
| **E. Neighborhoods 2735/2736** | D + A + doodad coverage + plot GOs + server heights (OnLoadGridMap or an exporter) | **Large** | Doodad rejection rate, server terrain, and 55 plots each sharing one map instance |

**Recommended order**: A → B (in map 35) → C → D → E.

**Smallest first build step: stand in a real retail room.**

1. Implement A (the `.wmo` adapter) with unit tests on 6431804 + 6432330 and 6431855.
2. Extend `RetailAssetProbe` so `retail/<id>.wmo` and `retail/<id>_000.wmo` go through the adapter, and confirm
   the four Square Room (Small) WMOs prepare.
3. Add 4 GameObjectDisplayInfo rows (60100+) and 4 `gameobject_template` type-14 rows, and spawn the 10
   components of HouseRoom 1 at their RoomComponent offsets, under the player, inside the map-35 house instance.
4. In game, check: walls, floor and ceiling textured; you can't walk through the walls; the floor holds you.

## Probe commands and real output

All probes ran from `G:\3.3.5a - Dev\Data\Retail` with
`P=R:\Users\Zach\Documents\GitHub\wowdecomp\build\retail-satchel\Release\RetailAssetProbe.exe` and
`INI=G:\3.3.5a - Dev\Data\Retail\RetailCasc.ini`. The only writes were to the scratchpad and the existing dev CDN
cache (`Data\Retail\Cache`).

1. **Fetching DB2s by name fails online.** `$P $INI retail/dbfilesclient/houseroom.db2 --extract-table out.db2`
   → `Cannot open Retail file (CascLib error 2)`. The FDIDs come from the community listfile
   (`github.com/wowdev/wow-listfile`, `community-listfile.csv`).
2. **Tables by FDID**: `$P $INI retail/<fdid>.db2 --extract-table <out>`:
   ```
   retail/1349477.db2: 132011 decoded bytes (original CDN data)   Map
   retail/5149690.db2: 375 …     House          retail/5163003.db2: 2141 …   HouseRoom
   retail/5503432.db2: 11886 …   RoomComponent  retail/5503433.db2: 5814 …   RoomComponentOption
   retail/5503435.db2: 1730 …    RoomWmoData    retail/5550624.db2: 502 …    NeighborhoodMap
   retail/5550625.db2: 12984 …   NeighborhoodPlot retail/6378467.db2: 1062 … RoomGridLine
   retail/6931309.db2: 729 …     HouseExteriorWmoData retail/6998392.db2: 706 … HouseTheme
   retail/7149539.db2: 414 …     HouseLevelData
   ```
3. **Decode**: `python -I docs/housing/tools/phase2/dump.py <dir with dbd/ and db2/>`. It uses the repo's
   wotlkconv DB2 reader (`Tools/MountStudio/runtime/reader/wotlkconv/db`) and writes `tables.json`. Layout hashes
   and row counts:
   `House 0x9e44e080 1 · HouseExteriorWmoData 0x95e7a088 8 · HouseLevelData 0xf8146f9d 12 · HouseRoom 0xf04dc279 37 ·
   HouseTheme 0x7307c6e9 17 · Map 0x75863e23 1187 · NeighborhoodMap 0x4973ba38 4 · NeighborhoodPlot 0x3fc4e53 114 ·
   RoomComponent 0xa5c69e52 257 · RoomComponentOption 0x5f9e3aa3 651 · RoomGridLine 0xf36993c5 23 · RoomWmoData 0x5eebe354 40`
4. **Raw files**: `$P $INI retail/<fdid>.bin --extract <out>`:
   ```
   5660542 (2735.wdt) 163908 · 5890928 (2783.wdt) 163908 · 8211407 (2783_31_31.adt) 304256 ·
   8211408 (_obj0) 2076 · 8211410 (_tex0) 8264 · 6155609 (2735_32_24.adt) 466233 · 6155610 (_obj0) 50444 ·
   6155612 (_tex0) 2159815 · 6431804 (floor root) 824 · 6431855 (wall root) 1292 · 6432330 (floor group 0) 59552
   ```
5. **Native read of a WMO**: `$P $INI retail/6431804.wmo` →
   `retail/6431804.wmo: Retail runtime supports M2, SKIN, ANIM and BLP references only`
6. **Room textures**: `$P $INI retail/7225255.blp` → `33940 decoded bytes (client-ready memory)`;
   `6243128.blp` → `350724 …`; `7225256.blp` → `33940 …`
7. **Chunk walk**: `python -I docs/housing/tools/phase2/chunks.py <files>`. The tables in §1 and §3 come from its
   output, for example:
   ```
   5890928: MVER 18 · MPHD flags=0x3ca · MAIN 25 tiles, x 31-35 y 31-35 · MAID 25 non-zero
   8211408: MVER 18 · MDDF 0 defs · MODF 0 defs
   6155610: MVER 18 · MDDF 1051 defs · MODF 21 defs
   6432330: MOGP flags=0x80000009 subchunks=['MOGX','MPY2','MOVI','MOVT','MONR','MOTV','MOBA','MOBS','MOBN','MOBR','MOTV','MOTV','MOTV','MOC2']
   ```
   A one-off height scan gave: 8211407, 256 MCNK, z -2.00..-2.00; 6155609, 256 MCNK, z 140.73..597.54.
8. **Transform feasibility**: `python -I docs/housing/tools/phase2/wmo_probe.py <raw dir>`. It runs in memory and
   writes nothing:
   ```
   root -> 824 -> 724 Status.LOSSY   group -> 59552 -> [43388] Status.LOSSY
   notes: MOTX built from 4 texture FDIDs; 1 shader reset to diffuse; 4 LODs dropped; dropped MAVG MDDI MFED MGI2 MNLD MOMX MOPE;
          MPY2->MOPY 1242 tris; MOBA bounds recomputed; UV layers 4->2; flags 0x80000009 -> 0x02000009; dropped MOBS MOC2 MOGX
   ```

## Report

**Works (inspected or probed):**

- All housing tables and map files are reachable from the pinned build by FDID.
- The maps and the room data model are identified.
- Room textures prepare natively.
- The stock client already spawns a WMO from a GameObject display.
- The server runs maps without terrain files.
- The retail→legacy WMO transform keeps the collision BSP.

**Doesn't work yet:**

- The client can't load any retail WMO, WDT or ADT (no adapter).
- Doorway and prefab model selection (RoomComponentOption) and how exteriors are chosen are not decoded.
- No server collision for retail geometry.
- Nothing was seen in game.

**Files changed (all new and untracked, docs only):**

- `docs/housing/maps-recon.md`
- `docs/housing/room-components.tsv`
- `docs/housing/tools/phase2/{dump.py,chunks.py,wmo_probe.py}`
- `docs/housing/README.md` (status row)

No code, DB or install file was touched. The dev CDN cache gained the fetched originals.

**Flags:** none added.

**Rollback:** delete the files above and revert the README row. The cache entries are ordinary CDN cache and can
be trimmed with the existing cache-trim tool.

**Next build prompt scope (Phase 2a: "native retail WMO + one real room"):**

- **In scope:**
  - `.wmo` preparation in `CascSource::Read`: root, GFID group dependencies, group rewrite, with unit tests
    against the probed FDIDs;
  - `RetailAssetProbe` support for `.wmo`;
  - type-14 GO display rows for the 4 Square Room (Small) pieces;
  - a GM or dev command in `mod-esteria-housing`, behind its flag, that spawns HouseRoom 1's components in the
    current house instance;
  - an in-game checklist (textures, wall collision, floor).
- **Out of scope:** terrain/WDT/ADT, map 2783, neighborhoods, the doorway/option rules, server collision.
- **Done means:**
  - the adapter's tests pass;
  - the probe prepares all 4 roots and their groups;
  - the user reports standing inside a textured retail room that blocks movement.
