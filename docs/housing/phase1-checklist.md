# Phase 1 in-game checklist

You need two accounts (owner A, visitor B) and the stage realm running (`tools/stage.py start <image>`, see the
report). Everything runs against `housing_dev_*`. The live realm and `acore_*` are not involved.

## Setup (once)

1. Point the dev client at the stage authserver. Copy `G:\3.3.5a - Dev\Data\enUS\realmlist.wtf` aside first; today
   it reads `set realmlist planetkai.hopto.org`. Then change it to `set realmlist 127.0.0.1:3725`.
2. Turn the client flag on. Either type `/console housingEnabled 1` in game (it persists in `WTF\Config.wtf`), or
   add `SET housingEnabled "1"` to Config.wtf.
3. Log in as A. The realm is "Esteria Housing Dev". As a GM, go somewhere public and type `.npc add 9500100` to
   spawn the Housing Steward. The spawn is saved to `housing_dev_world` only.

## Checks

| # | Do | Expect |
|---|---|---|
| 1 | Models. A: steward → **Claim a house**. | Loading screen into Stormwind Vault. A steward stands inside. The housing panel opens on the right. |
| 2 | Place one of each surface type: Folk Rug, Folk Table, Folk Wall Shelf (press Fwd/Up until it sits on a wall), Folk Chandelier (Up), Folk Standing Lamp. | Each model is visible with correct textures, not pink and not invisible. Try walking into the table. Report whether you collide. |
| 3 | Put the Folk Food Plate on the table. Place it, Move here while standing at the table, then Up until it rests on the top. | It sits on the table. |
| 4 | Rotate: +15 / -15 a few times. | Turns in 15° steps. |
| 5 | Rapid placement: click Place about 20 times fast with the Folk Rug or Pillow selected. | 5 of each item, then "You have none of that decor left." No duplicates, nothing lost. The status line says "sending N" and drains. |
| 6 | Bounds: walk far from the entry (or Fwd many times). | Past about 50 yd, or 15 below / 25 above the entry, "Out of the room's bounds." Note `.gps` at each room corner so the box can be fixed. |
| 7 | Remove two items. | They vanish, and their counts go back up in the list. |
| 8 | Relog inside the house. | You come back inside the same house with the same layout. |
| 9 | Second character on account A: log in, steward → **Take me to my house**. | Same layout. Editing works. |
| 10 | Visitor: B invites A to a group, or A invites B. B: steward → **Visit A's house**. | B sees A's layout. B's panel (open with `/housing`) refuses edits with "You can only edit your own house." B can't claim A's house. |
| 11 | Restart the stage worldserver: `docker restart housing-stage`, wait about a minute, log in again. | Layout unchanged. |
| 12 | Leave: steward inside → **Leave the house**, or the panel's Leave button. | Back where you entered from. After a server restart you go to your hearthstone location instead. |
| 13 | Flag off: `/console housingEnabled 0`, then `/reload`. | `/housing` says housing is off. Nothing housing-related appears in `Logs\Extensions.log` after that. |

## Send back

- `G:\3.3.5a - Dev\Logs\Extensions.log` (the lines starting with `Housing:`), `Logs\RetailAssets.log` and
  `WowClient_crash.log`.
- The server log: `G:\housing-build\stage\logs\Server.log`, or `python tools/stage.py check`. Its lines start with
  `housing:`.
- Screenshots of checks 2, 3 and 10.
- `.gps` output from the room corners (check 6).

## Undo after testing

- Put `realmlist.wtf` back.
- `python tools/stage.py stop` stops the stage containers.
- The client rollback is in the Phase 1 report.
