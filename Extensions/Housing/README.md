# Housing

Client side of Esteria housing, Phase 1 slice ([docs/housing/](../../docs/housing/README.md)). The server is
EsteriaWoW `modules/mod-esteria-housing` on the `housing-slice` branch. The panel is `Addons/EsteriaHousing`.

Off by default. It needs both:

- the client CVar `housingEnabled 1` (`/console housingEnabled 1`, or `SET housingEnabled "1"` in `WTF\Config.wtf`);
- a realm with `EsteriaHousing.Enable = 1`.

With the CVar at 0 the extension sends nothing. With it on and a realm that lacks the module, HELLO gets no answer
and every Lua function reports "not available".

The server holds every placement. This extension keeps the server's last answers and sends edits one at a time, so
each edit carries the revision the previous ack returned. A refused edit clears the queue. `STALE_REVISION` also
asks for a resync.

## Wire protocol

`src/HousingWire.h`, shared verbatim with the server's `modules/mod-esteria-housing/src/HousingWire.h`. It covers
opcodes 0x537–0x53E, the field layouts and the result codes. Update the client and the server together.

## Lua

- `EsteriaHousing_IsAvailable()`
- `EsteriaHousing_GetState()` returns `nil`, or `{ inHouse, isOwner, houseId, revision, placed, maxDecor, minX…maxZ,
  pending, decor = { { placedId, decorId, x, y, z, yaw } } }`. `yaw` is in degrees.
- `EsteriaHousing_GetCatalog()` returns `{ { decorId, name, available, placed, category, surface, fileDataId } }`.
- `EsteriaHousing_GetPlayerPosition()` returns `x, y, z, facingDegrees` in world coordinates. 3.3.5 Lua has no API
  for this.
- `EsteriaHousing_Place(decorId, x, y, z, yawDegrees)`, `EsteriaHousing_Move(placedId, x, y, z, yawDegrees)` and
  `EsteriaHousing_Remove(placedId)` each return `false` when the edit can't be queued (not available, not in a house,
  not the owner, or 32 edits pending).
- `EsteriaHousing_Sync()` and `EsteriaHousing_Leave()`.
- `EsteriaHousing_RequestNeighborhood()` asks for the Neighborhood window's contents. `EsteriaHousing_GetNeighborhood()`
  returns `nil` before the first answer, or `{ name, price, where, message, homePlot, rooms, placed, maxDecor,
  plots = { { plot, status, name } } }`. `where` is `"away"`, `"neighborhood"` or `"house"`; `status` is `"sale"`,
  `"yours"`, `"visit"` or `"private"`.
- `EsteriaHousing_TravelNeighborhood()`, `EsteriaHousing_TravelBack()` and `EsteriaHousing_TravelPlot(plot)` ask to
  travel. The server refuses restricted states and says why in `message`.
- `GetNeighborhood()` also holds `access = { party, friends, guild, everyone }`; `EsteriaHousing_SetHouseAccess(party,
  friends, guild, everyone)` changes who may visit your house.
- `GetPlot()` on your own plot holds `parts = { { type, chosen, options = { { id, parent, name } } } }`;
  `EsteriaHousing_SetPlotPart(type, component)` swaps one (0: the style's default).
- `EsteriaHousing_ShowPlotBounds(on)` shows retail's plot boundary while you decorate outdoors.
- Events:
  - `ESTERIA_HOUSING_AVAILABLE`
  - `ESTERIA_HOUSING_CATALOG`
  - `ESTERIA_HOUSING_STATE`
  - `ESTERIA_HOUSING_NEIGHBORHOOD`
  - `ESTERIA_HOUSING_RESULT(result, op, placedId)`. `result` is a `HousingWire.h` `Result`. 13 also means the server
    didn't answer within 5 s.

The log lines start with `Housing:` and go to `Logs\Extensions.log`.
