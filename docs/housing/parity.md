# Retail housing parity (Phase 0)

All 8 URLs were fetched on 2026-11-05. None was unreachable. The pages show day and month but no year, so
dates are quoted as shown, and the year is inferred only where the page says so.

| # | URL | Title | Date shown | Status of contents |
|---|---|---|---|---|
| 1 | news/24186690 | Azeroth Beautiful: A Look at Housing Interior Design | March 12 | Announced, "work in progress" |
| 2 | news/24221516 | WoW Housing: It's Only Neighborly | July 10 | Announced, "work in progress" |
| 3 | news/24244457 | Come Home to Azeroth with Housing in Midnight | February 24 | Announced |
| 4 | news/24244464 | Stake Your Claim with Housing Early Access—Now Live! | December 3 | **Shipped** (Early Access) |
| 5 | news/24244406 | A Look Ahead at Housing in Midnight | February 24 | Shipped and planned |
| 6 | news/24295382 | Curse of Ula'tek Housing Updates | August 10 | **Shipped** |
| 7 | news/24293281 | Curse of Ula'tek Content Update Notes | August 6 | **Shipped** (patch notes; the first 100k of 146k chars were read) |
| 8 | news/24298591 | Midnight What's Next Panel Recap | September 12 (BlizzCon 2026) | Announced for "Eclipse" |

Columns:
- **State:** S = shipped, A = announced or planned, E = announced for Eclipse. Sources in brackets.
- **MVP:** in our Phase 1 slice.
- **Later:** the later phase in the README that covers it.
- **Engine change:** does the 3.3.5 client or server need new native code? Answered from [feasibility.md](feasibility.md).
- **Blocker:** what stops it today.

## Decorating

| Feature | State | MVP | Later | Engine change | Blocker |
|---|---|---|---|---|---|
| Basic mode: slide on ground, climb objects, 15° snap rotation | A[1], S[4] | ✔ (place, move XY+Z, 15° snap) | | Client: ghost model + raycast exist (`Spell_C.cpp:8494-8512`, `Map.cpp:5071`). Needs an editor extension. | Decor collision is unverified on prepared retail M2s |
| Decor collides with decor, floors, walls | A[1] | | Advanced editing | Client: GO world objects collide (`GameObject_C.cpp:3199-3238`) | Same as above |
| Auto-align to surface (rugs to floor, paintings to wall) | A[1] | Surface hint from name only | Advanced editing | Client raycast normal | `HouseDecor.Type` not decoded yet (see existing-assets) |
| Small decor rides along on larger decor | A[1] | | Advanced editing | Server parent link plus a move cascade | Design only |
| Grid snap | A[1] | | Advanced editing | Client only | None |
| Advanced mode: no collision, 3-axis gizmo, free rotation | A[1], S[4] | | Advanced editing | Full quaternion already on the wire (`GameObject.cpp:2243-2271`, client `GameObject_C.cpp:1891`, `:2621`) | Gizmo UI (wxl-housing uses ImGuizmo, but under a different runtime) |
| Scale within limits | A[1], S[3] | | Advanced editing | Per-object `OBJECT_FIELD_SCALE_X` (`Object.h:129-130`), client applies `GetScale()` (`GameObject_C.cpp:2622`) | Server collision ignores it (no vmaps for decor) |
| Dyes / dyeable decor | A[1], S[6] | | Rooms and finishes | Yes: retail dye masks aren't in the 3.3.5 shader set | Unknown whether `PrepareModel` keeps dye data |
| Copy/paste, multi-select pivot | E[8] | | Advanced editing | Client UI plus a batch opcode | None |
| Undo/redo | (not in sources) | | Advanced editing | Server revision table | None |

## House, rooms, interiors

| Feature | State | MVP | Later | Engine change | Blocker |
|---|---|---|---|---|---|
| One house per account per faction, warband-shared | A[3] | One per account | Neighborhoods | Server: account-keyed tables | None |
| Interior size independent of exterior | A[1] | ✔ (one instanced interior) | | Server instance map (`MapInstanced.cpp:110-178`) | Instance binding is per player or group, so visitors need a hook (feasibility §3) |
| Wallpaper, floor, ceiling finishes | A[1] | | Rooms and finishes | Room WMO material swap | WMO roots not shipped. Retail room WMOs need conversion (gap-fill) |
| Partition walls | A[1] | | Rooms and finishes | Decor WMO placement | 65 WMO decor rows have no root |
| Room graph, movable entry room, Artisanal rooms | S[6] | | Rooms and finishes | Server room table. Client: assembling WMO rooms | Room kit conversion |
| Exterior kits (barber-shop style), large exteriors | A[3], S[6] | | Neighborhoods | Exterior WMO swap | `HouseExteriorWmoData` present, roots absent |
| House level (up to 12), XP | S[4], S[6] | | Neighborhoods | Server only | None |
| Decor budget / limits | S[5] | ✔ (simple count cap) | | Server only | None |
| Storage limit, delete extra decor | S[5] | | Polish | Server only | None |

## Neighborhoods and social

| Feature | State | MVP | Later | Engine change | Blocker |
|---|---|---|---|---|---|
| Public neighborhoods (~50 plots, server-created) | A[2], S[4] | | Neighborhoods | Outdoor map with plots | No neighborhood map/ADT |
| Guild / Charter neighborhoods, membership UI | A[2], S[4] | | Neighborhoods | Server tables. `mod-guildhouse` exists but phases by `guildId+10` as a bitmask (feasibility §3) | None |
| Plot and house permissions (friends, guild, neighbors, everyone); kick on revoke | A[2] | Owner-only edit; owner plus group can visit | Neighborhoods | Server only | Visitor instance hook |
| Relinquish house, regret timer, pack-up on eviction | A[2] | | Neighborhoods | Server only | None |
| Endeavors, Favor, currency, vendors | A[2], S[6] | | Neighborhoods / Polish | Server plus quest data | Content |
| Blueprints: save, share, import preview, 50+10 slots, export permission | S[6], S[7] | | Neighborhoods ("blueprints") | Server: revision snapshot. Text code over chat | None |
| Reset house, interior or exterior to storage | S[6] | | Advanced editing | Server only | None |
| Pet beds (100 indoor / 25 outdoor), roaming pets | S[6] | | Polish | Server summons | Pathing inside instanced interiors |
| Visitor codes, Showcase, Guest book | A[5], E[8] | | Neighborhoods | Snapshot instance | None |
| Mannequins | E[8] | | Polish | Client: dressed creature display | None |

## Acquisition and UI

| Feature | State | MVP | Later | Engine change | Blocker |
|---|---|---|---|---|---|
| Decor catalog (dashboard, sort, filter by type, style, culture) | S[4] | Plain Lua list | Polish | Lua bridge exists (`RegisterLuaFunction`, `Extensions/CharacterSwap/src/CharacterSwap.cpp:432-437`) | Retail `Blizzard_Housing*` Lua is unusable on 3.3.5 |
| Decor sources: vendors, quests, achievements, professions, AH | A[3], S[4] | GM grant / starting quantity | Neighborhoods | Server: item→decor mapping (`HouseDecor.ItemID`) | Retail item IDs don't exist on 3.3.5 |
| Decor crafting | S[4] | | Polish | Server recipes | Content |
| Buy extra copies in-house | E[8] | | Polish | Server only | None |
| Housing tutorial | S[4] | | Polish | Lua | None |
