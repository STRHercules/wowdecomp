# Phase 3b in-game checklist: Home Interior and Founder's Point

What changed:

- **Terrain loading:** the client now loads retail terrain natively. `Engine/Source/Retail/RetailTerrain.cpp` converts
  a retail WDT and each split tile (root, `_obj0`, `_tex0`) in memory into the 3.3.5 layout. `World\Maps\2783\2783.wdt`
  and `World\Maps\2735\2735.wdt` reach the retail files through two new rows in `Data\Retail\...\aliases.csv`.
- **New maps:** 2783 "Home Interior" (a party instance per house) and 2735 "Founder's Point" (open world) are in the dev
  patch's Map.dbc and in `housing_dev_world`. Map 35 is back to stock.
- **Houses:** houses are now built on Home Interior at retail's entry point (−1000, −1000). The floor is at z 0, and
  the map's own flat terrain sits 2 yd below it.
- **Steward:** "Visit Founder's Point" works from anywhere outside a house. A steward at the neighborhood's entry
  offers "Leave Founder's Point" and the house options.
- **Not loaded yet:**
  - water (MH2O), which is dropped;
  - terrain sound emitters;
  - the low-detail far terrain (WDL);
  - doodads whose retail models the M2 loader rejects. These are invisible; they leave lines in
    `Logs\RetailAssets.log`.

Setup is the same as [phase1-checklist.md](phase1-checklist.md). The first load of each map fetches its tiles from the
CDN, so expect a long loading screen, especially for Founder's Point (210 tiles, about 1,000 doodads each).

## Checks

| # | Do | Expect |
|---|---|---|
| 1 | Steward (Stormwind) → **Claim a house**. | Loading screen, then you stand in a Square Room (Small). Under or outside the room is a flat retail plane, not the void. |
| 2 | Add a hallway and a room, as in Phase 3a. | Same as before, now on map 2783. `.gps` shows map 2783. |
| 3 | Walk out through a doorway gap, if there is one, or jump off a room edge. | You drop about 2 yd onto the flat terrain and can walk on it. |
| 4 | Steward → **Leave the house**, then a steward → **Visit Founder's Point**. | Loading screen, then Founder's Point terrain: hills, road and textures. `.gps` shows map 2735 near 3807, −160. |
| 5 | Look around Founder's Point. | Ground textured, not white, green or black. Trees, rocks and houses mostly present. Note big gaps, and whether textures look right. |
| 6 | Walk and run around. | You walk on the ground. No falling through, no invisible walls. |
| 7 | Steward at the entry → **Leave Founder's Point**. | Back where you started. |

## Send back

- `G:\3.3.5a - Dev\Logs\RetailAssets.log`, after visiting both maps.
- `WowClient_crash.log`, if the client crashed.
- Screenshots of the house floor with terrain around it, and of Founder's Point, both close up and wide.
- How long each first loading screen took.

## Undo

- **Client:** restore `Wow.exe` and `Patch-ZZZZZZZZZZZZZZZZZZZZZZZZZHousingDev.MPQ` from
  `G:\3.3.5a - Dev\_backups\housing-phase3b-20261106\`. That folder also holds `aliases.csv`; copy it back to
  `Data\Retail\Registries\a679d9593d3ee82c\`.
- **Server:** restart the stage on `esteria-housing:slice3a`, and re-apply `housing_world.sql` from commit
  `fecfabb26`.
