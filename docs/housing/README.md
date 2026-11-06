# Esteria Housing — Implementation Guide

Retail-style player housing for Esteria (AzerothCore 3.3.5a server + wowdecomp client, build 12340).
This folder holds the plan and the agent prompts that drive it. Phase outputs (design docs, inventory,
protocol, schema) land here too as each phase completes.

## How to run it

Each phase is one agent session with one scope. Send a prompt, review its report, then send the next.
Never merge phases into one prompt — the work won't fit in a session and quality drops.

| Order | Prompt | Changes code? | Output |
|-------|--------|---------------|--------|
| 1 | [prompts/phase0-recon.md](prompts/phase0-recon.md) | No | `existing-assets.md`, `parity.md`, `feasibility.md`, `protocol.md`, `schema.sql`, `plan.md` |
| 2 | [prompts/phase1-slice.md](prompts/phase1-slice.md) | Yes (`housing-slice` branches) | Playable slice + report |
| 2b | ~~[prompts/assets-gapfill.md](prompts/assets-gapfill.md)~~ superseded | — | Retail assets load natively; no conversion |
| 3 | [prompts/phase2-maps-recon.md](prompts/phase2-maps-recon.md) | Probes only | `maps-recon.md`: retail housing maps, room WMOs, native loading plan |

2 and 2b can run in parallel after Phase 0; they touch different files.

## Before sending Prompt 1

Fill the `<…>` placeholders in the prompts:

- Server checkout path and worldserver build command.
- Dev DB host/port and access method.
- **Patch-Housing.MPQ origin** (converted retail / custom / mixed) and **current load path**
  (none / GameObject entries / DBC rows). Leave `<unknown>` if unsure; Phase 0 will find out.

## Fixed decisions

- **Assets:** Phase 1 décor is the retail HouseDecor catalog inventoried from Patch-Housing.MPQ
  ([existing-assets.md](existing-assets.md)), loaded as `retail\<FDID>.m2` through the Retail CDN source. The MPQ
  itself is removed from the dev client (see the decisions after Phase 0). Stock models only fill gaps.
- **Rights:** retail-origin assets are dev-only until a distribution decision is made. Phases list them, never ship them.
- **Server code:** new module `mod-esteria-housing` unless Phase 0 finds an existing property system.
- **Installs:** work only in `G:\3.3.5a - Dev`; never touch `G:\3.3.5a - Live`. Back up before replacing files.
- **Feature flags:** everything behind a worldserver.conf flag + client CVar, default off.
- **Tooling:** reuse `Tools/RetailAssets`, `Tools/MountStudio`, the `wow-retroport` skill, and the HD render
  paths before adding anything new.

## Rules every phase follows

- Cite `file:line` for every engine claim; say "not found" rather than guess.
- Show exact build/test commands with real output. Separate: inspected / implemented / compiled / tested in game.
- The agent can't drive game clients. In-game checks become a checklist for the user, with the logs to send back.
- Server never trusts client-supplied IDs, quantities, transforms, or permissions.
- No DB writes outside the dev DB, and only after showing the SQL.
- Each report ends with: what works, what doesn't, files changed, flags, rollback steps, next step.

## Later phases

Write these prompts only after Phase 1 works, using what Phases 0–1 found. Same shape: scope / out of scope / done means.

1. **Advanced editing** — gizmos, X/Y/Z translate, free rotation, scale, grid snap, parenting, undo/redo.
2. **Rooms and finishes** — room graph, doors/stairs, wall/floor/ceiling finishes, dyes.
3. **Neighborhoods and sharing** — plots, permissions, visitors, blueprints, acquisition, house level.
4. **Polish** — performance/LOD, launcher packaging, larger catalog, GM tools.

## Status

| Phase | State |
|-------|-------|
| 0 Recon | **Done 2026-11-05.** Outputs: [existing-assets.md](existing-assets.md) (+ `.tsv`, `stock-overrides.txt`, `tools/`), [parity.md](parity.md), [feasibility.md](feasibility.md), [protocol.md](protocol.md), [schema.sql](schema.sql) (draft, linted in a throwaway container, not applied), [plan.md](plan.md). Recommends hybrid: stock GO + per-house instance + custom edit opcodes 0x537–0x540. Blockers answered; see "Decisions after Phase 0" below. Phase 1 is ready to send. |
| 1 Slice | **Done; the user ran the in-game checklist on 2026-11-05 and reported every check passing.** Branch `housing-slice` in both repos (server in the `EsteriaWoW-housing` worktree). Both build. Rules unit tests, the addon Lua test and the SQL apply/rollback test pass. The stage realm `housing-stage` (8099) with `housing-stage-auth` (3725) runs on `housing_dev_*`. The in-game checklist is [phase1-checklist.md](phase1-checklist.md). 17 of the 20 proposed FDIDs loaded. crate01, fireplace01 and chest01 failed with "M2 lookup index is out of range" and were swapped for folk_crate02, rugged_fireplace01 and folk_pillow01. The interior is map 35 (Stormwind Vault). |
| 2b Assets | Superseded by Phase 2 recon (native loading, 2026-11-05) |
| 2 Maps recon | **Done 2026-11-05 (inspected + probed, nothing in game).** [maps-recon.md](maps-recon.md), [room-components.tsv](room-components.tsv), `tools/phase2/`. Neighborhoods are maps 2735/2736, the interior is 2783 (an empty flat plane, with rooms assembled at runtime from floor/wall/ceiling WMOs). The client can't load retail WMO/WDT/ADT yet. Recommends an in-memory `.wmo` adapter in `CascSource::Read`, with rooms spawned as type-14 GameObjects. The next prompt is "Phase 2a: native retail WMO + one real room" (scope at the end of maps-recon.md). |
| 3a Rooms and doorways | **Built and deployed 2026-11-06; not yet tested in game.** There are 12 generic retail room kinds (`mod-esteria-housing/data/room_kit.tsv`, made by `tools/phase3/make_kit.py`). The steward adds and removes rooms. Joined walls turn into the retail door (3 yd) or wide (11 yd) variant. All 51 kit WMOs prepare. Decoded rule: RoomComponentOption by MeshStyleFilterID + Theme; Type 1 SubType 1/2 = door/wide, measured from the group geometry. Corner rotation is fixed (retail OffsetRot is clockwise). Checklist: [phase3a-checklist.md](phase3a-checklist.md). |
| 2a Retail WMO + one room | **Done 2026-11-05. The user confirmed in game:** the steward sends you into the single retail room above the vault, and furniture places inside it. `CascSource::Read` now prepares `.wmo` (`Engine/Source/Retail/RetailWmo.cpp`, tests `RetailWmoTests`). 43 of 47 distinct RoomComponent WMOs prepare. The 4 `rugged_prefab_l01` files aren't in the pinned build (CascLib "file not found"). The stage realm (`esteria-housing:slice2a`) spawns HouseRoom 1 as 10 type-14 GameObjects 30 yd above the vault entry. Checklist: [phase2a-checklist.md](phase2a-checklist.md). |

### Phase 0 findings that change the fixed decisions

- **Patch-Housing.MPQ models can't load by path.** All 2,706 are raw retail MD21, and the loader accepts only
  MD20 v264 (`M2Shared.cpp:1683-1687`). Decor has to use `retail\<FDID>.m2` through the existing Retail source,
  which fetches from the CDN or cache. The MPQ provides the FDID map and the catalog, not the bytes. Phase 1's
  "decor_catalog rows reference existing Patch-Housing.MPQ paths" must become "reference FDIDs".
- **Patch-Housing.MPQ overrides 99 stock 3.3.5 models** (and 20 from patch-D) with unloadable copies. See
  existing-assets.md → Shadowing.
- **There's no dev DB.** `acore_*` are the running realm's DBs. Phase 1 needs `housing_dev_*` clones and a
  stage worldserver.

### Decisions after Phase 0 (answered 2026-11-05)

1. **Dev DB:** clone `acore_*` into `housing_dev_{auth,characters,world,playerbots}` and run a separate stage
   worldserver container, following the `start_stage.py` pattern in
   `EsteriaWoW/modules/mod-classless-wildcard/tools/pipeline/`. Never write to `acore_*`.
2. **Patch-Housing.MPQ: removed** from `G:\3.3.5a - Dev\Data` on 2026-11-05. It was moved, not copied, to
   `G:\3.3.5a - Dev\_backups\housing-mpq-removal-20261105\` (SHA-256 `46e13f96…a34`; verify with
   `sha256sum -c Patch-Housing.MPQ.sha256` in that folder). This ends its 119 stock-model overrides.
   Inventory data stays in [existing-assets.tsv](existing-assets.tsv). To restore it, move the file back into
   `Data\`.
3. **Assets come from the CDN** (`mode=online` in `Data/Retail/RetailCasc.ini`). No offline packing for the slice.
4. **Interior:** reuse an existing small instanced map. Phase 1 proposes which one, with evidence.
5. **Visitors:** add a **small core hook** that lets `mod-esteria-housing` pick the destination instance ID
   (in `MapInstanced::CreateInstanceForPlayer`, EsteriaWoW `src/server/game/Maps/MapInstanced.cpp:140-174`).
   Keep it minimal, off by default, and a no-op when the module is disabled.
