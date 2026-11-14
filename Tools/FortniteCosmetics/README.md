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
