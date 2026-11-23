# Implementation Plan: Uncapping the Quest Log, Classes, and Bag Sizes

Status: 1000-quest client + server implemented (2026-11-20: EsteriaWoW `MAX_QUEST_LOG_SIZE` 1000, shifted player
fields, uint16 abandon slot; `build/retail-satchel` configured with 1000 slots; quest UI in `Addons/EsteriaQuestLog`);
live acceptance pending
Target: WoW 3.3.5a client (build 12340) as reconstructed in this repo, x64 primary / Win32 for layout checks
Companion to: `README.md` (build instructions, `WOWDECOMP_DISABLE_SIGNATURE_CHECKS`)

---


## Selected scope (2026-10-16)

The current implementation targets **1,000 active quests** through widened update
masks (M2), optional **class IDs 1..254**, and an optional **254-slot container ceiling**. It does not create bag
items, textures, icons, models, DBC rows, or other content. Existing bags retain
their server-defined sizes. Backpack, bank, keyring, and daily-quest limits stay
unchanged. Dedicated quest-sync packets (M3) are deferred.

Default builds retain 25 quest slots, 36 container slots, 32-bit class masks, and 42 update masks. An opted-in
build can be configured as follows (use your installed Visual Studio generator):

```powershell
cmake -S . -B build/limits-254 -G "Visual Studio 18 2026" -A x64 `
  -DWOWDECOMP_EXTENDED_LIMITS=ON `
  -DWOWDECOMP_QUEST_LOG_SLOTS=1000 `
  -DWOWDECOMP_CONTAINER_SLOTS=254 `
  -DWOWDECOMP_MAX_CLASSES=254 `
  -DWOWCLIENT_OUTPUT_OVERRIDE=WowClient64-limits254
cmake --build build/limits-254 --config Release --target WowClient --parallel
```

These capacities require a matching custom server; an unmodified server does
not use the same player field offsets, even if it sends only 25 quests. Use a
separate build directory from stock. No server implementation or runtime feature
negotiation is included in this repository yet.

### Implemented client foundation

- `cmake/ExtendedLimits.cmake` validates the supported ranges (quests 25..1000,
  containers 36..254), requires explicit layout opt-in, and derives the mask cap.
  An explicitly cached mask cap below the requirement is rejected; clear it with
  `-U WOWDECOMP_MAX_UPDATE_MASKS` when changing an existing configuration.
- `Tools/gen_descriptors.py` generates player descriptors and player/container
  mirror maps from the preserved stock initializers during extended configuration.
  Stock builds compile their original tables and do not need Python. Extended
  configuration needs Python 3.10 or later on PATH.
- Generated `build/<configuration>/generated/limits/limits.json` exports exact
  field offsets and counts for server integration. At 1000/254: active player
  **6201 dwords**, remote player **5199**, container **574**, required masks **194**.
  Every player field at or after the original visible-item block shifts by
  **4875 dwords / 19500 bytes**, including fields in remote player updates.
- Extended player update-field classes are packed to four bytes, avoiding x64
  padding when an odd number of quest entries is added. Storage, descriptors,
  handlers, and mirror-map counts have compile-time layout checks.
- Quest slots, sorted quest/header rows, watches, POIs, and render groups scale
  with the configured capacity. Index checks use an exclusive upper bound.
  Collapse state beyond the original 25 groups is retained for the session.
- Extended quest updates coalesce full-log refresh/status queries within each
  object update. POI membership and overlap handling avoid unnecessary large-log
  scans. Watch order persists in bounded CVar chunks so it fits existing archive
  buffers; legacy watch data still loads.
- Abandon uses the existing `CMSG_QUEST_LOG_REMOVE_QUEST` opcode. When quest
  capacity exceeds 255, **every abandon request carries a little-endian uint16
  zero-based slot**, including slots below 256. Smaller configurations retain
  the original uint8 payload. The opcode number is unchanged; its payload is not
  stock-compatible in the 1000-slot configuration.
- Update masks reject out-of-range field bits and truncated mask/value payloads.
  Container slot counts are clamped to the configured capacity. Oversized NPC
  quest/gossip menu packets are rejected before overflowing the 32-entry arrays.
- Class selection/color/role bounds support the configured class-ID ceiling.
  No class content is added; unknown class roles remain unassigned until class
  data is supplied. Extra minimap class markers reuse existing generic dots.

### Class ceiling: 254 (opt-in)

`WOWDECOMP_MAX_CLASSES=254` requires `WOWDECOMP_EXTENDED_LIMITS=ON`. It can be used
independently of the quest and bag settings. This expands the supported ID range;
it does not add playable classes, class records, spells, talents, roles, or art.
ID 0 remains invalid and 255 is outside the selected supported range. Existing
class-ID fields remain one byte; player update-field layouts do not grow because
of this setting.

`WoW/Common/ClassMask.h` replaces player-class membership masks with eight
little-endian uint32 words (256 bits) in extended builds. Stock builds retain one
word. All-ones legacy constants expand to all words; ordinary legacy constants
populate word 0 and leave higher words zero. Mask membership never shifts by an
out-of-range class ID, so IDs 32, 33, 64, 128, and 254 do not alias lower classes.

| Format | Extended class mask | Bit convention |
|---|---|---|
| Item query/cache `allowableClass` | 32 bytes in place of 4 | class ID minus 1 |
| Talent, skill, faction DBC membership | 8 consecutive columns per mask | class ID minus 1 |
| `CMSG_WHO` class filter | 32 bytes after the unchanged race mask | class ID |
| Optional character-enumeration restrictions | 10 masks, 320 bytes total | class ID |
| Petition query/cache `allowedClasses` | 32 bytes in place of 4 | Existing raw mask bits preserved |
| Minimap tracking masks | 256-bit internal masks | class ID |
| Vulpera appearance requirements | 256-bit internal masks | class ID minus 1 |

Item cache version is 6 (stock 5); petition cache version is 2 (stock 1). Old
records are rejected by the existing size/version checks. The changed mask
payloads use their existing opcodes and therefore require matching server packet
writers/readers; opcode numbers are not changed. The optional character restriction
trailer retains race order: Human, Dwarf, Gnome, Night Elf, Draenei, Orc, Troll,
Tauren, Undead, Blood Elf. Its absence clears stale restrictions.

The four changed WDBC formats are:

| DBC | Stock columns / row bytes | Extended columns / row bytes |
|---|---|---|
| `TalentTab` | 24 / 96 | 31 / 124 |
| `SkillLineAbility` | 14 / 56 | 28 / 112 |
| `SkillRaceClassInfo` | 8 / 32 | 15 / 60 |
| `Faction` | 57 / 228 | 85 / 340 |

`Tools/extend_class_masks.py` converts one existing table to a separate output
file without adding rows or altering its string block. For example:

```powershell
python Tools/extend_class_masks.py path/to/original/TalentTab.dbc path/to/patch/TalentTab.dbc
```

Convert all four tables and supply them through your patch MPQ for extended
class builds. The converter refuses stock-format mismatches, already extended
inputs, and existing output files. Each old mask expands in place; `0xFFFFFFFF`
expands to eight all-ones words, and other values retain their original low word
with zero high words. `SkillLineAbility` expands both inclusion and exclusion
masks. `Faction` expands each of its four alternative race/class reputation
conditions separately; those four masks are not four segments of one mask.

When adding classes later, add rows to `ChrClasses`, `CharBaseInfo`, outfits,
skills, talents, and class-indexed `gt*` tables as appropriate. `ChrClasses` keeps
its original 60-column format. Missing game-table rows now return zero instead
of being dereferenced; correct class stats still require real table data. The
96-bit spell-family masks are unrelated to the number of player classes and
remain unchanged. Glue Lua exposes `GetClassCapacity()` in extended class builds
so a future class-selection UI can discover its compiled ceiling.

### Remaining integration and acceptance

- Server: matching active/remote player and container descriptors; sparse masks
  through 194 blocks; at least uint16 quest-slot persistence; uint16 abandon
  parsing; quest-log-full enforcement at 1000; POI request handling for up to
  1000 IDs. Require compatible clients before sending the expanded layout.
- UI MPQ: update any `MAX_QUESTLOG_QUESTS` assumptions and validate scrolling,
  watch display, and POIs at 1000. Extended builds expose
  `GetQuestLogCapacity()` and `GetContainerSlotCapacity()` for UI code to discover
  the compiled ceilings. Bag UI work is needed only when larger bag content is
  actually introduced; existing art can be reused.
- Live tests: accept, abandon, update objectives, watch, and relog with quests at
  slots 24, 25, 254, 255, 256, and 999; verify subsequent player fields (inventory,
  money, skills, talents, glyphs) and remote-player equipment; validate a future
  large bag's last slot. No server/live game acceptance has been performed yet.
- Classes: supply the extended DBCs and matching item/WHO/character-restriction/
  petition packets. Add class content only when desired. Test creation, relog,
  item restrictions, skills, talents, reputation, WHO and tracking at IDs 31, 32,
  33, 64, 128, and 254. No high-ID class has been tested live.
- NPC quest-giver menus retain their separate 32-entry limit. Increasing the
  active log does not raise the NPC offer limit. Daily completion remains 25.

### Local verification

`python Tools/test_extended_limits.py` checks exact stock table reproduction,
descriptor coverage, capacity boundaries, and exported server contracts.
`Tools/check_extended_layout.cpp` is a standalone compile check for stock and
extended field layouts on x86/x64. `Tools/test_update_mask.cpp` checks parser
boundaries and 20,000 seeded random masks against an independent reference.
`Tools/test_class_masks.cpp` checks stock and extended masks, boundary IDs,
non-aliasing, DBC shapes and cache versions. `python Tools/test_extend_class_masks.py`
checks conversion using synthetic tables; no installed game data is modified.
The original roadmap below is retained for architectural context; its M3 and
bag-content work are outside the selected scope.

---

## 0. Goals and ground rules

| # | Goal | Target | Client-only? |
|---|------|--------|--------------|
| 1 | Active quests | 1000 | No — requires matching server layouts + wider abandon slots (M2) |
| 2 | Class ceiling | IDs through 254 (opt-in) | No — wider client/server/DBC masks |
| 3 | Bag ceiling | 254 slots; no bag content or art | Client changes + matching server |

Verdict summary (from the survey): **classes are easy** (data + three out-of-bounds fixes),
**bag size is a systematic medium** (a set of self-consistent resize edits, no protocol change
below 256 slots), and **the quest log is a project** (player-object wire budget is
~99% full at the stock layout; thousands of quests cannot travel in update fields).

### Ground rules

1. **Fidelity first.** This repository's value is that every function maps to an original
   address. All layout-changing work (bags with a size other than 36, quest log with a size
   other than 25, update-mask widening) must be gated behind CMake options that default to the
   original 12340 layout. Default builds must stay byte-for-byte comparable (`re/tools/verify.py`,
   Win32 struct checks).
2. **Server agreement.** Every wire-layout change needs the server to emit the matching layout.
   This document lists the server requirements per workstream; the server itself is out of tree.
3. **UI lives in MPQs.** `ContainerFrame.lua`, `QuestLogFrame.lua`, `CharacterCreate.lua`, etc.
   are not in this repo. UI work is listed per workstream and ships via a patch MPQ; for local
   testing, build with `-DWOWDECOMP_DISABLE_SIGNATURE_CHECKS=ON`.
4. **Order of work.** Classes first (cheap, isolated), bags second (bounded), quest log last
   (depends on infrastructure built for the other two, and is the only one needing new opcodes).

### Proposed CMake options

```cmake
# docs/uncaps.md: they change struct layouts and descriptor tables away from 12340.
option(WOWDECOMP_EXTENDED_LIMITS "Enable layout-changing extended limits" OFF)
set(WOWDECOMP_CONTAINER_SLOTS 36  CACHE STRING "WOWGUID slots per container item (36 = 12340)")
set(WOWDECOMP_QUEST_LOG_SLOTS 25  CACHE STRING "PLAYER_QUEST_LOG slots (25 = 12340)")
# WOWDECOMP_MAX_UPDATE_MASKS is derived: 42 stock, 194 at 1000 quests.
# A larger explicit value through 255 is accepted for extended builds.
```

`WOWDECOMP_EXTENDED_LIMITS=ON` turns on the layout-dependent code paths; with the defaults the
tree compiles to the stock layout even when the option is on. The class workstream needs **no**
option: its fixes are bounds hardening plus DBC data and are safe for stock builds.

---

## 1. Shared subsystem background (read before touching anything)

All player/container state that the server sends flows through **object update fields**:

```
SMSG_UPDATE_OBJECT
  └─ (per object) uint8 blockCount            ← hard wire width: 1 byte
     blockCount × uint32 masks                ← bit i of mask b = dword (b*32+i) present
     present dwords only                       (sparse)
       └─ ObjectMgrClient copies dwords into CGObjectData/CGItemData/... storage
          → mirror system re-runs registered handlers for changed fields
```

Key facts and constants (all verified against this tree):

| Concept | Value | Where |
|---|---|---|
| Player dwords (active player) | **1326** | `WoW/Source/ObjectMgrClient/ObjectMgrClient.cpp:58`, `:479` |
| Player dwords (other players) | 324 | `ObjectMgrClient.cpp:479` |
| Max mask dwords accepted by client | **42 × 32 = 1344** | `ObjectMgrClient.cpp:507` (`if (count > 42) return 0;`), `masks[42]` at `:527`, `:915`, `:980` |
| Quest log block | 25 slots × 5 dwords (20 B/slot), data dwords 10..134 | `Mirror.cpp:167-266`, `Player.h:60` (`PlayerQuestLogEntry m_playerQuestLog[25]`) |
| Daily quest block | 25 dwords | `Mirror.cpp:366`, `Player.h:124` |
| Container block | `6 + 58 + 2 + 2×N` dwords (N=36 → 138) | `ObjectMgrClient.cpp:58`, `:466-472`, `Mirror.cpp:57-61`, `Container.h:26` |
| Descriptor tables (fixed arrays) | `PLAYER_DESCRIPTORS[215]`, `CONTAINER_DESCRIPTORS[3]` | `Mirror.h:23-30`, `Mirror.cpp:57`, `:157` |
| Expanded descriptor tables | sized from `sizeof(...)` — auto-grow | `Mirror.h:31-38` |
| Mirror-handler global table | `TSList[...] s_typeMirrorHandlers[TYPES][1326]` | `ObjectMgrClient.cpp:43` |
| Per-object mirror lists | player 176 + 1002, container 74, item 58 | `ObjectMgrClient.cpp:1032-1047` |
| Mirror index maps | `CGPlayer::s_mirrorFields[173]`, `s_localMirrorFields[870]`, fallback `1178` | `Player.cpp:5-104` |
| Field totals | `CGPlayer::TotalFields() = CGUnit::TotalFields() + 1043` | `Player.cpp:132-134` |

**Budget picture (active player):** 1326 of 1344 addressable dwords are used. 18 dwords free ≈
3 more quest slots **or** 9 GUID fields, not both. Everything larger must widen the mask cap
(the `blockCount` byte allows up to 255 masks = 8160 dwords) or leave update fields entirely.

**Implication for this plan:** there is a usable engineering ladder —
no-change ≤ 1344 dwords → mask-widened ≤ 8160 dwords → custom packets for unbounded.

### Infrastructure task I-1: descriptor table generator

The three fixed tables (`PLAYER_DESCRIPTORS`, `CONTAINER_DESCRIPTORS`, and the mirror index maps
in `Player.cpp`/`Container.cpp`) are hand-written expansions of arithmetic patterns. Resizing
them by hand is how this project gets corrupted field offsets. **Write a small generator**
(`Tools/gen_descriptors.py`, checked in, output pasted or `#include`d) that takes
`(questSlots, dailySlots, containerSlots)` and emits:

- `PLAYER_DESCRIPTORS` rows with all offsets recomputed (`Mirror.cpp`)
- `CONTAINER_DESCRIPTORS` count field (`Mirror.cpp`)
- `CGPlayer::s_mirrorFields` / `s_localMirrorFields` and the fallback index (`Player.cpp`)
- the quest-log handler range `0x28 .. 0x28 + 20*questSlots` (`Player_C.cpp:7199`)
- the daily-quest handler range and `s_objDwordCount`/`GetNumDwordBlocks` numbers
- the hardcoded byte ranges in `Player_C.cpp:7183-7197` and `ContainerFrame.cpp:29-47`, `:92-110`

This is the single highest-leverage piece of the whole plan; budget it first (2-4 days).

---

## 2. Workstream C — Classes (do this first)

**Selected target: class IDs through 254, without adding class content.** The
original 32-bit audit below describes the stock format; the implemented opt-in
mask expansion is detailed above. 3.3.5a uses ids 1–9 and 11 (10 is free);
Druid = 11 (`NamePlateFrame.cpp:43-53` colors, `Tooltip.cpp` feral-AP checks). Class is stored as
a full byte in `UNIT_FIELD_BYTES_0[1]` (`Unit.h:62`); nothing packs it into 4 bits. Most paths
iterate `ChrClasses.dbc`/`CharBaseInfo.dbc` dynamically, and the fork already extends races this
way (`CharacterCreation.h:18` `MAX_RACES = 129`), so classes can follow the same pattern.

### C-1 Required fixes (hard out-of-bounds today)

| Site | Problem | Change |
|---|---|---|
| `WoW/Source/Ui/NamePlateFrame.cpp:43` | `s_classColors[12]` hits class 12 OOB (used at `:362`) | Extend to `[33]` (or drive from `ChrClasses` colors if the DBC carries them in your pipeline); add `classId < ARRAY_SIZE` guard |
| `WoW/Source/Ui/CGLookingForGroup.cpp:28` | `s_classRoles[11]`, `GetClassRoles()` at `:38-40` has no bounds check | Extend table, add bounds check; new classes need a role byte (roles are per-class data, not derivable) |
| `WoW/Source/Glue/CharacterCreation.cpp:398` | `int32_t classes[10]` overflows on the 11th class for a race (`GetRandomClassID`) | Make dynamic (`TSStackArray`) or `[MAX_CLASSES]`; fix the `count <= 0` path at `:410-412` which returns `classes[0]` uninitialized |
| `WoW/Source/Glue/CharacterCreation_Scripts.cpp:533-535` | `Script_SetSelectedClass` dereferences `classRec` unchecked | Add null check (hardening, independent of uncap) |

### C-2 Data requirements (no client code change)

- `ChrClasses.dbc`: one row per new class (60 columns — column count is asserted at load,
  `WowClientDB.h:133-145`; **add rows only, never columns**). `m_spellClassSet` at
  `ChrClassesRec.h:23` must be unique per class; the spell-modifier pool is 3×32 bits shared
  across all classes (`Spell_C.cpp:1258-1292`, `SpellRec.h:101`).
- `CharBaseInfo.dbc`: race/class pairs (bytes; supports 12–255).
- `CharStartOutfit.dbc`, `SkillLineAbility.dbc`, `SkillRaceClassInfo.dbc`, talent `TalentTab.dbc`
  + `Talent.dbc`, and the `gt*` game tables (`gtChanceToMeleeCrit*`, `gtChanceToSpellCrit*`,
  `gtOCTClassCombatRatingScalar`, mana/health regen) — all extend by **rows**; the `gt` tables are
  addressed as `column + row * numRows` with class as the row (`tables.cpp:39-76`,
  `Player_C.cpp:10710-10718`, `Unit_C.cpp:17466-17475`, `:21147-21218`), so per-class columns are
  already the pattern.
- Class-specific visual/audio branches that are hardcoded by id (DK 6, Hunter 3, Rogue 4,
  Druid 11, etc.) do not need changes for new classes to function — they just fall through to
  defaults. Audit list (from survey): `Unit_C.cpp:19017, 19146, 21165`; `Player_C.cpp:8227,
  13787`; `ScriptEvents_Scripts.cpp:2890`; `SpellBookFrame.cpp:814`; `LCD.cpp:308-344`;
  `ComponentUtils.cpp:42-55`; `CharacterCreation.cpp:437, 500`; `CGlueMgr.cpp:397-407`.

### C-3 The 32-bit class-mask audit (this defines the ceiling)

All masks are 32-bit; class N uses bit N‑1 in most, bit N in two places. Practical ceiling:
**class ≤ 31 for full compatibility, ≤ 32 for everything except the who-list.**

| System | Site | Bit convention | Ceiling |
|---|---|---|---|
| Spell/skill lookups | `SpellTable.cpp:52-61`, `:105` | class−1 | 32 |
| Talent tabs | `TalentInfo.cpp:391, 394, 425, 428` | class−1 | 32 |
| Faction reputation | `ReputationInfo.cpp:367-386`; `Tooltip.cpp:2022` | class−1 (mask is `int32_t[4]` but only one element is ever used) | 32 now, 128 with code change |
| Item `allowableClass` | `ItemStats.h:22` (wire u32) | class−1 | 32 (protocol) |
| Who-list filter | `FriendList.cpp:603, 698, 771, 887` | **class ID** (not −1) | **31** (`1 << 32` is UB) |
| Minimap tracking | `MinimapFrame.h:131`, `MinimapFrame.cpp:175-191, 676-694` | class ID | 31 |
| Realm race/class restrictions | `CharacterSelection_Scripts.cpp:59-104`, `RealmConnection.h:61-70` (u32 masks) | class ID | 31 |
| Appearance system | `WoW/Common/ComponentCore/VulperaAppearance.h:283-295` | class−1, explicit `classId > 32` guard | 32 |

The stock format remains limited by these masks. The selected opt-in implementation
widens the item/WHO/realm/petition payloads, relevant DBC columns, tracking and
appearance masks to 256 bits, supporting IDs through **254**. It requires the
matching server and DBC formats described above; changing a table bound alone
does not enable high-ID classes.

### C-4 UI

`CharacterCreate.lua` drives class selection from `GetAvailableClasses()` /
`GetClassesForRace()` (`CharacterCreation_Scripts.cpp:210-232, 482-504`) — data-driven, but the
class list layout and icons (if any) need checking for >12 entries. `Script_FillLocalizedClassList`
(`ScriptEvents_Scripts.cpp:630-650`) is also data-driven.

### C-5 Tests / definition of done

- Create every new class; enter world; nameplate + unit frame colors; `UnitClass()` strings.
- Open talents (`TalentInfo`), trainer, spellbook; verify no other class's spells leak in
  (spellClassSet uniqueness).
- LFG role assignment for a new class; who-list filter includes/excludes correctly.
- Equip class-restricted item tests at the boundary (class 32 items behave as restricted).
- Character select/glue screens; DK-style special-case paths do not mis-trigger (they check
  exact ids).
- Stock build still byte-verifiable (no layout changes).

**Effort: 1 day of client code + N days of data/content. Risk: low.**

---

## 3. Workstream B — Bag sizes

**Target options:**

| Target | Feasibility | Protocol change? |
|---|---|---|
| 37..128 slots | straightforward (recommended first target: **128**) | none |
| 129..254 | same edits, more testing | none |
| 255 | slot value 255 collides with the "no bag" sentinel (`FindSlotIndex` returns `0xFF`) — **do not ship 255**, cap at 254 or fix sentinels | none, but risky |
| 256..630 | mask-widening + uint16 slot protocol + server + UI | yes (`CMSG_*` slot bytes) |
| >630 | new inventory transport | redesign |

The ceiling derives from: container dwords `= 66 + 2N ≤ 1326` (global handler table
`s_typeMirrorHandlers[...][1326]`, `ObjectMgrClient.cpp:43`) → **N ≤ 630**, with the mask cap
42×32=1344 giving a similar bound; both need the mask/constant work for anything above ~600.
For N ≤ 254 everything stays inside existing widths.

### B-1 Layout edits (all self-consistent; formula: `2 + 2N` container dwords)

| Site | Current | Change |
|---|---|---|
| `WoW/Source/Object/Container.h:26` | `WOWGUID m_containerFieldSlot1[36]` | `[WOWDECOMP_CONTAINER_SLOTS]` |
| `Container.h:12` + `Container.cpp` | `s_mirrorFields[72]`, fallback 74, `TotalFieldsSaved() + 72` | `2N`, `2+2N` per generator (I-1) |
| `ObjectMgrClient/Mirror.cpp:57-61` | `CONTAINER_FIELD_SLOT_1, 2, 72, 4` | `2N` |
| `ObjectMgrClient.cpp:58` | `0x8A` (138) | `66 + 2N` |
| `ObjectMgrClient.cpp:466-472` | `return 138` | same constant |
| `ObjectClient/Container_C.h:14` | `m_containerMirrorLists[74]` | `2 + 2N` |
| `ObjectMgrClient.cpp:1032` | `FreeMirrorHandlers(..., 74)` | `2 + 2N` |
| `ObjectMgrClient.cpp:100-102` | container field base `0x100` | unchanged (object+item sizes unchanged) |

`SetupObjectStorage` (`ObjectMgrClient.cpp:710-715`) and `s_objTotalSize`
(`ObjectMgrClient.cpp:61-70`) derive from `sizeof(...)` and need no edit once the struct grows.
The expanded descriptor arrays (`Mirror.h:33`) also auto-size. That is the whole storage
change — the system is consistent by design, which is why this is medium-honest work and not
hacky.

### B-2 Safety

- **Clamp the server value.** `Container_C.cpp:7-8` trusts `m_containerFieldNumSlots` and points
  `m_bag.m_slots` into the array with no clamp; a hostile/buggy server sending > N reads OOB.
  Clamp to `WOWDECOMP_CONTAINER_SLOTS` (and log once).
- **No guard changes needed for N ≤ 255**: the `srcSlot > 255` fatal guards
  (`Player_C.cpp:9925, 10007, 10113`) and uint8 wire writes stay valid.
- If N > 255 ever happens: `FindSlotIndex`/`FindItemSlot` use `uint8_t` loop counters
  (`Player_C.cpp:1611, 9858`) — they wrap into an infinite loop past 255; all `msg.Put((uint8_t)slot)`
  sites in `Player_C.cpp` (~15 CMSG builders) need widening, plus the server. Tracked here for
  completeness; recommend staying ≤ 254.

### B-3 Performance note

`ContainerFrame.cpp:85-89` registers one mirror handler per slot on open and unregisters on close
(2 lists per dword per bag). At N=128 this is ~128 handlers per bag — acceptable; at 600+, consider
a single whole-range handler plus manual diff, or lazy per-slot registration.

### B-4 UI + data

- Patch MPQ: `ContainerFrame.lua/.xml` grid (stock UI is built for small bags; validate row/column
  generation at the target size and extend if the grid hardcodes 4 rows). `GetContainerNumSlots`
  (`ContainerFrame_Scripts.cpp:114-142`) already returns the container's real `m_numSlots` for bag
  indices ≥ 0 — no client change; only the fixed values 16/28/32 for backpack/bank/keyring are
  literals (see appendix).
- Item tooltips print `stats->m_containerSlots` (`Tooltip.cpp:4423-4428`) from the client item
  cache — no cap; server item templates must set `ContainerSlots` to ≤ N.
- Item creation path: server defines actual slot count in the container update block; client
  follows.

### B-5 Backpack/bank (hard, deliberately descoped)

Growing `m_playerFieldPackSlot1[16]` / `BankSlot1[28]` (`Player.h:69-75`) in place is blocked by
the 18-dword player headroom and shifts the entire player field layout. If bigger backpack/bank is
ever wanted, the sane route is a **virtual container**: server supplies an extra equipable bag slot
(or two) hosting the extra space as a real `CGContainer` — no player-layout change at all. The
2024-era field arrays stay untouched. (Also note bank-bag purchases are capped at 7 in
`CGBankInfo_Scripts.cpp:94-99` and `ContainerFrame.h:13` `m_containers[11]`; extending those is
mechanical if needed.)

### B-6 Tests / definition of done

- Buy/equip a bag of the target size; open, swap slot 0 ↔ slot N−1, split to last slot,
  auto-store into last free slot, sort, vendor sell from last slot, mail-attach from last slot,
  trade from last slot, guild bank deposit, bank bag, loot to bag.
- Full-bag states: autostore returns "inventory full" correctly.
- Logout/login with items in high slots; bag persists.
- Two oversized bags open simultaneously; bank + bags open.
- Server sending a bad count is clamped, not crashing.
- Stock build unchanged.

**Effort: 2-4 days client edits + 2-5 days UI/testing. Risk: medium (systematic, wide).**

---

## 4. Workstream A — Quest log (the hard one)

**Why it is hard:** the client's own quest log is 25 update-field slots inside a player object
that already uses 1326 of the 1344 addressable dwords. "Thousands of active quests" cannot be
expressed as quest-log fields at all; even mask-widening tops out around ~1,390 slots. The
durable answer is a dedicated quest-log packet pair with the update-field log kept (or dropped)
as legacy.

### Milestones (each independently useful; each needs the matching server)

#### M1 — 28-slots rehearsal (existing mask format; matching server field layout required)

Exactly the 3 extra slots the mask budget allows (1326 + 3×5 = 1341 ≤ 1344).

Resize points (the full list is the template for M2/M3):

| Area | Sites |
|---|---|
| Storage | `Player.h:60` `m_playerQuestLog[25]` (20 B/slot, 5 dwords) |
| Descriptor table | `Mirror.cpp:167-266` (100 rows), `Mirror.h:27` `PLAYER_DESCRIPTORS[215]` |
| Field maps | `Player.cpp:5-17` (`s_mirrorFields`), `:20-76` (`s_localMirrorFields`), `:103` fallback `1178`, `:133` `+1043` |
| Handler ranges | `Player_C.cpp:7199-7201` (`0x28 < 0x21C`), `ScriptEvents.cpp:392, 407` (`0x28, 0x1F4`) |
| Block counts | `ObjectMgrClient.cpp:58` `0x52E`, `:479` `1326`/`324` |
| Mirror lists | `Player_C.cpp:7182+` registration loops, `ObjectMgrClient.cpp:1043-1046` counts |
| Slot checks | ~15 sites using `<= 25` / `> 25` (off-by-ones) — `Player_C.cpp:3802, 7128, 12511`; `QuestLog.cpp:326, 403, 1081, 1316, 1369, 1640, 2135`; `QuestLog_Scripts.cpp:273, 929, 1112, 1142, 1474`; `GameObject_C.cpp:3813` |
| Client quest arrays | `QuestLog.h:89/113/114/124` + `QuestLog.cpp:42/72/75/96` (`m_sortTypes`, `m_quests[50]=2×25`, `m_questWatches`, `m_questPOIs`) |
| Removal | `CGPlayer_C::QuestLogRemoveQuest(uint8_t)` `Player_C.cpp:1866`, caller `QuestLog.cpp:452` (fine at ≤255) |

Fix the off-by-ones to `>= N` when touching (`slot <= 25` is an OOB read at slot 25 today).

#### M2 — up to ~1,390 slots via update-mask widening (custom server required)

Wire format already supports it: `blockCount` is a single byte, so up to 255 masks = 8160 dwords.

| Change | Sites |
|---|---|
| Mask arrays 42 → `WOWDECOMP_MAX_UPDATE_MASKS` (255) | `ObjectMgrClient.cpp:507, 517-518, 527, 915, 980` |
| Player block counts | `GetNumDwordBlocks` `:479`, `s_objDwordCount[ID_PLAYER]` `:58`, non-active 324 |
| Global handler table | `s_typeMirrorHandlers[...][1326]` `:43` → max managed section length |
| Descriptor regeneration | generator I-1 (all of M1's table, sized N) + `PLAYER_DESCRIPTORS` extern size |
| Funds | player field budget check: `1326 + 5(N−25) ≤ 255×32` → **N ≤ 1391**; also daily quests + POI/watch arrays |
| Server | must emit sparse masks with > 42 blocks for the player object only when needed; DB quest slots widen from uint8 (e.g., 300 stored quests/slot table) |

Sparse masks matter: the highest changed field determines the mask length. A 300-slot
configuration needs up to 85 mask dwords for the complete player object, even when only a few
values change. A 1000-slot configuration needs up to 194. Still, at 1000+ slots the block count per update
and the per-update allocation grow; test with the update rate in mind.

#### M3 — thousands: dedicated quest-log transport (deferred; outside selected scope)

Design sketch (client is already questId-centric — `CGQuestLog::GetQuestIndex`,
`GetQuestLogSlot`, `IsOnQuest` key on questId, so the "slot" is mostly a server/UI artifact):

- Keep the legacy 25 `PLAYER_QUEST_LOG` fields untouched (or stop writing them) so stock paths
  and other subsystems keep working; the extended log lives in a new structure:
  `TSGrowableArray<QuestLogEntryEx>` + `questId → index` hash, owned by `CGQuestLog`.
- New opcodes (pick unallocated values in your server's table; 3.3.5 opcode space extends well
  past `0x400` — see `Net/Types.h:1032`):
  - `SMSG_QUEST_LOG_SYNC_HEADER {u32 totalCount, u32 chunkStart, u8 moreChunks}`
  - `SMSG_QUEST_LOG_SYNC_CHUNK {entries...}` — entry = `{u32 questId, u32 state, u16 count[4],
    u32 time, u8 flags}` (~23 B; chunk to ≤ 4 KB)
  - `SMSG_QUEST_LOG_ADD/DELTA {entry}` and `SMSG_QUEST_LOG_REMOVE {u32 questId}`
  - `CMSG_QUEST_LOG_REMOVE_QUEST32 {u32 questId}` (replaces the uint8-slot opcode for the
    extended range)
- Client integration points: `CGQuestLog::Update/FilterAndSortQuests` read from the new table;
  `m_quests[50]` becomes dynamic; watches (`m_questWatches[25]`) and POIs (`m_questPOIs[25]`)
  become growable and are fed by the same sync; `m_questWatche`s/POI handler loops
  (`QuestLog.cpp:150-161`, `:2108`) stop using literal 25.
- Keep `CMSG_QUEST_QUERY` per quest as-is (details already queried lazily and cached).
- Quest-giver lists: arrays are 32-deep (`Player_C.cpp:309-321`, `titles[32][512]` at `:7454`,
  parse loop `:7456-7475` **without a clamp**; `GossipInfo.h:37/43`; `QuestFrame.h` 32-deep
  arrays) — clamp now; grow to the desired NPC offer limit regardless of milestone.
- Daily quests: `PLAYER_FIELD_DAILY_QUESTS_1` 25 (`Mirror.cpp:366`), completed-count handlers
  `0x11B0..0x1214` (`Player_C.cpp:7203-7205`), `ReportDailyQuestsRemaining` (`:12205-12221`),
  `GetMaxDailyQuests` returns literal 25 (`QuestFrame_Scripts.cpp:135`) — move the counter into
  the new sync under M3, or grow descriptors under M2.
- Collapse-filter CVar is a 32-bit mask (`GameUI_CVar.cpp:493-494`, `1 << sortId` in
  `QuestLog.cpp:866-867`) — with thousands of quests, header groups can exceed 31; replace with
  a growable set when it matters.

**UI (all milestones; M3 mandatory):** `QuestLogFrame.lua` is a fixed-row faux-scroll list tuned
for 25 entries. Replace with a virtualized list (recycle ~12 visible rows, scroll over N).
Sorting/filtering and the detail pane read via `GetQuestLogTitle(index)` /
`SelectQuestLogEntry(index)` — 1-based Lua numbers, no width issues, but the C++ side must stop
deriving array positions from 25-slot assumptions.

**Server requirements (out of tree):**

- M1: matching shifted player-field layout is required, even if the server sends only 25 quests.
- M2: player-object mask emission > 42 blocks (usually capped server-side), quest slot storage as
  uint16 (e.g., `characters.quest_slots` layout), remove-quest opcode accepts wider slots.
- M3: new opcode handlers, slot→questId mapping, chunked sync on login, remove by questId; the
  "quest log full" check (`SMSG_QUEST_LOG_FULL` = `0x195`, client just displays
  `ERR_QUEST_LOG_FULL` at `Player_C.cpp:3777-3779`) moves to the new limit.

### Quest log tests / definition of done

- M1: accept 28 quests from a server with the matching field layout; all existing quest UI functions at slots 25-28;
  abandon from slot 27; watch/POI on new slots.
- M2: 300-1000 quests across relog (sync correctness), abandon/accept while at the cap, mask
  boundary dwords (slot 25, 32, 128, 256…), daily count > 25, quest giver with 40 quests.
- M3: 3000+ quests; login sync time; memory; search/sort responsiveness; watch limit; POI icon
  budget; all of the above.

**Effort: M1 2-3 days · M2 ~1-2 weeks client + server · M3 3-6 weeks client + server + UI.**
**Risk: high — M2 changes player field layouts and abandon payload width; M3 adds new opcodes.**

---

## 5. Cross-cutting: protocol versioning

- Any extended build must be detectable by the server. Simplest: version field in the auth
  handshake (or a `CMSG_EXTENDED_LIMITS_HELLO {u32 limitsBitmask, u32 containerSlots, u32
  questSlots}` custom opcode) sent after login; server refuses or degrades gracefully.
- Never ship a client that emits >42-block masks to a stock server, or uint8 slot ops against a
  widened server — both fail mid-session with confusing desyncs.
- Feature flags: `EXTENDED_CONTAINERS` (B), `EXTENDED_QUEST_LOG` (M2/M3), class work needs no
  flag.

## 6. Risk register

| Risk | Impact | Mitigation |
|---|---|---|
| Descriptor/index-map hand edits drift | corrupted field offsets, subtle UI bugs | generator I-1; asserts comparing `TotalFields()` against expanded table sizes at startup |
| Mask widening desyncs (M2) | dropped updates, ghost state | server-side sparse masks + boundary/randomized mask validation tests |
| Quest log UI at thousands | unplayable frame rate | virtualized Lua list; cap visible rows |
| Container handler spam | frame hitches with many big bags | coalesce per-slot handlers (B-3) |
| Win32 fidelity checks fail on extended build | CI noise | build matrix: stock config always; extended config skips `verify.py` comparisons for touched functions |
| Server/client slot width mismatch (bags >255, quests M2) | item loss / quest loss on edge slots | stay ≤254 slots; server rejects out-of-range; extensive boundary tests |

## 7. Recommended sequence & effort summary

| Order | Workstream | Effort | Deliverable |
|---|---|---|---|
| 1 | Hardening PR (independent of uncaps): OOB fixes C-1, clamp quest-giver/gossip parse loops, container numSlots clamp | 0.5-1 day | safer stock client |
| 2 | I-1 descriptor generator | 2-4 days | foundation |
| 3 | Classes to 32 | 1 day code + data | new classes playable |
| 4 | Bags to 128 | 4-9 days incl. UI/tests | 128-slot bags |
| 5 | Quest M1 (28) | 2-3 days | proves machinery |
| 6 | Quest M2 (~1390) | 1-2 weeks + server | "hundreds active" |
| 7 | Quest M3 (thousands) | 3-6 weeks + server + UI | the original ask |

## Appendix A — Site index per workstream

### A.1 Classes

- `WoW/Source/Ui/NamePlateFrame.cpp:43` (colors), use `:362`
- `WoW/Source/Ui/CGLookingForGroup.cpp:28` (roles), `:38-40`, `:125`
- `WoW/Source/Glue/CharacterCreation.cpp:397-415` (random picker)
- `WoW/Source/Glue/CharacterCreation_Scripts.cpp:533-535` (null check)
- Mask sites: `SpellTable.cpp:52-61, 105`; `TalentInfo.cpp:391-428`; `ReputationInfo.cpp:367-386,
  732-746`; `Tooltip.cpp:2022-2024, 5281-5391`; `ItemStats.h:22`; `FriendList.cpp:603, 690-698,
  771-772, 887`; `MinimapFrame.h:131`, `MinimapFrame.cpp:175-191, 676-694`; `GameUI.cpp:7120`;
  `CharacterSelection_Scripts.cpp:59-104`; `VulperaAppearance.h:283-295`
- Data-driven paths (no change): `CharacterCreation.cpp:165-175, 218-232, 377-393, 720-742`;
  `CharacterCreation_Scripts.cpp:210-232, 482-504`; `ScriptEvents_Scripts.cpp:630-650`
- DBC shape guard: `WowClientDB.h:133-145`; `ChrClassesRec.h:11-23`

### A.2 Bags

- Struct: `Container.h:12, 26`; `Container.cpp` (fields table, fallback 74)
- Descriptors: `Mirror.cpp:57-61`; extern sizes `Mirror.h:33`
- Object manager: `ObjectMgrClient.cpp:43, 58, 466-472, 100-102, 1032`
- Client: `Container_C.h:14`, `Container_C.cpp:7-8` (clamp), `ContainerFrame.cpp:29-47, 85-89,
  92-110` (ranges/loops)
- Packets (only >255): `Player_C.cpp:1604-1620, 9847-9865` (uint8 loops), `9925, 9943-9945,
  10007, 10066-10080, 10113, 10129-10130` (+ ~10 other uint8 slot sites: `9828, 8904, 8955,
  11208, 14358`, mail/trade/loot paths)
- UI: `ContainerFrame_Scripts.cpp:114-142`, `870-919`; `PaperDollInfoFrame.cpp:172-189`;
  MPQ: `ContainerFrame.lua/.xml`, `ItemButtonTemplate`
- Tooltip: `Tooltip.cpp:4423-4428`

### A.3 Quest log

- Storage/statics: `Player.h:60`; `QuestLog.h:89, 113, 114, 124`; `QuestLog.cpp:42, 72, 75, 96,
  150-161, 2108`
- Descriptors/fields: `Mirror.cpp:167-266, 366`; `Mirror.h:27`; `Player.cpp:5-17, 20-76, 88-104,
  132-140`
- Handlers: `Player_C.cpp:7199-7205`; `ScriptEvents.cpp:390-393, 405-408`
- Block counts/masks: `ObjectMgrClient.cpp:43, 58, 479, 507, 517-518, 527, 915, 980, 1032-1047`
- Slot checks: `Player_C.cpp:3802, 7128, 12511`; `QuestLog.cpp:326, 403, 452, 1081, 1316, 1369,
  1640, 2135`; `QuestLog_Scripts.cpp:273, 929, 1112, 1142, 1474`; `GameObject_C.cpp:3813`;
  `QuestLog_Scripts.cpp:1169`
- Giver/gossip caps: `Player_C.cpp:309-321, 7443-7475`; `GossipInfo.h:37, 43`;
  `QuestFrame.h:150, 153` (32s), reward caps `QuestFrame.h:31-32, 45-47`
- Dailies: `Player.h:124`; `Mirror.cpp:366`; `Player_C.cpp:7203-7205, 12205-12221`;
  `QuestFrame_Scripts.cpp:135-138, 320-334`
- POI/watch: `QuestLog.h:96-97` (`m_questPOIs[25]`), `CGQuestPOIFrame.h:71`, `CGQuestPOIFrame.cpp:39,
  152, 223, 571-575, 635, 706, 741`
- Collapse bitmask: `GameUI_CVar.cpp:277, 493-494`
- Opcodes: `Net/Types.h:411-420, 491-492, 550`
- UI (MPQ): `QuestLogFrame.lua/.xml`, `QuestWatchFrame.lua`

## Appendix B — Numbers cheat sheet

```
Player object (active)     1326 dwords used / 1344 addressable (42 masks)
Quest slot                 5 dwords (20 bytes); stock 25 slots (dwords 10..134)
Daily slot                 1 dword;      stock 25 slots (dwords 1132..1156)
Container                 66 + 2N dwords; stock N=36 → 138
Player headroom            18 dwords ≈ 3 quest slots OR 9 GUID slots
Mask byte ceiling          255 masks → 8160 dwords → ~1390 quest slots max
Container ceiling          min( (1326−66)/2 , (1344−66)/2+… ) → 630 slots safe
Slot wire width            uint8 (CMSG_SWAP/SPLIT/AUTOSTORE/…), 255 = sentinel
```
