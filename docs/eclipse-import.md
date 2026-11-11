# Eclipse import

`G:\3.3.5a - Dev\Eclipse` is the build output of Eclipse-DEV (the 32-bit D3D9 cosmetic overlay). Its MPQs hold
stock-format (MD20 v264) art; its `data\` folder holds the overlay's private catalogues and DBC deltas. Nothing of the
overlay itself is used: `Tools/EclipseImport` turns the content into ordinary Esteria content.

| Category | Tool | Result |
|---|---|---|
| Mounts | `eclipse_mounts.py` | 835 learnable mounts (spells 301957+, items 1001502+), [learn list](eclipse-mount-learn-commands.md) |
| Hearthstones | `eclipse_hearths.py` | 4 hearthstone items (Ethereal Portal already existed), [list](eclipse-hearth-commands.md) |
| Incarnations | `eclipse_incarnations.py` | 244 looks for 13 form/demon slots, picked with `/incarnation` (addon `EsteriaIncarnations`) |
| Items | `eclipse_items.py` | 12,075 equippable appearance items (1100000..1112074), `eclipse-items.csv` in the stage |

Every tool runs `stage` → `pack` → `install-client` → `install-server`. Stages live in `G:\EclipseImport\<category>`.
Set `ESTERIA_STORMLIB` to `Tools/MountStudio/runtime/backend/StormLib.dll` (x64).

## Rules

- **Esteria wins.** A mount, hearth or item look Esteria already has (same model + textures, or the same name) is
  skipped. Asset MPQs only carry paths no Esteria archive has; `eclipse.mpq`'s `eclipselegacy\` (Eclipse's copies of
  colliding files) is left out.
- **IDs** come from `Data/Retail/allocations.sqlite` (mounts: `mount_allocations`, like retail mounts; the rest:
  `eclipse_allocations`), checked against every client archive, the registry, server DBCs/continuations and SQL.
  Re-staging reuses them.
- **Client rows** go through the supplemental registry (`Data/Retail/Registries/<rev>`, `RetailCasc.ini tables=`),
  appended to the live registry rather than rebuilt from packs. `ClientTables.cpp` now also merges SpellVisual,
  SpellVisualKit, SpellVisualEffectName and SpellVisualKitModelAttach (hearth visuals need a client built after this).
- **Server rows**: Spell/CreatureDisplayInfo/CreatureModelData/SkillLineAbility as `*.dbc1-native-retail-eclipse-*`
  continuations (loaded at worldserver start) plus guarded SQL that rolls back on any ID collision.
- Realm-only references (sounds, blood, item visuals, particle colours) are zeroed unless stock 3.3.5 has that ID.

## Fixes after import

- Names: `rename_mounts.py` with `mount_names_en.json` (Spanish → English) and `mount_names_cleanup.json`
  (placeholder file names). Item and creature names are cached by the client (`Cache/WDB`) and by the worldserver
  (until restart).
- Fur/feather cards: Eclipse's converter left them unlit, so they drew as flat bright triangles.
  `fix_unlit_cards.py` lights them (effects stay unlit) in `patch-EclipseMountsFix.MPQ`, 87 models.

## Not imported

30 mounts and 12 incarnation looks reference textures Eclipse never shipped; 1,409 item looks reference missing files;
ammunition looks (client-side projectiles); Eclipse fonts and D3D9 shaders (`fonteclipse.mpq`); the `.pak` M2X1 models
(Eclipse's own renderer format: effects, collections and its item pack, which duplicates `eclipse.mpq`).

## Incarnations (server)

EsteriaWoW `Incarnations.{h,cpp}`: world tables `esteria_incarnation_category` / `_form` / `esteria_incarnation`
(written by `eclipse_incarnations.py`), per-character `character_incarnation`. Forms apply in `Unit::GetModelForForm`,
demons in `Unit::SetMinion`; the addon talks over addon whispers with prefix `ESTINC`. Needs a worldserver build.
