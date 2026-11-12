# Esteria — Shared Guild Halls

**Feature design specification and implementation concept**  
**Status:** Proposed — not an assertion of existing functionality  
**Target environment:** Esteria housing framework, AzerothCore server, customized/decompiled 3.3.5a client  
**Document focus:** Shared guild ownership, collaborative building, guild services, permissions, identity, progression, safety, and implementation direction

> **Design thesis:** A Guild Hall should feel like a home that an entire guild builds together, a social space worth visiting, and a functional headquarters for adventure. It must remain a durable **guild asset**, rather than a personal home that happens to admit guildmates.

---

## 1. Vision and experience goals

A Guild Hall is a persistent, guild-owned property that members can visit, decorate, develop, and use according to their guild's permissions. It should integrate with Esteria's existing housing placement, object customization, portal, training, banking, crafting, and collection systems instead of creating a parallel set of features.

### Player fantasy

- A newly formed guild secures a modest hall, raises its banner, and begins contributing furniture and decorations.
- Officers organize storage, define who may decorate, and designate meeting and crafting spaces.
- Members gather before dungeons, use shared facilities, and return to a home that visibly reflects their adventures.
- The guild displays trophies, honors notable members, records milestones, and gradually improves the property.
- The Guild Hall is still available and functional when the Guild Master or individual contributors are offline.

### Experience principles

1. **Belongs to the guild:** A Guild Hall survives a change in leadership; a leader cannot quietly take guild-owned resources as personal property.
2. **Collaborative, not chaotic:** Many members can participate, but every important action has a permission, clear ownership rule, and audit trail.
3. **Uses existing housing foundations:** Furniture placement, NPC functionality, item storage, access controls, blueprints, and decoration systems should be reusable.
4. **Useful without being mandatory:** Core conveniences are helpful, while progression mostly unlocks cosmetic expression and optional conveniences—not exclusive player power.
5. **Safe by design:** Sensitive actions are server-authoritative, logged, and recoverable where technically feasible.
6. **Easy to discover:** Guild Hall management belongs in the client UI, not behind GM commands or an obscure NPC.
7. **Scales:** Many guilds must be able to own halls without relying on a fragile assumption about finite phase bits or permanently loading every property.

### Non-goals for the initial release

- Replacing the existing guild rank and guild bank systems.
- Making a Guild Hall required for raiding, professions, or guild membership.
- Running housing-specific PvE siege / **Home Defense** events; that remains a separate deferred concept.
- Adding **Functional Kitchens** or **Table Games**; those remain separate deferred concepts.
- Giving guild leadership unrestricted control over personally owned items or accounts.
- Turning guild services into unrestricted progression or teleportation bypasses.

---

## 2. What is a Guild Hall?

### 2.1 Recommended ownership model: guild-owned property

Create a `Guild Hall` property type managed through the existing housing service, with **guild ID as the owning entity**. Its existence, furniture, upgrades, visitor policy, and service configuration are persisted independently of any particular character.

**Recommended:** The guild purchases or unlocks a dedicated Hall rather than converting a personal home by default. A separately owned property avoids ambiguity when a leader leaves, sells a personal house, or a guild disbands.

The Guild Master is the initial administrative authority, not the individual owner of the property.

### 2.2 Optional alternative: designate an existing personal house

Allow an owner to **temporarily designate** their personal property as the guild's meeting location without changing the actual owner. This should be a clearly labeled **Guild Headquarters / Linked Home** option, *not* the same thing as a guild-owned Hall.

| Capability | Guild-owned Hall | Linked personal home |
|---|---|---|
| Legal/game-system owner | Guild | Individual player |
| Survives leader departure | Yes | Only while owner permits |
| Guild-funded permanent upgrades | Yes | No, unless explicitly gifted under clear rules |
| Shared editable construction | Yes | Only areas authorized by homeowner |
| Guild bank and shared services | Yes | Access allowed, ownership stays distinct |
| Disbanding disposition | Guild asset settlement | Property stays with homeowner |

**Recommendation:** Ship dedicated guild-owned Halls first. Add linked personal homes later only if they are genuinely needed.

### 2.3 One hall or multiple halls?

**Initial recommendation:** One primary Guild Hall per guild. This produces an easily understood home destination and keeps moderation/maintenance straightforward. The data model can reserve support for future annexes, satellite outposts, or additional wings without committing to multiple primary properties at launch.

### 2.4 Guild Hall statuses

- `UNOWNED`: Guild has not acquired a hall.
- `ACTIVE`: Hall is live and member access is available.
- `MAINTENANCE`: Administrator-controlled maintenance; no destructive edits.
- `FROZEN`: Ownership or moderation dispute; public access and sensitive actions may be limited.
- `ARCHIVED`: Hall is closed to normal visitors pending a defined guild-disband resolution.

---

## 3. Acquisition, location, and arrival

### 3.1 Founding a Hall

A Guild Master opens **Guild > Hall > Establish Hall** and completes a short flow:

1. Review eligibility (guild exists; optional minimum membership/age if desired).
2. Choose a supported plot or interior template.
3. Preview the property and its starting room capacity.
4. Confirm the required guild contribution, deed, quest, or fixed gold cost.
5. Establish the Hall and assign its initial arrival point and privacy settings.
6. The guild receives a Hall Management entry, an announcement, and an initial guild banner.

Use simple, predictable acquisition rules. Avoid a real-estate bidding market unless property scarcity is an intentional game mechanic.

### 3.2 Types and aesthetic themes

Offer distinct environments without changing baseline gameplay power:

- **Fortified Keep:** Stone courtyard, war room, training space, defensive walls.
- **Elven Enclave:** Elegant hall, terraces, gardens, arcane detailing.
- **Dwarven Lodge:** Mountain workshop, forge ambience, trophy gallery.
- **Horde Great Hall:** Timber-and-stone gathering space, large banners, communal areas.
- **Arcane Sanctum:** Floating architectural details, magical portals, study rooms.
- **Undercity Crypt:** Gothic chambers, eerie lighting, underground ambience.
- **Neutral Adventurer's Lodge:** Compact, adaptable headquarters suitable for mixed aesthetics.

These are **design themes**, not hard faction or class restrictions unless Esteria intentionally imposes them.

### 3.3 Guild Hall placement in the world

Two supported presentation patterns are worth considering:

**Private Hall entrance in an existing Neighborhood:** A door or portal in a common neighborhood plaza directs each guild to its own isolated property. This improves discovery and minimizes the need for hundreds of unique physical map locations.

**Dedicated exterior plot:** A guild selects from preset exterior plots and enters its own isolated copy. This supports more elaborate grounds and outdoor furniture.

The Hall's **instance/property identity** must be derived from a stable hall identifier, not merely from a guild member's current character or a naïve phase-bit assignment.

### 3.4 Arrival and exits

- Define a safe **Hall Spawn Point** in an authorized public/member area.
- Store a valid exit anchor back to the Neighborhood or an approved city.
- If permissions change while a player is inside, safely eject them or relocate them to the entrance.
- Resolve logout/login, server restart, death, teleport interruption, and disband events without trapping players.
- Travel follows normal combat, battleground, dungeon, cooldown, and progression restrictions unless explicitly overridden by Esteria design.

---

## 4. Client UI and navigation

The Guild Hall should feel like a native Esteria feature within the 3.3.5a client.

### 4.1 Entry points

**Primary:** Add a **Guild Hall** tab in the Guild or Social interface, with a clearly visible **Visit Guild Hall** button.

**Secondary:** Connect the existing **Neighborhood/Housing** navigation button to a **My Guild Hall** destination card. If the character has no guild or the guild has no Hall, explain why the action is unavailable.

This avoids adding too many top-level micro-buttons while still keeping Hall travel one or two clicks away.

### 4.2 Proposed tabs

| Tab | Available to | Contents |
|---|---|---|
| Overview | Members | Hall name, scene preview, message, online visitors, amenities, Visit button |
| Build | Authorized decorators | Furniture inventory, placement mode, rooms, permissions, draft changes |
| Facilities | Members and allowed visitors | Banks, crafting, portals, trainers, auctioneer, repair, mail |
| Projects | Members | Shared construction projects, material requirements, contributions |
| Chronicle | Members/approved guests | Guild history, trophy entries, milestones, notable events |
| Events | Members | Upcoming meetings, raid planning, invitations, attendance tools |
| Permissions | Guild Master/authorized officers | Rank rules, room rules, service access, temporary grants |
| Activity | Authorized viewers | Filterable audit log and recent build changes |
| Settings | Guild Master/authorized officers | Hall name, visitor policy, theme, arrival points, upgrade management |

### 4.3 In-world interaction

- Approaching placeable equipment displays familiar interaction cues.
- A contextual radial/interaction menu or custom housing frame distinguishes **Use**, **Edit**, **Move**, **Pick Up**, and **Configure**, based on rights.
- Players without edit rights should never see misleading controls that appear to allow changes.
- Authorized players can inspect an object's contribution/ownership status before removal or transfer.
- Dangerous actions include explicit confirmation and explain where the affected furniture or items will go.

### 4.4 Example Guild Hall overview

```text
┌───────────────────────────────────────────────────────┐
│ GUILD HALL                                            │
│ The Argent Lantern                                   │
│ Emberwatch Lodge · 14 members currently inside       │
├───────────────────────────────────────────────────────┤
│ [Visit Hall] [Invite Guest] [Hall Map]                 │
│                                                       │
│ Upcoming: Raid Planning · Saturday 7:00 PM            │
│ Current Project: Complete the Trophy Gallery (68%)    │
│ Available: Guild Bank • Forge • Portals • Auctioneer  │
│                                                       │
│ [Overview] [Build] [Projects] [Chronicle] [Activity]  │
└───────────────────────────────────────────────────────┘
```

This is a design mockup, not an existing UI.

---

## 5. Permissions and collaborative governance

### 5.1 Use guild ranks, but do not reuse unrelated guild rights

Guild Hall permissions should **map to existing guild ranks** but be stored as a separate housing-specific permission set. For instance, permission to invite guild members should not automatically imply permission to remove valuable furnishings.

Existing guild-rank permissions remain authoritative for standard guild actions; the Hall layer adds its own rights and must check current membership/rank at action time.

### 5.2 Suggested roles

These are templates, **not hardcoded ranks**. Guild Masters can assign rights to custom ranks.

| Role template | Typical rights |
|---|---|
| Guild Master | Full Hall administration; destructive actions require extra confirmation |
| Hall Steward / Architect | Manage layouts, rooms, decor, nonfinancial services |
| Officer | Manage events, approve requests, access approved secure facilities |
| Decorator | Place and move furniture in assigned zones |
| Member | Visit, use services, contribute to projects, view Chronicle |
| Recruit / Trial | Limited visit and amenity rights |
| Invited Guest | Access approved public/guest rooms and services only |

### 5.3 Granular permission flags

**Visiting**
- Enter Hall / enter while guild is offline / invite guests / use guest portal.
- View private rooms / view public gallery / view Activity log.

**Construction**
- Place cosmetic furniture / move furniture / rotate-scale where supported / edit dye or appearance.
- Remove to shared storage / consume guild-funded materials / edit room layout / create room.
- Publish blueprints / apply a blueprint / modify spawn and travel anchors.

**Services**
- Use guild bank / administer storage tabs / use crafting stations / use portals.
- Place or configure NPCs / configure merchant listings / collect guild merchant earnings.
- Access auctioneer, mailbox, repair station, personal bank, trainer, and other service NPCs.

**Governance**
- Adjust member permissions / adjust public visitor policy / approve high-value projects.
- Spend guild treasury / perform expensive upgrades / change Hall template.
- View audit records / perform authorized recovery / request Hall relocation.

### 5.4 Room-based permissions

Every room or named zone can have a policy such as:

- **Public:** Anyone meeting visitor conditions can enter and use permitted amenities.
- **Guest:** Invited nonmembers and guild members can enter.
- **Members:** Guild members only.
- **Officer:** Only designated ranks can enter.
- **Restricted:** Explicit allowlist for a workshop, archive, or officer's vault.

Permissions to **enter**, **interact**, **decorate**, **remove**, and **withdraw** must be separate. An object inherits the stricter of its own restrictions and the room's restrictions when relevant.

**Important:** Decorative secret doors cannot be used to bypass access checks. The server enforces room access regardless of how the client renders a door.

### 5.5 Recommended default configuration

- Guild Master: full management.
- Officers: visits, event management, and limited room configuration; no unbounded high-value withdrawals.
- Members: visit, use common facilities, contribute items, place approved décor in public build zones.
- Recruits: visit and use basic public amenities, but no removal/editing rights.
- Guests: visit only explicitly accessible areas; no guild storage or financial access.

**Default deny** for resource extraction and irreversible actions. Granting new ranks permissions should require deliberate setup.

### 5.6 Collaborative building controls

- **Edit leases:** When one person edits a specific object, temporarily lock that object against conflicting edits by others.
- **Revision numbers:** Reject stale changes instead of silently overwriting another decorator's work.
- **Draft mode:** A decorator may preview furniture arrangements; only authorized publishing commits shared state.
- **Change summaries:** Publish a build batch as one named revision (e.g., “Trophy Gallery renovation”).
- **Undo/recovery:** Restore recent layout placements where possible, without duplicating consumed items or reversing unrelated transactions.
- **Soft locks:** Officers can temporarily freeze a room for construction or special events.

---

## 6. Building, rooms, architecture, and blueprints

### 6.1 Room presets and layouts

Start with configurable rooms built on the normal housing placement system:

- Grand Hall / meeting area
- Workshop / crafting wing
- Vault / treasury
- Trophy Gallery
- War Room / expedition planning
- Portal Chamber
- Training Hall
- Library / Chronicle archive
- Courtyard / stable / mount display
- Social lounge or guest area

The guild may rename rooms and choose color themes, lighting, furniture arrangements, and access policy.

### 6.2 Shared versus personal decorative items

A placeable object must have **provenance**, not merely a current holder:

1. **Guild-funded:** Purchased or crafted from guild-owned resources; the guild owns it.
2. **Donated:** A member explicitly gifts the furniture to the guild; ownership transfers permanently after confirmation.
3. **Loaned:** A member lends furniture to the Hall but retains ownership; only appropriate users can move or recall it, and it follows a special return policy.
4. **Server-granted:** Awarded directly to the guild via an achievement, event, or support action.

**Recommendation:** Launch with guild-funded and donated assets. Loaned items add complexity and should arrive later with clear dispute handling. Never assume that placing a personal item automatically donates it.

### 6.3 Build zones and object budgets

- Configurable room-level limits for total objects, animated objects, particle effects, and NPCs.
- Server-side collision, valid placement surface, map boundary, and forbidden-area checks.
- Visual indicators when a placement exceeds the selected room's resource budget.
- Conservative limits on large NPCs, pathfinding, and effect-heavy decorations.
- Object budget can expand with hall upgrades, but every upgrade remains bounded by server performance limits.

### 6.4 Guild blueprints

A guild blueprint saves **layout references and transforms**, not new copies of items.

- Save an entire room, themed set, or Hall layout as a blueprint.
- Preview material/furniture requirements before applying.
- If required items are missing, offer a partial placement preview rather than creating them for free.
- Require authorization and optionally a second approver for replacing large areas.
- Record creator, date, revision, and sharing policy.
- Preserve room permissions, bank links, and NPC configurations only when explicitly supported and valid for the destination guild.

**Critical:** Applying a blueprint must never mint item inventory, vendor stock, currency, or restricted travel permissions.

---

## 7. Functional facilities

Guild Halls should reuse Esteria's existing placeable services, with a guild-aware configuration and permission layer.

### 7.1 Guild Bank and personal banking

- Place a guild vault that opens the **existing guild bank**, not a duplicate financial ledger.
- Respect existing guild-bank rank/tab permissions and withdrawal allowances.
- Optionally add decorative vault doors, themed bank NPCs, and visible but non-extractable display shelves.
- Place a separate **Personal Bank** service for private character inventory; its contents remain personal.
- Audit Hall-specific interactions that change shared property; do not assume an extra Hall log can replace canonical guild-bank audit behavior.

### 7.2 Shared crafting wing

- Place anvils, forges, looms, alchemy stations, enchanting facilities, and other compatible workstations.
- Enable relevant crafting UI only when character skill, recipes, and materials permit.
- Allow guild projects to post material requests near workstations.
- Optional “workshop steward” NPC displays active construction requests and points members to the required station.
- Use ordinary profession mechanics; no automatic crafting without an explicitly designed separate feature.

### 7.3 Portal Chamber and meeting stones

- Place guild-managed portals to approved destinations and dungeon entrance stones.
- Let authorized officers configure allowed destinations, visuals, names, and room access.
- Show a confirmation/destination tooltip before teleporting.
- Verify each user independently against travel rules, unlocks, and phase access.
- Respect combat, instance, battleground, and cooldown restrictions.
- The destination is an approved city, location, or **dungeon entrance**, not a bypass into a locked raid.

### 7.4 Trainers, repairs, mail, and auction access

- Place profession trainers, mailboxes, repair NPCs, personal bank access, and **Auctioneers** as eligible services.
- The Auctioneer must open the existing AzerothCore Auction House; it is not a parallel market.
- Use normal AH fees, faction/neutral rules, listing restrictions, and server checks.
- If reduced repair costs are enabled in housing, define exactly who pays the subsidy (personal gold, guild treasury with limits, or server discount), and apply it consistently.
- NPC placement, removal, and configuration require explicit Hall service permissions.

### 7.5 Training Hall

- Configurable dummies and timed training sessions.
- Shared room ownership with isolated per-player or per-party session metrics.
- Prevent one member's dummy session from contaminating another's combat metrics.
- Optionally reserve training lanes and set room-specific training permissions.

### 7.6 Trophy Gallery, mount display, and mannequins

- Display raid completions, boss trophies, guild milestones, armor sets, and selected mounts.
- Distinguish **guild-earned trophy unlocks** from **character-owned collectibles** displayed with permission.
- Support stable-style mount exhibitions or static animated/statue showcases.
- Allow guild members to submit display nominations, with approval rights for official guild trophies.
- When a member leaves, personal display rights should expire without deleting the guild's own achievements.

### 7.7 Library, guestbooks, and social gathering

- Create a Guild Chronicle bookshelf with lore and readable records.
- Let members contribute eligible books and exploration discoveries.
- Add a guestbook with moderation, spam limits, and optional guest signing permissions.
- Provide furniture, seating, a jukebox, and a configurable event space.
- Social gathering does not require the deferred **Table Games** or **Functional Kitchen** features.

### 7.8 Guild shop and vendor possibilities (optional expansion)

Two distinct commerce models are possible:

**Guild Shop:** Guild-owned stock is sold by a guild merchant, and proceeds go to a separate auditable guild shop treasury or explicitly into the guild bank where supported. Authorized ranks control inventory and withdrawals.

**Member Stall:** Individual members operate their own shops in designated guild market spots. Their stock and proceeds remain personal unless a disclosed commission policy applies.

**Recommendation:** Do not blend these models. Establish clear owner, seller, inventory, fee, tax, and withdrawal fields before enabling guild storefronts.

---

## 8. The War Room — a signature Esteria feature

**Recommended flagship addition:** A configurable **Guild War Room** that turns the Hall into an adventuring headquarters.

### 8.1 Interactive expedition table

Place a large interactive war table with a world map or campaign-style UI:

- See the guild's upcoming dungeon and raid events.
- Post a proposed destination, difficulty, planned time, and required roles.
- Let members sign up as tank, healer, DPS, substitute, or observer.
- Show known meeting locations and eligible entrance teleports where supported.
- Open related social/party interfaces without inventing a new group system.
- Display the guild's recent clears and featured objectives.

### 8.2 Raid banners and memorials

- Defeating a designated boss with qualifying guild participation can unlock a banner, pedestal, or wall plaque.
- Trophy inscriptions can show encounter, date, difficulty, and participating guild members based on stored event data.
- Leaders can curate visible trophies without erasing the underlying accomplishment record.
- Old members may remain credited for achievements they helped earn, even after leaving the guild.

### 8.3 Preparedness without forced bonuses

War Rooms should streamline organizing groups and travel rather than offer large combat buffs. Avoid making the Hall a compulsory power upgrade before every raid.

### 8.4 Example interaction

1. An officer walks to the War Table and creates “Naxxramas — Friday 8 PM.”
2. Guild members see the event on the Hall panel and sign up for roles.
3. The tabletop map highlights the destination and meeting time.
4. Before departure, members meet in the Hall, craft consumables, repair equipment, and visit the shared bank.
5. Eligible members use the relevant meeting stone to arrive at the dungeon entrance.
6. After the clear, a record may be added to the guild Chronicle and trophy gallery.

---

## 9. Additional Esteria-specific ideas

These are optional creative extensions to distinguish Guild Halls from generic guild-house modules.

### 9.1 Guild Chronicle: the living history of a guild

A persistent history book or wall of memories could record:

- Guild founding date and Hall establishment date.
- Leadership changes and Hall relocations.
- First guild raid clear, major milestones, and celebrated achievements.
- Contributors to important building projects.
- Custom captions for memorable moments (moderated and limited in length).

Historical records should be factual when auto-generated and clearly marked as player-written when not.

### 9.2 Community construction projects

Instead of having only one officer pay for every expansion, give guilds **project boards**:

- “Restore the Great Library: 40 lumber, 20 enchanted thread, 10 arcane crystals.”
- “Construct the Eastern Workshop: gold and construction supplies.”
- “Craft the Guild Banner: insignia and dyes.”

Members deposit eligible items. Contributions are immediately recorded; on completion, the upgrade unlocks for the guild. Avoid mandatory daily chores, punishing failure timers, or rewards so powerful that guildless players are disadvantaged.

### 9.3 Guild identity across the Hall

- A **Guild Crest Editor** that reuses or extends the guild emblem/tabard motif for signage, rugs, stained glass, flags, and banners.
- Automatic display of the guild name and insignia at the Hall entrance.
- A themed Hall soundtrack/jukebox setting controlled by authorized ranks.
- Optional colors for banners and interior light accents.
- A prominent guild message board that shows welcome text and event notices.

### 9.4 Recruit Open House

Guilds may set a scheduled **Public Open House** period:

- Approved guest rooms become accessible for a limited window.
- Visitors can inspect the trophy gallery and public amenities.
- A recruitment board shows guild description, preferred activities, language, and contact/application options.
- Officers can end the session early, revoke access, or eject disruptive visitors.

This creates a social reason to visit guild neighborhoods without weakening default privacy.

### 9.5 Guild Member Corners

Allow limited, optional **member showcases**:

- A member may decorate one allocated alcove, desk, or trophy niche.
- Their display remains under the guild's overall room rules.
- On leaving, personal items are returned according to provenance rules.
- Their contribution to the guild Chronicle remains intact.

Do not sell permanent ownership of guild rooms to individuals unless a detailed tenancy system exists.

### 9.6 Hall ambience and NPC life

- Noncombat custodians, archivists, porters, guards, or stablehands with simple schedules.
- Ambient conversations, sitting, sweeping, and patrolling within defined paths.
- Companion creatures or pet sanctuary decorations in designated rooms.
- Limit persistent AI and pathfinding to active Hall instances to control performance.

### 9.7 Neighborhood visibility

- Guild entrances can appear in a searchable **Guild Hall Directory** with privacy controls.
- Public galleries may be listed under **Open to Visitors**.
- Guilds can pin a featured room image or approved event preview.
- No automatic public visibility of private rooms, resources, or members' precise activity.

---

## 10. Contributions, treasury, and progression

### 10.1 Contributions

Support contributions of:

- Eligible furniture and decorations.
- Required materials for construction projects.
- Approved gold donations for fixed upgrades.
- Time spent assisting on curated guild projects (as a nonmonetary recognition metric).

A contributor sees whether a deposit is a **gift**, a **reversible pledge**, or a **loan** before confirming. For the first release, simplify to gifts and explicit guild-funded purchases.

### 10.2 Guild treasury versus guild bank

Do not create a second unrestricted guild money store by accident.

**Recommended model:** Use existing guild bank gold and withdrawal rules for guild money whenever feasible, with Hall projects recording *authorized spending intents* and project reservations. If a specialized project escrow is required, it needs its own atomic lifecycle and audit records.

### 10.3 Suggested cosmetic progression tiers

| Hall stage | Example unlocks | Design boundary |
|---|---|---|
| Founding Hall | Entrance, meeting room, banner, essential shared access | No guild power advantage |
| Established Hall | Additional rooms, decorative NPC slots, trophy displays | Functionality still obtainable elsewhere |
| Renowned Hall | Expanded galleries, custom architecture, richer ambient effects | Cosmetics and organization |
| Legendary Hall | Signature façade, grand banners, distinguished Chronicle displays | Prestige, not mandatory combat power |

Prefer progress through **guild accomplishments and community builds**, not endless gold sinks.

### 10.4 Anti-abuse and fairness

- Do not grant major stats, raid damage bonuses, or exclusive crafting power because a guild owns a Hall.
- Limit repair discounts or travel shortcuts in line with broader Esteria balancing.
- Prevent repetitive item turn-ins from becoming exploitable farming loops.
- Never require members to donate to remain in a guild.

---

## 11. Events, visitors, and public life

### 11.1 Event scheduling

- Plan guild meetings, raid nights, social gatherings, recruitment sessions, and celebrations.
- Allow event creators to designate allowed rooms and maximum attendance.
- Integrate with existing guild event/calendar functions where compatible; do not silently create conflicting event stores.
- Show reminders and Hall location in the event detail.
- Offer officer-approved seating arrangements, temporary decorations, and room setups.

### 11.2 Visitors and invitations

Support:

- Guild-only mode.
- Member-invited guests (with configurable issuer rights).
- Officer-approved guest invitations.
- Public open-house sessions.
- A small explicit allowlist of allied guests, if desired later.

Guest permissions may expire by time, logout, event end, or manual revocation. A **guest is never implicitly authorized to use guild inventory or change property layout**.

### 11.3 Presence and capacity

- Show a headcount and room occupancy, with privacy-aware display of names.
- Set a safe max-occupancy policy based on map/instance performance and server configuration.
- If a Hall reaches capacity, show an informative message and allow members to retry; do not drop characters into an unsafe map location.

---

## 12. Ownership lifecycle and exceptional cases

This section is essential. Many guild-house designs work beautifully until the Guild Master leaves, a guild disbands, or a member demands furniture back.

### 12.1 Leadership changes

- Hall ownership remains bound to stable **guild ID**.
- Guild Master transitions update administrative rights; they do not transfer furniture or money to the new leader's inventory.
- The server updates rights immediately after a rank or leadership change.
- Former leaders retain only the rights associated with their new rank.
- High-impact policy changes, like allowing public access or changing property template, can trigger a guild announcement.

### 12.2 Member leaves or is removed

- Revoke Hall membership access and rank-based permissions immediately.
- Move them safely to a default location if they are in a restricted part of the Hall.
- Return only properly tagged **loaned** items, subject to the loan agreement; **donations remain guild property**.
- Cancel or reassign unfinished personal commissions according to their explicit escrow rules.
- Preserve historical credit in the Chronicle.

### 12.3 Hall relocation

- Only an authorized role can initiate a move.
- Preview how current room geometry affects furniture placement and NPC placement.
- Require an explicit confirmation and a safe snapshot/backup.
- If the new plot cannot accommodate everything, transfer non-fitting furniture to **guild-owned storage**, never discard it.
- Update arrival anchors, directory listings, and portal configurations atomically or through a recoverable migration procedure.

### 12.4 Disbanding the guild

**Recommended conservative policy:** When the guild is disbanded, immediately **freeze and archive** the Hall rather than deleting its contents on the same tick.

- Deny new bank withdrawals, vendor changes, furniture moves, guest invitations, and paid upgrades through Hall services.
- Move or log out current visitors safely.
- Generate a final inventory of guild-owned, donated, loaned, and server-granted assets.
- Return personal **loaned** assets to valid owners through a recoverable delivery mechanism.
- Preserve guild-owned donated assets in an archive during an administrator-defined retention window; **do not automatically award them all to the last leader**.
- After the retention period, apply a published server policy (e.g., permanent deletion of guild-owned decor, or staff-mediated recovery only for proven errors).
- Record the transition in audit logs and prevent orphaned Hall instances or resources.

Any guild bank money/item behavior should follow its own canonical guild disband implementation and must be checked before this workflow is coded.

### 12.5 Other edge cases

- Guild rename: same guild ID, same Hall, signage refreshed.
- Inactive leader: respect existing guild succession policy; do not invent an unreviewed Hall-only takeover mechanism.
- Guild merges: no automatic merge of properties/inventory in v1; provide a future explicit migration flow.
- Faction changes: validate cross-faction guest/service rules.
- Character deletion: Guild-owned assets unaffected; personal loans follow documented recovery.
- Hall instance crash: persistence and rollback must prevent item losses and duplication.

---

## 13. Audit logs, security, and recovery

### 13.1 What to log

| Event | Minimum audit fields |
|---|---|
| Furniture placed, moved, removed | Actor, guild, Hall, object, old/new location, timestamp |
| Item donated/withdrawn | Actor, item identifier, quantity, source/destination, transaction ID |
| Gold/project spending | Actor, amount, project, balance movement reference, timestamp |
| Rank/permission change | Actor, target rank, before/after rule, timestamp |
| NPC/service configuration | Actor, NPC/object, changed configuration, timestamp |
| Blueprint applied | Actor, blueprint revision, affected objects, result |
| Hall relocated/disbanded | Initiator, reason, status transition, snapshot/archive reference |
| Guest access changed | Actor, invite/permission scope, expiry/revocation |

### 13.2 Preventing griefing and exploitation

- Validate every action on the server: membership, role, room policy, item ownership, spatial limits, and current revision.
- Use database transactions for inventory and currency moves, and idempotent retries for network failures.
- Put configurable rate limits on repeated placement, deletion, purchases, and teleports.
- Require confirmation for removals of rare, expensive, or guild-donated items.
- Protect bank and vendor transactions by reusing existing systems, not trusting a modified client.
- Include staff tools to inspect recent changes, freeze a Hall, and restore supported layout states.
- Avoid allowing client-supplied teleport coordinates, item GUIDs, prices, or NPC templates without server validation.

### 13.3 Permission-change notifications

An optional guild activity feed can show important events:

- “The Great Library was completed.”
- “Officer access to the Treasury was changed.”
- “A rare display was moved to guild storage.”

Keep private details (e.g., individual bank histories) visible only to roles already authorized to see them.

---

## 14. Conceptual technical architecture

> This is an architectural recommendation, not a drop-in SQL migration or a claim that specific APIs already exist in Esteria's current codebase.

### 14.1 Server authority and shared modules

Build Guild Halls **on top of** a common housing service:

- `HousingPropertyService` — property lifecycle, ownership type, plot and map association.
- `HousingPlacementService` — object placement, bounds, revisions, and persistence.
- `HousingPermissionService` — role-based permissions, room policies, access checks.
- `HousingInteractionService` — server-validated actions for furniture and services.
- `GuildHallService` — guild ownership, roles mapping, projects, relocation, and disband.
- `HousingAuditService` — immutable event records and administrative inspection.
- `HousingTravelService` — member entrance, visitor tokens, teleport safety.

A separate guild module should not copy all personal housing code. It should supply the **guild-specific ownership/permission adapter** and Guild Hall-only features.

### 14.2 Property isolation strategy

AzerothCore has a public `mod-guildhouse` implementation using guild-specific phasing and service NPCs. That is a helpful prototype/reference, but it is **not equivalent to a general scalable collaborative housing engine**.

**Recommended evaluation:** Compare (a) dedicated per-property instances/maps, (b) dynamic phases, and (c) hybrid neighborhood entrances with isolated interiors. Evaluate visibility, collision, phase limits, cross-guild leaks, persistence, and concurrent visitor capacity.

Avoid deriving phase masks naively as `guildId + offset`; phase masks and phase IDs are not interchangeable, and valid values/collision behavior must be audited for the specific client/core. Do not assume a unique bit per guild scales indefinitely.

The implementation should have a stable `hall_id`, `property_id`, and `guild_id` association, with an isolation backend that can be swapped without rewriting furniture ownership.

### 14.3 Suggested conceptual data entities

```text
housing_property
  property_id, property_type, owner_type, owner_id,
  template_id, map/isolation_reference, status, created_at

guild_hall
  hall_id, property_id, guild_id, hall_name,
  arrival_anchor_id, theme_id, visitor_policy, revision

guild_hall_rank_permissions
  guild_id, guild_rank_id, permission_flags,
  withdrawal/project limits, updated_at

guild_hall_room
  room_id, hall_id, name, room_type, access_policy, build_budget

housing_object
  object_id, property_id, template_id, room_id,
  transform, state, owner_provenance, donor_guid, revision

guild_hall_project
  project_id, hall_id, requirements, funded_state,
  progress, created_by, approved_by, completed_at

guild_hall_contribution
  contribution_id, project_id, actor_guid, resource_type,
  amount, transfer_txn_id, recorded_at

guild_hall_audit
  audit_id, hall_id, actor_guid, action,
  entity_type, entity_id, before_state, after_state,
  transaction_ref, occurred_at
```

These are **logical entities**, not final table names or a migration-ready schema. Design types, indexes, storage engines, constraints, and foreign-key behavior against the actual Esteria/AzerothCore schema before implementation.

### 14.4 Example server action contract

```text
PlaceHallObject(character, hallId, templateId, transform, sourceItem)
  -> validate guild membership and current rank
  -> validate room/build permissions and placement limits
  -> reserve/consume the source item atomically
  -> persist object + audit record
  -> broadcast placement to hall occupants

ChangeHallPermissions(character, hallId, rankId, newRules)
  -> validate Hall administration rights
  -> validate target rules and prevent privilege escalation
  -> store revision + audit change
  -> invalidate cached rights for affected members

EnterGuildHall(character, hallId, invitationToken?)
  -> validate live guild membership or active guest access
  -> enforce travel and instance restrictions
  -> resolve safe entry coordinates and available instance
  -> teleport and update current occupant state
```

### 14.5 Client / server responsibilities

| Client (custom 3.3.5a) | Server (AzerothCore) |
|---|---|
| Guild Hall menu, room browser, placement preview | Permission and resource checks |
| Build gizmos, object selection, color/appearance UI | Authoritative transforms, persistence, valid templates |
| Guild War Room interface and event presentation | Event state and eligibility checks |
| Display audit messages, project progress | Transaction history and project fulfillment |
| Visual weather/lighting theme where supported | Authorized settings and shared replicated state |
| Visitor UI and travel requests | Eligibility, safe teleport, instance separation |

Do not let the client decide item ownership, guild rank, gold balance, or permanent object creation.

### 14.6 Reliability and performance

- Lazy-load Hall instances when visited; unload after an inactivity window while retaining persistent state.
- Replicate object changes only to relevant occupants.
- Use batched saves/transactions where safe, with immediate durable writes for item/currency changes.
- Define build budgets and per-instance soft/hard caps.
- Add metrics for Hall creation, active occupants, objects, object spawn time, update latency, and failed permission checks.
- Load-test concurrent decorators and many halls across multiple guilds.

---

## 15. Example player journeys

### Journey A — Founding a new Guild Hall

A Guild Master selects **Guild > Hall > Establish Hall**, previews an Adventurer's Lodge, and confirms a modest guild treasury cost. Esteria creates a Guild Hall owned by the guild's stable ID, assigns a spawn location, and offers a starter banner. Members now see **Visit Guild Hall** in their Guild panel.

### Journey B — A member helps decorate

An Architect opens a Public Gallery for member decorating. A member places an earned chair and sees a dialog: **Donate to Guild** or **Keep in Personal Inventory**. After donation, the furniture is guild property. The server logs the contribution. Another decorator can move the chair only if they have permission for that room.

### Journey C — The guild prepares for a raid

An officer posts a raid on the War Table. Members sign up, gather at the Hall, repair gear, take eligible materials from the guild bank, and use approved travel facilities. A trophy unlocks afterward if the guild-clear requirement is met.

### Journey D — An officer leaves on bad terms

The guild removes an officer's membership. Their Hall access and admin rights disappear immediately. Guild-donated decorations remain in the Hall; properly tagged personal loans enter the return process. Audit logs show all recent removals and spending. No single client action can transfer the Hall to the former officer.

### Journey E — The guild disbands

The Hall enters a frozen archival state. Guests are returned to a safe location. Loaned goods are scheduled for return through a reliable delivery mechanism. Guild-owned property is handled under a transparent retention policy. The former Guild Master does not automatically inherit everyone's gifts.

---

## 16. Implementation roadmap

Build the foundations in dependency order. This order assumes that Esteria's basic housing placement and persistence systems already exist or are being developed.

### Phase 0 — Codebase audit and integration design

- Inspect the existing Esteria housing/property tables and ownership model.
- Inspect the current AzerothCore guild rank, guild bank, guild lifecycle, event, and travel paths.
- Identify available client UI surfaces and custom packet/RPC approaches.
- Prototype instance/phase isolation and confirm its actual scaling behavior.
- Define property IDs, item ownership provenance, transaction boundaries, and recovery rules.

**Exit condition:** A verified technical design and testable lifecycle plan; no assumptions about existing APIs left undocumented.

### Phase 1 — Core Guild Hall MVP

- Guild-owned property type, creation, persistence, and safe travel.
- Guild Hall tab / Neighborhood destination.
- Simple Hall templates and a common meeting room.
- Permission mapping from guild rank to Hall rights.
- Basic decorating and placement with room limits.
- Guild Bank access, mailboxes, and basic crafting stations where already supported.
- Audit log for build changes and Hall configuration.
- Leader changes and membership revocation handling.

**Exit condition:** Two guilds can own isolated halls, multiple members can collaborate safely, and leader changes do not affect ownership.

### Phase 2 — Collaboration and useful services

- Room permissions, custom titles, guest invitations, and common service access controls.
- Shared furniture donation workflow and guild object inventory.
- Guild portal chamber, repair, trainer, personal bank, Auctioneer, and approved meeting stones.
- Trophy room, mount display, and mannequins.
- Basic project board, contribution logs, and initial room upgrades.
- Staff tools for freezing and inspecting halls.

**Exit condition:** Guild leadership can configure a functioning headquarters while preserving ownership and economy safety.

### Phase 3 — Distinctive Guild Hall experience

- Interactive Guild War Room with event and expedition planning.
- Guild Chronicle and curated trophy milestones.
- Guild crest decoration system and expanded architectural themes.
- Build drafts/revisions, limited undo, and Guild Hall blueprints.
- Public open houses, recruitment boards, and Hall directory.
- Member showcases and ambient NPC life.

**Exit condition:** Guild Halls feel unique, social, and worth visiting regularly.

### Phase 4 — Advanced scale and polish

- Additional room/wing layouts and improved placement tooling.
- Optional guild shops and member stalls with distinct ownership.
- Robust move/relocate/archive workflows.
- Larger-scale performance optimizations and operational dashboards.
- Potential linked-home, multi-hall, or allied-guild extensions after policy review.

---

## 17. Acceptance criteria and test matrix

The feature should not be declared production-ready until the following behaviors are validated:

### Ownership and identity

- [ ] Creating a Hall associates it with the correct guild ID, never the Guild Master's personal character.
- [ ] A Guild Master transfer preserves Hall, furniture, services, and history.
- [ ] Guild rename updates signage without changing property identity.
- [ ] One guild cannot enter or see another guild's private Hall without authorization.

### Permissions

- [ ] A recruit cannot decorate, remove objects, or withdraw valuables by default.
- [ ] A decorator can move approved furniture but cannot access a restricted vault.
- [ ] Rank changes take effect immediately for users currently inside the Hall.
- [ ] Room secret doors and client-side UI hacks cannot bypass server-side checks.
- [ ] Public guests cannot use guild bank or alter Guild Hall configuration.

### Items and economy

- [ ] Donated furniture transfers to guild ownership exactly once.
- [ ] Moving an object to guild storage does not duplicate or destroy its underlying item.
- [ ] Blueprints consume/reserve only legitimately owned objects/materials.
- [ ] Repair and guild-bank spending follow correct balance and allowance rules.
- [ ] Interrupted resource transactions recover without item or gold duplication.

### Travel and persistence

- [ ] Members can enter after reconnecting and after a server restart.
- [ ] Visitors are not stranded if permissions expire mid-visit.
- [ ] Portals and meeting stones cannot bypass progression or combat restrictions.
- [ ] The Hall safely unloads and reloads with its placed layout intact.
- [ ] Multiple guilds can occupy distinct interiors concurrently without overlap.

### Lifecycle and administration

- [ ] Member removal revokes access immediately.
- [ ] Disband freezes Hall actions and begins an explicit asset-handling workflow.
- [ ] Admins can find an object's provenance and recent change history.
- [ ] A large blueprint/relocation failure can be rolled back safely.
- [ ] Concurrent decorators cannot overwrite one another's changes silently.

### Performance

- [ ] Placement, movement, and object loading remain responsive at defined per-room limits.
- [ ] Many inactive Hall properties do not consume active-world simulation resources.
- [ ] Busy guild events and active ambient NPCs do not degrade unrelated maps.

---

## 18. Decisions to lock before development

| Decision | Recommended default |
|---|---|
| Who owns the Hall? | The guild, keyed by stable guild ID |
| Can a personal house become the Hall? | Not in MVP; separate linked-home extension later |
| How many Halls per guild? | One primary Hall initially |
| Can any member decorate? | Only approved zones and guild ranks |
| Do donated items return when members leave? | No; explicit donation permanently transfers ownership |
| Are temporary loans supported? | Defer until return and dispute flows are robust |
| Do Hall services bypass normal travel rules? | No |
| Does the Guild Bank remain authoritative? | Yes |
| Can visitors enter? | By explicit permission or timed public events |
| Can guilds earn combat power from Hall upgrades? | No major combat advantages |
| What happens at disband? | Freeze, archive, return loans, apply published retention policy |
| Should Hall design be limited to default AzerothCore phase logic? | No; test scalable property isolation first |
| Which signature feature gets priority after MVP? | Guild War Room + Chronicle + projects |

---

## 19. Recommended product positioning

The best way to describe this feature to players is:

> **Your guild. Your headquarters. Your legacy.**  
> Build a shared home for your guild, decorate it together, prepare for adventures, display the accomplishments you earned, and create a place worth returning to.

A great Guild Hall is not just an upgraded bank room. It is the place where a guild's history becomes visible. Players should want to stop by because their friends are there, because they helped create it, and because the building tells the story of what they have accomplished together.

**Recommended first milestone:** A guild-owned, safely isolated Hall with shared decorating, usable guild facilities, role-based permissions, and an audit log. Everything else gets much easier when those foundations are correct.

---

## 20. Technical reference links

The following resources are useful starting points for **auditing real AzerothCore behavior**, not proof that Esteria currently implements the proposed features:

- AzerothCore `mod-guildhouse` (guild-specific phase-based reference and common service NPCs): <https://github.com/azerothcore/mod-guildhouse>
- `mod-guildhouse` implementation source: <https://github.com/azerothcore/mod-guildhouse/blob/master/src/mod_guildhouse.cpp>
- AzerothCore guild rank table (rights and guild identifiers): <https://www.azerothcore.org/wiki/guild_rank>
- AzerothCore guild API reference (illustrates existing guild operations): <https://www.azerothcore.org/eluna/Guild/index.html>
- AzerothCore map information: <https://www.azerothcore.org/wiki/map>

**Related Esteria planning documents:**

- `Esteria_Housing_Main_Updated.md` — personal housing and shared services specification.
- `Esteria_Housing_Deferred_Features.md` — separately maintained feature concepts, including the original short Guild Halls proposal. This standalone document is the detailed expansion of that concept.

