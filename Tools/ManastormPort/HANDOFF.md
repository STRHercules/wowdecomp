# Manastorm port: handoff (2026-11-12)

Porting CoA's Manastorm to Esteria. Nothing is committed, nothing is deployed, and no live database has been touched.
All database work so far is in scratch schemas on `ac-database`.

## Decisions (from the user)

- **What to port:** the live module `modules/mod-manastorm-endless`, not the old core `src/server/coa/AscensionManastorm*`.
- **Client UI:** native in our decomp client. Keep CoA's wire protocol (opcodes 0x58D and 0x651–0x6AD).
- **IDs:** keep CoA IDs where Esteria lacks them. Remap only real clashes: items to 3300001+, spells to 3800001+
  (moved below the client's 5,000,000 table limit in Phase 3, which also forces remapping CoA's own IDs above 5M).
  Same ID and same thing (a renamed item, the same spell) keeps Esteria's row.
- **Cache loot:** port Ascension's scaled items (about 35k). **Greedy Demon pool:** port everything (about 117k more items).
- **Deploy target:** Docker dev + `G:\3.3.5a - Dev` only, with a backup first. Never Live.

## Sources

| What | Where |
|---|---|
| CoA core | `R:\Users\Zach\Documents\GitHub\azerothcore-wotlk-coa` (module, `data/coa-world/coa-world-20260912.zip`, `data/sql/updates/pending_db_*`) |
| CoA client | `G:\CoAClient\Client` (DBCs come out of its MPQs) |
| Esteria core | `R:\Users\Zach\Documents\GitHub\EsteriaWoW` (worldserver built in container `housing-build`, see `modules/mod-esteria-housing/tools/build.py`) |
| Work files | `G:\ManastormPort` (`coa-dbc`, `est-dbc`, `est-cont` = live server DBC continuations, `patches`, build and syntax logs) |

## Phase 1: server module compiling (done: worldserver links)

Done in EsteriaWoW (uncommitted):

- **Module:** copied `modules/mod-manastorm-endless` (whole folder, including docs, tests and client overlay).
- **Module-local copies of small CoA files:** `AscensionManastormGadgets.h`, `AscensionManastormProtocol.h`,
  `AscensionRaidReleasePolicy.h`, `NativeClientConfig.h`, `ClientDBC.{h,cpp}`.
- **Two module-local stubs:**
  - `AscensionRaidRelease.cpp`: every raid tier counts as released.
  - `AscensionClientLua.h`: server-pushed Lua does nothing.

  Both carry `shortcut:` comments.
- **Module edits:**
  - `ManastormClientBridge.cpp` sends its own 0x58D config, and CoA's config registry call is removed.
  - The "no Manastorm challenge" entry check is dropped.
  - `ManastormRewards.cpp`: the game mode mask is always 0 (CoA's `coa_character_gamemode` table doesn't exist here), and `GetNextTimeWithDayAndHour` now gets 2 arguments.
- **Core opcodes:** Manastorm opcodes added and `NUM_MSG_TYPES` = 0x6AE (`Opcodes.h/.cpp`). The 3 client requests go to `Handle_NULL`; the module consumes them in `CanPacketReceive`.
- **Core private instances:** ported from CoA (see `G:\ManastormPort\patches\*.diff`):
  - Applied as patches: `Map.h/.cpp`, `MapMgr.h/.cpp`, `MapInstanced.cpp`, `Player.h`, `GridObjectLoader.cpp`, `SpellHandler.cpp`.
  - Merged by hand, because CoA's hunks would have deleted Esteria features:
    - `Creature.h/.cpp`: a `privateData` parameter on `LoadCreatureFromDB`.
    - `Player.cpp`: `RemoveActivePetSpellCooldowns`, and saving the outside position while in a private run. Esteria's `RestoreAllSpellCharges` is kept.
    - `Unit.h/.cpp`: `HasManastormMovementGrace`, checked inside Esteria's `CanCastSpellWhileMoving`.
- **Other core additions:**
  - Script hook `UNITHOOK_RESOLVE_SPELL_CAST` / `ResolveSpellCast`, called at the top of `Unit::CastSpell(targets, ...)`.
  - `SpellMgr::RegisterRuntimeSpell`.
  - 12 `CHAR_*_MANASTORM_*` and 2 `WORLD_SEL_MANASTORM_*` prepared statements (same SQL as CoA).

Status:

- **First full build:** everything compiled except 15 module files. All of their gaps are addressed by the edits above.
- **Syntax re-check: clean** (`G:\ManastormPort\syntax-errors3.txt`), after removing 9 spell-charge declarations that
  the `Player.h` patch had duplicated. Build command (copy `src/` and `modules/mod-manastorm-endless/{CMakeLists.txt,src}`
  into the container first, as the build script does; a `Map.h` change rebuilds most of the server, about an hour):
  `docker exec housing-build sh -c "cd /azerothcore/build && cmake . && ninja -j14 -k0 worldserver"`
- **Reviewed:** CoA's `SpellHandler.cpp` hunk only adds a debug log on spell cancel (no behaviour change).
- **Full build 1** (`G:\ManastormPort\server-build.log`): one error, `Map::CannotEnter` defined inline in `Map.h` and again
  in `Map.cpp`. `Map.h` now only declares it, as CoA does. Every module, including Manastorm, compiled.
- **Full build 2** (`G:\ManastormPort\server-build2.log`): clean, `worldserver` linked in `housing-build`. Phase 1 compiles;
  not run yet. It has not been installed or packaged into an image (that's Phase 4).

## Phase 2: SQL and server DBC data (done, tested on scratch copies)

Tool: `Tools/ManastormPort/manastorm_port.py` (`rebuild | stage | test`), plus `idset.sql`, `stage_rows.sql`, `verify.sql`.

**Result (2026-11-12, second session):** `stage` writes `G:\ManastormPort\stage` (`world.sql` 53 MB, `characters.sql`,
`server/*.dbc1-manastorm`, `report.json`). `test` applied it to `ms_test_world`/`ms_test_characters` and every
`verify.sql` check is 0. Info-only leftovers, all identical in CoA: 4 shrine items point at missing page text, 3,164
Greedy-pool items name random-property groups no table has (core logs it; no random stats), 15 enabled segments name
an objective creature that doesn't exist. Numbers: 125,181 items (11,265 remapped to 3300001+), 5,243 spells (278
remapped to 3800001+), 24 creatures, 25 spawns, 5 gameobject templates, 162 teleports, 83,844 reference-loot rows.

Fixes made while getting there:
- `apply_marker_aura` is a bool, not a spell; it had pulled spell 1 into the closure.
- Esteria's core reads `item_template.ItemLimitCategory` as int16. CoA's 4 categories above 32767 are remapped to
  30001-30004 (table `ms_port_limit`, `report.json`, and the continuation row).
- `world.sql` widens Esteria's `item_template.description` to VARCHAR(1024): 36 Ascension descriptions run to 423 chars.
- The loadout table is in CoA's `rev_20260908_01_manastorm_progression.sql`, not `_00`; both are now included.
- Six gadget-rank spells (300603/300604/300819-300822) clash with Esteria mounts. `AscensionManastormGadgets.h` now
  names their remaps 3800147-3800152; `stage` refuses to run (and prints old -> new) if the C++ names a clashing or
  unknown remap ID.
- `ItemSet` member items and `ItemExtendedCost` required items get the item remaps too.
- DBC reader: junk offsets in unused locale slots are read as empty strings.
- Not remapped (shortcut): creature equipment and SmartAI spell params of the 24 new creatures (none use clashing IDs today).

Background still true:
- `ms_coa_world` = CoA world package + 177 Manastorm SQL files in CoA's filename order; the faithful source.
- Everything Manastorm-specific is absent from Esteria (pure addition). Same-ID-same-thing rows keep Esteria's copy.
- 2,905 of 2,926 segment spawn GUIDs exist in Esteria (56 sit more than a yard off; Esteria's position wins); 21 are added.
- Server DBC rows go in `<Table>.dbc1-manastorm` continuations in `/azerothcore/env/dist/data/dbc-continuations`
  (loaded by mod-wxl-dbc). `stage` reads Esteria's existing continuations when checking for clashes.
- Scratch schemas on `ac-database`: `ms_coa_world`, `ms_stage_world`, `ms_test_world`, `ms_test_characters`.

## Phase 3: client (done, built and tested offline; not installed)

User decision (2026-11-12): **no transplant of Ascension's addon.** It needs a large slice of Ascension's forked
FrameXML/SharedXML. Instead: our own **Esteria Manastorm UI in the style of Paths of Fate / Incarnations**, over a native
API. CoA's art is reused (the portal scene in `Interface\Manastorm\ManastormAtlas.blp`).

CoA's client-side Manastorm files are extracted to `G:\ManastormPort\coa-client\<archive>\...` (addon kept only as a
behaviour reference; art; 4 private DBCs; icons; creature VO; a shop sign).

**Client ID budget:** `ClientTables.cpp` refuses supplemental rows with ID > 5,000,000, so the server remaps moved below
it: items 3300001+ (12 clashes + 11,253 CoA IDs above 5M), spells 3800001+ (278: 235 clashes + 43 above 5M). The gadget
header now names 3800147-3800152; a ManastormMgr.cpp comment names 3800277.

What was built:

| Piece | Where | Result |
|---|---|---|
| Registry tables | `Engine/Source/Retail/ClientTables.cpp`, `CascSource.cpp`, `Tools/RetailAssets/tests.cpp` | 9 more tables merge: SpellCategory, SpellDuration, SpellRadius, SpellRange, ItemSet, ItemLimitCategory, ItemExtendedCost, SpellItemEnchantment, ScalingStatDistribution, GameObjectDisplayInfo. RetailAssetTests pass except the old BareFeet check (fails before this work too). |
| Client stage | `Tools/ManastormPort/manastorm_client.py stage\|pack\|install` -> `G:\ManastormPort\client` | 125,181 Item rows; 256 new ItemDisplayInfo (20,786 CoA displays reuse an identical Esteria look: Ascension renumbered stock displays, so 20,833 item display IDs are moved in the server SQL via `ms_port_display`); 5,235 Spell rows with 221 icons, 443 visuals, 540 kits, 242 effect names, 43 attachments, 23 spell categories; the lookup rows; 4 creature displays, 3 models, 2 gameobject displays (+ server continuations for those and SpellCategory). `patch-Manastorm.MPQ` 789 files (models/textures/icons from CoA, the portal art). |
| Native API | `Extensions/Manastorm/src/Manastorm.cpp` | Handlers for 0x58D, 0x652, 0x65D-0x660, 0x666, 0x67B, 0x67F, 0x688, 0x68A, 0x68B, 0x6AD; senders 0x651/0x665/0x689. Lua `EsteriaManastorm_GetConfig/GetCompletedDepths/GetActive/GetChaoticLink/GetLoadout/GetRewards/Enter/Leave/SetLoadoutSlot`; events `MANASTORM_*`. State is cleared on leaving the world. Client builds clean (`build/retail-assets`, `bin/Win/WowClientRetail.exe`). |
| UI | `Addons/EsteriaManastorm` (+ generated `EsteriaManastormData.lua`) | Window: hero art + mode + group bests; checkpoint picker (every 5th depth every member reached) and Enter; run panel (scene, objective, Chaotic Link, caches, Leave with checkpoint warning); 4 gadget slots with picker; rewards (hidden/locked handled). Tracker while in a run; toasts for cleared depths, unlocks and failure. `/manastorm`, key binding, DragonUI micro button (crystal icon). Tests: `python Tools/EsteriaShell/test_manastorm.py` (37 checks pass). |
| Packing | `Tools/EsteriaShell/build_shell_mpq.py` | Ships the addon + style kit and the micro button; builds (98 files) into scratch. |

Known gaps (all noted where they live):
- XP and reward modifiers per depth (CoA's `ManastormModifiers.dbc`, 32,768 rows) are not shown in the UI.
- Creature voice lines (patch-O) are not shipped: they need SoundEntries rows, which the registry doesn't merge.
- No stock micro bar button (DragonUI strip, slash and key binding only).
- Client-only references zeroed when Esteria lacks them: sounds/camera shakes in spell visuals, 4 SpellDescriptionVariables, item display spell visuals/item visuals/particle colours. 137 asset files are missing in CoA too (80 item displays draw partly). SpellRange 23 exists nowhere.
- Spell tooltips that quote another spell by ID (`$12345s1`) still name CoA's ID for the 278 remapped spells.
- Creature equipment and SmartAI spell params of the 24 new creatures are not remapped (none clash today).

## Phase 4: deploy (done 2026-11-12 on the dev realm + `G:\3.3.5a - Dev`; Live untouched)

The local Docker `ac-worldserver` / `acore_*` IS Live (realm "Esteria", public address). "Docker dev" = the housing
stage realm: `housing_dev_*` clones, `housing-stage` worldserver on 8099, `housing-stage-auth` on 3725
(`mod-esteria-housing/tools/stage.py`). Tool: `Tools/ManastormPort/deploy_dev.py db | image <tag> | start <tag>`.

What ran:
- `db`: backed up `housing_dev_*` (`G:\ManastormPort\deploy-backup-20261112-150649` = the old 11-08 dev DBs; `-152917`
  = the first Manastorm attempt), re-cloned them from `acore_*` (read-only dump), applied `stage/world.sql` +
  `characters.sql`, realmlist 1 -> 127.0.0.1:8099, verify.sql all 0.
- `image esteria-manastorm:dev1`: `ninja install` in housing-build, FROM `esteria-coa:20261111d-housing` (runtime in
  `G:\ManastormPort\runtime-dev1`).
- `start`: the dev realm mounts Live's data volume read-only, so `G:\ManastormPort\dev-data\dbc-continuations` (Live's
  continuations + `*.dbc1-manastorm`) is bound over the folder; CoA's `Manastorm.dbc` is bound at
  `/manastorm/Manastorm.dbc`. Manastorm keys go into the stage `worldserver.conf`: housing-build has no `conf/` for the
  module, so no module config file is registered (all keys fall back to defaults; `Manastorm.Enable` defaults to 1).
- Client `G:\3.3.5a - Dev`: registry `manastorm-96f07664ebc2` (previous `hots-26a5ed9ad10e0a6e`), `patch-Manastorm.MPQ`,
  `Wow.exe` = `bin\Win\WowClientRetail.exe`, shell MPQ rebuilt, `Cache\WDB` moved out. Backup:
  `_backups\manastorm-20261112-151430` (RetailCasc.ini, Wow.exe, shell MPQ, WDB). Realmlist untouched (still Live).

Fixed during deploy (all in the port tools, world.sql regenerated, scratch test re-run):
- Greedy Demon pool: 389 rows name items Esteria already had with another quality; the module refused to load the whole
  datastore. Rows are re-tiered to Esteria's quality, 50 outside uncommon..epic disabled (`stage_rows.sql`).
- `manastorm_Endgame_*` were empty: their seed reads `manastorm_scaling_curve`, which zz_ files fill later in filename
  order. `rebuild` now re-runs the two endgame files last (`ENDGAME_RESEED`); applied to `ms_coa_world` by hand.
- 8 `spell_manastorm_*` script bindings on spells Esteria already has (Arugal's Curse, Hex of Jammal'an, baseline
  tools, reward randomizer) were dropped; now copied.
- Expected at startup: "Mobility support disabled" and "Unable to open SpellCharges.dbc" (Ascension-client-only
  features on opcodes 0x092A/0x0570/0x0571/0x09C5 that our client doesn't handle), "Ignoring grant for unknown or
  disabled Manastorm unlock" / "Ignoring authored spawn" for CoA's disabled segments.

Second pass (same day, after the first in-game test):
- Every model-less item (caches, tokens, card packs) showed the ring icon: `look_key` compared models/textures but not
  the inventory icon, so all of them matched Esteria display 224. The icon is now part of the key (2,934 displays
  added, 17,996 reused). The client was re-staged against the pre-Manastorm registry (RetailCasc.ini restored first,
  old outputs in `G:\ManastormPort\client-manastorm1` / `stage-manastorm1`, Dev `_backups\manastorm-redo-*`); now
  registry `manastorm-c27b14d55aa4` on `hots-26a5ed9ad10e0a6e`.
- Cogsley (gadget vendor, repairs) is not a spawned NPC: item 98074 teaches companion spell 93418, usable only in a
  run. Nothing in the Manastorm tables named the item, so it was not ported; `idset.sql` now seeds items that teach or
  cast a Manastorm-scripted spell (also Millhouse Mixtape 453836, soulbound matrix 2200002). CoA has no source for
  98074 either (Ascension gave it outside the world DB): `.additem 98074` for now.

Original plan (kept for reference):
1. **Rerun if anything changed:** `manastorm_port.py stage`, then `manastorm_client.py stage` (it rewrites
   `stage/world.sql` with the display moves), then `manastorm_port.py test`. Both stage dirs must be empty first.
2. **Server data:** back up `acore_world`/`acore_characters`; apply `G:\ManastormPort\stage\world.sql` and
   `characters.sql`; copy `stage/server/*.dbc1-manastorm` into `/azerothcore/env/dist/data/dbc-continuations`; put CoA's
   `Manastorm.dbc` (`G:\ManastormPort\coa-client\patch-M.MPQ\DBFilesClient\Manastorm.dbc`) where
   `Manastorm.NativeDBCPath` points (native mode refuses to load segments without it); config `Manastorm.Enable = 1`,
   `Manastorm.ClientMode = 2`.
3. **Server binary:** the module in `housing-build` is current (build 3 log: `G:\ManastormPort\server-build3.log`);
   `ninja install` and package an image as `modules/mod-esteria-housing/tools/build.py` does; restart ac-worldserver.
4. **Client:** stop Wow.exe; `manastorm_client.py install --client "G:/3.3.5a - Dev"` (new registry revision + MPQ,
   backup in `_backups\manastorm-*`); back up and replace `Wow.exe` with `bin\Win\WowClientRetail.exe`; rebuild the
   shell MPQ into `Data\Patch-ZzzzzzzzzzzEsteriaShell.MPQ` (back up the old one); clear `Cache\WDB` (item displays moved).
5. **In game:** `/manastorm`, enter depth 1 solo, clear it, leave; set a gadget; check items from a cache show icons
   and equip with the right look.
