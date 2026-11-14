# Backpack icons

`stage_icons.py --out <new external stage>` clones the active installed Cosmetics registry and updates only
ItemDisplayInfo inventory-icon field 5 for matched catalog displays. Source cloaks and learning tokens share
that display, so both item kinds use the matching icon. Item IDs, display IDs and all unrelated DBC data stay
unchanged. The new archive includes alpha-preserving icon BLPs, generated addon paths, current UI code and the
normal transmog addon override.

`backpack_icons.py` uses unique normalized mesh/package names plus explicit gender/style aliases. It does not
use fuzzy scoring to choose an icon. `icons.json` in each external stage records input files, match reason,
source hashes, BLP hashes, stable IDs and missing icons. Original source images remain immutable.

Current supplied folder coverage: 97 of 99 backpack models. No matching icon was identified for original
`SK_F_MED_Heist_Backpack` (burglar bag) or `Backpack_Male_Commando_Heist_Briefcase` (silver cuff case). These retain
the previous generic cape icon and are explicitly reported as missing. `T-Icon-Backpacks-235-Heist.png` depicts
a different green llama charm and is deliberately not used for either model.

To refresh code in a completed icon stage, use `stage.py --repack-from <icon stage> --out <new stage>`.
That preserves the icon registry and stable IDs while packing the latest addon sources. Installation is
parent-owned and requires closed client processes plus the receipt's baseline configuration/registry hashes.

Run `python -m unittest discover -s Tools/CosmeticsPack -p 'test_*.py'` for exact-variant matching,
alpha/mipmap preservation, DBC field isolation, stable IDs and additive table checks.
