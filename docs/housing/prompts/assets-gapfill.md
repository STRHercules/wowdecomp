# Esteria Housing — Retail décor gap-filling (dev-only)

Read docs/housing/README.md and docs/housing/existing-assets.md first.
Only convert what Patch-Housing.MPQ lacks (likely: room kit — walls/floors/ceilings/doors — and
dyeable variants). Reuse whatever pipeline produced Patch-Housing.MPQ if it's findable in Tools/ or
git history; otherwise extend Tools/RetailAssets + the wow-retroport skill. Don't add new extractors
unless you show why the existing ones can't do it. Output to a gitignored cache, never into the repo
or the Live client.

- Pin one retail build + listfile + WoWDBDefs revision; record them in a manifest.
- Find housing décor via its DB2 tables (record how you found them).
- Convert up to 10 missing items, prioritizing: wall piece, floor, ceiling, doorway, one dyeable item.
- Per item: manifest row (FileDataID, deps, hash, status, losses: shaders/dyes/anim), a separate patch
  MPQ for G:\3.3.5a - Dev only (don't modify Patch-Housing.MPQ), and a load check in the dev client
  (I'll run it; tell me what to look for).
- Update the Status table in docs/housing/README.md.
- Report which items load, which fail and why, and whether dyes can work on 3.3.5 shaders.
