# In-game checklist: neighborhoods (spec 1.2 to 1.5)

Live realm `esteria-coa:20261108a-housing` (slice21), protocol 15. Client, addon and patch MPQ in both `G:\3.3.5a - Dev`
and `G:\3.3.5a - Live`. Every account now has its own neighborhood (an instance); the old shared Founder's Point is
gone, and the house that stood there is in its owner's neighborhood on the same plot.

| # | Do | Expect |
|---|---|---|
| 1 | Open the Neighborhood window. | "<name>'s Neighborhood", Founder's Point, and tabs Plots, My Homes, Neighborhoods, Favorites, Recent and Manage. |
| 2 | **Teleport to** it. | You arrive at Founder's Point; your house stands on its plot. Nobody else's house is there. |
| 3 | Buy a second plot. | The buy button opens a menu: **A new house**, or **Mirror your house on plot N**. Mirror it. The same house now stands on both plots, and walking into either goes into the same rooms. |
| 4 | My Homes tab. | One row per plot each house stands on; **Visitors** sets who may visit that house. |
| 5 | Plot window on your plot, **Move Out**. | The house leaves the plot and shows under My Homes as "Saved: on no plot", with **Enter**. Buying a plot offers "Your saved house". |
| 6 | Manage tab: invite another character by name. | They get a chat line and see your neighborhood under Neighborhoods ("Invited"); they can go there and buy a plot. **Remove** sends them out. |
| 7 | Manage tab: **Move this neighborhood to Razorwind Shores**. | Everyone inside is sent back; travelling there again lands at Razorwind Shores with every house on its same plot number. Map, minimap, world map, zone name and loading screen are Razorwind's. |
| 8 | Visit a friend's house (their visitors allow friends), then open Recent. | It is listed; **Favorite** adds it to Favorites (shared by your characters); **Go** takes you to its plot. Closing their house greys it out. |
| 9 | As a Guild Master with 5000 g in the guild bank: Manage tab, **Buy Guild Neighborhood**. | The guild's neighborhood appears under Neighborhoods for every member. **Rank Permissions** sets buy plots, invite and manage per rank. |
| 10 | A member with a house there leaves the guild. | A few seconds later their house is off the plot and saved under their My Homes. |

## Send back

Screenshots, `Logs\Extensions.log` lines starting `Housing:`, and any Lua error text. Server side: `docker logs
ac-worldserver 2>&1 | grep housing:`.

## Undo

- Client: `_backups\housing-neighborhoods-*` in each client folder holds the previous `Wow.exe`, patch MPQ, aliases.csv
  and addon.
- Server: `G:\housing-build\live-backup-*` (newest) holds both databases from before the migration; point
  `docker-compose.twoforms.yml` back at `esteria-coa:20261107c-housing`.
