# Esteria Housing — Feature Specification

> **Status:** Proposed features and intended player-facing behavior. This is a design specification, not a statement that these systems are already implemented.
>
> **Goal:** Make housing a persistent, interactive extension of character progression, professions, commerce, collections, and social gameplay—not just a decoration system.

## Core Design Principles

- **Persistent:** Properties, placed objects, shop inventories, earnings, settings, and permissions persist across logouts and server restarts.
- **Player-controlled:** Owners manage layout, available services, access, and customization through in-game interfaces.
- **Useful to visitors:** Public houses can offer shopping, crafting, social venues, training, and other services while owners are offline.
- **Permission-based:** Distinguish owner, co-owner, trusted guest, guild member, and public visitor permissions. A guest may interact with an object without being allowed to move, edit, or withdraw from it.
- **Server-authoritative:** Inventory changes, gold transactions, rewards, combat outcomes, access checks, and teleports must be validated and recorded server-side.
- **Progression-aware:** Services respect character unlocks, professions, dungeon access rules, and other game progression unless an explicit Esteria rule says otherwise.
- **Expandable:** Housing features should use shared interaction, placement, permission, storage, and customization systems wherever possible.

---

## 1. Navigation & Property Access

### 1.1 Neighborhood Button in the Game UI

Add a **Neighborhood** button alongside existing navigation buttons such as Character, Talents, and Social.

- Open a dedicated Neighborhood/Housing interface without requiring a chat command or NPC.
- Show the character's home, neighborhood, available properties, and access options.
- Provide **Teleport to Neighborhood** as the primary action.
- Display relevant information such as property name, ownership, neighborhood, and visitor permissions.
- Where supported, show bookmarked or recently visited public properties.
- Disable or explain teleporting when the character is in a restricted state (such as combat, a battleground, or an instance that disallows travel).
- Keep the button and related UI consistent with the customized Esteria 3.3.5a client.

**Design intent:** Housing should be treated as a first-class game system, not buried behind commands or obscure menus.

### 1.2 Favorites & Recent Visits

Let players return to houses they enjoyed without hunting for them again.

- Add a **Favorites** list to the Neighborhood window: favorite a house from its plot window or while visiting, and unfavorite it from the list.
- Keep a short **Recently Visited** list (for example, the last 10 houses entered), newest first.
- Show each entry's owner, neighborhood, and plot, with a **Go** action that travels to the plot under the same restrictions as other Neighborhood travel.
- Hide or mark entries the character can no longer enter (the owner changed visitor access, or the house no longer exists) instead of failing at the door.
- Store favorites per account so every character shares them; recent visits may be per character.

### 1.3 Alliance & Horde Neighborhoods

Add a **Horde neighborhood** alongside the Alliance one (Founder's Point), as retail does.

- Each faction gets its own neighborhood map, plots, exterior styles, and arrival point.
- A player can **choose which neighborhood to live in** and **swap** later, moving their house (rooms, decor, and settings) to a plot in the other neighborhood.
- Visiting the other faction's neighborhood follows the same access rules as any other house.

### 1.4 Personal Neighborhoods & Multiple Homes

Give each **account** an **instanced neighborhood** of its own, and let an account own more than one home.

- Neighborhoods belong to the **account, not the character**: every account has exactly one private neighborhood instance.
- All characters on an account live in that **account's neighborhood** by default; a new character arrives already moved in and shares the account's homes there.
- The account can **invite** other players into its neighborhood, as with house visits.
- When settling into another neighborhood, the account chooses to **mirror** its existing house there or build a **separate, unique** house.
- There is **no limit** on how many houses an account can own: plots can be bought across any number of neighborhoods.
- The Neighborhood window lists every home the account owns and travels to each, from any of its characters.
- This replaces the current rule of one house per account; each house keeps its own rooms, decor, budget, upgrades, and permissions (shared decor storage stays per account).

### 1.5 Guild Neighborhoods

Let guilds own neighborhoods that their members can move into.

- A **Guild Master** can purchase a **Guild Neighborhood**, choosing which neighborhood map it uses.
- Guild members can buy a plot there and move in; plots are reserved for the guild.
- When a guild neighborhood fills up, the Guild Master can purchase **additional** neighborhoods.
- Guild rank permissions decide who may buy plots, invite visitors, or manage the neighborhood.
- A member's guild house is **saved** when they leave the guild, with its rooms, decor, and settings, and can be moved to a plot in their new guild's neighborhood when they join one.

---

## 2. Player Commerce & Professions

### 2.1 Player-Owned Shops

Allow a homeowner to create a functioning storefront by placing and configuring a **Merchant NPC**.

**Merchant setup**
- Place the merchant anywhere permitted on the property.
- Choose the merchant's name, appearance, outfit, and greeting dialogue.
- Deposit eligible, player-owned items into a dedicated shop inventory.
- Set the selling price of each listing, including stack quantities where applicable.
- Publish, update, remove, or restock listings through an owner-only management interface.
- Allow visitors to browse and purchase through a familiar WoW-style vendor window.
- Keep the shop operational while the homeowner is offline.

**Shop customization**
- Customize storefront signs with a visual editor inspired by the **guild emblem editor**, using symbols, colors, borders, and text where supported.
- Place display cases, shelves, racks, and counters that showcase designated shop listings.
- Customize merchant greetings and optional purchase/farewell dialogue.
- Support multiple merchant themes (for example, blacksmith, alchemist, general goods, or rare collectibles) without requiring separate inventory systems.

**Earnings and ledger**
- Deposit completed-sale proceeds into a **shop treasury**, separate from the owner's carried gold.
- Let the owner withdraw available earnings by interacting with their merchant or management interface.
- Provide a ledger showing total sales, gross revenue, available earnings, best-selling items, individual transactions, and recent customers.
- Track stock levels and optionally flag sold-out listings.

**Important rules**
- Items listed for sale are reserved in server-managed inventory and cannot simultaneously be used, sold, or traded elsewhere.
- Purchases, stock deductions, and gold transfers must complete atomically to prevent duplication or lost purchases.
- Only authorized owners/managers may change prices, retrieve unsold goods, or withdraw earnings.
- Visitor permissions determine who can enter the property and access the shop; a public merchant must remain reachable under those permissions.

### 2.2 Crafting Commissions

Add a placeable **Commission Board** where customers can request crafted items from other players.

- Customers select a supported recipe or define an eligible item request.
- Customers supply required materials, the target quantity, and a gold payment or tip.
- Materials and payment are held securely in server-managed escrow while the request is open.
- Eligible crafters browse available commissions and accept jobs they can fulfill.
- Completed orders transfer the finished item to the customer and the agreed payment to the crafter.
- Customers do not need to remain online while a commission is accepted or completed.
- Include order states such as **Open**, **Accepted**, **Completed**, **Cancelled**, and **Expired**.
- Provide safe cancellation, expiration, return-of-materials, and delivery rules.
- Support blacksmithing, tailoring, leatherworking, alchemy, engineering, jewelcrafting, and enchanting as compatible workflows are implemented.

**Design consideration:** Enchants and other crafts applied to an existing item require a specialized, secure item-handling flow rather than a simple item-delivery transaction.

---

## 3. Combat, Training & Adventure

### 3.1 Advanced Training Hall

Provide placeable, configurable **Training Dummies** and specialized combat training equipment.

- Configure target level, armor, resistances, health, and whether the target can be defeated.
- Set a training session length or run until the player ends the session.
- Track damage, DPS, healing, HPS, and other useful session statistics where supported.
- Show session summaries and optionally retain recent personal bests.
- Include training modes for single-target, multiple-target, tanking, and healing practice.
- Add optional boss-mechanic challenges using telegraphs, hazards, interrupt windows, or timed objectives.
- Ensure training damage and threat calculations are isolated from unrelated guests and real encounters.

**Design intent:** A serious practice space for testing builds, rotations, equipment, and reactions without leaving home.

### 3.2 Placeable Portals

Allow players to place magical portals as interactive housing objects.

- Assign each portal an eligible destination from an owner-configurable list.
- Customize portal visuals, names, and placement.
- Configure access for owners, trusted guests, party members, or the public.
- Display the destination before teleporting, preventing accidental travel.
- Respect destination unlocks, server travel restrictions, and any applicable cooldowns or costs.
- Support portals between authorized properties where allowed.

### 3.3 Dungeon Meeting Stones

Allow owners to place a dungeon-specific **Meeting Stone**.

- Select or bind the stone to a supported dungeon or raid entrance.
- Interacting with the stone teleports an eligible player to that entrance, **not directly into the instance**.
- Display the target dungeon and any access requirements before travel.
- Optionally support party-oriented use, with individual eligibility checks for each participant.
- Apply the same travel restrictions as other housing teleport services.

### 3.4 Homeowner Teleport & Portal Spells

Provide housing-specific spells or abilities that allow quick access to an owned home.

- **Teleport: Home:** Returns the homeowner to their selected house arrival point.
- **Portal: Home:** Creates a temporary portal that authorized party members can use.
- Support an assigned destination when a character owns or has access to more than one property.
- Use distinct spell visuals or themed variants where desired.
- Enforce cooldowns, access permissions, and combat/instance restrictions consistently.
- Ensure portal expiration and destination changes cannot strand or misroute travelers.

---

## 4. Collections & Display

### 4.1 Trophy Displays

Allow players to exhibit accomplishments from quests, dungeons, raids, PvP, and special events.

- Place wall mounts, pedestals, plaques, or glass cases.
- Assign eligible unlocked trophies, achievements, or collectible replicas to a display.
- Support item names, tooltips, earned dates, and achievement descriptions where available.
- Include themed collectibles such as boss heads, raid relics, banners, and statues.
- Where possible, support decorative animation or effects tied to special trophies.
- Displaying a trophy should not consume or transfer the original item or achievement.

### 4.2 Mount Displays

Showcase the homeowner's mounts through two presentation modes.

- **Stable mode:** Display selected mounts in stalls or paddocks, inspired by Warlords of Draenor garrisons, with appropriate idle animations where supported.
- **Statue mode:** Create static, decorative representations of selected mounts on pedestals or platforms.
- Allow selection from mounts the character or account has legitimately unlocked under Esteria's collection rules.
- Provide mount labels, themed stalls, and configurable display positions.
- Limit active animated displays as needed for client performance.

### 4.3 Mannequins

Place customizable mannequins to exhibit armor sets and weapon combinations.

- Equip mannequins with selected appearances or display copies of owned gear.
- Configure pose, orientation, nameplate, and mannequin type.
- Save named outfits for later display or quick reconfiguration.
- Allow inspection of displayed sets where practical.
- Keep displayed appearances distinct from transferable inventory items to prevent duplication.

### 4.4 Personal Library

Create a collectible library documenting a player's adventures and discoveries.

- Place bookshelves, lecterns, display tables, and reading nooks.
- Showcase collected lore, readable books, achievements, and exploration discoveries.
- Let guests interact with books to read unlocked texts or inspect discovery records.
- Support themed collections by zone, faction, expansion, profession, or storyline.
- Add an optional catalog interface showing acquired and missing entries.
- Preserve the distinction between cosmetic display and actual quest/item ownership.

### 4.5 Pet Sanctuary

Allow collected companion pets to populate parts of a house.

- Assign unlocked companion pets to designated sanctuary areas.
- Pets wander, idle, sleep, and interact with suitable objects when supported by their animations.
- Place pet beds, toys, feeding stations, and themed habitats.
- Configure which pets are visible and how many may appear at once.
- Prevent pet displays from consuming, transferring, or modifying the underlying collection unlocks.
- Use movement limits and NPC budgets to avoid crowding, clipping, and performance issues.

---

## 5. Household Services & Utilities

### 5.1 Crafting Equipment

Make profession stations placeable and usable in housing.

- Support anvils, forges, cooking fires, alchemy workstations, and other profession-specific equipment.
- Count appropriate stations as valid crafting locations for recipes that require them.
- Customize workstation models and placement where compatible.
- Respect skill, recipe, and material requirements; stations do not automatically grant recipes.
- Allow public or permission-restricted access.

### 5.2 House Innkeeper & Custom Hearthstone

Let an owner place an **Innkeeper NPC** who can bind a visitor's hearth destination to that particular property when authorized.

- Offer a dedicated **House Hearthstone** that returns to the selected home, separate from the standard hearthstone where desired.
- Bind each hearthstone to a valid house and assigned arrival point.
- Allow the owner to decide whether guests may bind to the property.
- Handle permission revocation, deleted properties, and moved houses with a safe fallback destination.
- Respect configured cooldowns and travel restrictions.
- Provide a clear confirmation before a character replaces an existing house binding.
- Import retail's **housing hearthstone animations** and visual effects for casting the House Hearthstone, so returning home looks distinct from a normal hearth.

### 5.3 Personal Bank Vault

Place a vault or banker NPC that opens the owner's personal bank.

- Use the character's existing bank storage rather than creating a duplicate item pool.
- Allow a choice of vault, chest, or banker visual styles.
- Restrict access to the appropriate character; visitors cannot open the owner's bank.
- Respect ordinary bank slot and item rules.

### 5.4 Guild Bank Access

Place a guild-bank object or NPC within an authorized property.

- Open the existing guild bank for characters with guild access.
- Preserve guild rank permissions, withdrawal limits, and transaction logs.
- Allow placement in any authorized property where the rules permit it.
- Never grant extra guild-bank privileges merely because someone can enter the house.

### 5.5 Placeable Mailboxes

Provide usable mailboxes in homes, taverns, shops, and other permitted properties.

- Open the normal in-game mail interface.
- Support multiple cosmetic mailbox styles.
- Allow guests to send and collect their own mail, subject to access permissions.
- Use the existing mail system and delivery rules.

### 5.6 Profession Trainers

Allow owners to place trainers for supported professions.

- Choose a profession-specific trainer from an unlocked or permitted roster.
- Offer normal training services with existing skill and cost requirements.
- Customize placement and appearance within supported models.
- Optionally require the homeowner to have developed or unlocked the corresponding profession service.
- Prevent trainers from bypassing expansion-specific or progression-dependent requirements.

### 5.7 Repair Station

Place a repair NPC or workstation with a **reduced repair cost**.

- Allow visitors or approved users to repair damaged equipment.
- Display the normal cost, housing discount, and final cost before confirmation.
- Make the discount configurable and capped to protect the broader gold economy.
- Allow guild support or housing upgrades to affect the discount if implemented later.
- Keep repair payment and item-durability changes server-authoritative.

### 5.8 Placeable Auctioneer & Auction House Access

Allow homeowners to place an **Auctioneer NPC** that provides access to the existing in-game **Auction House (AH)** from their property.

- Place and position an Auctioneer NPC anywhere the property's NPC placement rules permit.
- Let the owner choose from eligible auctioneer appearances, names, and optional greeting dialogue.
- Allow authorized visitors to interact with the NPC to open the familiar Auction House interface.
- Support normal AH functions: browsing and searching auctions, placing bids, buying out listings, creating auctions, and collecting completed or expired auction results through the existing mail system.
- Keep the Auctioneer available while the homeowner is offline, provided visitors have access to the property.
- Respect the character's normal Auction House access rules, including faction/neutral AH routing or any unified-AH rules configured by Esteria.
- Apply standard listing deposits, fees, auction durations, and mail delivery rules; placing an Auctioneer does not grant cheaper listings or a separate market by default.
- Use the existing server-authoritative Auction House backend rather than storing auctions in the housing NPC or player-owned shop inventory.
- Let owners control who can access the property and use the Auctioneer, without allowing them to view, change, or withdraw another player's auction proceeds.

**Design intent:** Offer convenient Auction House access from home while preserving the standard in-game economy and auction mechanics. This is distinct from **player-owned shops**, which use owner-stocked vendor inventories and their own sales ledgers.

### 5.9 Storage Chests

Add persistent storage containers independent of decorative chests.

- Place different chest styles and assign names such as **Materials**, **Consumables**, or **Event Supplies**.
- Configure access separately for owner, co-owner, trusted guests, and guild roles.
- Support shared containers where specifically enabled, with server-side deposit/withdrawal logs.
- Show capacity, available slots, and item restrictions in the container interface.
- Define what happens to contents when a chest is moved, stored, sold, or a property is deleted.
- Prevent withdrawing or duplicating items via blueprints, furniture transfer, or concurrent access.

---

## 6. Interactive Home & Atmosphere

### 6.1 Functional Doors & Secret Doors

Let builders place doors that operate as interactive objects rather than static decorations.

- Open, close, lock, or unlock doors through player interaction.
- Configure door access independently from overall property entry permissions.
- Offer standard doors, gates, sliding panels, hidden bookshelves, and other themed options.
- Link secret doors to hidden switches, levers, pressure plates, or configured triggers.
- Support auto-close timers and optional owner-only controls.
- Validate passage and lock states server-side so unauthorized players cannot bypass restricted areas.

### 6.2 Jukebox

Place a music player that controls the property's ambient soundtrack.

- Select from a curated library of eligible in-game music tracks.
- Build playlists and toggle shuffle, repeat, volume, and playback zones where supported.
- Allow the owner to permit guests to request or change tracks.
- Persist the selected playlist and playback configuration.
- Apply music locally to the relevant housing area rather than changing unrelated world audio.

### 6.3 Time-of-Day Controls

Let owners select the time and lighting mood of their housing environment.

- Choose dynamic time or a fixed time such as dawn, noon, sunset, or midnight.
- Update compatible world lighting, sky effects, and visible lighting through windows.
- Where the environment supports it, synchronize exterior window views with the selected time.
- Keep changes scoped to the property or housing instance, not the global world.
- Allow preset configurations to be saved as part of property settings.

**Technical note:** Window/lighting updates may require custom client-side rendering support; they are not guaranteed by the existing 3.3.5a systems.

### 6.4 Weather Controls

Give owners control over the visible weather and atmosphere of outdoor or compatible housing areas.

- Select weather presets such as clear skies, rain, snow, fog, and storms.
- Support dynamic weather or a fixed preset.
- Apply appropriate environmental visuals and sounds where supported.
- Keep effects local to the housing area and independent of public-zone weather.
- Coordinate weather settings with time-of-day and interior/exterior transitions.

---

## 7. Social Experiences

### 7.1 Guestbooks

Allow homeowners to place a guestbook for visitors to leave messages.

- Visitors can sign, leave a short note, and optionally select a greeting or reaction.
- Owners can view entries by date and remove inappropriate entries.
- Provide spam limits, text moderation, and reporting controls.
- Allow the owner to restrict who can write and who can read the guestbook.
- Preserve entries across server restarts; offer pagination for heavily visited properties.

### 7.2 Co-Decorating

Let homeowners invite others to help decorate.

- Add an **edit permission** beside the existing visitor access: who may decorate (party/raid, friends, guild members), granted separately from who may visit.
- Trusted decorators can place, move, rotate, scale, and store decor inside and outside the house, using the owner's decor budget.
- Decor placed by a helper comes from the **helper's** storage; storing a piece returns it to whoever placed it.
- Room layout, house style and parts, plot placement, upgrades, and permissions remain **owner-only**.
- Record who placed or changed each piece (the edit log already keeps the placing character) so owners can review and undo changes.
- Revoking the permission takes effect immediately for anyone currently editing.

---

## 8. Housing Creation Tools

### 8.1 Housing Blueprints

Allow builders to save and reuse **furniture layouts and room designs**.

- Save a selected group of objects, an entire room, or an eligible property layout as a named blueprint.
- Preserve compatible positions, rotations, scales, visual variants, and allowed cosmetic settings.
- Preview a blueprint before applying it and report missing or incompatible furniture.
- Require the destination property to have the necessary furniture or unlocked objects before placement.
- Allow personal blueprint libraries, sharing with friends, and sale of original layout designs through an approved system.
- Support an undo/rollback action for blueprint placement when feasible.
- Check plot boundaries, object limits, collisions, and property permissions before applying a layout.

**Security boundary:** Blueprints must **never** copy real inventory, shop listings, vendor earnings, stored chest items, personal or guild bank data, bound destinations, privileged permissions, or other ownership-sensitive state. They represent arrangements and permitted cosmetic configuration—not free duplicates of assets.

### 8.2 Decor Acquisition & House Level

Make collecting decor part of play instead of buying everything for gold from the catalog.

- Add **decor vendors** in the neighborhood with rotating stock, priced in gold or other currencies.
- Award decor as **quest rewards** and from **loot tables** (dungeon, raid, and world drops), achievements, professions, and events, using retail's decor-to-item links where available.
- Add **world treasure hunts** that lead players to hidden decor around the world.
- Grant **house experience** the first time a decor piece is acquired (retail's per-decor first-acquisition XP), raising a **house level** that unlocks larger budgets, more rooms or floors, and new exterior styles.
- Keep the gold catalog for basic furniture so new homeowners can still furnish a house.
- Show where an unowned decor piece comes from in the decor chest's catalog.

### 8.3 Retail Decor Coverage

Close the remaining gaps in the retail decor catalog.

- Five retail decor models are left out because they use shader effects the client cannot draw yet (two retail shader combinations).
- Add those shader effects to the client's model renderer; this also fixes any other retail model that uses them.
- Re-check newly released retail decor against the client and add what loads, so the catalog keeps pace with retail.

### 8.4 Decor Dyes

Let players recolor decor, as retail housing does.

- Use retail's per-model dye data (dye slots and their color and influence maps) to recolor marked parts of a decor piece.
- Add a **dye picker** to the decor editor for the selected piece, with the colors the player has unlocked.
- Save the chosen dyes with each placed piece so everyone sees the same colors, inside and outside the house.
- Requires a new decor shader in the client's renderer that blends the chosen colors through the dye maps.

### 8.5 World Doodads as Decor

Investigate offering the game world's own doodads (the props placed in zones and cities) as decor.

- Survey which doodads would make good decor, such as furniture, crates, plants, and banners from familiar zones.
- Check each one loads, renders, and scales correctly as a placed decor piece, and give it a name, category, placement surface, and price.
- Decide how they are unlocked (for example, tied to visiting or completing the zone they come from) so they fit decor acquisition (8.2).

---

## 9. Shared Interfaces & System Requirements

These are reusable foundations needed across multiple features; they are not additional standalone player-facing promises.

| Shared system | Used by |
| --- | --- |
| **Placement and persistence** | Furniture, portals, NPCs, training dummies, displays, doors, blueprints |
| **Property roles and permissions** | Shops, chests, portals, guestbooks, doors, PvP arenas, co-decorating |
| **NPC customization and interaction** | Merchants, bartenders, innkeepers, auctioneers, trainers, household service NPCs |
| **Inventory, escrow, and gold ledger** | Shops, taverns, commissions, storage, repairs |
| **Character collection validation** | Mounts, pets, trophies, mannequins, libraries |
| **Teleport routing and destination checks** | Neighborhood access, portal objects, dungeon stones, hearthstones, homeowner spells |
| **Isolated activities and encounter state** | Training halls, PvP arenas |
| **Custom client UI and rendering hooks** | Neighborhood button, shop management, blueprints, jukebox, lighting, weather, decor dyes, retail shader coverage |
| **Audit logs and recovery** | Shared storage, merchant sales, commissions, property changes |

### Recommended Implementation Order

**Foundation — Housing framework:** Persistent object state, permissions, object interaction, client UI entry point, and teleport validation.

**Phase 1 — Everyday utility:** Neighborhood button, favorites and recent visits, crafting equipment, mailboxes, bank access, placeable auctioneer/AH access, innkeeper/hearthstone (with housing hearthstone animations), homeowner spells, basic portals, repair station, simple storage, and retail decor coverage.

**Phase 2 — Economy and progression:** Player shops (including ledger and displays), taverns, crafting commissions, training hall, profession trainers, meeting stones, trophies, mannequins, mounts, pets, libraries, decor acquisition with house levels, and the Horde neighborhood.

**Phase 3 — Social and interactive spaces:** Co-decorating, personal neighborhoods and multiple homes, guild neighborhoods, functional doors, jukeboxes, guestbooks, and private PvP arenas.

**Phase 4 — Advanced systems:** Time-of-day/window updates, local weather, shareable/sellable blueprints, decor dyes, and world doodads as decor.

> **Implementation guidance:** The phases are suggested dependencies, not requirements. A feature can move earlier if its shared services already exist in Esteria.
