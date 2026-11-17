# Garrisons on Esteria: integration notes (2026-11-16)

Kanban: G:\LiveKanban, project **Gameplay Systems / Garrisons**.

## What the handoff is

A UWoW Draenor garrison port, built and run on a different Esteria install (`G:\risServer\Esteria - Client`,
`G:\risServer\EsteriaWoW`). It follows the same pattern as mod-esteria-housing: a server module that hooks the core
through null-by-default function pointers, three custom opcodes, and a thin client extension that passes one text blob
to a Lua addon.

| Piece | Path in handoff | State vs Esteria Live |
|---|---|---|
| Server module (Garrison, GarrisonMgr, DB2 loader, packets, scripts, `.garrison` command) | `server/modules/mod-esteria-garrison` | Not in EsteriaWoW. Includes headers that exist in Esteria's core (RBAC.h, InstanceSaveMgr.h, CreatureTextMgr.h). |
| Core hook files | `server/core/game/Handlers/EsteriaGarrison*.{h,cpp}` | New. Same shape as EsteriaHousingHooks. |
| Core edits | README.txt "Core edits" | Opcodes 0x54A/0x54B/0x54C are free (Esteria stops at 0x549, Manastorm starts at 0x58D). MapInstanced.cpp has the housing branch to sit after (line ~167). |
| Config | `server/etc/modules/esteria_garrison.conf` | Enable = 1. Not on Live. |
| Client extension | `client-source/Extensions/Garrison/src/Garrison.cpp` | Not in wowdecomp. Drops into `Extensions/Garrison/src/`; CMake picks it up automatically; it only uses ExtensionMgr calls that exist. CVar `garrisonEnabled` defaults to 1 (Live has no Config.wtf). |
| Addon | `client/Interface/AddOns/EsteriaGarrison` | Not installed. Opened by `/garrison`, `/garr`, `/garrisonmission`. No EsteriaShell button. |
| Window art | `client/Interface/AddOns/Blizzard_GarrisonUI` (66 MB) | Not installed. Keep its `.toc.bak` name. |
| Client DBC patch | `client/Data/Patch-ZZZZZZZZZZZZZZZZZZZZZZZZZZGarrison.MPQ` | **Do not install as is** (see below). |
| SQL | `sql/characters`, `sql/world`, `data/sql/db-world` | Not applied on Live. No garrison or db2_* tables exist. |

## Problems found

1. **Client DBC patch is built on the wrong base.** It holds four whole DBCs cloned from risServer's winning copies:
   - Map.dbc = risServer's HousingDev Map.dbc (145 rows) + 6 garrison maps. On Live the winning Map.dbc is
     `Patch-ZZZZZZZZZZZZZZZZZZZZZZZZZZZCoAMapsDbc.MPQ` (207 rows). The garrison archive sorts *before* it, so its
     maps would be invisible; renamed to win, it would delete the 68 CoA/imported map rows.
   - CreatureDisplayInfo / CreatureModelData: CoA base + 574 / 252 garrison rows, but missing the 4 rows HousingDev adds.
   - GameObjectDisplayInfo: 8047 rows vs Live's 3811-row winner. Different base.
   - Fix: point `tools/make_maps.py`, `make_buildings.py`, `make_residents.py` at the Live/Dev client (they hardcode
     `G:\risServer\Esteria - Client`), rebuild from Live's winning DBCs, and name the archive so it sorts after
     CoAMapsDbc. All garrison IDs are free in Live's DBCs.
2. **No terrain anywhere.** The six maps (Alliance 1158/1331/1159, Horde 1152/1330/1153) need WDT aliases in the active
   registry (`G:\3.3.5a - Live\Data\Retail\Registries\manastorm-2fb66b3aec40\aliases.csv`; it has only housing's
   2783/2735/2736). The handoff does not list the retail WDT FileDataIDs. The server has no maps/vmaps/mmaps for them
   either (spawns use fixed Z; pathing and LoS will be wrong without them).
3. **The world SQL overwrites stock 3.3.5 rows.** `garrison_uwow_world.sql` deletes and reinserts 19 stock
   creature_templates (Black Rat, Lethlas, Winter Revelers, ...: 191 Live spawns) and 13 stock gameobject_templates
   (Winter Veil gifts/tree, Gypsy Wagon, ...: 555 Live spawns) with UWoW versions. Everything else is in free ranges
   (creature 9601100+, GO 9510016+/9525024+/9602000+, guids 5100000+, displays 66524+/67016+/68100+/700000+,
   waypoints 51000000+), checked against Live acore_world.
4. **The characters SQL is destructive on re-run** (`DROP TABLE IF EXISTS character_garrison*`). Fine for first apply,
   wipes every player's garrison after that.
5. **Players get garrisons only by chat command.** `.garrison create/enter/leave/status` are open to every player
   (RBAC_PERM_COMMAND_HELP). There is no quest, NPC or item that grants one.
6. `tools/garrison_skins.py` writes body-texture rows to `Data/HotS/MountSkins.csv`; that client file is not in the
   handoff. Retail humanoid garrison NPCs may draw white without it.
7. The startup hook moves offline characters off garrison maps if they have no matching garrison; harmless, but it
   writes to `characters` on every boot.

## Install order (once the above are fixed)

1. EsteriaWoW: module + core files + 4 core edits; build in `housing-build`; new `esteria-coa:<date>-garrison` image.
2. acore_characters: `garrison_characters.sql` (back up first). acore_world: `garrison_db2.sql`, `garrison_world.sql`,
   `garrison_uwow_world.sql`, then the rest of `sql/world/*.sql`.
3. `etc/modules/esteria_garrison.conf` with Enable = 1.
4. Client (Dev and Live): Wow.exe with Extensions/Garrison, rebuilt DBC patch, aliases.csv rows, both addons.
