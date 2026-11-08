# Housing section 4: collections and display

All 16 pieces are decor in the editor's **Collections** category (catalog 40201-40216, `housing_collections_world.sql`).
Server: `HousingCollections.inc`. Client: `Addons/EsteriaHousing/Collections.lua`. Commands start with `col\t` on the commerce channel.

| Spec | Pieces | What it does |
|---|---|---|
| 4.1 Trophies | Wall Mount, Pedestal, Plaque, Case | Pick any achievement the account earned (statue of the boss for kill achievements, cup / banner / head / name-only otherwise, optional effect aura) or any item you own (shows its name and tooltip). Earned date and description shown. Nothing is consumed. |
| 4.2 Mounts | Stall, Paddock, Pedestal | Any mount any character on the account knows. Stall: idles in place. Paddock: roams 4 yd. Pedestal: a frozen statue (natural / stone / spectral). Size, height, ahead, turn and name/title per display. |
| 4.3 Mannequins | Mannequin | A creature: race, sex, face and body through the merchant customizer. 14 slots from owned gear or mod-transmog collected looks (copies, by item id). Type (character / stone / spectral / still), 8 poses, nameplate, saved named outfits. Visitors can inspect the outfit. |
| 4.4 Library | Bookshelf, Lectern, Display Table, Reading Nook | One library window, four tabs: Books (logged from now on when read in the world; readable by guests), Exploration (account's explored areas), Achievements, Storylines (quest chains of 3+). Acquired and missing, grouped by zone or category, with search. |
| 4.5 Pets | Habitat, Pet Bed, Pet Toy, Feeding Station | Up to 6 account companions per habitat; they wander its radius (2-10 yd) and go sleep / play / eat at pet furniture nearby. |

Limits: `EsteriaHousing.MaxAnimatedDisplays` (default 12) moving mounts and pets per instance; past it, mounts stand still and pets nap.

Known shortcuts:
- Item trophies show the item's name and tooltip, not its model: 3.3.5 only draws an item's model on a character. Use a mannequin to show gear.
- The mannequin pick list holds at most 400 items per slot, owned gear first.
- Exploration counts every continent area with an explore bit, including some that may not be reachable.
- Display names use a wrapping pool of 2900 creature entries, as merchants do.
