# Phase 3a in-game checklist: several retail rooms joined by doorways

What changed:

- **Rooms:** a house is now a list of retail rooms. There are 12 generic kinds: Square Small, Tiny, Medium and
  Large, Hallway, Closet, T, L, Cross, and Small, Medium and Large Octagon. They are built from the retail
  RoomComponent and RoomComponentOption rows (Generic theme) in `mod-esteria-housing/data/room_kit.tsv`.
- **Joining:** where two rooms touch, both walls become a doorway. The doorway is the retail wall with a 3 yd
  door, or the one with an 11 yd opening when one side is a hallway-width wall.
- **Rotation fix:** piece rotation now turns the way retail's tables mean it. Phase 2a turned it the other way, which
  put the corner pillars and two walls about 0.7 yd outside the room.
- **No client code change.** Only the dev patch MPQ gained display rows for the kit models.

Setup is the same as [phase1-checklist.md](phase1-checklist.md).

## Checks

| # | Do | Expect |
|---|---|---|
| 1 | Enter your house. | Same single room. Corners now sit flush inside the room, with no pillars poking out past the walls. |
| 2 | Steward → **Add a room...** → **Hallway**. | A list of free walls, such as "Room 1 (Square Room (Small)), north wall". |
| 3 | Pick one. | The menu closes. A hallway appears through that wall, and the wall between them has an 11 yd opening. Walk through it. |
| 4 | **Add a room...** → **Square Room (Small)** → pick the hallway's far end. | A second room. You walk from room 1 through the hallway into room 2. |
| 5 | Add a **Square Room (Small)** directly against another small room's wall. | The shared wall shows a 3 yd door on both sides. Walk through it. |
| 6 | Try a large room where it would overlap another. | That wall isn't offered, or the list says "That room does not fit against any free wall." |
| 7 | Place decor in the new rooms. | Placing works in every room. Placing outside all rooms says "Out of the room's bounds." |
| 8 | **Remove the newest room.** | It disappears. The doorway it used turns back into a solid wall. If you were standing in it, you're moved to the entry. With decor inside it, the steward says "Remove the decor in that room first." |
| 9 | Relog, and restart the stage server. | The layout is unchanged. |
| 10 | Visitor in your group. | Sees the same rooms. Their steward shows only "Leave the house." |

## Send back

- `Logs\RetailAssets.log`. Lines naming `retail\…wmo` mean a piece failed to prepare.
- Screenshots of a door and a hallway opening, from both sides.
- Anything misaligned, such as a gap or overlap between joined walls, with `.gps` at that spot.

## Undo

- Client: restore `Patch-ZZZZZZZZZZZZZZZZZZZZZZZZZHousingDev.MPQ` from
  `G:\3.3.5a - Dev\_backups\housing-phase3a-20261106\`.
- Server: restart the stage on `esteria-housing:slice2a` and re-apply the Phase 2a `housing_world.sql` from git
  (`git -C EsteriaWoW-housing show 376b0ca6f:modules/mod-esteria-housing/sql/world/housing_world.sql`).
- `housing_room_layout` can stay, because the Phase 2a code ignores it.
