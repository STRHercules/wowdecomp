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
The deployed server image is `esteria-coa:20261114c-cosmetics`; its rollback base is
`esteria-coa:20261114b-emoteui`. Server packaging evidence is under `G:/FortniteCosmetics/server-runtime-20261114`.

Focused verification includes conversion/multimaterial/texture tests, stable-ID and existing-DBC preservation
tests, C++ cloak-anchor/catalog assertions, Lua tabs/realm-sync/emote regression tests and the server token
commit-policy harness. Offline checks do not establish every race/pose or all 99 final material looks.

## Rollback

The installation receipt records the previous client config/executable/archive and server image. Close clients
before restoring their matched files. The previous registry remains intact. Keep appearance collection rows and
items until an explicit saved-data migration is agreed; do not delete learned appearances or issued tokens to
roll back the renderer. The earlier local Journey shield trial remains separately available.
