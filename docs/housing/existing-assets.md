# Patch-Housing.MPQ inventory (Phase 0, read-only)

Source: `G:\3.3.5a - Dev\Data\Patch-Housing.MPQ`, a regular MPQ file of 1,206,796,881 bytes with an `MPQ\x1a` header.
Nothing was extracted into the repo or changed. The per-row table is [existing-assets.tsv](existing-assets.tsv),
with 2,906 HouseDecor rows. The scripts that produced it are in [tools/](tools/), described at the end.

## Origin: retail, unconverted

- The MPQ is the **"Official Housing Content Pack"** of `wxl-housing`
  (`R:\Users\Zach\Documents\GitHub\wxl-housing`, origin `github.com/sogyboi/wxl-housing`, v0.8.2).
  Its README and `docs/OfficialContent.md` describe exactly this layout: `world\...` decor, `interface\housing\...`,
  `DBFilesClient\*.db2`. The pack is built by `tools/Build-OfficialHousingContent.ps1` from `retail-decor-raw`,
  `retail-db2` and `retail-housing-ui`. It excludes WMO roots, `custom-assets` and `phase1-furniture`.
- **Origin is retail for every model.** The DB2 sidecars are retail 12.1.0 (`HouseDecor` layout hash `6A051268`,
  which wxl-housing `src/Catalog.cpp:81` names as 12.1.0.68914).
- wxl-housing targets the WarcraftXL runtime, not wowdecomp. It reads `HouseDecor.db2` through the `wxl-db2`
  extension and saves placements only on the client (`placements.tsv`). None of that runs in our client.
  Our client tree has no `Extensions` folder and no housing code.

## Contents

| Kind | Count | Notes |
|---|---|---|
| `.m2` | 2,706 | **All MD21 chunked, versions 274 (2,695) and 272 (11).** None are 3.3.5 MD20/264. |
| `.skin` | 10,314 | Usually only `00.skin` of the LODs the model lists in SFID (for example `1/4`). |
| `.blp` | 6,012 | Retail BLPs, referenced by FileDataID through each model's TXID chunk. |
| `.png` | 2,554 | Catalog thumbnails for wxl-housing's UI. |
| `.db2` | 11 | `house`, `housedecor`, `housedecormaterial`, `housedecorthemeset`, `houseexteriorwmodata`, `houseleveldata`, `houseroom`, `housetheme`, `texturefiledata` (WDC5); `ModelFilePath` (588,723 rows) and `TextureFilePath` (844,199 rows), which are custom 2-field WDC1 FDID→path tables. |
| `.lua` / `.xml` / `.toc` | 66 / 27 / 15 | Retail `Blizzard_Housing*` addons. They need the retail API and can't run on 3.3.5, so treat them as reference only. |

Top folders: `world\expansion11\doodads` (10,618 files), then expansion10/06/07/09/05 doodads.
`world\wmo\expansion11` holds 211 files (WMO groups only, no roots).

## Can the client load these?

**Not by their MPQ paths.** The stock loader rejects anything that isn't MD20 version 264
(`Engine/Source/Model2/M2Shared.cpp:1683` checks the magic, `:1687` checks `m_version` == 0x108).
A GameObjectDisplayInfo row pointing at `world\expansion11\...\12ph_folk_chair01.m2` would load the raw
MD21 bytes from this MPQ and fail.

**By FileDataID, yes, through the existing Retail source:**

- SFile asks the Retail source first for any name it `Handles` (`Engine/Source/BlizzardCore/SFile2-Core.cpp:708`, `:1172`).
- `Handles` accepts `retail/<id>.*` and any alias row (`Engine/Source/Retail/CascSource.cpp:475-479`).
  Aliases come from `aliases=` in `RetailCasc.ini`, as `FileDataID;path` rows (`CascSource.cpp:399-416`).
- `CascSource::Read` fetches the original bytes, runs `PrepareModel` (MD21 to a legacy layout), and resolves the
  SFID skins and TXID textures as `retail/<id>.skin|.blp` (`CascSource.cpp:550-626`).
- Original bytes come from, in order:
  1. packed `EsteriaRetail\<product>-<build>\<locale>\<id>.bin` inside MPQs (`CascSource.cpp:180-193`, reader installed at `SFile2-Core.cpp:160`);
  2. the decoded cache;
  3. CASC/CDN.
- The dev client is built with `WOWDECOMP_RETAIL_ASSETS=ON` (`build/retail-satchel/CMakeCache.txt`).
  `Data/Retail/RetailCasc.ini` pins `build=88d5e8ef…` (retail 12.1.5.70448 per `docs/retail-race-handoff.md:141`),
  `mode=online`, and `aliases=Registries/a679d9593d3ee82c/aliases.csv`. The aliases file has 7 rows, none for housing.

So `Patch-Housing.MPQ`'s model bytes are not on the load path at all. What it gives us is the **FDID↔path map**
and the **decor catalog** (HouseDecor). A decor model reaches the screen via `retail\<ModelFileDataID>.m2`, with
bytes from CDN or cache. Generic-doodad `PrepareModel` (not character) keeps "strict unsupported-shader
rejection" (`Engine/Source/Retail/CascSource.h:33-35`), so **some decor may refuse to load**. Nothing has been
tested in game.

Repacking the MPQ's bytes as `EsteriaRetail\…\<fid>.bin` would make them load offline, but only if the bytes
match the pinned 12.1.5 build. The MPQ came from 12.1.0, so they may differ. Flagged as an open question.

## Summary

| | Rows |
|---|---|
| HouseDecor rows | 2,906 |
| **OK** (M2 in MPQ, ≥1 skin present, every TXID texture is in MPQ by path) | **2,686** |
| of which `12ph_*` purpose-built housing models | 745 |
| `wmo` (ModelType WMO, roots not shipped) | 65 |
| `model-not-in-mpq` (FDID→path known, file absent) | 103 |
| `no-skin` and/or `tex-missing` | 36 (mostly old `world\generic\…` paths, see below) |
| `fdid-not-in-ModelFilePath` | 13 |
| `no-model` (ModelFileDataID 0) | 3 |
| Existing GameObjectDisplayInfo rows pointing at a decor model | **0** |
| M2s in MPQ that no decor row uses (dependencies or variants) | 23 |

Categories of OK rows (name heuristic, same token idea as wxl-housing `Catalog.cpp:166-194`):
Misc 1,188 · Furniture 418 · Accents 346 · Lighting 307 · Nature 277 · Structural 150.

The client GameObjectDisplayInfo checked was the top archive's copy (`Patch-ZZZZZZZZZZZZZZZZZZZZZZZZCoA.MPQ`,
3,811 rows, max ID 1,081,385). None of its rows use a `retail\` path or a decor path. Server
`acore_world.gameobject_template` has no housing rows, and no table named `*hous*`/`*decor*` exists except
`mod-guildhouse`'s.

### Flags to know

- **Stock-path overrides.** Some decor reuses old stock paths, such as
  `world\generic\human\passive doodads\chairs\generalchairloend01.m2`. This MPQ sorts above every base
  archive, so it may **replace a working 3.3.5 model with an MD21 file the loader rejects**. That would break
  stock world doodads anywhere they appear. The size of this is in [Shadowing](#shadowing) below.
- **Duplicate FDIDs.** Several decor IDs share one model (for example 494, 508 and 744 are all
  `12ph_folk_chair01.m2`). Key GameObjectDisplayInfo by FDID, not by decor ID.
- **HouseDecor columns decoded without a DBD.** `ID`, `ModelFileDataID` (`field_7`), `ItemID` (`field_10`) and
  `ModelType` (`field_6`: 1 = M2 for 2,839 rows, −2 for 65 rows = the WMO count) cross-check. `Type`
  (floor/wall/ceiling) and `Name_lang` did not decode reliably, so placement surface comes from the model name.
  Get the WoWDBDefs `HouseDecor.dbd` before relying on more columns.
- **Rights:** every row is retail-origin, so dev-only, per the README.

## Proposed Phase 1 slice (20 items, untested in game)

All are `12ph_folk_*`, status OK, distinct FDIDs. The placement surface is inferred from the name.

| Decor | FDID | Model | Category | Surface |
|---|---|---|---|---|
| 498 | 6375820 | 12ph_folk_rug01 | Furniture | floor (rug) |
| 1044 | 6390440 | 12ph_folk_rug02 | Furniture | floor (rug) |
| 496 | 6373543 | 12ph_folk_table01 | Furniture | floor |
| 497 | 6373544 | 12ph_folk_table02 | Furniture | floor |
| 494 | 6372825 | 12ph_folk_chair01 | Furniture | floor |
| 1991 | 6390428 | 12ph_folk_stool01 | Furniture | floor |
| 495 | 6373411 | 12ph_folk_bed01 | Furniture | floor |
| 373 | 6008928 | 12ph_folk_tallshelf01 | Furniture | floor |
| 374 | 6008929 | 12ph_folk_wallshelf01 | Furniture | wall |
| 390 | 6046867 | 12ph_folk_bookcase01 | Accents | floor |
| 379 | 6008935 | 12ph_folk_foodplate01 | Accents | on table |
| 1123 | 6390444 | 12ph_folk_barrel01 | Accents | floor |
| 1994 | 6390443 | 12ph_folk_crate01 | Accents | floor |
| 383 | 6008939 | 12ph_folk_standinglamp01 | Lighting | floor |
| 15287 | 7338170 | 12ph_folk_walllamp01 | Lighting | wall |
| 377 | 6008933 | 12ph_folk_chandelier01 | Lighting | ceiling |
| 1435 | 6390441 | 12ph_folk_fireplace01 | Lighting | floor/wall |
| 388 | 6008944 | 12ph_folk_walldecor01 | Structural | wall |
| 520 | 6383338 | 12ph_folk_roseflower02 | Nature | floor/on table |
| 1280 | 6390437 | 12ph_folk_chest01 | Misc | floor |

**Phase 1 probe (2026-11-05):** `RetailAssetProbe` loaded 17 of 20. 6390443 crate01, 6390441 fireplace01 and 6390437 chest01 failed with "M2 lookup index is out of range". So did several other candidates: 6712066, 6379069, 6435027, 6905476, 6379087, 6390436, 6390439 and 6390448. The swaps that load are 7338177 folk_crate02 (decor 12215), 6033664 rugged_fireplace01 (decor 81) and 6390442 folk_pillow01 (decor 1701). The rejection rate for `12ph_*` models is high, and the PrepareModel cause hasn't been investigated.

Phase 1's first step is a load check of these 20 FDIDs with `RetailAssetProbe.exe` (`docs/retail-casc.md:144-146`).
Swap any that `PrepareModel` rejects.

## Shadowing

`tools/shadow.py` looked up every `.m2`/`.skin`/`.blp` path from this MPQ in every other `Data\*.MPQ`.
The client loads later names first (case-insensitive descending sort of `patch-*` archives, `WoW/Source/WowServices/PatchFiles.cpp:9-11`, `:74`;
base archives rank below every patch), so
`Patch-Housing.MPQ` sits above all base archives and below `Patch-J…` through `Patch-Zzz…`.

| | Paths |
|---|---|
| Housing paths that also exist in another archive | 853 |
| Housing overrides a lower archive | 506 (254 blp, 131 m2, 121 skin) |
| Housing is itself overridden by a higher archive | 397 |
| **`.m2` where housing is the winning copy** | **119** |
| of which replace a **stock 3.3.5** model (common/expansion/lichking/patch/patch-2/patch-3) | **99**, listed in [stock-overrides.txt](stock-overrides.txt) |
| of which replace a model from `patch-D.mpq` (custom) | 20 |

**Likely live bug in the dev client:** each of those 119 stock world models now resolves to an MD21 file that
`M2Shared.cpp:1687` rejects. Examples include Dalaran tables, benches and sconces, Zangarmarsh mushroom trees,
Shattrath lights, Duskwood coffins and `generalchairloend01`. Expect them to be **missing or invisible
wherever they're placed in the world**. Their `00.skin` and BLPs are replaced too. I haven't confirmed this
in game. Quick check: Dalaran (`dalaran_table_01`, `dalaran_bench01`) or Zangarmarsh (`zangartree*03`),
with and without `Patch-Housing.MPQ`.

Fix options: either rebuild the MPQ without those paths, or remove the MPQ from the dev client entirely.
The client doesn't need it to render decor; models come by FDID.

## Reproduce

The scripts in [tools/](tools/) read only. Each one reads and writes its inputs and outputs **beside itself**,
so copy them to a scratch folder first. Run them in this order:

1. `dump_listfile.py` writes `listfile.txt`.
2. `m2scan.py` writes `m2scan.json` with headers, chunks, SFID/TXID and skins present.
3. `tables.py` writes the DB2s and `ModelFilePath.json`/`TextureFilePath.json` (about 2 minutes).
4. `inventory.py` writes `existing-assets.tsv` and `stats.json`.
5. `shadow.py` writes `shadow.json` (slow, because pure-Python hashing runs against every archive).

They use `Tools/VulkanShaders/bls/mpq_extract.py` and `Tools/MountStudio/runtime/reader/wotlkconv/db/db2.py`.
