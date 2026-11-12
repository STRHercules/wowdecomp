# Esteria — Paths of Fate: Zomboid-Inspired Starting Traits

**Implementation specification for Claude**  
**Target:** `STRHercules/EsteriaWoW` (`main`) + `STRHercules/wowdecomp` (`Esteria`)  
**Scope:** Extend existing **FATE** character-creation background selection into a multi-trait budget system. Preserve existing six **Paths of Fate** in-game card progression.  
**Status:** Proposed feature design; no implementation has been made by this document.  
**Version:** v1.0 — 2026-11-11

> **Claude: implement against the repository as it currently exists.** The paths, line numbers, and rules below were verified against the repositories on 2026-11-11, but the branches may move. Re-open current code, inspect adjacent hooks/tests, and adapt to reality; never overwrite newer work. Make actual code changes, tests, migrations, client assets, and documentation. Do not merely draft a plan. Only mark a trait selectable when both its benefit **and** its drawback are implemented and tested. Preserve all existing progression, account, character-creation, housing, starting-zone, race, and appearance features.

## Executive summary

Add **Fate Trait Points (FTP)** as a separate character-creation currency inspired by Project Zomboid. Players can choose multiple **Merits** (positive-only bonuses, spending points), **Flaws** (negative-only, grant points), and **Quirks** (paired advantage + drawback, cost zero, limited slots). The original eight paired background traits remain valid with their numeric IDs unchanged; Unmarked remains 0 and means an empty selection. Introduce 110 additional Quirks, 20 Merits, and 24 Flaws: **163 catalog records including Unmarked**, 162 selectable traits. All 54 existing Paths of Fate progression cards continue to exist independently, and their current tier gates / earned points remain unchanged.

**Why this model:** it preserves the user's original dual-edged backgrounds while providing the real Zomboid dynamic: you can voluntarily accept meaningful disadvantages to afford more powerful advantages. Do **not** credit the point budget for a Quirk's built-in downside; that is already paid for by its paired benefit.

## 1. Repository-grounded integration points

Existing files observed (inspect their latest versions again):

| Repository | Existing path | Responsibility / required change |
|---|---|---|
| Server | `src/server/shared/PerkTraitWire.h` | v1 currently serializes **one uint8 trait**, `[version=1, traitId] + 'PFT1'` footer on `CMSG_CHAR_CREATE`. Introduce v2 multi-select footer without breaking v1. |
| Server | `src/server/shared/PerkRules.h` | 54 cards across six paths; level 10, 30, 50, 70 gates; first of 15 progression points at level 10, every five levels thereafter through 80. **Do not spend these points on starting traits.** |
| Server | `src/server/shared/PerkWire.h` | Runtime perk snapshot sends a single `trait(u8)`. Version carefully; do not break old client decoding. |
| Server | `src/server/game/Handlers/CharacterHandler.cpp` | Strips last trait footer first, then start-zone footer, then other appearance footers; saves one selected trait in character transaction. Extend to v2 vector, preserve footer removal order and atomic creation. |
| Server | `src/server/game/Handlers/PerkHandler.cpp` | Loads `_esteriaTrait` and ranks, owns progress snapshots, purchases and resets. Add separate starting-trait state and explicit trait respec path, not the level-card reset. |
| Server | `src/server/scripts/World/esteria_perk_effects.cpp` | Existing script-side trait/perk effects. Replace direct single-trait checks with registry-aware queries; retain existing behavior and idempotent event hooks. |
| Server | `src/server/game/Handlers/PerkEffects.cpp` + `src/server/shared/PerkFx.h` | Core call-sites for price, speed, casting, damage, etc. Reuse and extend carefully; avoid repeated multiplicative passes. |
| Server | `src/server/game/Entities/Player/Player.h` | Stores `_esteriaTrait`; add selection set and safe public query methods. |
| Server | `src/server/database/Database/Implementation/CharacterDatabase.h/.cpp` | Add prepared statements for selection rows, audit, optional usage caps and snapshots. |
| Server | `data/sql/updates/pending_db_characters/rev_20261101020000000_esteria_perk_profile.sql` | Current `character_esteria_perk_profile(guid, trait, respecs)`. Add an idempotent new migration; **do not destructively rewrite the original migration or discard old profile data**. |
| Client | `Tools/RetailAssets/glue/fate_page.lua` | Currently fixed nine rows on FATE step. Replace with searchable, scrollable category + budget UI, selection cart and confirmation. Preserve body/appearance/start-zone page flow. |
| Client | `WoW/Common/ComponentCore/EsteriaAppearance.cpp/.h` | Implements `SetCreateTrait` and writes current one-id footer. Add native multi-selection bridge and PFT2 writer. |
| Client | `Engine/Source/Retail/PerkTraitWire.h` | Must stay byte-for-byte/protocol-identical with server counterpart. |
| Client | `Addons/EsteriaPerks/Data.lua` | Displays original trait info and in-game Paths of Fate. Generate/extend trait metadata; preserve the 54 card definitions. |
| Tests | `src/test/server/game/Miscellaneous/PerkRulesTest.cpp` + `Tools/RetailAssets/glue/test_fate_page.py` | Expand coverage for creator, serialization, point budget, trait rules and backward compatibility. |

**Current observed constraint:** `PerkWire.h` runtime snapshot `Proto=1` sends `trait(u8)`; `PerkTraitWire.h` creation `Version=1` and `'PFT1'` are **different protocols**. Version each surface independently, and never append unexplained bytes to an existing v1 packet.

## 2. User-facing rules (final design)

| Rule | Value |
|---|---|
| Base Fate Trait Points on new character | **4** |
| Points granted by Flaws | **+2 or +3 per trait** as listed |
| Maximum points obtained from Flaws | **8** across the character |
| Maximum total points available | **12** (4 base + at most 8 flaws) |
| Merits | Spend **2–4 points** each; maximum **3** Merits |
| Flaws | Give points, maximum **3** Flaws |
| Quirks | Paired advantage/drawback; cost **0**, maximum **2** Quirks |
| Total chosen traits | Maximum **8** (2 Quirks + 3 Merits + 3 Flaws) |
| Unmarked | Empty set = no selected traits, all 4 base points unused |
| Leftover FTP | Allowed; does not convert to XP, gold, progression perk points or later power |
| Availability | Starting choices normally selected at character creation and persist by character GUID |
| Progression cards | Existing 54 cards and their separate earned points, gates, respec costs and ranks **unchanged** |
| PvP | No new combat-changing effects in arenas, BGs, duels or player-vs-player combat. Define PvP safety at the final target/event boundary, not just map type. |
| Class/feature locking | Disabled or impossible-to-receive downside = selection disallowed, not a free bonus |
| Event semantics | All percentage modifiers are relative unless explicitly written **percentage points** |
| Implementation policy | Quirk appears selectable only when both halves are live; merits/flaws only when their entire effect is live |

**Budget calculation (pure and testable):**

```text
base_points   = 4
flaw_points   = sum(selected[flaw].grant)
merit_spend   = sum(selected[merit].cost)
quirk_spend   = 0
valid          = flaw_points <= 8
              && merit_spend <= base_points + flaw_points
              && merit_count <= 3 && flaw_count <= 3 && quirk_count <= 2
              && total_count <= 8
              && all IDs unique and unlocked and eligible and not conflicting
remaining     = base_points + flaw_points - merit_spend
```

**IMPORTANT:** A Flaw is never a refund after character creation, cannot be toggled off by swapping class/spec, and must remain relevant to the character's possible gameplay. For custom/classless builds, re-validate on class/kit/power-type changes and reject changes that would render a selected drawback inert, or use an explicitly documented neutral fallback penalty. Do not silently hand out free points. A character with no traits still has 4 *unspent* points, which is fine.

**Client flow:** FATE step after character body/appearance, before starting-zone choice if configured. Show `Starting Points 4`, `+ Flaws`, `− Merits`, `Remaining`; tabs for All / Merits / Flaws / Quirks and categories, search, select/unselect and details. Quirks show green benefit + red drawback. Flaws show red drawback + `+N`, Merits show green benefit + `−N`. Tooltips show class restrictions, gameplay restrictions and conflicts. Allow a temporarily negative budget while editing, but disable **Create** and **Next** until valid. Require final confirmation summarizing all chosen traits, net point math and permanence. Color is supplemental, not sole semantic signal; use text and icons. Limit creator Lua locals and UI frame density; paginate/virtualize instead of creating 163 full rows with permanent expensive updates. Preserve new-character reset on returning to the creator, retries after a name collision, Back behavior and existing appearance/starting-zone footer handling.

## 3. Immutable identifiers and catalog flags

- **IDs 0–8 are reserved exactly as they already exist. Do not renumber.** ID 0 `Unmarked` is a sentinel, not stored as a row.
- **IDs 9–118:** 110 new dual-edged Quirks.
- **IDs 119–138:** 20 Merits.
- **IDs 139–162:** 24 Flaws.
- IDs use `uint16` in PFT2, databases and runtime trait snapshots; future IDs only append. Keep legacy compatibility mapping from 1–8.
- Trait definitions must have stable ID, localized display name, category, kind, benefit text, drawback text, cost/grant, feature flags, eligibility predicates, conflict groups, explicit effect handlers and `implemented` status. The companion JSON sets `implementation_status` to `verify_existing` for IDs 0–8 and `not_implemented` for new entries; Claude must populate concrete effect handlers and switch availability only after tests pass.
- `Esteria.Fate.HousingTraits.Enable=false`, `Esteria.Fate.QuestCategoryTraits.Enable=false`, `Esteria.Fate.WeatherTraits.Enable=false` until relevant APIs are verified.
- Quirk `First Strike` from concept list is **renamed Opening Strike** here, since existing level-10 progression card ID 1 already uses `First Strike`; `Apprentice's Luck` quirk is **renamed Early Apprentice** since existing progression card ID 33 already uses that name. Mechanics are unchanged.
- This catalog is the proposed design; a companion JSON is supplied for machine ingestion. Never infer a mechanic just from a display string; define and test server handlers.

## 4. Full starting-trait catalog

**Point column:** `0` = paired Quirk; `−N` = Merit spends N FTP; `+N` = Flaw grants N FTP. All values are initial balance proposals. The table is exhaustive.

### Unmarked (1)
| ID | Trait | FTP | Benefit | Drawback |
|---:|---|---:|---|---|
| 0 | Unmarked | 0 | None | None |

### Legacy Backgrounds (8)
| ID | Trait | FTP | Benefit | Drawback |
|---:|---|---:|---|---|
| 1 | Wanderer | 0 | +5% outdoor movement speed while on foot and out of combat | −10% quest reputation |
| 2 | Glass Cannon | 0 | +2% damage against non-player enemies | −4% maximum health in PvE |
| 3 | Ironbound | 0 | +4% maximum health in PvE | −2% damage against non-player enemies |
| 4 | Artisan Soul | 0 | +10% profession skill-gain chance | −10% first-discovery exploration rewards |
| 5 | Curious Mind | 0 | +10% first-discovery exploration rewards | −10% profession skill-gain chance |
| 6 | Folk Hero | 0 | +10% quest reputation | −10% gold looted from humanoids |
| 7 | Outlaw Heart | 0 | +10% gold looted from humanoids | −10% quest reputation |
| 8 | Frugal Survivor | 0 | +15% duration of food, drink, and other non-combat buffs | −10% healing from your own potions |

### Combat & Fighting Styles (10)
| ID | Trait | FTP | Benefit | Drawback |
|---:|---|---:|---|---|
| 9 | Opening Strike | 0 | +4% damage against PvE enemies above 90% health | −3% damage against enemies below 30% health |
| 10 | Executioner | 0 | +5% damage against enemies below 25% health | −3% damage against enemies above 75% health |
| 11 | Relentless | 0 | +3% damage after fighting the same enemy for 8 seconds | −3% damage during the first 5 seconds |
| 12 | Ambusher | 0 | First damaging attack after 10 seconds out of combat deals +6% damage | −3% damage for the next 6 seconds |
| 13 | Beastbreaker | 0 | +5% damage against beasts | −3% damage against humanoids |
| 14 | Gravebane | 0 | +5% damage against undead | −3% damage against elementals |
| 15 | Elemental Grudge | 0 | +5% damage against elementals | −3% damage against undead |
| 16 | Duelist | 0 | +3% damage when fighting a single enemy | −4% damage when fighting three or more |
| 17 | Crowd Fighter | 0 | +4% damage when fighting three or more enemies | −3% damage when fighting only one |
| 18 | Heavy Handed | 0 | +5% damage from basic weapon attacks | −3% damage from special weapon abilities |

### Defensive & Physical Traits (10)
| ID | Trait | FTP | Benefit | Drawback |
|---:|---|---:|---|---|
| 19 | Thick Hide | 0 | −3% physical damage received from NPCs | +3% magical damage received from NPCs |
| 20 | Spellscarred | 0 | −3% magical damage received from NPCs | +3% physical damage received from NPCs |
| 21 | Death Defiant | 0 | −5% damage received below 30% health | +3% damage received above 70% health |
| 22 | Soft Landing | 0 | −30% fall damage | +10% damage from other environmental hazards |
| 23 | Stubborn | 0 | −15% duration of ordinary enemy movement slows | +15% duration of ordinary enemy roots |
| 24 | Quickstep | 0 | +3% movement speed during PvE combat | −3% maximum health in PvE |
| 25 | Stonewall | 0 | −4% damage received after standing still for 3 seconds | +4% damage received while moving |
| 26 | Nimble | 0 | −4% damage from non-boss area attacks | +4% damage from direct physical attacks |
| 27 | Second Skin | 0 | +6% armor from equipped gear | −4% movement speed during combat |
| 28 | Unbreakable Spirit | 0 | −10% duration of ordinary enemy fear effects | +10% duration of ordinary enemy stuns |

### Magical & Spellcasting Traits (10)
| ID | Trait | FTP | Benefit | Drawback |
|---:|---|---:|---|---|
| 29 | Mana Miser | 0 | −5% mana cost of damaging spells | −3% spell damage against NPCs |
| 30 | Overcharged | 0 | +3% spell damage against NPCs | +5% mana cost of damaging spells |
| 31 | Deep Focus | 0 | +4% spell damage after standing still for 3 seconds | −3% spell damage while moving |
| 32 | Restless Spark | 0 | +4% instant-spell damage shortly after moving | −3% spell damage while stationary |
| 33 | Ritualist | 0 | +4% damage from spells with cast times above 1.5 seconds | +5% casting time for those spells |
| 34 | Quick Study | 0 | −4% casting time for non-instant damaging spells | −5% damage from those spells |
| 35 | Hex-Touched | 0 | +5% periodic spell damage (DoTs) | −4% direct spell damage |
| 36 | Flashfire | 0 | +4% direct spell damage | −5% periodic spell damage |
| 37 | Mana Hoarder | 0 | +6% maximum mana | −5% passive mana regeneration during combat |
| 38 | Volatile Focus | 0 | +2 percentage points spell critical chance against NPCs | −4% maximum mana |

### Companions, Parties & Teamwork (10)
| ID | Trait | FTP | Benefit | Drawback |
|---:|---|---:|---|---|
| 39 | Lone Wolf | 0 | +3% PvE damage while not grouped | −3% PvE damage while grouped |
| 40 | Pack Mentality | 0 | +3% PvE damage while grouped | −3% PvE damage when solo |
| 41 | Guardian Spirit | 0 | −3% PvE damage received while grouped | +3% PvE damage received when solo |
| 42 | Solitary Defender | 0 | −3% PvE damage received when solo | +3% PvE damage received while grouped |
| 43 | Beast Bond | 0 | +5% maximum health for your combat pet | −3% damage dealt by you while the pet is active |
| 44 | Feral Pact | 0 | +5% combat pet damage | −5% maximum pet health |
| 45 | Protective Handler | 0 | −5% damage received by your pet | −3% damage dealt by your pet |
| 46 | Wild Handler | 0 | +5% combat pet movement speed | −4% your movement speed while your pet is active |
| 47 | Battle Medic | 0 | +5% healing on party members below 40% health | −4% healing you receive from your own spells |
| 48 | Self-Reliant | 0 | +5% healing from your own healing spells | −4% healing you provide to other players |

### Exploration, Traveling & Movement (10)
| ID | Trait | FTP | Benefit | Drawback |
|---:|---|---:|---|---|
| 49 | Night Owl | 0 | +4% out-of-combat foot speed at night | −4% during daytime |
| 50 | Early Riser | 0 | +4% out-of-combat foot speed during daytime | −4% at night |
| 51 | Cave Dweller | 0 | +5% out-of-combat foot speed in caves | −4% in outdoor wilderness |
| 52 | Riverborn | 0 | +15% swimming speed | −4% out-of-combat running speed |
| 53 | Mountain Goat | 0 | −25% fall damage | −10% swimming speed |
| 54 | Longstride | 0 | +6% foot speed after 15 seconds of continuous out-of-combat travel | −3% foot speed during the first 15 seconds |
| 55 | Underbrush | 0 | −10% detection radius of ordinary outdoor hostile NPCs while on foot | −4% out-of-combat foot speed |
| 56 | Hearthbound | 0 | −20% Hearthstone cooldown | +10% flightmaster travel costs |
| 57 | Hitchhiker | 0 | −10% flightmaster travel costs | +20% Hearthstone cooldown |
| 58 | Stormborn | 0 | +5% outdoor foot speed during rain or snow | −3% during clear weather |

### Crafting, Gathering & Professions (10)
| ID | Trait | FTP | Benefit | Drawback |
|---:|---|---:|---|---|
| 59 | Green Thumb | 0 | +10% chance to gather bonus herbs | +10% herb-gathering time |
| 60 | Ore Whisperer | 0 | +10% chance to gather bonus ore | +10% mining time |
| 61 | Leatherwise | 0 | +10% chance for bonus leather from skinning | +10% skinning time |
| 62 | Threadpicker | 0 | +8% cloth drops from humanoids | −5% gold looted from humanoids |
| 63 | Hasty Hands | 0 | −15% item-crafting time | −10% profession skill-gain chance |
| 64 | Careful Hands | 0 | +10% profession skill-gain chance while crafting | −5% quest gold rewards |
| 65 | Early Apprentice | 0 | +15% profession skill-gain chance below skill 150 | −10% skill-gain chance at skill 150+ |
| 66 | Late Bloomer | 0 | +10% profession skill-gain chance at skill 150+ | −15% skill-gain chance below 150 |
| 67 | Tinker's Mind | 0 | +15% Engineering skill-gain chance | +10% equipment repair costs |
| 68 | Resourceful Salvager | 0 | +8% chance of extra common materials from milling, prospecting, or disenchanting | −5% general profession skill-gain chance |

### Gold, Loot & Commerce (10)
| ID | Trait | FTP | Benefit | Drawback |
|---:|---|---:|---|---|
| 69 | Copper Counter | 0 | −5% NPC vendor purchase prices | +10% equipment repair costs |
| 70 | Scrap Dealer | 0 | +15% vendor sale value of gray items | −5% quest gold rewards |
| 71 | Chest Rat | 0 | +15% gold found inside treasure chests | −8% gold looted from humanoids |
| 72 | Bounty Earner | 0 | +10% gold rewards from kill quests | −10% gold rewards from delivery quests |
| 73 | Burdened Purse | 0 | +10% gold looted from humanoids | +12% equipment repair costs |
| 74 | Repair Savant | 0 | −12% equipment repair costs | +8% NPC vendor purchase prices |
| 75 | Market Broker | 0 | −10% Auction House listing deposits | +10% Auction House sale commission |
| 76 | Hoarder | 0 | +10% money found inside lootable containers | −4% foot speed when inventory is at least 80% full |
| 77 | Junk Magnet | 0 | +12% chance of an extra gray item from eligible humanoid kills | −5% gold looted from humanoids |
| 78 | Gilded Tongue | 0 | +8% vendor sale prices for ordinary items | −8% quest reputation gains |

### Survival, Food, Medicine & Rest (10)
| ID | Trait | FTP | Benefit | Drawback |
|---:|---|---:|---|---|
| 79 | Iron Stomach | 0 | +20% health restored by eating food | −20% mana restored by drinking |
| 80 | Thirsty Mind | 0 | +20% mana restored by drinking | −20% health restored by eating |
| 81 | Potion Affinity | 0 | +12% healing from your own health potions | −10% healing received from bandages |
| 82 | Field Dresser | 0 | +15% healing received from bandages | −10% healing from your own health potions |
| 83 | Well Fed | 0 | +20% duration of food-provided stat buffs | −10% duration of elixir buffs |
| 84 | Alchemical Blood | 0 | +15% duration of elixir buffs | −10% duration of food-provided stat buffs |
| 85 | Poison-Bred | 0 | −15% poison-effect duration from ordinary enemies | +15% disease-effect duration |
| 86 | Plagueproof | 0 | −15% disease-effect duration from ordinary enemies | +15% poison-effect duration |
| 87 | Restless Sleeper | 0 | +20% rested experience accumulation | −5% monster-kill experience while not rested |
| 88 | Tipsy Brawler | 0 | +5 percentage points dodge chance against NPCs while intoxicated | −5 percentage points hit chance while intoxicated |

### Reputation, Experience & Background (10)
| ID | Trait | FTP | Benefit | Drawback |
|---:|---|---:|---|---|
| 89 | Story Seeker | 0 | +5% quest experience | −5% monster-kill experience |
| 90 | Monster Hunter | 0 | +5% monster-kill experience | −5% quest experience |
| 91 | Local Legend | 0 | +10% reputation gains with your racial home-city faction | −8% reputation gains with other capital-city factions |
| 92 | Foreign Soul | 0 | +10% reputation gains with other capital-city factions | −8% reputation gains with your home-city faction |
| 93 | Outsider's Friend | 0 | +10% reputation gains with neutral factions | −8% reputation gains with your home-city faction |
| 94 | Guild Loyalist | 0 | +5% experience from eligible kills while grouped with guild members | −5% experience from kills outside guild groups |
| 95 | Errand Runner | 0 | +10% experience from delivery quests | −8% experience from kill quests |
| 96 | Bounty Pursuer | 0 | +10% experience from kill quests | −8% experience from delivery quests |
| 97 | Dungeon Disciple | 0 | +8% reputation from dungeon quests | −8% reputation from outdoor quests |
| 98 | Open Road | 0 | +8% reputation from outdoor quests | −8% reputation from dungeon quests |

### Strange, Cursed & Unusual (10)
| ID | Trait | FTP | Benefit | Drawback |
|---:|---|---:|---|---|
| 99 | Bloodscent | 0 | +5% PvE damage while below 35% health | −5% healing received while below 35% health |
| 100 | Grave Feeder | 0 | Killing an experience-eligible enemy restores 1.5% maximum health (10-second internal cooldown) | −20% natural out-of-combat health regeneration |
| 101 | Moon-Cursed | 0 | +5% PvE damage at night | −5% PvE damage during daytime |
| 102 | Fearless Fool | 0 | +4% damage against elite NPCs | +5% damage received from elite NPCs |
| 103 | Run Rabbit Run | 0 | Gain +8% foot speed for 5 seconds when falling below 25% health (60-second internal cooldown) | −4% maximum health in PvE |
| 104 | High Roller | 0 | +10% quest gold if completing without dying since accepting | −10% quest gold if you died during that quest |
| 105 | Fractured Luck | 0 | +10% critical-strike bonus damage against NPCs | −2 percentage points critical-strike chance against NPCs |
| 106 | Haunted | 0 | −5% damage received from undead enemies | +5% damage received from living enemies |
| 107 | Frenzied | 0 | +4% attack speed for 8 seconds after killing an experience-eligible NPC | −3% attack speed while bonus is inactive |
| 108 | Wild Card | 0 | Every 2 minutes, next PvE combat randomly grants +5% damage for 10 seconds | Or applies −5% damage for 10 seconds |

### Esteria Housing & Community (10)
| ID | Trait | FTP | Benefit | Drawback |
|---:|---|---:|---|---|
| 109 | Homebody | 0 | +20% rested XP accumulation inside your own home | −10% rested XP accumulation in ordinary inns |
| 110 | Wandering Tenant | 0 | −20% personal housing teleport cooldown | +20% Hearthstone cooldown |
| 111 | Shopkeeper | 0 | −10% commission on sales through your offline player shop | +10% Auction House sales commission |
| 112 | Market Merchant | 0 | −10% Auction House sales commission | +10% player-shop sales commission |
| 113 | Interior Artisan | 0 | +15% skill-gain chance when crafting housing decorations | −8% skill-gain chance for other crafting recipes |
| 114 | Neighborhood Regular | 0 | +8% out-of-combat foot speed in housing neighborhoods | −3% out-of-combat foot speed in capital cities |
| 115 | Tavernkeeper | 0 | +8% proceeds from services provided by your player-owned tavern | −8% proceeds from ordinary player-shop sales |
| 116 | Antique Dealer | 0 | +10% sales proceeds from housing decorations sold in your shop | −5% proceeds from other shop merchandise |
| 117 | Guild Steward | 0 | −10% required contributions toward guild-hall upkeep | +10% upkeep costs for your personal home |
| 118 | Generous Host | 0 | +15% duration of food buffs you serve to guests at your property | −10% duration of food buffs you consume outside your property |

### Combat (7)
| ID | Trait | FTP | Benefit | Drawback |
|---:|---|---:|---|---|
| 119 | Vigorous | −4 | +5% maximum health versus NPCs | None |
| 120 | Keen Edge | −4 | +3% damage dealt to non-player enemies | None |
| 139 | Frail | +3 | None | −5% maximum health versus NPCs |
| 140 | Pacifist's Hesitation | +3 | None | −3% damage dealt to NPCs |
| 141 | Unsteady Hands | +2 | None | −2 percentage points critical chance against NPCs |
| 147 | Thin Blood | +3 | None | +6% physical damage received from NPCs |
| 148 | Arcane Sensitivity | +3 | None | +6% magical damage received from NPCs |

### Magic (3)
| ID | Trait | FTP | Benefit | Drawback |
|---:|---|---:|---|---|
| 121 | Calm Mind | −2 | +5% maximum mana | None |
| 161 | Unfocused Mage | +2 | None | +5% mana cost of damaging spells |
| 162 | Slow Incantation | +2 | None | +4% cast time on damaging spells with cast times over 1.5 sec |

### Survival (7)
| ID | Trait | FTP | Benefit | Drawback |
|---:|---|---:|---|---|
| 122 | Natural Healer | −3 | +5% healing from your own spells cast on yourself | None |
| 130 | Deep Sleeper | −2 | +10% rested XP accumulation | None |
| 146 | Picky Eater | +2 | None | −15% food buff duration |
| 150 | Slow Recovery | +2 | None | −15% natural out-of-combat health regeneration |
| 151 | Butterfingers | +2 | None | +15% equipment durability loss on PvE death |
| 153 | Restless Mind | +2 | None | −15% rested XP accumulation |
| 155 | Shaky Potioner | +2 | None | −10% healing from your own potions |

### Progression (3)
| ID | Trait | FTP | Benefit | Drawback |
|---:|---|---:|---|---|
| 123 | Experienced Adventurer | −3 | +4% quest experience rewards | None |
| 124 | Experienced Hunter | −3 | +4% eligible monster-kill experience | None |
| 137 | Dungeon Regular | −2 | +4% experience from eligible dungeon quests | None |

### Professions (5)
| ID | Trait | FTP | Benefit | Drawback |
|---:|---|---:|---|---|
| 125 | Swift Gatherer | −2 | −8% gathering interaction time | None |
| 126 | Fast Learner | −3 | +8% relative profession skill-gain chance | None |
| 127 | Lucky Scrounger | −3 | +4% chance of extra common gather material, max 4/day | None |
| 142 | Slow Learner | +2 | None | −7% relative profession skill-gain chance |
| 154 | Clumsy Gatherer | +2 | None | +10% gathering interaction time |

### Economy (6)
| ID | Trait | FTP | Benefit | Drawback |
|---:|---|---:|---|---|
| 128 | Gold Nose | −2 | +5% eligible humanoid gold loot, capped at 10 gold bonus/day | None |
| 135 | Mercantile | −2 | +4% sale prices for ordinary vendorable goods | None |
| 136 | Durable Tools | −2 | −6% repair costs | None |
| 144 | Poor Negotiator | +2 | None | +8% NPC vendor purchase prices |
| 152 | Wasteful | +2 | None | +10% equipment repair cost |
| 159 | Shy Vendor | +2 | None | −8% vendor sale price for ordinary goods |

### Reputation (3)
| ID | Trait | FTP | Benefit | Drawback |
|---:|---|---:|---|---|
| 129 | Friend of Factions | −3 | +6% reputation from quests | None |
| 143 | Unpopular | +2 | None | −8% reputation from quests |
| 158 | Sour Reputation | +2 | None | −5% reputation from neutral factions |

### Exploration (7)
| ID | Trait | FTP | Benefit | Drawback |
|---:|---|---:|---|---|
| 131 | Lightfoot | −2 | −8% ordinary fall damage | None |
| 132 | River Runner | −2 | +10% swim speed | None |
| 138 | Hearth Attuned | −2 | −10% Hearthstone cooldown | None |
| 145 | Clumsy Climber | +2 | None | +20% ordinary fall damage |
| 149 | Sluggish | +3 | None | −3% out-of-combat foot speed |
| 156 | Poor Sense of Direction | +2 | None | +10% taxi flight costs |
| 157 | Short Breath | +2 | None | −12% swim speed |

### Companions (3)
| ID | Trait | FTP | Benefit | Drawback |
|---:|---|---:|---|---|
| 133 | Pet Trainer | −3 | +4% combat pet damage to NPCs | None |
| 134 | Party Medic | −3 | +4% healing on other group members | None |
| 160 | Weak Bond | +2 | None | −8% max combat pet health |

### Implementation availability and conditional semantics

**The catalog is an implementation target, not permission to display fake functionality.** Flags control affected categories or individual traits. No selectable trait can be marked implemented until each benefit, each drawback, all eligibility checks, relevant PvP exclusions, and the character save/reload test exist. A feature-gated trait stays in metadata and UI as `Coming Later` but is disabled and cannot enter a saved build. If the matching custom system is already present, wire it, test it and enable it.

- All 10 housing/community quirks need actual housing/shops/teleport/decoration/inn/service hooks. Examples: `Guild Steward` needs real maintenance costs; if guild halls have no upkeep concept, that card remains disabled, **not** an invented currency.
- `Stormborn` requires an actual per-map weather state and transition callback. No guessed weather from sky texture alone.
- `Errand Runner`, `Bounty Pursuer`, `Bounty Earner`, `Dungeon Disciple`, `Open Road`, and Merit `Dungeon Regular` need a verified, maintained quest-category registry. Do not infer kill/delivery type from objective presence alone; some are mixed.
- `High Roller` needs quest acceptance/death/completion identity and per-character saved state, including logging off/relogging; no XP farming from repeated repeatables and no accepting a quest again to exploit the reward boost. Its bonus is **gold**, not an epic drop increase.
- `Hasty Hands` and `Ritualist` touch cast/interact timing. If a recipe or cast is protected by anti-cheat/profession cooldown logic, preserve that logic; do not speed restricted crafting, cooldowns, channel durations or scripted casts.
- `Underbrush` must modify only ordinary NPC aggro/detection, never boss/instance pull mechanics, stealth detection, boss scripts, guard anti-exploit rules, or player visibility.
- `Wild Card` uses cryptographically unnecessary but server-owned deterministic PRNG with reproducible test injection; 50/50 outcome, only once per 120 seconds while eligible, modifier expires after 10 seconds, and no trigger on noncombat/duels.
- `Fractured Luck`: critical-strike bonus damage refers to the bonus portion, **not** total critical damage. Clamp probability after ±percentage-point changes; avoid double stacking with universal crit damage handling.
- `Restless Sleeper`/other rested XP modifiers change acquisition rate only; never raise the actual rested XP cap.
- Never let a Flaw grant FTP unless it demonstrably affects at least one active character subsystem in ordinary gameplay.

## 5. Trait rule families, mutual exclusion and stacking caps

The following groups are a **conservative initial mutual-exclusion table**, not merely UI hints. Claude should inspect all combinations before shipping, keep explicit named ID sets in the registry, and tighten/relax with balance testing. A selection with more than one member of a group is rejected server-side. This blocks opposite traits cancelling their disadvantages.

1. `Wanderer`, `Folk Hero`, `Outlaw Heart`, `Gilded Tongue`, `Unpopular`, `Friend of Factions`.
2. `Glass Cannon`, `Ironbound`, `Vigorous`, `Frail`, `Keen Edge`, `Pacifist's Hesitation`.
3. `Artisan Soul`, `Curious Mind`, `Fast Learner`, `Slow Learner`, `Careful Hands`.
4. `Night Owl`, `Early Riser`, `Moon-Cursed`.
5. `Beastbreaker`, `Gravebane`, `Elemental Grudge`.
6. `Duelist`, `Crowd Fighter`.
7. `Mana Miser`, `Overcharged`, `Unfocused Mage`.
8. `Hex-Touched`, `Flashfire`.
9. `Deep Focus`, `Restless Spark`.
10. `Lone Wolf`, `Pack Mentality`, `Guardian Spirit`, `Solitary Defender`.
11. `Green Thumb`, `Ore Whisperer`, `Leatherwise`.
12. `Hearthbound`, `Hitchhiker`, `Hearth Attuned`, `Wandering Tenant`.
13. `Copper Counter`, `Repair Savant`, `Poor Negotiator`, `Wasteful`, `Durable Tools`.
14. `Story Seeker`, `Monster Hunter`, `Experienced Adventurer`, `Experienced Hunter`.
15. `Errand Runner`, `Bounty Pursuer`, `Bounty Earner`.
16. `Dungeon Disciple`, `Open Road`, `Dungeon Regular`.
17. `Iron Stomach`, `Thirsty Mind`.
18. `Potion Affinity`, `Field Dresser`, `Shaky Potioner`.
19. `Poison-Bred`, `Plagueproof`.
20. `Thick Hide`, `Spellscarred`, `Thin Blood`, `Arcane Sensitivity`.
21. `Restless Sleeper`, `Deep Sleeper`, `Restless Mind`.
22. `Homebody`, `Restless Sleeper`, `Deep Sleeper`.
23. `Shopkeeper`, `Market Merchant`.
24. `Riverborn`, `River Runner`, `Short Breath`.
25. `Beast Bond`, `Feral Pact`, `Protective Handler`, `Wild Handler`, `Pet Trainer`, `Weak Bond`.
26. `Gilded Tongue`, `Shy Vendor`, `Mercantile`, `Scrap Dealer`.
27. `Local Legend`, `Foreign Soul`, `Outsider's Friend`, `Sour Reputation`.
28. `Frenzied`, `Heavy Handed`, `Unsteady Hands`.

Additional caps evaluated **after** all eligible character traits and Paths of Fate perk sources are composed:

| Effect family | Combined cap | Notes |
|---|---|---|
| Out-of-combat foot movement (non-mount) | +15% net bonus | Does not bypass roots/slows; negative sources remain. Respect speed state on map changes. |
| In-combat foot movement | +5% net bonus | NPC-only bonuses not applied to PvP interactions. |
| Ordinary fall damage mitigation | −60% total | No flying/mount exploit or complete immunity. |
| Rested XP accumulation speed | +30% total | Cap remains unchanged. |
| Quest XP | +15% total | Apply once; honor quests' original eligibility. |
| Eligible kill XP | +15% total | No gray/no-reward targets; no bot farm abuse. |
| Quest reputation | +20% total | Never before/after-cast twice; preserve negative changes and caps. |
| Humanoid gold bonus | +20% of base, **max 10 gold bonus/day** from Fate traits/perks together | Store earned bonus for caps; not shared with baseline loot. |
| Vendor purchase discount | 10% total | Clamp buy/sell and disallow vendor arbitrage. |
| Vendor sell bonus | 15% total | Reject buy-then-sell arbitrage. |
| Profession skill-gain relative bonus | +25% total | Changes chance, not guaranteed skill-up, unless original chance was 100%. |
| Ordinary PvE damage bonus from starting traits | +6% total | Separate existing Paths of Fate effects; preserve their behavior where sensible. |
| Ordinary PvE effective health increase from starting traits | +8% total | Avoid compounding max-health recalculations. |
| Potion/bandage/food effect strength | +25% total | Never scale fixed on-use procs or create free items. |
| Health restore on kill | 1 trigger / 10 seconds | Requires XP/honor-eligible NPC and correct loot/kill credit. |

**Computation:** each effect family gets a single deterministic aggregator. Keep positive and negative contributions separately where caps should prevent positive overflow without erasing the downside. Normalize order, apply once on the core award event, and round currency to integer copper conservatively. All combat-paired modifiers must consistently apply to NPC-vs-player and pet/minion attribution as specified; no player-target PvP loopholes.

**Clarify before release for edge mechanics:** `Duelist` / `Crowd Fighter` determine active threatening NPCs via combat references, not total units nearby; `Frenzied` procs from credited kills, not killing innocuous summons; `Stonewall` movement is based on trusted authoritative movement flags with server-side time; `Night Owl` uses server time-of-day synchronized with client presentation, not player local time; `Cave Dweller` needs a verified area-type dataset; `Hoarder` counts inventory occupancy in normal bag slots, excludes bank and currency tabs; `Tipsy Brawler` applies only under nonzero recognized drunkenness state and requires accuracy penalties always active alongside dodge advantage; `Moon-Cursed` uses the same day/night clock in the creator tooltip and server runtime; `Unbreakable Spirit` excludes non-ordinary/boss CC to avoid bypassing encounter mechanics.

## 6. Data storage, migration and backfill

### 6.1 Proposed characters DB tables

- Keep existing `character_esteria_perk_profile(guid, trait, respecs)` for compatibility with v1 clients and existing data; do not drop/change semantics of `respecs` (it counts in-game progression-card respecs). Add `trait_respecs` column only if no conflict with the actual schema, or use the separate `character_esteria_trait_state` table below.
- New `character_esteria_trait_selection (guid INT UNSIGNED, trait_id SMALLINT UNSIGNED, selected_at INT UNSIGNED, PRIMARY KEY(guid, trait_id))`.
- New `character_esteria_trait_state (guid INT UNSIGNED PRIMARY KEY, selection_version TINYINT UNSIGNED, respec_count SMALLINT UNSIGNED, points_base TINYINT UNSIGNED DEFAULT 4, last_changed INT UNSIGNED)`; store a schema version, **not an authoritative, independently mutable points balance**. Recompute remaining from selected IDs, rules and base value on every validation.
- New `character_esteria_trait_audit (id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY, guid INT UNSIGNED, occurred_at INT UNSIGNED, action VARCHAR(24), before_json TEXT, after_json TEXT, admin_guid INT UNSIGNED NULL, reason VARCHAR(128))` (adapt data types to project conventions and parameterized statements).
- New `character_esteria_trait_usage (guid INT UNSIGNED, yyyymmdd INT UNSIGNED, bucket SMALLINT UNSIGNED, value BIGINT UNSIGNED, PRIMARY KEY(guid, yyyymmdd, bucket))` for reward caps where required. Prune safely with operational policy.
- New `character_esteria_trait_quest_state (guid INT UNSIGNED, quest_id INT UNSIGNED, accept_token BIGINT UNSIGNED, death_count SMALLINT UNSIGNED, PRIMARY KEY(guid, quest_id))` **only** if High Roller requires persistent state not already available. Track only still-active quests; clean on abandonment and completion.
- Do not duplicate static metadata in each character row; use a compiled/shared registry generated from the JSON alongside this document. SQL names are proposed: align them with actual DB migration conventions.

### 6.2 Safe migration

1. Create new tables/indexes with idempotent, reversible migration strategy and verified backups; include prepared statements.
2. For a legacy character with `profile.trait IN (1..8)` and no selection rows, insert exactly one selection row with that ID. For `trait=0` or missing profile, leave selections empty. Copy neither progression-card rank points nor respec history into FTP.
3. Mark migration progress by schema/selection version. Make rerunning migration a no-op, not a second trait award.
4. On v2 selection update, sync `profile.trait` to the legacy trait only when the **entire selection set contains exactly that single legacy trait**; otherwise sync `0` as a backward-compatibility sentinel. V1 clients may then see Unmarked, but never a bogus random trait. Prefer feature-gated client-version enforcement for mismatches.
5. Before deleting a character, delete/cleanup trait selection/state/usage/active quest state in the same deletion handling as existing perk/profile rows; keep historical audit per retention policy.
6. Handle GM race/class/level updates, account transfers, faction changes, rename, paid customization and character copies without duplicating points or losing selections.
7. Reject invalid persisted rows at load and log a repair audit; **never silently delete intentional selections**. If a currently missing feature renders traits unplayable, mark inactive/locked and preserve the historical selection for recovery. Admin repair tools can explicitly remove/refund with audit.

### 6.3 In-memory model

```cpp
struct TraitSelectionState {
    std::vector<uint16_t> chosenSortedUnique;  // canonical order
    uint16_t basePoints = 4;
    uint16_t earnedFromFlaws = 0; // derived only
    uint16_t spentOnMerits = 0;   // derived only
    uint16_t remaining = 4;      // derived only
    uint16_t respecCount = 0;
    uint32_t contentVersion = 2;
};
```

Use APIs such as `HasEsteriaTrait(id)`, `GetEsteriaTraitSelection()`, `ValidateTraitSelection(selection, classAndCharacterState)`, `SetTraitSelectionAtomic(...)`, and `RecomputeTraitModifiers(reason)`. Replace old equality checks `GetEsteriaTrait() == CharacterPerkTrait::X` with `HasEsteriaTrait(X)` but preserve existing old getter temporarily if other code still uses it. Never turn a missing row into an invalid bonus.

## 7. Network protocol: PFT2 character creation + runtime trait snapshot

### 7.1 Character-creation PFT2 trailer

Current PFT1 footer is `[u8 version=1][u8 trait_id][magic 'PFT1']`. It is fixed-size and single-choice. Keep its parser for old clients. New trailer should be appended **last** after start-zone and appearance trailers, just as the existing FATE trailer is currently last:

```text
[u8 version=2]
[u8 count: 0..8]
[count × u16 trait_id, little-endian, sorted ascending, unique]
[u16 payload_length: length of preceding version+count+ids]
[u32 magic: ASCII 'PFT2' = 0x32544650, little-endian]
```

`payload_length == 2 + 2*count`, `count <= 8`, total footer size `payload_length + 6`, and bounded total size <= 24 bytes (count=8). `ReadTail` sees the trailing magic, verifies length, parses, rejects duplicates, forbidden ID 0, invalid or disabled IDs, negative budget and conflicting/ineffective effects; only then strips trailer **without corrupting ByteBuffer::rpos()**. In contrast, an absent recognizable trailer falls back to empty/Unmarked; recognizable but malformed PFT2 fails creation cleanly. If both PFT1 and PFT2 appear, reject rather than accept ambiguous tails; tests must cover magic collisions and extra bytes. Enforce canonical sorting at both ends.

**Implementation nuance:** The first create-stage read occurs before some appearance/race fields are fully validated. Separate *framing* validation from *gameplay selection* validation. Run the latter after the server has the canonical race/class/spec/power facts, before writing the character transaction. Never trust values precomputed by Lua/native client. Commit all selection rows in the **same character-creation transaction** as the new GUID/profile and existing zone/body/appearance data. On a failed create, no orphan rows. Resend/create retry preserves selection in the client until successful create (existing native currently clears single trait at footer write).

### 7.2 Native client and glue

- Add a stable native bridge (`SetCreateTraitList` taking a bounded serialized ID list, or equivalent tested per-ID add/remove bridge). Do not pass untrusted UI strings directly into packet writes; range-check IDs and validate their sort/uniqueness. Preserve old `SetCreateTrait` for old glue or gracefully delegate it to a one-trait vector.
- Generate a single catalog asset for client display and server validation. The server is authoritative; a client-side cost/eligibility preview is advisory only.
- Current `fate_page.lua` is designed for nine hardcoded rows and references `SetCreateTrait`; rework its data binding rather than generating 163 fixed frames. Do not break `StartZonePage_Follows`, `CharacterCreate_Okay`, `CharacterCreate_Back`, `CharacterCreate_OnShow`, `PAID_SERVICE_TYPE` exclusions or the handler chain.
- Keep packet header/footer stripping order. Do not replace PFT1 magic in-place without compatibility support.

### 7.3 In-game runtime snapshot

`PerkWire.h` v1 snapshot contains a single `trait(u8)` and the 54-card fields. Do **not** add arbitrary trait IDs to that payload with `Proto=1`. Choose either:

**Preferred:** independent `CMSG_ESTERIA_TRAIT_SNAPSHOT_REQUEST`, `SMSG_ESTERIA_TRAIT_SNAPSHOT`, `CMSG_ESTERIA_TRAIT_RESPEC`, `SMSG_ESTERIA_TRAIT_RESULT` with collision-free opcodes verified against the custom opcode map; v2 trait snapshot includes protocol version, sorted IDs, base/earned/spent/remaining, current cap/limits, feature availability and reason codes. The existing 54-card `PerkWire` remains byte-for-byte compatible. Attach a new Traits tab/panel to existing in-game EsteriaPerks addon.

**Alternative:** increment existing `PerkWire::Proto` and change both client+server atomically with strict version mismatch rejection. Avoid unless the existing architecture makes an independent channel impossible. This is not the default.

**Result codes:** invalid ID, duplicate, class ineligible, feature disabled, mutual conflict, Quirk cap, Merit cap, Flaw cap, Flaw grant cap, insufficient FTP, in combat, location restriction, respec cost, incompatible client version, internal failure. Server sends authoritative revised snapshot on completion/rejection so UI can recover from stale local state.

## 8. Runtime effect engine: design obligations

Implement reusable effect operations rather than 162 ad-hoc scattered equality checks; for complex conditional/proc effects, use named handlers. Each operation must preserve whether it applies to NPC damage, spell or pet damage, a reward type, a known class/resource, a specific source category or a player movement context. The server owns all modifiers.

### 8.1 Declarative effects + named conditions

Proposed model:

```text
TraitDef {
  id, kind, category, cost_or_grant, eligibility, conflicts,
  feature_flag, implemented,
  benefit_effects[], drawback_effects[]
}
Effect {
  hook_family, polarity, operation, scalar, scope, source_filter,
  trigger_condition, internal_cooldown_ms, daily_bucket, cap_group
}
```

Support core effect families: `PVE_DAMAGE_DONE`, `PVE_DAMAGE_TAKEN`, `MAX_HEALTH`, `MAX_MANA`, `SPELL_COST`, `SPELL_CAST_TIME`, `SPELL_CRIT`, `HIT_CHANCE`, `DODGE_CHANCE`, `ARMOR_FROM_GEAR`, `FOOT_SPEED_OOC`, `FOOT_SPEED_COMBAT`, `SWIM_SPEED`, `FALL_DAMAGE`, `ENV_DAMAGE`, `HEALING_SELF`, `HEALING_OTHER`, `POTION_HEAL`, `BANDAGE_HEAL`, `FOOD_BUFF_DURATION`, `ELIXIR_DURATION`, `NATURAL_REGEN`, `RESTED_XP_ACCRUAL`, `QUEST_XP`, `KILL_XP`, `QUEST_REPUTATION`, `FACTION_REPUTATION`, `QUEST_GOLD`, `LOOT_GOLD`, `CHEST_GOLD`, `LOOT_COMMON`, `VENDOR_BUY`, `VENDOR_SELL`, `AH_DEPOSIT`, `AH_COMMISSION`, `REPAIR_COST`, `TAXI_COST`, `PROF_SKILL_CHANCE`, `GATHER_TIME`, `CRAFT_TIME`, `Hearthstone_COOLDOWN`, `PET_*`, `COMBAT_PROC`, and feature-specific housing service hooks.

**Important:** many hook families need core call-site integration in this fork (not merely `PlayerScript`); inspect existing `PerkFx.h` and `PerkEffects.cpp` and reuse their existing guarded injection points. AzerothCore's general `PlayerScript` hooks can cover XP/reputation/loot/skill/quest lifecycle but cannot be assumed to cover every precise modifier. Compile against the actual headers in this branch, not the latest upstream signatures.

### 8.2 Hot-path performance

- On trait load/respec and on relevant state transitions (movement/combat/map/gear/quest/guild/party/day-night/weather), compute cached modifier facets. No DB call in damage or spell combat loops.
- Event-based recalculation; time-dependent predicates may use a coarse tick with change detection, never all-trait scans every server tick.
- Use guarded internal cooldowns and character-GUID keyed state; clean on logout/destruction. Persist reward/quest caps needed to prevent relog/restart exploitation.
- Deterministic per-hook order, with a signed delta accumulator; round once after applying caps. Ensure server/client agree on visible speed and stats.
- **No double dip:** Do not independently modify quest XP in both `OnPlayerGiveXP` and `OnPlayerQuestComputeExp` for the same reward. Use a dedicated reward-source check. Likewise do not multiply reputation through multiple callbacks.
- Classless/alternate races: do not hardcode vanilla class count where this fork has new playable races/classes. Inspect actual class masks and classless powers.

### 8.3 Combat, PvP and control effects

- A trait's PvE effects apply only when the final hostile event targets an eligible NPC, and a defensive effect only when an eligible NPC is the source. Treat pets/totems/guardians consistently; do not allow owner/target laundering into PvP.
- Disable new offensive/defensive/CC/hit/dodge proc modifiers in any PvP damage interaction, duels, arenas and BGs, including controlled pets and hostile-player-owned units. Existing original perks should retain their current PvP semantics unless a deliberate separate change is approved.
- Do not reduce scripted boss stuns/fears/roots or boss telegraphs, and do not allow encounter failure by aggro/detection quirks.
- Health modifiers on toggling PvE eligibility must not heal or kill a player unintentionally. Recompute max HP and clamp current to valid range; use a documented health-ratio policy.
- Follow aura remove reasons, death, shapeshift, talent changes and relog with no orphan bonuses.

### 8.4 Professions, rewards, economy and housing

- All extra-material procs limited to common materials; do not generate soulbound/unique/quest items or rarer equipment. Check inventory delivery failure; only consume a proc/cap when item was actually awarded. Preserve loot rights/taps and gather ownership. Per-day UTC buckets reset across restart through DB state.
- Classify creature family from actual creature templates, not name strings. `beast`, `humanoid`, `undead`, `elemental`, `living` need explicit definitions.
- Quest classification needs maintained IDs/metadata or explicit rules, with mixed quests excluded. Validate quest abandon/reaccept/reward loops.
- Economic calculations are server-authoritative in copper. Watch for vendor buy/sell loops, negative costs, fractional rounding gains, and auction deposit refund/commission changes. Only change a player's own transaction, not shared market price.
- Housing traits must integrate with Esteria housing APIs without altering third-party player ownership or permission enforcement; for percentage benefits, define whether modifiers affect listing price, seller net proceeds or server commission and avoid minting extra gold on both sides.

## 9. Character creation and trait respec UX

### 9.1 Proposed creator wireframe (functional layout)

```text
 ┌───────────────────────────── PATHS OF FATE ─────────────────────────────┐
 │  ORIGIN TRAITS                  Starting: 4  Flaws: +5  Merits: -7      │
 │                                 REMAINING: 2  [VALID]                  │
 │  [All] [Merits] [Flaws] [Quirks]   Search [________________________]    │
 │  Filter: Combat ▼  Only usable [✓]  Show locked [✓]                   │
 │  ┌─────────────────────────────────┬────────────────────────────────┐   │
 │  │ +2   RESTLESS MIND         [x] │   RESTLESS MIND                  │   │
 │  │ -3   NATURAL HEALER        [x] │   Flaw  ·  +2 Fate Trait Points │   │
 │  │  0   MOON-CURSED           [x] │   −15% rested XP accumulation   │   │
 │  │  0   WANDERER              [ ] │   Restrictions: none            │   │
 │  │ ... scroll/virtualized ...    │   [Remove]                      │   │
 │  └─────────────────────────────────┴────────────────────────────────┘   │
 │  Selected: 1/2 Quirks · 1/3 Merits · 1/3 Flaws   [Review Choices]      │
 │                                       [Back]             [Next]        │
 └────────────────────────────────────────────────────────────────────────┘
```

This is **behavioral guidance**, not pixel-perfect UI. Use Esteria's actual character-creator style, fonts, spacing, localization and 3.3.5 Lua capabilities. Resolved data should drive list entries and filters. Preserve current ability to see the six Paths of Fate, but visually distinguish **background FTP now** versus **path card points earned starting level 10**. If a row is gated, show *why* and do not allow adding it.

### 9.2 Respec policy

- New character: can edit until Create succeeds.
- Existing characters: expose separate **Trait Rechoose** service in the in-game Paths of Fate window at an inn/city, out of combat, not in battleground/arena, not during trade/auction transaction. First trait rechoose **free**, later rechoices cost `min(level × 5000 copper, 500000 copper)` as a suggested progression matching the existing respec pattern, but maintain an **independent counter**.
- A rechoose is an **atomic complete replacement** of the selection set, not stepwise removing Flaws while retaining paid Merits. Re-evaluate budget/eligibility against the entire proposed new build before charging money or committing. On failure keep old build and gold.
- On a permitted reset, remove every previous trait-produced stat/aura/timer effect before applying the new build; clear proc states where needed, avoid duplicated procs, preserve achievements and existing Paths of Fate ranks.
- On disabled trait patches, supply a **free forced repair/rechoose** for affected builds without deleting existing rows silently.
- GM commands: `.perk traits info [player]`, `.perk traits validate`, `.perk traits reset [player] [reason]` with adequate permission level and audit, without altering `.perk reset` for progression.

### 9.3 Migration/player onboarding

Legacy one-trait characters should continue to receive their old existing trait exactly as before. A migrated existing character has the original single Quirk in their new selection set, 4 FTP available for future Merits, and may use the first-free Trait Rechoose to take a complete current build. Do not automatically add a Merit, Flaw, or a second Quirk. Never reset the existing 54-card ranks just because the character updates.

## 10. Engineering milestones and acceptance gates

**Implement incrementally; do not declare completion just because metadata/UI tables exist.**

### Milestone A — Audit + pure rules + catalog

- Confirm current repository branches, fork hooks and actual client build/update pipeline; inventory current original eight trait handlers, current six-path cards and their DB state.
- Add machine-readable, ID-stable registry, explicit conditions, conflicts, flags, and pure `ValidateSelection` / `GetBudget` with unit tests.
- Compare generated client/server trait metadata and duplicated wire header; add CI drift check.
- **Gate:** full catalog parses, 0..162 contiguous and stable; zero duplicate names/IDs; initial 4, at most +8, correct caps; all known conflict pairs rejected; invalid class/feature denied.

### Milestone B — DB, PFT2 creator and client UI

- New migration, safe legacy backfill, prepared statements and character transaction updates; PFT1 compatibility; multi-trait save/load and runtime snapshot.
- Native bridge and search/filter/virtualized glue creator; adapt tests for page flow and retries.
- **Gate:** new char with 0/1/8 choices saves and reloads exactly; old v1 client/character still maps 0..8; malformed/duplicate/over-budget packets fail without partial row/character; starting-zone/appearance trailers still work.

### Milestone C — Implement original eight + shared runtime engine

- Preserve all existing legacy effects exactly; refactor single trait checks to multi trait registration and add cache/apply/remove lifecycle.
- Implement pure Merits/Flaws as usable pairs of same effect families first: health, PvE damage, vendor prices, profession gains, fall damage, rested XP and movement. Never activate a Flaw alone until its penalty is verifiably live.
- **Gate:** relog, rebuild, respec, switch map/class, death and login do not leak/duplicate modifiers. PvP/source rules hold.

### Milestone D — Remaining Quirks and specialist effects

- Implement all quirk pairs in batches by hook families, with tests for positive, negative and combinations. Prioritize combat, exploration, professions, economics; then pets/group, special procs, class-dependent conditions.
- For feature-dependent housing/weather/quest-category traits, add verified integration or leave locked with meaningful reason. Do not silently treat complex mechanics as +X% generic stat.
- **Gate:** all selectable trait pairs have independent verification of both effects, including cooldown/caps; no placeholder selectable traits.

### Milestone E — Release and ops

- In-game trait panel, separate respec endpoint and GM commands, audit metrics and localization/tooltip review.
- Add feature flags and a migration/rollback guide; stage rollout on non-production realm. Validate with server+client builds from matching commits/versions (especially if AFK branch client and active main diverge).
- **Gate:** manual test matrix, automated tests and live GM smoke test completed; screenshots or short video of creator budget and in-game snapshot; known unsupported traits explicitly listed.

## 11. Required test matrix

| Case | Expected |
|---|---|
| Empty choice / Unmarked | 0 selected, 4 remaining, no effect; no saved `id=0` row |
| Legacy trait 1 or 8 sent through PFT1 | Preserved legacy benefit+drawback; migrate exactly once |
| Three flaws grant 3+3+2 | +8 points, total 12; fourth Flaw blocked |
| Three flaws each +3 | +9 not allowed even though count=3 |
| Two Quirks, three Merits and three Flaws | Up to 8 selections when budget and conflicts pass |
| Two opposite Quirks that cancel weaknesses | Server rejects via conflict registry |
| Mana-only Flaw on Warrior (without relevant mana source) | Rejected; cannot gain free FTP |
| Pet-only Flaw without eligible pet | Rejected; cannot gain free FTP |
| Wrong ID, duplicate ID, sortedness violation | Clear error; no partial write |
| PFT2 trailer with bad magic or size | Safe rejection; no packet tail corruption |
| Both PFT1 and PFT2 trailing, or malformed recognized PFT2 | Explicit rejection, not silent fallback |
| Old client with PFT1, new server | Old eight usable; no crashing or bad IDs |
| Cross-account forged packets / later class change | Server-authoritative restrictions preserved |
| One health bonus, relog, dungeon enter/leave | No duplicate stat stacking or accidental healing |
| Direct PvP, world PvP, duel, arena, BG | Trait combat modifiers absent for player-v-player interactions |
| NPC pet/guardian/totem attribution | Only intended PvE effects apply |
| Negative vendor pricing combinations | Prices do not go below zero; no buy-sell arbitrage |
| 10 gold/day capped loot bonus, restart server | Cap survives restart and cannot be bypassed by relog |
| High Roller quest accepted, death, relog, completion | Negative/positive selection based on persisted death state |
| Night/day and weather transitions | Bonuses change once, not at frame-rate and not client-only |
| Quest category mixed/unknown quest | No unintended bonus or penalty; classification documented |
| Current level-card perks and purchase/respec | All 54 cards and current 15-point progression unchanged |
| Trait rechoose with insufficient gold | No changes, no charge, actionable error |
| Forced disable of a trait with feature flag | Selection remains auditable and offers free repair |
| Character delete/transfer/copy | Trait data cleaned/transferred exactly once; audits retained per policy |
| Failed character creation / name collision | No orphan rows; choice remains for retry |
| Initial playerbots / classless / custom races | Group and resource eligibility sane; no free flaws |

## 12. Implementation notes to Claude and response format

1. **First read actual code**, not just this document; check active branches and code search, verify original eight work today, find any partially implemented or obsolete hooks. Provide a short audit with found file paths and caveats before edits.
2. Create a small, buildable implementation diff per milestone. Favor shared helpers in existing files over a parallel duplicate Fate system. No broad refactors to unrelated character creation code.
3. Use exact positive **and** negative mechanics as defined; if a semantic cannot be realized in 3.3.5 without new systems, implement the necessary subsystem cleanly or disable the card in both creator and snapshot and document it. **Never simulate success by only changing tooltip text.**
4. For questionable design details (e.g. runtime pet eligibility, PvP vs area effects, day/night clocks), make the smallest safe default following this spec, document any divergence and include verification/tests. Do not repeatedly ask for approval for routine coding details.
5. Deliver an implementation report listing changed files, SQL files, generated catalog files, protocol versions, build/test commands and results, enabled trait IDs, locked trait IDs with reasons, migration/rollback instructions and any remaining blockers.

**Non-goals:** redesigning the 54 Paths of Fate cards, converting existing card points into FTP, adding pay-to-win paid-service trait purchases, changing talents/spells/spec mechanics outside the required hooks, mass-altering rare drops, giving PvP combat advantages, adding speculative unimplemented guild-upkeep currencies.

## 13. Source references (verified 2026-11-11)

These source URLs are repository references for Claude, not a claim that implementation is complete:

- Server trait creation footer: https://github.com/STRHercules/EsteriaWoW/blob/main/src/server/shared/PerkTraitWire.h
- Server progression rules: https://github.com/STRHercules/EsteriaWoW/blob/main/src/server/shared/PerkRules.h
- Server runtime perk handler: https://github.com/STRHercules/EsteriaWoW/blob/main/src/server/game/Handlers/PerkHandler.cpp
- Server scripted effects: https://github.com/STRHercules/EsteriaWoW/blob/main/src/server/scripts/World/esteria_perk_effects.cpp
- Client FATE page: https://github.com/STRHercules/wowdecomp/blob/Esteria/Tools/RetailAssets/glue/fate_page.lua
- Client original data: https://github.com/STRHercules/wowdecomp/blob/Esteria/Addons/EsteriaPerks/Data.lua
- Client native extension: https://github.com/STRHercules/wowdecomp/blob/Esteria/WoW/Common/ComponentCore/EsteriaAppearance.cpp
- AzerothCore PlayerScript hook reference (verify fork signatures): https://github.com/azerothcore/azerothcore-wotlk/blob/master/src/server/game/Scripting/ScriptDefines/PlayerScript.h

---

### Final acceptance statement

This feature is done **only** when a player can select a valid mix of Quirks, Merits and Flaws at creation; the **server** checks their 4 + up-to-8-point budget; every **enabled** effect and drawback works; choices persist through login; players can view and rechoose them separately from level-card points; v1 characters migrate safely; the original 54 Paths of Fate cards still function; and client/server serialization remains compatible with the current creator and in-game Paths of Fate.
