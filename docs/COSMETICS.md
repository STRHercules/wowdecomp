# Cosmetics

`/cosmetics` opens Wings, Backpacks and Emotes using the existing Esteria collection layout.
`/emotes` opens its original collection panel inside the Emotes tab; playback, wheel slots and bindings remain
owned by EsteriaEmotes. The DragonUI strip and stock micro bar have a Cosmetics entry point.

## Content and items

The first backpack catalog has 99 exported meshes. Each has a cloak appearance source item and a separate
unique, bind-on-pickup learning token. The token unlocks its source appearance account-wide through mod-transmog's
existing collection. A duplicate use preserves the token. Learning and consuming the actual token commit in one
inventory transaction; a failed commit uses DigitalInventory's existing disconnect/recovery path.

| Identity | Range |
|---|---|
| Cloak source item | 3400001–3400099 |
| Learning token | 3410001–3410099 |
| Item display | 3400001–3400099 |
| Shared dummy use spell | 3420001 |
| Journey Mentor Shield source / token | 3400014 / 3410014 |

`Tools/CosmeticsPack/ids.json` is the stable allocation manifest. Never renumber entries on re-export.
New tokens have no automatic loot/vendor placement; they can be distributed through ordinary item rewards.
Applying a backpack calls the existing cloak transmog operation, retaining ownership and equipment validation.
Catalogued backpack sources cost no gold or tokens in either window. Cosmetics permits their application without
a nearby transmogrifier, while preserving alive/combat guards. Ordinary transmog rules and prices stay unchanged.
Visible-item replication makes other updated clients see the selected backpack.

The Wings catalog comes from actual server exclusive spell group 9100 (201 entries) and the effective client
visual graph. Known spells come from the player's spellbook; secure buttons cast/remove the existing wing aura.
The UI does not grant spells, appearances or items.
Wing cards show only the wings. The selected stage shows them attached to the player using the real visual kit.
Previews stay transparent until loading and camera fitting finish. The native fitter measures visible skinned
vertices in their evaluated pose, then centers them at the current facing and fits the viewport aspect ratio;
unused vertices and animation scales do not inflate the camera bounds.

## Rendering

### Live fitting tool

`/pickaxefit` provides the same local authoring workflow for Rainbow Smash: scale, model-space X/Y/Z offsets,
and X/Y/Z rotations in degrees relative to the normal weapon attachment. Equip/transmog the pickaxe and draw it
to tune the grip. Save/Revert saved/Reset work per weapon display and body model; exports contain all saved
pickaxe profiles under `WTF/Esteria/PickaxeFits/`. These fits are local and apply to matching preview/weapon
instances; final distribution still requires shipping the saved defaults. The current pickaxe catalog contains
Rainbow Smash (display 3430001).
Drawn and Sheathed tabs store independent transforms. Switch the weapon's actual state in game to inspect each
fit; editing the other tab does not move the current attachment. Existing single-state saved profiles migrate
to Drawn and the new Sheathed profile starts at the default transform. Export includes an explicit state column.
Approved family defaults are stored in `Tools/CosmeticsPack/family-default-fits.json`. The two-handed axe Drawn
and Sheathed values come from `PickaxeFits-20261114-092812.txt`; the pet defaults come exclusively from Wolf Pup
display 3400100 in `BackpackFits-20261114-092930.txt`. Other backpack rows in that export are intentionally ignored.
`import_family_fits.py` generates the shared native pickaxe defaults and updates only the Wolf Pup default row.
Future pet catalog rows can use `family: pet`; future Fortnite two-handed axe integrations share
`Retail::PickaxeDefaultFit`. Per-body saved authoring profiles continue to override the shipped family defaults.

The Wolf Pup material keeps ordinary lighting but excludes both exterior shadow-map casting/receiving because
that dense facial bake produced self-shadow artifacts. Lumenveil's depth-reconstructed wet-sheen pass also
excludes the pup/carrier's rendered world bounds; other scene surfaces retain normal weather/shadow settings.

Equip a cosmetic backpack and open `/backpackfit`. Scale is a multiplier of the automatic cloak fit; X/Y/Z
are model-space offsets added to that fit. Sliders and numeric fields update the equipped backpack immediately.
Save stores a profile in `EsteriaCosmeticsDB.backpackFits`, keyed by backpack display and body-model path.
Profiles load on player login and affect matching models locally, including subsequent previews and roster
views in that client session. They are authoring settings, not server-replicated appearance data.
Previous/Next applies the adjacent collected backpack through the server and saves the current fit before
switching; the list wraps at each end. Revert saved restores the saved profile, while Reset previews the
published default fit without overwriting the saved profile. Export writes every saved backpack/body profile to a new
timestamped `.txt` file beneath `WTF/Esteria/BackpackFits/` in the client directory. It includes all saved profiles,
not only the currently equipped backpack, and reports the full filename in chat. The native writer validates
the data and never takes an arbitrary output path from the addon. A normal logout/reload persists SavedVariables.
The November 14 accepted fits for all 99 backpacks are retained verbatim in
`Tools/CosmeticsPack/backpack-default-fits.txt` (export `BackpackFits-20261114-063046.txt`). At the owner's request,
their scale and offsets apply to every character body, relative to each body's automatic cloak anchor.
Packaging writes the same defaults to the runtime catalog and generated fitter data. Equipped models,
previews and roster views use the published defaults without needing local authoring profiles; deliberately
saved local fits still override them. New backpacks must receive a default before packaging.

`BackpackCosmetics.cpp` reads `EsteriaCosmetics/Backpacks.csv` from the MPQ stack. Equipment display IDs select
the model. Cloak geometry supplies upper-bone, height and lateral placement; the accepted Journey midpoint
correction supplies back depth. The model uses a private attachment identity, leaving weapon and wing sockets
alone. Shared-pose clothing collections can provide a cloak when it is absent from the parent mesh.

Generated meshes preserve all exported material surfaces, use lit diffuse BC1 textures and freeze the exported
bind pose. They do not reproduce Unreal physics, animation, emissive graphs or normal/specular shader response.
Eleven assets have additional special-material warnings recorded in the external asset catalog. Source files
are never modified. Normal-map-opposed authored triangles are retained instead of guessed repairs.

## Tools and deployment

- `Tools/FortniteCosmetics/convert_backpacks.py`: intake, dependency-backed texture resolution, M2/SKIN/BLP,
  per-asset loss report, dedicated art MPQ and exact StormLib readback.
- `Tools/CosmeticsPack/stage.py`: stable IDs, generated UI catalogs, additive DBC registry, pending world SQL,
  packed art/UI/DragonUI changes. `--repack-from` refreshes code/UI without reallocating IDs.
- `Tools/CosmeticsPack/deploy.py verify-sql`: validates migrations twice against isolated tables cloned from the
  real schema. `install-client` requires closed client processes and matching baseline hashes, then backs up,
  installs a new registry and the dedicated MPQ, and records installed hashes.
- The client build uses `build/retail-satchel`, target `WowClient`; the opt-in executable is `WowCosmetics.exe`.

External stages live under `G:/FortniteCosmetics/`. The installed client registry is copied from the active
baseline and changes only Item, ItemDisplayInfo and Spell. Root archives are not replaced with donor DBCs.
Server changes are in mod-transmog, the two prepared-statement registries and a focused DigitalInventory
transaction overload. Apply both pending Cosmetics world migrations before starting that binary.
The deployed server image is `esteria-coa:20261114d-cosmeticsfix`; its earlier rollback base is
`esteria-coa:20261114b-emoteui`. Server packaging evidence is under `G:/FortniteCosmetics/server-runtime-20261114`.

Focused verification includes conversion/multimaterial/texture tests, stable-ID and existing-DBC preservation
tests, C++ cloak-anchor/catalog assertions, Lua tabs/realm-sync/emote regression tests and the server token
commit-policy harness. Offline checks do not establish every race/pose or all 99 final material looks.

## Rollback

The installation receipt records the previous client config/executable/archive and server image. Close clients
before restoring their matched files. The previous registry remains intact. Keep appearance collection rows and
items until an explicit saved-data migration is agreed; do not delete learned appearances or issued tokens to
roll back the renderer. The earlier local Journey shield trial remains separately available.

## November 14 pet and weapon collection

The collection contains 108 backpacks: the original 99 rigid backpacks plus nine animated pets. Eight
new pet source/learning pairs extend the existing catalog; Wolf Pup keeps display/source 3400100 and token
3410100. There are 83 Fortnite weapon appearances, all classified as two-handed axes. Rainbow Smash keeps
3430001; new weapons occupy 3430002 through 3430083. `Tools/FortniteCosmetics/weapon-ids.json` is authoritative
for source-to-item assignments, and the staged receipt lists names and item IDs.

Pets inherit the accepted Wolf Pup fit `(1.36, 0.37, 0, 0.45)`. Every weapon inherits the accepted Rainbow
Smash drawn and sheathed defaults from `family-default-fits.json`. Saved personal profiles continue to
override them. `/backpackfit` covers every new pet, and `/pickaxefit` resolves all 83 weapons with independent
profiles per display, body and drawn/sheathed state. The native wet-sheen exclusion now covers the pet
model namespace. Existing Wolf Pup and Rainbow Smash art is preserved byte-for-byte.

The pickaxe fitter's Previous/Next buttons save the currently edited state before cycling through the
Fortnite catalog, wrap at both ends, and apply the selected appearance to the main-hand weapon through
the existing transmog transport. The Drawn/Sheathed editing tab is retained. Normal weapon transmog
ownership, compatibility and pricing rules apply; errors release the pending switch so it can be retried.

## Reviewed weapon types and rotation fitting

The owner's 11:24 weapon and 11:28 backpack exports supersede the earlier per-appearance defaults.
`weapon-default-fits.json` contains both states for the 60 retained weapons; supplied states are imported
exactly and missing states retain the prior family default. All 108 backpack defaults are imported,
requiring duplicate body rows to agree. Personal SavedVariables remain overrides of shipped defaults.

`weapon-policy.json` reserves 23 removed weapon IDs and assigns the reviewed dagger, axe, mace, sword,
polearm and fist classifications. Client Item.dbc and server item_template agree on subclass, inventory
type and sheath. Left Adamantium Claws uses off-hand-only inventory 22, right uses main-hand-only 21.
The fitter's Type button filters Previous/Next, which uses the selected source's equipment slot. Drawn
and sheathed defaults/reset are per appearance. Removed sources disappear from the fitter, client item
table, server templates and learned appearance rows; only matching active transmogs are cleared. Scoped
world/character SQL backups precede installation; stable IDs are never reused.

`/backpackfit` now has pitch, yaw and roll controls in degrees, included in live fitting, saved profiles
and text exports. Older profiles and native calls default rotations to zero. Rotation is a local child
transform after the established cloak bone mount, so it does not rotate the saved attachment position.
Duplicate player previews copy that local correction along with the attachment frame. Runtime catalog
parsing accepts both legacy scale/XYZ and optional three-angle defaults.

Star Wand's opaque, incorrectly body-textured glitter cone is removed and replaced by looping additive
sparkles; Sparklaxe also gains a native looping sparkle approximation. The base weapon remains rigid.
These are not original Fortnite Niagara effects: the particle/material definitions were not exported.
Vibe Axe uses a dedicated solid green texture on both surfaces. The stage, per-weapon animation checks,
readback receipt, SQL verification and installation backups are under
`G:/FortniteCosmetics/collection-revision-20261114`. Reclassification can change sheath sockets and requires
in-game review of the imported local fits on the newly selected weapon types.

## Global backpack rotation and pet facial repair

All 108 backpack defaults now assign X rotation -7.5 degrees and Z rotation +7.5 degrees. They are
absolute assignments, including already-correct rows; scale, translation and Y rotation are preserved.
`BackpackFitter.lua` applies a one-time SavedVariables migration so an older saved zero-angle profile
cannot hide the published correction. Subsequent local edits remain possible and are not reset.

Fox, Drift Fox, Hamster and Woodsy have shared-skeleton constant facial tracks that do not match their
mesh bind transforms. The repair restores neutral facial bone TRS and evaluates the exported pose-curve
deltas onto each pet's rig before sampling, including non-unit curve influences. This keeps facial
movement while avoiding the shared neutral jaw rotation and eyelid scale/position distortion. The
affected original three-clip idle cycles remain embedded. Existing accepted Wolf Pup art is untouched.

Woodsy's carved eye geometry is bound rigidly to its animated head, with a baked forward pupil
approximation instead of the unsupported procedural eye shader; it does not reproduce procedural eye
gaze/blinks. Source exports are unchanged. `inspect_faces.py` renders before/after samples for offline
visual review. `package_faces.py` replaces only the four pet assets, the rotation catalog and matching
addon files, preserving all unrelated weapon/backpack art and classifications. Receipts and backups
are under `G:/FortniteCosmetics/backpack-face-repair-20261114`. Native in-game acceptance is still distinct
from the offline render and emitted-key checks.

Rigid backpack mounts use the player's standard spine key bone (4) when present. The cloak geoset
continues to supply the bind-space position and width, but its upper-edge cloth weights no longer
select an independently swaying mount. Models without a spine key retain the validated cloak-weight
fallback. Published and personal scale/XYZ/rotation fits are unchanged, as are internal pet animations.

After the spine-mount fix, the owner reverted the global X/Z correction: all 108 default rows now use
X=0 and Z=0. Saved-profile migration version 2 clears matching old X=-7.5/Z=+7.5 values once, preserving
other custom angles and all scale/translation/Y-rotation fields. Later edits are left intact. The
current package is `G:/FortniteCosmetics/backpack-zero-rotation-20261114`; the rigid spine mount remains.

## Swords of Fire and Steel / Halo Energy Sword

Three one-handed sword appearances extend the retained weapon collection to 63: Swords of Fire and
Steel main hand (3430084, inventory 21), off hand (3430085, inventory 22), and Halo Energy Sword
(3430086, inventory 13). Their IDs remain reserved in `weapon-ids.json`. The source weapon meshes keep
their authored grip origins and are rotated to the common +X blade axis. Initial per-state fits use
the existing one-handed sword defaults; `/pickaxefit` can adjust all three independently and cycles
the off-hand source through equipment slot 16.

`elemental_swords.py` constructs 64-frame, two-second looping material atlases from the exported
BlessFlan fire-noise/mask and ClaimReflect flow-map/mask/emissive data. BlessFlan's exported glow
meshes carry the fire; their coordinates/UVs are fitted along the blade and the opacity fades at the
effect boundary. ClaimReflect's blade has an isolated planar UV projection, with blue energy color
derived from the exported emissive texture. The opaque hilt keeps its original diffuse UVs. Blade
plasma runs in a separate additive layer; base blade emission remains visible between pulses.

The M2 uses global-sequence texture translation keys, sampling an 8x8 atlas at 32 fps with step
interpolation. Additive unlit, double-sided effect materials and alpha-preserving BLP2 textures avoid
opaque shader volumes. The weapon itself stays rigid and inherits normal player sword attacks.
The matching exported `.ueanim` files were player/emote/prop rigs, not weapon-fire/plasma animations.
These material effects are reconstructed appearances; exact Fortnite shader parameters, particle
emission events and impact effects are not available in the exports and are not claimed reproduced.

`preview_elemental.py` renders the emitted surfaces at multiple effect phases for offline inspection.
`test_elemental.py` checks loop variation/seams, wrapping, alpha preservation and grip-axis transforms;
the encoder reparses every texture-animation table. `stage_elemental.py` preserves the installed
MPQ/registry, adds only the three item/display records, validates SQL twice against cloned tables and
grants the appearances to Zach's account. No server binary change is needed. Stages, rendered previews,
source/art hashes and installation backups are under `G:/FortniteCosmetics/elemental-swords-20261114`.
Live equipped fit, effect intensity/occlusion and sheathed appearance still require in-game acceptance.

## Collections tabs and emote sheathing

`/collections` and the existing `/cosmetics` entry point open the same six-tab window: Wings, Backpacks,
Emotes, Hearthstones, Weapons and Mounts. The new tabs share the existing incarnation-style rail,
searchable six-card model grid, ownership/favorite filters, page controls and draggable large preview.
DragonUI's existing Cosmetics microbutton now reads Collections. The Emotes panel remains embedded.

The Weapons catalog comes only from the retained native Fortnite allowlist (63 appearances), preserving
removed IDs and using each weapon's main/off-hand slot. Applying/removing reuses the normal transmog
transport, with its existing ownership, compatibility and pricing checks. Account collection ownership
and selected hand appearances arrive in the server's chunked ESTCOS snapshot.

Hearthstones includes the normal item and the four installed custom styles, including Dark Portal.
The generated model paths follow the effective spell visual/kit/attachment graph. Owned means carried
by this character in ordinary bags or digital inventory; use routes through secure item macros or the
existing digital-inventory item-use action, preserving item validation, cast time and cooldowns. It
does not grant the items or allow an unowned hearthstone to be cast as a free spell.

The Mounts catalog includes 3,408 registered mount spells from effective SkillLineAbility 777 and mount
learning items, filtered to mounted auras. It resolves live realm creature models through effective
client display/model records. 3,402 have model previews; six self-body or missing-model definitions fall
back to icons. Preview creature display binding preserves the mount's texture/skin. Account mounts
are shared by the existing login module; it now checks all three spell effects for mounted auras,
retaining its race/exclusion policy and avoiding sharing displayless self-mounts. The current learned
set is sent from the logged-in player after that sharing. Summon is a secure spell action and locked
mounts cannot be summoned. Dismount uses the stock command.

Native M3 mount previews must fit the ASECM3 binding's source bounds, not its `ErrorCube.mdx` host.
Both `EsteriaCosmeticsFit` and `RetailModelFit` query `PreviewModelBounds`, which advances the preview
binding and keeps the loading gate closed until real bounds are available. Ordinary M2 fitting is
unchanged. This covers all 578 M3 mount entries in the current catalog, in both cards and the stage.
Run `python Tools/CosmeticsPack/test_mount_preview.py` for the native delayed-bounds regression and
`python Addons/EsteriaCosmetics/test_cosmetics.py` for the collections UI checks. Live visual acceptance
is still required, including the Amber Nexus Battle Beast, Ancient Dark Nexus Hound and Azure Elemental Wolf.

Fortnite emotes sheath equipped weapons at start and while their shipped emote state remains active.
This server-side field change is visible to nearby players. Movement/stop clears the emote normally;
weapons remain sheathed until drawn through normal controls. Preview models use DressUpModel's sheathed
item mounting. No equipment is removed or appearances altered.

`Tools/CosmeticsPack/generate_collections.py` builds matching Lua/server allowlists from current data;
`package_collections.py` preserves every installed model, registry and item record. Stages, catalog
verification, archive hashes, prior compose configuration and server/client receipts are stored under
`G:/FortniteCosmetics/collections-20261114`. Lua tests cover locked mounts, exact creature display binding,
physical/digital hearth use, curated weapon application and emote preview sheathing. Server module
builds and package readback are separate from in-game visual/collection acceptance.

Hearthstone cards and the selected preview clone the player and loop the selected spell's precast
animation through `EsteriaCosmeticsHearthPreview`. Its effective SpellVisualKit supplies the hand,
body and ground effects plus every SpellVisualKitModelAttach prop, including authored offsets and
rotations. These preview-only attachments participate in the shared camera fit and loading gate.
Selecting another hearthstone clears the clone and restarts the preview; previewing never casts a
spell or uses an item. Clients without the native API retain the standalone effect previews.

Character-frame `SetSequence` and `SetSequenceTime` override the base Model methods to mark explicit
animation for that render update. Otherwise `CGCharacterModelBase::UpdateModel` resets emote/cast poses
to Stand before drawing. The emote clock and hearthstone clock keep the mark alive each frame; ordinary
idle reset resumes once callers stop driving a pose. `test_preview_playback.py` covers virtual dispatch,
emote frames, hearth casting and the retained idle reset using the production character-frame methods.

Dress-up frames for untransformed players with mapped Retail races duplicate the live character model
through `CGCharacterModelBase`, including its collection bindings and saved body form. Loading the
native display's CreatureModelData path instead creates a Wrath body without the custom emote sequences,
so the Emotes cards and stage silently stand idle. Transformed players, unmapped races and NPCs retain
the native dress-up path. The playback regression also executes this production model-selection method.

## Unified Collections tabs

Collections now hosts Pets, Incarnations and the full Manastorm Transmog panel alongside its existing
tabs. Incarnations keeps its category/search/apply protocol; `/incarnations` opens the embedded tab.
Transmog keeps its outfit, appearance, price and Apply handlers. Leaving its tab or closing Collections
respects the existing unsaved-work confirmation. Its entry points and the Transmog micro button route
to the embedded panel.

Pets uses a catalog generated from effective summon spells with SummonProperties slot 5, plus learned
companions reported by the client. The initial catalog has 237 spells and 234 resolved model paths;
other learned companions use the creature-cache preview. Only learned companions can be summoned.
Mounts and Pets support search, collected/uncollected and favorite filters; Mounts also has ground,
flying and aquatic type filtering. Their cards can be dragged to action bars.

`Companions.lua` retains DragonUI's companion/favorite/random-macro services while redirecting its old
journal entry points to Collections. Both tabs expose Random Favorite controls that reuse the existing
macro creation/drag path, including existing action-bar macros and DragonUI favorites. Cosmetics mount
favorites migrate into that shared service; subsequently toggling a favorite updates both stores.
Incarnation and DragonUI Pets & Mounts micro buttons are removed from the strip. The old journal stays
closed even if its module is re-enabled; its data services stay available.

`Tools/CosmeticsPack/package_unified_collections.py` packages only the changed UI files into a separate
overlay and records winning baselines, hashes and backups. Loose Incarnation addon files are backed up
and updated too. Roll back by removing the UnifiedCollections overlay and restoring the loose Lua file
from the package's `before/Interface/AddOns/EsteriaIncarnations/` directory. No DBC or server data changes
are installed by this UI package. The Lua behavior harness covers the new tabs, locked-pet actions,
favorite migration, random-action dragging, mount type filtering and transmog close guards.
Mount/pet Summon controls use the learned companion's API index through `CallCompanion`, with an
explicit spell-ID cast for an account-known mount that is not yet in that API list. The client's
name-based secure spell action cannot resolve numeric spell attributes. Reopening a preview also
reloads its model when native `HideThis` has discarded it, instead of keeping an empty cached card.
Live validation must use the newly mounted MPQ after a complete client exit: `/reload` retained the
older packaged Lua during the integration check, despite loose files with matching paths.

`Tools/FortniteCosmetics/port_collection.py` runs the shared animated encoder and rigid weapon converter.
`stage_collection.py` appends client item/display records and builds the installed-MPQ-preserving package,
tests the scoped world migration twice in a scratch schema, and installs the verified world data. The
existing server binary loads the expanded mappings on restart. Stages, per-pet animation checks, package
readback hashes and installation backups live under `G:/FortniteCosmetics/collection-20261114`.

Motion is preserved from the exported base idles, idle breaks and facial poses. Robo Kitty uses Cat base
idles plus its own idle break; its Unreal face-atlas shader animation is not reproduced. Vibe Axe lacks
exported material parameters/textures and currently uses neutral materials. Unreal reactive weapon VFX,
cloth physics and harvest sounds remain outside the port. The larger facial-animation bakes need live
performance and visual acceptance; structural checks do not establish every race/pose or material look.
