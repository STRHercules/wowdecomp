# Housing plan (Phase 0 output)

Architecture: **hybrid** ([feasibility.md §4](feasibility.md#4-a-vs-b-vs-hybrid)).
- Server: `mod-esteria-housing` plus a minimal core opcode edit.
- Decor: temporary GameObjects in a per-house instance, with models from `retail\<FDID>.m2`.
- Client: a new `Extensions/Housing` editor talking over opcodes 0x537–0x540 ([protocol.md](protocol.md)).
- Schema: [schema.sql](schema.sql).

Everything sits behind `EsteriaHousing.Enable = 0` (worldserver.conf) and the client CVar `housingEnabled 0`.

## Environment found (fill in the prompt placeholders from this)

| Item | Value | Source |
|---|---|---|
| Server checkout | `R:\Users\Zach\Documents\GitHub\EsteriaWoW` (AzerothCore with Esteria core changes) | — |
| Running worldserver | Docker `ac-worldserver`, image `esteria-coa:20261104f` (`docker-compose.twoforms.yml`) | `docker ps` |
| Server build | Copy `src/` and the module into the `coa-lead` container, then `cd /azerothcore/build && cmake . && ninja -j12 -k0 worldserver && ninja install`. Package with `package_runtime.py esteria-coa:<tag>`. | `modules/mod-classless-wildcard/tools/pipeline/verify_build.py`, `package_runtime.py` |
| DB | MySQL 8.4 in `ac-database`, `127.0.0.1:3306`, root password read from the container env | `docker-compose.override.yml`, `start_stage.py` |
| Databases | `acore_auth/characters/world/playerbots`, which **are the running realm's DBs**, plus `coa_b7_*` clones from a previous stage | `SHOW DATABASES` |
| **Dev DB** | **None exists.** Proposed: clone to `housing_dev_{auth,characters,world,playerbots}` with `backup_db.py` + `stage_databases.py`, then run a stage worldserver on a separate container (`start_stage.py` pattern, port 8095) | pipeline README |
| Client build | `cmake --build build/retail-satchel --config Release --target WowClient` → `bin\Win\WowClientRetailInventory.exe` → `G:\3.3.5a - Dev\Wow.exe` | existing dev-client deploy notes; `build/retail-satchel/CMakeCache.txt` (`WOWDECOMP_RETAIL_ASSETS=ON`) |
| MPQ origin / load path | Retail (wxl-housing content pack). Load path: **none**, because its MD21 files can't load by path ([existing-assets.md](existing-assets.md)) | — |

## Milestones

### M0: decide the blockers (you, before Phase 1)

Done on 2026-11-05; see "Decisions after Phase 0" in the README. Patch-Housing.MPQ was removed from the dev client.

### M1: asset path proven (no gameplay)

- Add GameObjectDisplayInfo rows 60000+ (`retail\<fdid>.m2`) for the 20 slice FDIDs, in a new dev-only patch
  MPQ, plus `gameobjectdisplayinfo_dbc` rows on the stage server.
- Add `gameobject_template` 9500000+ (type 5 generic, no flags).
- **I run:**
  - `RetailAssetProbe.exe <RetailCasc.ini> retail/<fdid>.m2` for each of the 20, expecting all to load.
  - SQL dry run on a clone.
- **You run:** `.gobject add 9500000` … on the stage realm, using the dev client.
  - Check: model visible, textures right, you can't walk through it.
  - Send `Logs\RetailAssets.log` and `WowClient_crash.log`, plus a screenshot.

### M2: server module skeleton

- Add `mod-esteria-housing`: config flag, schema applied to `housing_dev_*`, catalog loader, house claim via
  gossip NPC, enter/leave instance.
- **I run:**
  - stage build (`verify_build.py` pattern);
  - migration apply, then rollback, on `housing_dev_*`;
  - worldserver starts with the flag on and off.
- **You run:** claim a house, enter, exit, relog inside. Send `Server.log` lines tagged `housing`.

### M3: protocol and validation

- Add opcodes 0x537–0x53E (core `Opcodes.h/.cpp`) and the edit handler with the full validation order.
- **I run:**
  - unit tests for each error code (bad IDs, out of bounds, over budget, non-owner, duplicate seq, stale
    revision, NaN transform, rate limit);
  - a packet harness that sends raw CMSGs over a test session and checks the acks.

### M4: client editor (MVP)

- Add `Extensions/Housing`: handshake, catalog request, a Lua panel (list, Place, Remove, error toasts), a ghost
  from the GO template via the Spell world-object path or a direct `CreateModel`, a `World::Intersect` cursor,
  and 15° snap.
- **I run:** the client build, and an extension unit test for packet encode/decode.
- **You run:**
  - place one of each surface type;
  - put the food plate on the table;
  - rapid-place 20 times;
  - relog;
  - restart the server;
  - a second character on the same account sees the same layout;
  - a second account visits via group and **can't** edit.
  - Send `Logs\Housing.log` (new), `Server.log`, and screenshots.

### M5: Phase 1 done

- Update the README status table and write the report.

## Tests split

| I can run | You must run in game |
|---|---|
| Server and client builds (real command output) | Model visibility, textures, collision |
| Migration apply + rollback on `housing_dev_*` | Claim, enter and exit flow |
| Validation unit tests, packet harness | Ghost cursor feel, snap, surface placement |
| `RetailAssetProbe` load checks per FDID | Relog, restart, second character, visitor |
| SQL lint (throwaway MySQL 8.4 container, as done for schema.sql) | Performance with about 100 decor in one room |
