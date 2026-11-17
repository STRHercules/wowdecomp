Observed on WowCollectionsPlaybackFix.exe with the initial UnifiedCollections overlay:

- All nine tabs fit the header. Incarnations and full Manastorm Transmog render within Collections.
- Pets shows 6 learned of 237 catalog entries, model cards and stage, ownership filters, favorite control and Random Favorite Pet.
- Incarnations shows existing active look, categories, search and Apply controls.
- Transmog shows the current character, saved outfit, slots, appearance catalog and Apply controls. No paid appearance transaction was submitted.
- Incarnation and the legacy DragonUI Pets & Mounts buttons are absent from the micro strip.
- Random Favorite Pet puts macro 6 on the cursor; the cursor was cleared without replacing any action-bar slot.
- CallCompanion("CRITTER",1) summoned the green Corrupted Kitten, later visible behind the player after UI reload.
- Found numeric secure spell binding did not summon a selected pet. Replaced with an exact learned-companion CallCompanion macro; account-known mount fallback casts its explicit spell ID.
- Found hidden cards cleared by the engine stayed blank after returning to Pets. Fixed the same-row fast path to require an actual model filename.
- Expanded Transmog to fill its available content area and handled button-object arguments from the Cosmetics micro button.
- Loose-file refinements did not override the mounted addon Lua after /reload: action binding still printed "spell nil". Temporary loose files were removed after the user exited.
- Final overlay installed and read back: state/unified-collections-final-20261115-042136/receipt.json. SHA256 4aba04c27a91e3f14e9e3b41a2d828deaecd8b2200c7a9ebaa2aea52bff043ff.
- Final native-companion bindings, tab-return behavior and expanded Transmog await post-restart visual acceptance. Lua 5.1 behavior tests and packaged-source compilation pass.

Post-restart check of the final installed overlay:

- Black Tabby Cat Summon button changed the stage state to Summoned and enabled Dismiss. Dismiss returned it to Collected.
- The Kart mount Summon changed its state to Summoned and enabled Dismount; Dismount cleared it.
- Ground and Flying filters changed shown counts to 3185 and 221, from 3408 all entries. These counts reflect the existing classification metadata, not an audit of every mount's flight capabilities.
- Returning from Mounts to Pets retained/reloaded all six model cards and the stage. No blank cached cards remained.
- Transmog now fills the available tab content. Appearance grid, equipped model, slots, saved outfit and Apply controls render within the shell.
- No paid transmog operation was submitted. No exhaustive model, mount classification or every-race check was performed.
