# EsteriaPerks

Client side of Paths of Fate in game, phase 0. The server is `EsteriaWoW/src/server/game/Handlers/PerkHandler.cpp`,
with the rules in `src/server/shared/PerkRules.h`. Enable it with `Esteria.Perks.Enable = 1`.
`Esteria.Perks.SellPlanned = 1` also sells cards that have no effect yet, for testing on a development realm.
The background trait is picked in character creation (`Tools/RetailAssets/glue/fate_page.lua`).

The server holds the only real state. The client asks for a snapshot, sends purchase and respec requests, and redraws
from the snapshot that follows every answer.

## Wire protocol

`src/PerkWire.h`, shared verbatim with the server's `src/server/shared/PerkWire.h`.

| Opcode | Dir | Payload |
|---|---|---|
| `0x52B` `CMSG_ESTERIA_PERK_SNAPSHOT_REQUEST` | C→S | proto(u8) |
| `0x52C` `SMSG_ESTERIA_PERK_SNAPSHOT` | S→C | proto earned spent trait respecs (u8) respecCost(u32) respecLock(u8) count(u8), then per card: id path tier rank maxRank cost lock flags (u8) |
| `0x52D` `CMSG_ESTERIA_PERK_PURCHASE` | C→S | perk(u8) rank(u8) |
| `0x52E` `CMSG_ESTERIA_PERK_RESPEC` | C→S | none |
| `0x52F` `SMSG_ESTERIA_PERK_RESULT` | S→C | action(u8) perk(u8) rank(u8) result(u8) |

Results (also each card's `lock`): 0 ok, 1 unknown card, 2 wrong or max rank, 3 level, 4 path points, 5 points,
6 another legendary owned, 7 feat, 8 planned (no effect yet), 9 class, 10 disabled, 11 respec place (combat, or not
resting in an inn or city), 12 gold, 13 nothing to reset, 14 protocol version.

Update the client and the server together.

## Lua

- `EsteriaPerks_RequestSnapshot()`: asks for `SMSG_ESTERIA_PERK_SNAPSHOT`.
- `EsteriaPerks_GetSnapshot()`: `nil` until the server answers, then `{ earned, spent, trait, respecs, respecCost,
  respecLock, cards = { { id, path, tier, rank, maxRank, cost, lock, planned } } }`. Cleared on logout and
  disconnect.
- `EsteriaPerks_Purchase(perk, rank)`: `rank` is the card's current rank + 1.
- `EsteriaPerks_Respec()`.
- Events: `ESTERIA_PERK_SNAPSHOT`; `ESTERIA_PERK_RESULT(action, perk, rank, result)` (action 0 snapshot, 1 purchase,
  2 respec).
