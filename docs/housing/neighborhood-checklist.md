# In-game checklist: the Neighborhood button (spec 1.1)

Stage realm `esteria-housing:slice17`, protocol 11. Client, addon and shell archive in `G:\3.3.5a - Dev` were updated
together; an older client stays hidden against this server, and this client against an older one.

| # | Do | Expect |
|---|---|---|
| 1 | With DragonUI on, look at the micro strip. | A house icon between Radio and the game menu. All 15 buttons fit on one row, none on top of the bags. |
| 2 | Hover it, then click it. | Tooltip "Neighborhood". The **Neighborhood** window opens and the button stays pressed. Escape closes it. |
| 3 | Read the window. | **Your Home** with plot, owner, rooms, decor and visitors, or **No Home Yet**. Then **Teleport to Neighborhood**, and plots for sale with their price. Your plot is listed first, a grouped owner's next. |
| 4 | **Teleport to Neighborhood** from a city. | You arrive at Founder's Point's entry. The button now reads **Leave Founder's Point**. |
| 5 | **Go** on a plot for sale. | You land by that plot's cornerstone. Clicking the sign opens its plot window. |
| 6 | **Leave Founder's Point**. | You are back where step 4 started. |
| 7 | Enter combat with the window open. | Teleport and Go grey out and the reason shows in red; they return when combat ends. |
| 8 | Repeat 7 dead, on a flight path, in a dungeon and in a battleground. | Greyed with the matching reason. |
| 9 | Open it inside your house, then **Teleport to Neighborhood**. | It works from a house. **Leave** afterwards goes back outside the neighborhood if you came from there, else to your hearthstone. |
| 10 | Group with a plot owner and reopen. | Their plot is listed as "Name's home". |
| 11 | Disable DragonUI in the AddOns list and reload. | The same house button sits after the last stock micro button, not covering the key ring, and does all of the above. |
| 12 | Stock UI: get on a vehicle with a vehicle bar, then off. | The button follows the other micro buttons and comes back. |
| 13 | `/console housingEnabled 0`, relog. | The button is grey, its tooltip says housing isn't available, and clicking does nothing. |

## Send back

Screenshots of the strip, the stock bar and the window, plus `Logs\Extensions.log` lines starting `Housing:` and any Lua
error text.

## Undo

- Client: `G:\3.3.5a - Dev\_backups\neighborhood-*\` holds the previous `Wow.exe`, shell archive and addon.
- Server: `python modules/mod-esteria-housing/tools/stage.py start esteria-housing:slice16` runs the previous image (it
  pairs with the backed-up client).
