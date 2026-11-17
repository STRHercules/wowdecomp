# Fortnite backpack art conversion

`convert_backpacks.py` discovers every `.uemodel` whose export path contains `backpack`, including the
astronaut mesh outside the two accessory roots. It does not write to the supplied exports.

```powershell
rtk proxy python -m unittest discover -s Tools/FortniteCosmetics -p test_convert_backpacks.py -v
rtk proxy python Tools/FortniteCosmetics/convert_backpacks.py --audit-only --out G:/FortniteCosmetics/new-audit
rtk proxy python Tools/FortniteCosmetics/convert_backpacks.py --out G:/FortniteCosmetics/new-stage
```

Outputs live outside Git. Each run requires a new output directory. `catalog.json` records every input,
stable path-derived slug, source hash, material surfaces, referenced texture, dimensions, conversion
losses and any blocked asset. `patch-root` contains namespaced M2/SKIN/BLP files; the art-only
`Patch-ZzzzzzzzzzzFortniteBackpacks.MPQ` is read back byte-for-byte through StormLib. `receipt.json`
contains the archive and per-entry hashes. No installer, SQL, DBC or live client is touched.

Model vertices are already in skeletal reference pose. They are rigidly frozen there, preserving all
material triangle ranges. The output is MD20 version 264, with explicit opacity data and one rigid bone.
UE coordinates become WoW coordinates as `(Y,X,Z)/100`, centered laterally with the top at Z=0 and
the body-facing surface at X=0. Existing accepted cloak fitting owns the final position and width scale.

Texture resolution first follows the actual mesh-to-texture sequences recorded by FortnitePorting's
export log. Those records resolve shared body/prop/weapon textures that are absent from the mesh's
own folder. An unambiguous source package-family lookup is the fallback. Every selected texture and
its log provenance are recorded. Missing or ambiguous dependencies block that asset rather than
silently substituting a placeholder. Base colour atlases use opaque BC1 BLP2 with a complete mip chain.

The batch is a lit diffuse approximation matching the accepted trial. Source normal/specular/emissive
responses, physics, animation, glass, outlines and vertex-crunch shader behavior are not reproduced.
Special shader surfaces retain their geometry and package diffuse with a per-asset warning; inspect
their appearance in the native renderer before visual acceptance. Existing texture skin variants do
not become separate collectible appearances in this base-mesh batch.

Current inventory: 99 source meshes, all structurally convertible. Native/in-world acceptance is a
separate gate from these parser, texture, dependency and MPQ checks.

## Animated pets and two-handed axes

`port_collection.py` extends the accepted Wolf Pup/Rainbow Smash tools, rather than freezing pet rigs.
The November 14 collection adds eight pets (Bonesy, Cat, Woodsy, Dragon, Fox, Drift Fox, Hamster and Robo
Kitty) and 82 weapons. Wolf Pup and Rainbow Smash retain their installed art and IDs. Separately exported
left/right or combined weapon meshes remain distinct appearances. Every weapon uses the two-handed axe
item class, inventory type and sheath, regardless of its original Fortnite weapon type.

```powershell
rtk proxy python Tools/FortniteCosmetics/port_collection.py prepare
rtk proxy python Tools/FortniteCosmetics/port_collection.py weapons
rtk proxy python Tools/FortniteCosmetics/port_collection.py pet --pet cat
rtk proxy python Tools/FortniteCosmetics/stage_collection.py stage
rtk proxy python Tools/FortniteCosmetics/stage_collection.py verify-sql
```

Use `--pet` for each prepared pet. Outputs are in `G:/FortniteCosmetics/collection-20261114`.
Each pet has its actual carrier, a looping sequence of original base idles and idle breaks, and exported
facial poses where available. Robo Kitty uses Cat base idles plus its own idle break. Morphs, skeletal
shear and source weights beyond M2's four influences use evaluated vertex helpers, with authored pivots,
unit/sign-continuous quaternion keys and palettes of at most 75 bones. Source/sample and emitted-key
checks run per pet. The dense facial bakes are larger than ordinary skeletal models; performance in a
crowded live scene still requires acceptance.

The generated `PickaxeCatalog.h` and `EsteriaPickaxesData.lua` make fitting resolve the equipped weapon's
own display ID. `weapon-ids.json` preserves weapon assignments across runs. Pets extend the existing stable
backpack manifest and receive cloak source/learning items. The approved family fitting values are applied
unchanged, with existing personal overrides retained. All pet models opt out of the incompatible exterior
shadow and wet-sheen passes used by the accepted Wolf Pup repair.

Integration starts with the current installed MPQ and registry, proves unrelated entries/records remain
unchanged, and reads back every packed entry. World SQL adds only the new item IDs and pet mappings, and
is tested twice against cloned scratch tables before installation. `install-server` applies those verified
rows and grants their appearances to Zach's account; restart the existing worldserver afterward to reload
the catalog/cache. No server C++ change or rebuild is needed for this batch.

Known material limits: Unreal reactive effects, physics, harvesting sounds and Robo Kitty's animated face
atlas shader are not reproduced. Neon/Vibe Axe has no exported textures or material parameters and is
explicitly included with neutral black/white materials; its real colors/effects require those source
parameters. Other selected source textures are recorded in each spec/catalog. Optional cached metadata
from `https://fortnite-api.com/v2/cosmetics/br/search/all?type=pickaxe` supplies exact matched display names;
source mesh names remain the fallback. Additional animation families without exported pet meshes are
not fabricated into models. In-game seating, previews and drawn/sheathed fitting remain visual gates.
