# Animated Wolf Pup

`export_blender.py` opens the supplied Remus assembly without saving it and samples the original Idle and
IdleBreak_02 actions, including their facial shape keys. Constant facial translations are retargeted to the
Wolf's bind pose; the shared Dog skeleton has different jaw/tongue bind positions. Forepaw contact in the neutral
idle determines the dog's seating alignment relative to the carrier opening.

The sampled skeletal skin transforms use authored joint pivots. Facial shape deformation uses vertex helper
bones with their own bind-vertex pivots, preserving facial positions without a runtime morph implementation.
Sections are split into palettes of at most 75 bones. The existing shader-skinning path draws those palettes.

`build_model.py` joins both original clips into one repeating idle of about 13.27 seconds, with 250 ms transitions.
It makes quaternion keys hemisphere-continuous and subdivides fast rotations for the client's approximate
per-key quaternion interpolation. Source keys remain present. These details matter: zero-origin joint pivots
and equivalent +/- rotation-key jumps can match authored frames while stretching at times between them.

`verify_animation.py` reparses emitted keys using the client's uint16 quaternion decoding and compares them with
the sampled source. `test_animation.py` checks sign continuity, adaptive rotational sampling, retention of both
source clips and looping transitions. The repair receipt also records jaw motion and interpolation checks.

`integrate.py` performs the initial additive item/catalog installation. `update_art.py` stages subsequent art
repairs from the actually installed MPQ and proves unrelated entries are retained byte-for-byte. Item IDs are
3400100 (cloak appearance), 3410100 (learning token); existing `/backpackfit` profiles remain applicable.

Current repair art: `G:/FortniteCosmetics/wolf-motion-repair-v2`; paired weapon repair:
`G:/FortniteCosmetics/rainbow-orientation-v2`; installed package/backup receipt:
`G:/FortniteCosmetics/wolf-rainbow-repair-package-v1/installed.json`.

`repair_normals.py` follows the motion bake with consistent area-weighted smooth bind and posed normals. It
replaces facial helper normal rotations without changing geometry, skin weights, pivots or translation tracks.
This avoids mixing static split-corner normals with evaluated smooth facial normals across helper boundaries.
Latest normal repair: `G:/FortniteCosmetics/wolf-shading-repair`; package and client update:
`G:/FortniteCosmetics/pickaxe-fitter-shading-v1`.

Material response is lit diffuse; Unreal normal/specular effects and cloth simulation are not reproduced.
Fresh-client seating, a full jaw/idle cycle and Rainbow Smash's drawn/sheath orientation remain visual gates.
