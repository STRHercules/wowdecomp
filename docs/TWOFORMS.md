# TWOFORMS.md
# Esteria Two Forms: Worgen / Gilnean and Dracthyr / Visage

**State:** RESEARCH AND IMPLEMENTATION PLAN ONLY. No source, server, SQL, DBC, launcher, live-client, Docker, or runtime change is authorized by this document.
**Prepared:** 2026-10-20.
**Client:** `R:\Users\Zach\Documents\GitHub\wowdecomp`, branch `Esteria`, observed HEAD `3611992ebe44f7d522f018b94ff9f8119e6e15a3`.
**Live test client (inspect only):** `G:\3.3.5a - Dev`.
**Server:** `R:\Users\Zach\Documents\GitHub\EsteriaWoW`, branch `main`, observed HEAD `ad743b51470b28244cae247234864f63316bdd64`; deployed worldserver runs through Docker. No Docker files/containers were altered.
**Reference donor:** [Medviten/mod-worgoblin-high-elf](https://github.com/Medviten/mod-worgoblin-high-elf), `master` inspected at commit `13cd9021f509d981e43942288cc78e935dd22232` (2026-10-19).
**Implementation rule:** No wholesale import, no donor DBC/world database reset, no client `patch-A.MPQ` installation, and no arbitrary borrowed spells or display IDs.

## 1. Short answer

Build **one native, two-profile appearance system** in `wowdecomp` plus a small **server-authoritative appearance and form-selection service** in `EsteriaWoW`.

- **Worgen**, Esteria client/server race **12**: edit and preview **Wolf / Worgen** and **Human / Gilnean** independently.
- **Dracthyr**, Esteria client/server race **28**: edit and preview **Dragon** and **Visage** independently.
- The race, class, faction, statistics, character GUID, name, player inventory, achievements, skill/racial eligibility and network identity **do not change** when previewing or transforming.
- **Creation**: add a `Wolf | Human` or `Dragon | Visage` selector to the existing Esteria creation frame. Each selection opens the actual underlying form's customization options, not a simple skin on the other form.
- **Persistence**: store two validated, *separate* ordered sets of Retail customization **choice IDs**, the selected starting/display form, and an asset/schema version on the server. Keep them separate from the five stock WotLK appearance bytes and existing Esteria packed extra bytes.
- **Gameplay**: when the player requests a form change, the server authorizes the state transition. The client renders the correct *saved* form. Druid, polymorph, disguise, mount, and other visual overrides temporarily take precedence and then restore the selected base form.
- **Optional future**: extend the same model to barbershop and character-selection previews once creation, saving and in-world display are proven.

### One form's controls must never overwrite the other

| Race | Form | Customization backing | Save/display principle |
|---|---|---|---|
| Worgen (12) | Wolf | Retail Worgen wolf `ChrModel`/its option and choice rows | Keep unique fur, face, ears, mane/hair and other authored wolf settings |
| Worgen (12) | Human / Gilnean | A validated human/Gilnean `ChrModel` and its options | Keep unique skin, facial features, hairstyle, color and facial hair; do **not** turn race into Human |
| Dracthyr (28) | Dragon | Native Retail Dracthyr dragon `ChrModel` and its options | Preserve authored dragon scales, horns, markings, geosets and equipment behavior |
| Dracthyr (28) | Visage | Verified Visage `ChrModel` with its own option rows | Preserve the independent visage appearance |

**UI recommendation:** show a two-tab selector directly above the customizers, retaining the existing full-size rotating 3D character preview. Switching tabs should be instant after assets are loaded, should not discard unsaved choices, and should not alter the actual race selection. Optionally show small `Wolf`/`Human` or `Dragon`/`Visage` thumbnails to distinguish saved profiles.

## 2. What the donor actually implements, and what it does not

### 2.1 Verified donor evidence

The current donor's [root README](https://github.com/Medviten/mod-worgoblin-high-elf/blob/master/README.md) warns that the module is unfinished and its build scripts may overwrite server DBCs and reset the world database. It is **not safe to install wholesale** on Esteria. Its [playable-Worgen readme](https://github.com/Medviten/mod-worgoblin-high-elf/blob/master/modpaks/F-024_mod-playable-worgen/readme.md) explicitly states:

> Two Forms is not yet implemented correctly.

It also says Worgen start in wolf form and only have spells to become *pseudo-humans*. Consequently the donor **does not provide** a completed system for individually customizing two forms in the creation interface, persisting both complete appearances or authenticating a synchronized form swap.

The donor supplies three useful patterns:

**A. Skin/hair-dependent druid model lookup:** its [`whe-acore.patch`](https://github.com/Medviten/mod-worgoblin-high-elf/blob/master/integration/whe-acore.patch) changes `ObjectMgr::GetModelForShapeshift` to select **skin color for Tauren, hair color for others**. [`F-011` SQL](https://github.com/Medviten/mod-worgoblin-high-elf/blob/master/modpaks/F-011_mod-druid-forms/sql/db-world/base/%5BBASE%2CF-011%5D_player_shapeshift_models.sql) populates `player_shapeshift_model` with a five-dimensional rule, effectively `(shapeshift form, race, customization, gender) -> CreatureDisplayInfo ID`, with fallback value `255` and wildcard gender `2`. A **CreatureDisplayInfo** is deliberately *not* a `CreatureModelData` ID. This is a practical example of data-driven **visual form resolution**, not an appearance editor or Two Forms core. Esteria already implements this `ObjectMgr` lookup and `Unit::GetModelForForm` fallback.

**B. Temporary humanoid disguise:** [`src/mod_race_costumes.cpp`](https://github.com/Medviten/mod-worgoblin-high-elf/blob/master/src/mod_race_costumes.cpp) has a `spell_costume_override` `AuraScript` that saves `GetDisplayId()` on applying a `SPELL_AURA_DUMMY`, sets a gendered static disguise `SetDisplayId(...)`, and restores the saved display on removal. Example donor-only mapping: spell `68994` -> static displays `94135/94136`. This is helpful for testing a localized cosmetic effect but **cannot** express the player's unique human hair/face/body choices or prevent out-of-order stacked aura restoration. Do not hard-code those display IDs as Esteria form models.

**C. Two Forms / Darkflight Lua prototype:** [`lua_scripts/racials.lua`](https://github.com/Medviten/mod-worgoblin-high-elf/blob/master/lua_scripts/racials.lua) labels `68995`/`68996` and related `68994`/`110020` as placeholders and removes known human-form auras after casting Darkflight `68992`. It handles Running Wild separately. This shows an intended interaction rule, **not** reliable form ownership, server persistence or a working native Two Forms implementation. The [Gilnean modpak](https://github.com/Medviten/mod-worgoblin-high-elf/blob/master/modpaks/F-027_mod-gilnean/readme.md) specifically revises its human-form spells to use Gilnean visuals and notes missing appearances/sound issues.

The donor **does not implement Dracthyr**. That part must be designed for Esteria's own Retail source and extended race 28.

### 2.2 What can be borrowed

Adopt: explicit race/gender validation, a data-driven appearance/model rule table, safe rollback/fallback selection, client/server visual consistency, separate form-state semantics, and the concept that certain movement/ability events (e.g. Darkflight) can influence Worgen form.

Do **not** transplant: the DBC merge/install shell scripts; `patch-A.MPQ`; the full `whe-acore.patch` (it also alters unrelated fatigue, taxis, reputation, spell handlers); preset `SetDisplayId` costume IDs as permanent faces; a Lua aura as the canonical two-form store; donor race IDs, glyph/display IDs, spell ranks or masks; or donor's hair-color-for-every-race policy over Esteria's existing per-race druid logic without verifying impacts.

### 2.3 Esteria already contains a related but older module

The server **already has** `modules/mod-worgoblin-high-elf/`. Its `src/Worgoblin.cpp` implements Running Wild, Goblin and other custom racials, and its `src/mod_race_costumes.cpp` includes a similar costume aura. Its `lua_scripts/racials.lua` contains the **placeholder** Two Forms references. Its local modpaks use a different `F-030`/`F-040` structure from the current donor's `F-024`/`F-011` manifests. They are **not assumed equivalent revisions**.

The local module's readme also marks Two Forms NYI. Do not clone another `mod-worgoblin-high-elf` module on top of it: extend its hooks or create a dedicated small server module with a single form-state owner and coordinate the existing racial scripts.

## 3. Current Esteria implementation: exact integration points

### 3.1 Race identity and verified mapping

The actual **Esteria game race IDs** are confirmed in `EsteriaWoW/src/server/shared/SharedDefines.h`:

| Meaning | Esteria race ID | Retail source race ID used by wowdecomp | Evidence |
| --- | ---: | ---: | --- |
| Worgen | **12** | **22** | `wowdecomp/Tools/RetailAssets/RetailRaces.csv` (`12;22;Worgen`) |
| Dracthyr | **28** | **70** | same CSV (`28;70;Dracthyr`) |
| Dracthyr Visage male | race remains **28** | `ChrModel 127` -> `M2 FileDataID 4395382` | `wowdecomp/Engine/Source/Retail/RacePolicy.h`, `docs/cdn-downloads.md` |
| Dracthyr Visage female | race remains **28** | `ChrModel 128` -> `M2 FileDataID 4220448` | same |

**Critical ID distinction:** Retail race **52/70** may be considered by the Visage policy, but **Esteria game race 52 and 53 are Skyborne**. Never change a playable character's race to 52/70 to render a Dracthyr form. Never use a Retail `ChrModel`, `M2 FileDataID` or `CreatureDisplayInfo` ID interchangeably.

The Worgen wolf and human `ChrModel` IDs, the preferred Gilnean model lineage, and the source dragon `ChrModel` IDs are **not verified in the research**. Do **not** fabricate them. The first implementation investigation must enumerate all appropriate `ChrRaceXChrModel` rows, `ChrModel` rows, `ChrCustomizationOption` ownership and actual `CreatureDisplayInfo -> M2` graph from the mounted Retail source for both genders. Verify authored model-family relationships and fallback semantics instead of assuming any same-race model is automatically a full second form.

### 3.2 Client already has relevant infrastructure

- `Engine/Source/Retail/Appearance.cpp`: loads `ChrRaceXChrModel`, `ChrModel`, `ChrCustomizationOption`, `ChrCustomizationChoice` and their elements/texture layouts; builds `app::Recipe`. Today `modelByRaceSex` stores **one** model per race/sex key and `BuildForCharacter` uses it. **Two Forms needs an explicit model/form selector**, not overwriting that map globally.
- `Engine/Source/Retail/RacePolicy.h`: **currently forces Dracthyr to Visage** via `CharacterModel(race, sex, defaultModel)` when source is Retail race 52/70. This is exactly why selecting the dragon customization **cannot work by just changing the Lua UI**. Add an explicit `FormKind` path so Dragon can use its actual model, while Visage keeps verified 127/128. Preserve current default policy for non-Two-Forms callers during transition.
- `WoW/Common/ComponentCore/RetailRace.cpp`: `RetailRaceId`, `OptionsFor`, `Choices`, `LookFor`, `RetailRaceBody`, `RetailRaceGeometry`, `RetailRaceEquipment`, `RetailRaceCycleCommand`, `Refresh` and `g_state`. At present `g_state` is **per character component**, not per `(character, form)`; new entries must be independently indexed and safely freed on component destruction/race/sex changes.
- `RetailRace.cpp`: `SyncBodyModel` has already implemented a **conditional native M2 swap** for Orc/Mag'har posture. It safeguards attachments, world ownership, callbacks, animation state and refcounts. Reuse the *architecture and invariants* for two-form preview swapping, but test incompatible skeleton/attachment sockets before adopting it as-is.
- `WoW/Common/ComponentCore/EsteriaAppearance.cpp/.h`: has `EA_*` Lua bridge, `EsteriaRetailFields`, `EsteriaRetailChoice`, `EsteriaRetailStoreChoice`, `EsteriaCreateExtra`, `EsteriaEnumExtra` and `EsteriaSelectCharacter`. Its **13-byte packed field representation** covers authored legacy configurations; it **does not** provide arbitrary unlimited Retail choice lists for both forms. `RetailRace.cpp` explicitly says unrepresented extra choices are currently **preview-only** pending a wider server transport.
- `WoW/Source/Glue/CharacterCreation_Scripts.cpp`: `Script_CycleCharCustomization` already multiplexes `CycleCharCustomization("EA_*", ...)`. Introduce proposed `EA_FORM_GET/SET/COUNT/LIST` and model-specific `EA_GET/SET` operations through this existing bridge. Names are **proposals**, not existing commands.
- `Tools/RetailAssets/glue/staged/patch-Z-CharacterCreate.lua` and `Tools/RetailAssets/glue/patch_character_create.py`: already manage extended two-column customization, dropdowns, labels and up to 64 underlying controls (per project handoff). **Do not replace these patches.** Add a dedicated form tab and refresh the controls based on the active model.
- `WoW/Source/Glue/CharacterCreation.cpp`, `WoW/Source/Glue/CharacterSelection.cpp`, `WoW/Source/Glue/CharacterCreation_Scripts.cpp`, `WoW/Source/Object/ObjectClient/Unit_C.cpp` and `WoW/Common/ComponentCore/CharacterComponent.cpp`: creation preview, character-select actors, active-unit models and display updates. Review individual component lifetimes and update/restore boundaries.
- `WoW/Source/WowServices/RealmConnection.cpp` and `WoW/Source/WowSvcs/WowSvcsClient/ClientServices.cpp`: see custom character enumeration, create and server-message interactions.

**Dracthyr status:** `docs/cdn-downloads.md` confirms Visage male/female assets and customization selection have passed *asset preparation*; Dragon currently requires restoring an explicit native dragon model path. `docs/retail-race-handoff.md` distinguishes asset preparation from successful live creation, animation, equipment and relog, and says complete extra-choice persistence is not implemented.

### 3.3 Server transport and DB already have custom-race extensions

- `EsteriaWoW/src/server/game/Handlers/CharacterHandler.cpp::HandleCharCreateOpcode` reads the stock race/sex and **five** appearance bytes plus `OutfitId`. Existing extended races validate extra values: some reuse the outfit byte; Haranir has a bounded 12-byte custom extension. The `CreatureAppearance` and Vulpera paths **reject unexpected extra bytes**. A new Two Forms payload needs a **separately validated, versioned, race-gated parser**, not an indiscriminate appended blob.
- `HandleCharEnum` already sends server-specific trailers `HXE1`/`HXE2` (`0x31455848`/`0x32455848`) and cosmetic wings (`0x31475743`). `EsteriaEnumExtra` recognizes **up to three** trailers, strips them before stock enumeration and stores GUID-linked data. A new suffix must be inserted/parsed in a **well-defined order**, include bounds/count checks, and not silently exceed its loop limit or overwrite `g_roster`. Prefer a separately designed, versioned form-roster message after capability negotiation where practical, to avoid incompatibilities with old clients.
- `EsteriaWoW/src/server/game/Entities/Player/Player.cpp` saves certain extended bytes to existing unit/object padding, **not** a pair of full Retail option sets. Do not repurpose `UNIT_FIELD_PADDING` or `OBJECT_FIELD_PADDING` for a second appearance.
- `EsteriaWoW/src/server/game/Globals/ObjectMgr.cpp::GetModelForShapeshift` already consults `player_shapeshift_model`. `Unit::GetModelForForm` delegates to it before stock fallback. Keep this for **druid forms**, not for choosing a Worgen human or Dracthyr visage base state.
- `EsteriaWoW/src/server/game/Entities/Unit/Unit.cpp::RestoreDisplayId`, `Player::InitDisplayIds` and shapeshift/transform handling in `src/server/game/Spells/Auras/SpellAuraEffects.cpp` must integrate with a persistent *desired base form* so restoration never jumps to an unrelated default race model.
- `EsteriaWoW/data/sql/updates/pending_db_characters/` is the correct target location for any future schema migration per the server's `AGENTS.md`. This document does **not** create a migration.

### 3.4 Live client considerations

`G:\3.3.5a - Dev` was **inspected for its directory listing only**; it contains the installed `EsteriaAppearance*.bin/.dll` and client `Data` plus Retail resources. No live file was opened for modification or changed. The proposed work must be developed and tested on a **disposable client copy** or isolated test root, never directly by writing the production `Data` folder. The project uses `WOW_RETAIL_RACES`, Retail/Forever source configuration and optional on-demand acquisition: both form families must resolve within the intended source and cache, not depend on whichever model happened to be previewed previously.

## 4. Proposed design in detail

### 4.1 TwoForms profile contract

A race/sex-specific `TwoFormsProfile` is **one character's pair of looks**:

```text
Character GUID + Race (12 or 28) + Sex + SchemaVersion + Revision
  - PreferredBaseForm: WOLF | HUMAN | DRAGON | VISAGE
  - Appearance[formA]:
      sourceKind (Retail / other explicit supported source)
      validated ChrModel ID (or validated canonical form resolver)
      sorted, unique (ChrCustomizationOptionID, ChrCustomizationChoiceID) tuples
  - Appearance[formB]:
      independent source / model / ordered option-choice tuples
  - runtime ActiveBaseForm (server-authoritative)
```

Design rules:

1. The logical race/sex does not change. Save **form kind and option/choice identity**, not raw M2 model pointer, cached texture address, `SetDisplayId` snapshot or runtime phase.
2. Validate every option is owned by **that form's** chosen `ChrModel`, every choice belongs to its option, all prerequisite relations hold, limits are bounded and duplicate options are rejected. Apply exactly the source version/hash/catalog revision used by the server for validation, or a verified synchronized allowlist exported by the client asset pipeline.
3. Treat unknown/removed choice IDs as invalid or migrate via **explicit** versioned mappings. Do not silently apply dragon horns to a Visage mesh or corrupt the other form.
4. For the first version, keep both form payloads modest and bounded (explicit max forms=2, max options and max total packet bytes derived from the actual catalog; no invented fixed number before measuring the 45-option Dracthyr). Validate client/server parity.
5. Support **old characters** with a one-time deterministic default per form. Retain the existing saved appearance as the currently compatible form when possible; initialize the other form from authored defaults, without wiping extant fields. Save only on an intentional successful transaction.
6. Persist the selected **preferred/active base form** separately from transient druid, mount, polymorph, disguise and combat state. On login, restore once the character is available and the server has loaded appearance data.
7. Expose a shared resolver: `ResolveModel(race, sex, form)`, `GetAllowedOptions(model)`, `ResolveRecipe(form, choices)`, `SelectBaseForm(character, form)`. Render and network consumers must call this same policy, not implement independent ID guesses.

**Proposed server storage** (schema only; no executable SQL here): one `character_two_forms` table keyed by character GUID storing race/sex, schema and revision, preferred form and a bounded canonical serialized pair of option-choice sets; or a parent table plus child rows `(guid,form,option)` with a unique constraint for each selected option. Use atomic commit/rollback, foreign key or explicit character-deletion cleanup consistent with AzerothCore's character DB patterns, and a clear migration/version strategy. Do not store game state in client-only settings.

### 4.2 Explicit form selection in the Retail asset resolver

Currently the Retail appearance loader maps each `ChrRaceXChrModel` race/sex pair to a **single model** and `RacePolicy::CharacterModel` forces Visage for Dracthyr. The new implementation must:

1. Enumerate all candidate `ChrRaceXChrModel` entries for `Retail race 22` and `Retail race 70` (and any explicitly referenced associated humanoid/visage model).
2. Confirm source gender, model family, verified `ChrModel` row, `DisplayID`, M2 FileDataID, skeleton/texture layout, `ChrCustomizationOption` ownership, choice prerequisites, and expected geoset collections.
3. Define explicit, tested mapping rules:
   - Worgen Wolf: the actual source wolf model for Worgen 22 + sex.
   - Worgen Human: the **authored** human/Gilnean recipe when available; otherwise a deliberate local human-body policy that retains race 12 and stores its *own* choices. The user should choose whether this should look strictly Gilnean or allow the general human customization set before implementation.
   - Dracthyr Dragon: original dragon model for the intended source Dracthyr race and sex, **not** the current forced Visage override.
   - Dracthyr Visage: verified `ChrModel 127/128` per sex, preserving current preview work.
4. Introduce an explicit `ChrModelForRaceForm(...)` and `BuildForCharacterForm(...)` (proposed names) instead of mutating a process-global race/sex selection. Preserve the original single-form API for all other races.
5. Provide separate `OptionsFor(form)`, `ChoicesFor(form)`, cache keys and `LookFor(form)` so switching form never blends two texture atlases, option sets, collections or model caches.
6. Preload each form's M2/SKIN/skeleton/BLP and preference catalog **only for the two selected source models** where feasible; fall back to asynchronous loading and a clear loading indicator when a cold CDN download is required.
7. For the creator, swap the preview body using established `SyncBodyModel`-style safe model ownership. Preserve rotation/camera/lighting/animation and rebind attachment sockets only after checking compatibility. On failure, keep the previously valid preview and selected choices without corrupting the component.

**Important:** the donor's static `CreatureDisplayInfo` costumes lack dynamic per-player texture composition. All full-form customizations must resolve through the native Retail recipe or a verified equivalent typed player-model renderer.

### 4.3 Creation UI: two complete customizers in the existing frame

Proposed controls:

```text
RACE: Worgen                     RACE: Dracthyr
[ Wolf ]  [ Human ]               [ Dragon ]  [ Visage ]
 3D model in current form          3D model in current form
 Form-specific choices             Form-specific choices
 [Randomize this form]             [Randomize this form]
 [Preview other form]              [Preview other form]
                    [Create Character]
```

**Expected behavior:**

- Changing tabs rebuilds the option labels/list from the **active form's** option model and restores that form's current in-memory choices. Hidden fields are not reset.
- The creation frame continues to use the established Esteria two-column layout and capped/paginated visible controls. Some Dracthyr dragon sources have 45+ options; do not rely on only five original selectors or silently truncate at 29 widgets. Confirm actual current widget-instantiation/capacity paths.
- `Randomize` randomizes **only** the active form. `Race` or `Sex` change invalidates incompatible temporary form previews; both forms initialize from their correct gender source. `Back` or cancel discards unsaved pair state safely.
- A model may have distinct camera facing/bounds and animation. Auto-frame per form; do not scale dragon model as if it is a Blood Elf or reuse a Worgen humanoid head anchor.
- The optional small side-by-side thumbnails are cosmetic only. The **main panel should always show an actual customizable 3D model**, not a static donor portrait.
- `Create Character` validates that **both form snapshots** are complete before any server write, then submits a versioned bundle.
- Form selection appears **only** for the supported Esteria race IDs 12 and 28, independent of faction and donor retail race values.

Proposed `EA_FORM_*` Glue calls use `CycleCharCustomization` and the existing `EA_*` bridge; exact opcode names and Lua helper implementation remain to be chosen when coding. Keep existing `EA_PREVIEW_SAVE/RESTORE/END` behavior for ordinary races and for the currently displayed form; extend preview snapshot to save/restore both form profiles atomically.

### 4.4 Character creation and synchronization: two possible protocols

**Preferred for correctness: capability-negotiated, versioned transaction.** The core already supports custom character packets and trailers; Two Forms should be a first-class contract, not an Eluna-only ambient spell side effect.

**Option A (preferred):** if a checked, versioned native client/server extension can be negotiated in the login/character-selection session, encode the bounded pair-of-looks data **with the create operation** as one atomic transaction. The server only accepts it for races 12/28, validates the chosen source form/sex/choice IDs, and creates both records in the same logical transaction as the character. Legacy clients send the regular packet and get authored defaults. Crucially the existing receive code must **not** mistake extension bytes for stock trailing data.

**Option B (fallback):** preserve the stock `CMSG_CHAR_CREATE` exactly, then make a separate *authenticated, GUID-scoped and nonce-/revision-validated* appearance-commit operation immediately after successful creation, with explicit ACK/timeout/retry/rollback rules. This is less atomic: incomplete commits can produce a character with only defaults. Make it idempotent and recoverable; never claim success based on `SendPacket` alone.

**Character enum/list:** a client must load the pair for selected GUIDs during enum, or fetch lazily from an authenticated extension. Avoid unbounded extensions to `SMSG_CHAR_ENUM`; if using new trailers, update `EsteriaEnumExtra` to recognize **all** existing `HXE1`, `HXE2`, wing trailers **plus the new form data**, and handle the pre-existing 3-trailer loop and max-100 character capacity. A dedicated versioned per-GUID request/response is safer for larger 45-option Dracthyr snapshots.

**Network safety:** no new opcode or packet magic is assigned in this plan. Audit exact free custom opcode ranges **against the live Esteria source**, other modules, client `WoW/Source/Net/Types.h` and any future WXL integration before allocation. Ensure packet owner, session/account/GUID, race, sex, schema, length, option count, duplicate keys, catalog allowlist, stale revisions and disconnect generation are checked.

**Client model replication:** server publishes the authoritative selected form and valid revisions with unit data or a bounded side channel; the client must use a *per-GUID* form profile for **other players**, not just the logged-in user's creator component. Character select, in-world, login, logout, zone reload, late join and other observer clients must all render the correct form.

### 4.5 In-game Two Forms must not be a fragile transform-aura snapshot

**Server state machine (proposed):**

```text
Worgen:    WOLF  <--> HUMAN
Dracthyr:  DRAGON <--> VISAGE

CurrentBaseForm = saved, server-authoritative
EffectiveVisual = highest-priority active display override, else CurrentBaseForm
```

On an authorized toggle:

1. Confirm game race 12 or 28, ownership, current connection and policy restrictions.
2. Apply rules for spellcasting, combat, casts, movement, druid form, stealth, mount, vehicle, transformation and forced-visual auras. **Exact restrictions are a gameplay design decision**, not a set of assumed Blizzard 2026 rules.
3. Persist the desired base form (if configured to persist across logout), update the actual native/base visual state and notify observers.
4. If an overriding shapeshift/disguise/polymorph aura is active, queue the selected base form and show the override until it ends. After the override is removed, recompute the display from current state, **not from the display ID captured when an aura began**.
5. Restore equipment, hairstyle, geosets, shoulders/helmets, attachments, animations, weapon sheathing and hitbox/camera policy without ever changing original race or player stats.

**Visual precedence (proposed):** cinematic/GM and spell-forced transforms > polymorph/disguise > druid shapeshift/other class transformation > vehicle/mount appearance > selected Worgen/Dracthyr base form. Validate actual AzerothCore aura ordering; this is a policy to implement, **not a claim of its present internal precedence**.

**Integration points:** `Player::InitDisplayIds`, `Unit::RestoreDisplayId`, `Unit::GetModelForForm` fallback, native-appearance and display update paths, `SpellAuraEffects.cpp`, custom Worgen scripts and the client `CGUnit_C::UpdateDisplayInfo` / component model owner. Keep `GetModelForShapeshift` for druid model selection; adding a fake `FORM_WORGEN_HUMAN` or reusing a Druid spell state would interfere with actual druid forms. Do not alter game race masks or shapeshift power-type behavior.

**Spells:** Retain separate in-game “Two Forms” racial interaction only after checking actual installed Spell.dbc/SpellInfo and allocating/confirming an Esteria-specific ID. The donor's `68994`, `68995`, `68996`, `110020`, and Darkflight `68992` are **existing donor/local reference IDs or placeholders**, not a verified two-race solution. Running Wild is already managed by Esteria's Worgen module and must be regression-tested. The default design is native form state with an optional spell button; the spell triggers the authoritative toggle, rather than representing the sole storage of a human-looking aura.

### 4.6 Druid visual form changer, separately

The donor's relevant implementation is **`player_shapeshift_model` data plus hair-/skin-based lookup**. Esteria already has those exact functional seams; we do not need to move a whole `ObjectMgr` patch into the server.

For later support of druid visual choices:

- Make a separate, validated `DruidVisualSelection` lookup keyed by **race, sex, form and optional selected cosmetic appearance** instead of basing future customization solely on one stock hair-color byte. This could choose Bear/Cat/Travel/Flight/etc. display variants from existing `player_shapeshift_model` or a compatible new overlay table.
- Return a **verified CreatureDisplayInfo ID** for druid animal forms. Use the existing `GetModelForShapeshift` as default fallback if no user selection is present.
- Druid form preview (if offered in a future character customization UI) is **not** one of the Worgen/Dracthyr Two Forms profiles. Druid visual selection is a third, independent cosmetics scope; casting actual druid spells preserves combat mechanics.
- Keep existing Tauren skin-color behavior and other race-specific logic unless a conscious migration is planned. Do not copy the donor's universal “hair color except Tauren” behavior without checking Esteria's extended packed appearance and skin/hair choices.

## 5. Phased implementation process (FUTURE: do not execute now)

### Phase 0: backup and catalog proof

1. Wait for explicit implementation authorization. Work in a new Git worktree/feature branch, not the dirty `Esteria` checkout. Never `reset --hard`, `clean`, auto-stash or overwrite unfinished Retail race work.
2. Create an immutable local source/DB/source-format inventory and capture the original installed client executable/config/source checksums. Make a disposable test client clone. Take explicit backups of the Docker character database and mounted world/client DBCs only when preparing to implement.
3. Enumerate **all** relevant source model rows for Worgen and Dracthyr, both sexes; verify exact dragon, wolf, human/Gilnean and Visage recipe/asset identities. Record a source revision/hash and matching `ChrModel`/option/choice IDs.
4. Identify current `RACE_DRACTHYR` display IDs in installed server `ChrRaces.dbc`, source/target mapping and whether player objects can currently display the base dragon model. Identify existing spell/script override collisions.
5. Record the current status of both server/client full Retail choice persistence and its known losses. No “working” claim without save/relog proof.

**Exit:** form-to-source mapping exists for both sexes; no guessed model ID or silent fallback, and a known-good rollback fixture exists.

### Phase 1: local, save-free preview proof

1. Add a proposed `TwoForms` client source adapter with `FormKind` and two distinct in-memory choice records.
2. Extend `Appearance.cpp` API with explicit model selection while keeping current single-form entry points intact.
3. Add a bare native diagnostic form switch for Worgen and Dracthyr that swaps **only the creation preview** and preserves separate chosen values.
4. Validate: correct model, exact authored options, changed choice affects only its own form, both camera scales, repeated toggling, memory/resources, case where assets are still downloading, failure/retry.
5. No changes yet to server, character DB, spells, character-selection list or live game. Do not stage a client patch into `G:\3.3.5a - Dev`.

**Exit:** independent dual-form editing works offline from an isolated client, including Dracthyr Dragon which is currently hidden by the fixed Visage selection.

### Phase 2: GlueXML character creation UI

1. Add the form tab to the existing `patch-Z-CharacterCreate.lua` and associated authored Glue installer/source. Keep dropdown/two-column and existing race icons.
2. Add Lua/native form commands, adjust active option count and ensure the current 64-control strategy handles source options.
3. Provide form-specific randomization, reset/cancel and correct profile save/restore on race and sex changes.
4. Verify stable preview on DX9, Vulkan and the current DX11 where supported. The new feature must not assume Furioz's future DX11/WXL integration is already present.

**Exit:** UI behavior correct; no persistence promised yet.

### Phase 3: Server-owned full appearance persistence

1. Introduce a dedicated, narrowly scoped Two Forms server owner/module. Prefer extending current race customization service over altering unrelated spell/racial modules.
2. Add a new schema migration **only** in `data/sql/updates/pending_db_characters/` when authorized; use verified `CharacterDatabase` patterns and idempotent updates. Do not modify historical base SQL or existing donor modpak SQL.
3. Implement versioned create/read/update/ack messages, validation, character ownership checks, concurrency/revision rules and explicit failure reporting.
4. Maintain one-time conversion/defaulting rules for existing Worgen/Dracthyr characters. Leave other race records untouched.
5. Verify create, char-select, login, world inspection, logout/relog and two-client observation. A forged invalid choice cannot change another character or corrupt saved appearance.

**Exit:** two independently customized forms survive full character lifecycle and are visible to other compatible clients.

### Phase 4: In-game form toggle and compatibility

1. Add an authoritative server toggle; optionally expose through a verified racial spell once its actual ID/effects and UI have been inspected.
2. Make the form the correct **base visual**. Integrate Unit restore and aura hooks so form changes don't fight shapeshift, morph, polymorph, equipment, mounts or Running Wild.
3. Test Worgen Druid (bear, cat, travel, flight, aquatic, tree/moonkin if accessible), Worgen Darkflight/Running Wild, Dracthyr custom class/abilities, vehicles and non-player morphs.
4. Ensure observer streaming, group/nameplate, login/model resets and all renderer paths show the authoritative form.

**Exit:** no appearance reversion or corruption after repeated transform and logout cycles.

### Phase 5: optional druid appearance chooser / barber + deployment

1. Optionally expose individual druid model variants via verified display rows, not race-changing. Add barber edits only after server save/validation succeeds and appearance priority is stable.
2. Produce client/server compatibility/build matrix, migrations, explicit backups, restore instructions and controlled Docker deployment steps.
3. Configure/build the **server Docker image** (not merely host binaries) in an authorized test environment, confirm proper bind-mounted DBC paths and DB migrations, then validate a staged client. Never imply local build alone updates a running container.
4. Install in `G:\3.3.5a - Dev` only after explicit separate permission and a tested rollback procedure.

## 6. Acceptance / regression matrix

| Test case | Expected result |
| --- | --- |
| Worgen male/female preview | Wolf and Human each render the correct model, both customize independently |
| Dracthyr male/female preview | Original Dragon and verified Visage each render, own option catalogs remain independent |
| Toggle 50x rapidly / cold cache | No lost choices, duplicate model attachments, crash, stale async texture or UAF |
| Create/relog each supported race | Both form profiles, starting/selected form and source version persist |
| Legacy Worgen/Dracthyr from old account | Safe default second form, existing look preserved if representable, no mass rewrite |
| Race 52/53 Skyborne | No form UI, no Dracthyr mapping accidental crossover |
| Druid shapeshift during form toggle | Real druid form wins while active, correct chosen base form restored after removal |
| Darkflight / Running Wild | Worgen racials continue to work; form state doesn't hijack movement auras |
| Morph, polymorph, disguise, mount, vehicle | Authoritative state and restoration, never snapshot-race visual corruption |
| Real humans / other races | No added form controls, no race/creator/regression |
| Armor, weapons, facial animations, geosets | Correct after each preview and gameplay swap |
| Two clients / second observer | Both see selected form with per-character appearance, not their own local defaults |
| Disconnect while saving / forged packet | Reject or replay safely without partial DB write |
| Stock client / no negotiated capability | Safe default or explicit unsupported status, never packet parser corruption |
| D3D9, DX11, Vulkan | No backend-specific model assumption or renderer regression |
| Docker restart and rollback | Persisted forms remain consistent; old builds can run with documented defaults |

**What counts as done:** positive in-game acceptance on both races and both sexes, two sets of saved choices retained after logout/relog, correct peer visuals, safe druid/disguise precedence, zero current race/other-appearance regressions, and verified Docker server deployment. Merely seeing a gendered human costume or passing asset-file probes does **not** count.

## 7. Risks and open questions to settle with evidence

- **Source model identities:** exact wolf/human/Gilnean/dragon `ChrModel` IDs and model graph **unresolved** until live Retail tables are enumerated; no guesses or invented `displayId`/spell IDs.
- **Gilnean vs generic Human:** choose an authored Gilnean human source if available; if not, use a verified generic Human source with a documented design tradeoff. Keep Worgen voice/skills/faction regardless.
- **Dracthyr model mapping:** existing code currently pins Visage 127/128, so the dragon source must be recovered from the underlying `ChrRaceXChrModel` data and type-checked.
- **Beyond five legacy bytes:** full authoring requires variable-length validated choice pairs; `EsteriaRetailFields`/unit padding and `g_state` are insufficient.
- **Display replication:** stock 3.3.5 `UNIT_FIELD_DISPLAYID` conveys a model/display reference, **not** the whole Retail option/choice payload; custom peer customization transport must be considered.
- **Equipment and bones:** wolf/humanoid/draconic rigs may have different sockets, geosets, tattoo layers and mesh collections. Do not force immediate detach/replace of an incompatible attachment tree.
- **Revisioned schema and security:** authorize create and update by account/GUID and bounded source allowlist; never trust arbitrary client-submitted FileDataIDs.
- **Dracthyr abilities:** server custom Dracthyr class/racial effects are not established by this donor. Do not modify movement or combat stats to fake form changes.
- **Existing server extension conflicts:** character enum HXE1/HXE2/CWG1 trailers and current extra payload parser must remain valid; coordinate with future WXL NativeProtocol if integrated.
- **Rollback:** rollback of code/visuals is separate from migration of saved data. New tables should remain backward-compatible and be backed up before rollout.

## 8. Source map and attribution

**Medviten donor**
- https://github.com/Medviten/mod-worgoblin-high-elf
- https://github.com/Medviten/mod-worgoblin-high-elf/blob/master/README.md
- https://github.com/Medviten/mod-worgoblin-high-elf/blob/master/modpaks/F-024_mod-playable-worgen/readme.md
- https://github.com/Medviten/mod-worgoblin-high-elf/blob/master/modpaks/F-027_mod-gilnean/readme.md
- https://github.com/Medviten/mod-worgoblin-high-elf/blob/master/integration/whe-acore.patch
- https://github.com/Medviten/mod-worgoblin-high-elf/blob/master/src/mod_race_costumes.cpp
- https://github.com/Medviten/mod-worgoblin-high-elf/blob/master/lua_scripts/racials.lua
- https://github.com/Medviten/mod-worgoblin-high-elf/blob/master/modpaks/F-011_mod-druid-forms/sql/db-world/base/%5BBASE%2CF-011%5D_player_shapeshift_models.sql

**Locally inspected wowdecomp**
- `Engine/Source/Retail/Appearance.cpp`, `Appearance.h`, `RacePolicy.h`.
- `WoW/Common/ComponentCore/RetailRace.cpp`, `EsteriaAppearance.cpp`/`.h`, `CharacterComponent.cpp`.
- `WoW/Source/Glue/CharacterCreation.cpp`, `CharacterCreation_Scripts.cpp`, `CharacterSelection.cpp`.
- `Tools/RetailAssets/glue/staged/patch-Z-CharacterCreate.lua`, `Tools/RetailAssets/RetailRaces.csv`.
- `docs/cdn-downloads.md`, `docs/retail-race-handoff.md`.
- `G:\3.3.5a - Dev` directory listing (read-only).

**Locally inspected EsteriaWoW**
- `modules/mod-worgoblin-high-elf/README.md`, `src/Worgoblin.cpp`, `src/mod_race_costumes.cpp`, `lua_scripts/racials.lua`, `integration/whe-acore.patch`.
- `src/server/shared/SharedDefines.h`, `src/server/game/Handlers/CharacterHandler.cpp`.
- `src/server/game/Globals/ObjectMgr.cpp`, `src/server/game/Entities/Unit/Unit.cpp`, `src/server/game/Spells/Auras/SpellAuraEffects.cpp`, `src/server/game/Entities/Player/Player.cpp`.

Preserve donor copyright/license notices if later copying any source. The current donor root has a `LICENSE` file; do not assume all third-party assets/DBCs/models are available for redistribution merely because the source is public.

---

**Scope of this task:** create **only** this `TWOFORMS.md` in the `wowdecomp` repository root. No source edit, server edit, Docker restart, live-client modification, SQL execution, DBC import, Git commit, build or test run is permitted by this plan.
