# Phase 5 in-game checklist: buying a plot and the House Editor

Stage realm `esteria-housing:slice5`, same setup as [phase1-checklist.md](phase1-checklist.md). The client and the
`EsteriaHousing` addon in `G:\3.3.5a - Dev` were updated together; the old addon talks protocol 1 and stays hidden
against this server. Plots cost 100 gold (`EsteriaHousing.PlotPrice`, in copper).

| # | Do | Expect |
|---|---|---|
| 1 | At Founder's Point, click a free plot's sign. | A **Plot N / For Sale** window with the price, not a gossip menu. With under 100 g the price is red and **Buy Plot** is greyed. |
| 2 | **Buy Plot** → Yes. | 100 g is taken, the house appears, and the window turns to **Your Home** with "Welcome home!". |
| 3 | **Enter House** (or walk in the door). | You are inside. No steward. The **House Editor** bar sits above the action bars: Decor, Layout, Leave House. |
| 4 | **Decor**. | A catalog on the right: category tabs, search, 3D previews with a count in each corner. Note any tile whose preview is blank or huge. |
| 5 | Click a tile, turn it with the mouse wheel, click the floor. | It lands where you clicked, at that turn. Shift-click places more copies. Right-click cancels. |
| 6 | **Select Placed Decor**, click a placed piece. | A bar shows its name: Move (then click a new spot), Turn L/R, Up/Down, Store. The wheel turns it too. |
| 7 | **Layout**. | A top-down blueprint: your rooms to scale, north up, a yellow arrow for you, and a **+** on each free wall. |
| 8 | Click a **+**, pick a room kind. | The room is built there, and the blueprint redraws. **Remove Newest Room** takes it away again. |
| 9 | A grouped second account visits. | The bar shows only **Leave House**. |
| 10 | Click the sign of someone else's plot (not grouped). | **Occupied**, no button. |

## Send back

- Screenshots of the plot window, the catalog and the blueprint.
- Where a placed piece ended up compared to where you clicked (it should be under the cursor). If it lands at the
  wrong height or far away, the cursor ray is missing the room floor; say which.
- Stage log lines `housing: … claimed plot …` and `housing: house … added room …`.

## Undo

- Houses and plots: the [phase4-checklist.md](phase4-checklist.md) undo steps.
- Client: `G:\3.3.5a - Dev\_backups\housing-ui-20261106-035212\` holds the previous `Wow.exe` and addon.
- Server: `python modules/mod-esteria-housing/tools/stage.py start esteria-housing:slice4` runs the previous image (it
  pairs with the backed-up client).
