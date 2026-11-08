# Housing wire protocol (draft v1)

> **As built in Phase 1 (2026-11-05).** The authoritative layout is `HousingWire.h`. One copy is in wowdecomp
> `Extensions/Housing/src/`, the other in EsteriaWoW `modules/mod-esteria-housing/src/`. The build differs from this
> draft in these ways:
> - Only 0x537–0x53E are defined. `NUM_MSG_TYPES` becomes 0x53F; 0x53F–0x540 stay unassigned.
> - `CMSG_ENTER` takes `u8 action`: 0 resends `SMSG_STATE`, 1 leaves the house. You enter and visit a house through
>   the Housing Steward's gossip. The server checks group membership and that the owner has a house.
> - `SMSG_STATE` is `inHouse u8, isOwner u8, houseId u32, revision u32, placed u16, maxDecor u16`, then the room
>   bounds min and max (f32×3 each), then `n u16`, then n × `placedId u64, decorId u32, pos f32×3, yaw f32`.
>   It goes to everyone inside after each accepted edit.
> - Each `SMSG_CATALOG` entry ends with a NUL-terminated `name`.
> - In the validation order, the rate check now comes right after seq, ahead of revision, IDs, transform and
>   budget. A flood gets refused before any lookups. Result 14 `BAD_OP` covers an unknown `op`.
> - Edits that fail the enabled, in-house or permission checks don't consume `seq`. Every other refusal does.
>   The seq counter is per account and resets on each HELLO.
> - The client sends one edit at a time and waits for its ack.

> **Phase 5 (2026-11-06): protocol 2.** Three more opcodes, so `NUM_MSG_TYPES` is now 0x542. `HousingWire.h` has
> the layouts.
> - `0x53F CMSG_ACTION`: buy the plot whose cornerstone you last clicked, enter it, add a room through a free wall, or
>   remove the newest room. The server re-checks range (15 yd), money, ownership and fit.
> - `0x540 SMSG_PLOT`: the plot window's contents. Clicking a cornerstone sends this instead of a gossip menu to
>   clients that said HELLO with protocol 2; older clients get a gossip line saying they need it.
> - `0x541 SMSG_LAYOUT`: the owner's blueprint (room kinds, rooms, free walls and the kinds that fit each). It goes with
>   every `SMSG_STATE` to the owner.

> **Neighborhood button (2026-11-06): protocol 11.** One more opcode, so `NUM_MSG_TYPES` is now 0x543. `HousingWire.h` has
> the layouts.
> - `0x542 SMSG_NEIGHBORHOOD`: the Neighborhood window's contents (your home, every plot's status).
> - `CMSG_ACTION` gains `ActionNeighborhood` (ask for it) and `ActionTravelNeighborhood`, `ActionTravelBack` and
>   `ActionTravelPlot`. The server decides travel (`TravelBlock`: combat, dead, flight path, battleground or arena,
>   dungeon or raid) and answers every one with `SMSG_NEIGHBORHOOD`, whose `message` says why it refused.

> **Visitors and house parts (2026-11-07): protocol 12.** No new opcode.
> - `SMSG_NEIGHBORHOOD` ends with `access` (`Access` bits: party, friends, guild, everyone), and names the owner of every
>   plot you may visit. `ActionHouseAccess` sets it; visitors who may no longer stay are sent out.
> - `SMSG_PLOT` ends with your plot's swappable parts (walls, roof, door, windows, chimney) and their options.
>   `ActionPlotFixture` picks one; 0 goes back to the style's default.

> **Shops and commissions (2026-11-08): protocol 17.** One more opcode, so `NUM_MSG_TYPES` is now 0x544.
> - `CMSG_ACTION` `ActionCommerce`: one tab-separated text command (`place`, `deposit`, `buy`, `post`, `complete`, ...).
> - `0x543 SMSG_COMMERCE`: one text answer, read by `Addons/EsteriaHousing/Commerce.lua`. The server side is
>   `mod-esteria-housing/src/HousingCommerce.inc`, which re-checks range, ownership, stock and money on every command.

## Opcode range: 0x537–0x540

Evidence that these are unused:
- Server: the last defined opcode is `SMSG_ESTERIA_SPELL_VISUAL_OVERRIDE = 0x536`, and `NUM_MSG_TYPES = 0x537`
  (EsteriaWoW `src/server/game/Server/Protocol/Opcodes.h:1364-1365`).
- A search of `EsteriaWoW/modules` for `0x53[7-9A-F]` and `0x54x` in `.cpp`/`.h` found nothing.
- Client: the stock table ends at 0x51F (`WoW/Source/Net/Types.h:1319`), and Esteria handlers use
  0x521–0x536 (`Spell_C.cpp:297`, `Extensions/*`). The only other custom opcode is HelloWorld 0x5F0
  (`Extensions/HelloWorld/src/HelloWorldNet.cpp:10`).

| Opcode | Name | Dir | Purpose |
|---|---|---|---|
| 0x537 | `CMSG_ESTERIA_HOUSING_HELLO` | C→S | Capability handshake |
| 0x538 | `SMSG_ESTERIA_HOUSING_HELLO` | S→C | Capabilities, limits, catalog version |
| 0x539 | `CMSG_ESTERIA_HOUSING_CATALOG` | C→S | Request owned decor (paged) |
| 0x53A | `SMSG_ESTERIA_HOUSING_CATALOG` | S→C | Owned decor page |
| 0x53B | `CMSG_ESTERIA_HOUSING_EDIT` | C→S | Place / move / remove (one op) |
| 0x53C | `SMSG_ESTERIA_HOUSING_EDIT_ACK` | S→C | Result for one `seq` |
| 0x53D | `CMSG_ESTERIA_HOUSING_ENTER` | C→S | Enter own house / visit by owner GUID |
| 0x53E | `SMSG_ESTERIA_HOUSING_STATE` | S→C | House summary (revision, budget used) on enter or change |
| 0x53F–0x540 | reserved | | Later: blueprint export/import |

Making these real needs:
- Server: extend `Opcodes.h` (raise `NUM_MSG_TYPES` to 0x541) and add `DEFINE_HANDLER` rows in `Opcodes.cpp`.
  The handlers forward into `mod-esteria-housing`.
- Client: register through `sExtensionMgr->RegisterMessageHandler` and send with `SendClientMessage`.

All integers are little-endian and strings are NUL-terminated, matching existing Esteria packets.

## Handshake

`CMSG_HELLO`: `u16 protocolVersion (=1)`, `u32 clientFeatures` (bit0 editor, bit1 retailAssets).

`SMSG_HELLO`:
- `u16 protocolVersion`
- `u8 enabled` (worldserver flag)
- `u32 serverFeatures`
- `u32 catalogVersion`
- `u16 maxDecorPerHouse`
- `f32 maxScale`
- `f32 minScale`
- `f32 rotationSnapDeg` (15 in MVP)

The client hides all housing UI unless it gets `enabled=1` and a matching major version. A server that has
the module disabled replies `enabled=0`. A stock server drops the CMSG, so the client times out and stays hidden.

## Edit request / ack

`CMSG_EDIT`:

| Field | Type | Notes |
|---|---|---|
| seq | u32 | Client counter, strictly increasing per session |
| baseRevision | u32 | House revision the client edited against |
| op | u8 | 1 place, 2 move, 3 remove |
| placedId | u64 | move/remove: server-issued placement ID. place: 0 |
| decorId | u32 | place only: catalog ID. The server checks ownership and quantity |
| pos | f32×3 | World position |
| rot | f32×4 | Quaternion. The server re-normalizes it and, in MVP, snaps to 15° yaw only |
| scale | f32 | MVP: must be 1.0 |

`SMSG_EDIT_ACK`: `u32 seq`, `u8 result`, `u32 newRevision`, `u64 placedId`, then the **server-final**
`pos` f32×3, `rot` f32×4, `scale` f32.

The client discards its ghost and trusts only this ack plus the respawned GO.

### Server validation, in this order (never trust the client)

1. Feature flag on. Player is in a housing instance. That instance's house exists.
2. **Permission:** in MVP, editing requires the player's account to be the house owner. Permissions come from
   the DB, never from the packet.
3. **seq:** must be greater than the last seq for this session. Otherwise `DUPLICATE_SEQ`, ignored without
   applying it.
4. **Revision:** `baseRevision` must equal the current revision. Otherwise `STALE_REVISION`, and the client
   re-syncs.
5. **IDs:** `decorId` must be in `decor_catalog` and enabled. `account_decor.quantity_available` must be ≥ 1.
   For move/remove, `placedId` must belong to this house.
6. **Transform:**
   - finite floats only;
   - position must sit inside the room's bounds box (from the `room` row);
   - quaternion magnitude in [0.5, 2] before it's normalized;
   - scale must be within the server limits.
7. **Budget:** placed count + 1 ≤ `house.max_decor`.
8. **Rate:** at most N edits per second per session (config). Above that → `RATE_LIMITED`.
9. Apply in one DB transaction: change `placed_decor`, change `account_decor` quantity, insert a `revision`
   row, bump `house.revision`. Then respawn the GO for viewers.

## Error codes (`result`)

| Code | Name |
|---|---|
| 0 | OK |
| 1 | DISABLED |
| 2 | NOT_IN_HOUSE |
| 3 | NO_PERMISSION |
| 4 | DUPLICATE_SEQ |
| 5 | STALE_REVISION |
| 6 | UNKNOWN_DECOR |
| 7 | NOT_OWNED / NONE_LEFT |
| 8 | UNKNOWN_PLACEMENT |
| 9 | OUT_OF_BOUNDS |
| 10 | BAD_TRANSFORM |
| 11 | OVER_BUDGET |
| 12 | RATE_LIMITED |
| 13 | INTERNAL (DB failure; nothing changed) |

## Catalog

`CMSG_CATALOG`: `u32 page`.

`SMSG_CATALOG`: `u32 page`, `u32 pageCount`, `u16 n`, then for each of the n entries:
`u32 decorId, u32 gameobjectEntry, u32 modelFileDataId, u16 available, u16 placed, u8 category, u8 surface`.

The client gets no free-form model paths. It resolves models only through the GO template it already knows.
