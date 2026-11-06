# Phase 2a in-game checklist: one real retail room

What changed:

- **Client:** `Engine/Source/Retail/RetailWmo.cpp` prepares retail `.wmo` roots and groups in memory, so the
  client can load `retail\<fdid>.wmo`.
- **Server:** `mod-esteria-housing` spawns HouseRoom 1, "Square Room (Small)", as 10 type-14 GameObjects:
  1 floor, 4 walls, 4 corners and 1 ceiling, at the retail RoomComponent offsets. They sit 30 yd above the
  Stormwind Vault entry, and the house entry point is now on that floor.
- **Client patch:** the dev patch `Patch-ZZZZZZZZZZZZZZZZZZZZZZZZZHousingDev.MPQ` now also carries
  GameObjectDisplayInfo 60050–60053 (`retail\6431804.wmo`, `6431855`, `6431794`, `6431786`).

Setup is the same as [phase1-checklist.md](phase1-checklist.md): stage realm, realmlist `127.0.0.1:3725`,
`housingEnabled 1`.

Decor you placed in Phase 1 is still saved at the vault floor, now 30 yd below the room. It still spawns there,
outside the new room bounds. Remove it from the panel, or ignore it.

## Checks

| # | Do | Expect |
|---|---|---|
| 1 | Steward → **Take me to my house** (or **Claim a house** on a new account). | You arrive standing on a stone/wood floor inside a 23 × 23 yd square room with four walls and a ceiling. The steward stands next to you. |
| 2 | Look at the floor, walls, corners and ceiling. | All textured: not pink, green or missing. Note any piece that is missing or wrong. |
| 3 | Walk into each wall. | You stop at the wall. |
| 4 | Jump and walk around the floor, including the corners. | You never fall through. If you do fall, you land about 30 yd lower in the Stormwind Vault. Note where you fell. |
| 5 | `.gps` in two opposite corners. | About ±11 yd from the entry x/y. Send both readings. |
| 6 | Place a Folk Rug and a Folk Table (housing panel). | They sit on the retail floor. Placing past the walls says "Out of the room's bounds." |
| 7 | Relog inside. | You come back in the room, standing on the floor. |

## Send back

- `G:\3.3.5a - Dev\Logs\RetailAssets.log`. Any line naming `retail\643…wmo` means a piece failed to prepare.
- `WowClient_crash.log`, if the client crashed.
- The stage server log lines `housing: house … populated instance … with … room pieces`.
- Screenshots of checks 1 and 2.

## Undo

- Client: put back the `Wow.exe` and `Patch-ZZZZZZZZZZZZZZZZZZZZZZZZZHousingDev.MPQ` copies from
  `G:\3.3.5a - Dev\_backups\housing-phase2a-<date>\`.
- Server: re-apply the Phase 1 SQL. `git -C EsteriaWoW-housing diff modules/mod-esteria-housing` shows the
  change; reverting `tools/make_slice.py` and regenerating gives the old `housing_world.sql`. Then
  `python tools/stage.py apply` and restart the stage worldserver with the Phase 1 image `esteria-housing:slice1`.
