# Esteria Housing — Phase 2 recon: retail housing maps and room WMOs, loaded natively

Read docs/housing/README.md and every Phase 0/1 doc in docs/housing/ first. Same environment and rules as Phase 0
(prompts/phase0-recon.md) and Phase 1 (the stage realm, `housing_dev_*`, the `EsteriaWoW-housing` worktree).
This is a recon phase. No gameplay code, and no DB writes. Throwaway probes are fine: small probe tools, a test
console command behind a CVar, files in the dev cache. Back up any `G:\3.3.5a - Dev` file before replacing it.

## Goal

Replace the Stormwind Vault stand-in with what retail housing actually uses:
- the neighborhood map(s);
- the house interior, built from retail room WMOs (walls, floors, doorways, hallways, stairs).

## Fixed decision

**Load retail assets natively in this client. Don't retroport terrain or WMOs to 3.3.5 formats.**
- The client already loads retail M2s by FileDataID through the Retail source (`Engine/Source/Retail/CascSource.cpp`).
  WMO/ADT/WDT should use or extend that same path.
- `docs/retail-casc.md:211` says "WMO/ADT: Not implemented" in that source. Check whether that is still true, and look
  for any other native path: the HD/Forever map paths, commit `5714f6de` (newer-WMO shader clamp), and patch-L's world
  WMOs.
- Conversion (wotlkconv) is a last resort. Propose it only if native loading is shown to be impractical, with evidence,
  and get my OK first.

## Questions to answer (cite file:line, or say "not found")

1. **Retail housing maps.**
   - Which Map IDs are the neighborhoods and the house interior(s)? Use the pinned 12.1.5 build's Map.db2 and the
     housing DB2s already inventoried (`house`, `houseroom`, `houseexteriorwmodata`, `houseleveldata`, `housetheme`).
   - For each map: its WDT/ADT FileDataIDs, whether the WDT is WMO-only or terrain, and its size.
   - Is the interior a fixed map, or an empty or void map that rooms are assembled into at runtime?
2. **How retail assembles a house.**
   - Decode HouseRoom and related tables well enough to say: which WMO each room uses, how rooms connect (doorway
     sockets or attachment points), and how exterior WMOs are chosen.
   - Get WoWDBDefs `.dbd` files for these tables rather than guessing columns.
3. **Native loading today.**
   - Can the client open a retail WDT/ADT (split `_obj0`/`_tex0`/`_lod` files, MAID FileDataID chunks) and a retail
     WMO root and its groups (MOHD v17+, FileDataID-based MODI/MOTX, newer group chunks)?
   - For each format, give the exact entry point, what parses, and what's missing.
   - Probe at least one housing room WMO and one neighborhood ADT tile through `RetailAssetProbe`, or a small
     extension of it.
4. **Showing a room WMO in game.** Two routes are possible:
   - (a) as a map object placed in an empty map;
   - (b) as a GameObject whose display is a `.wmo` (3.3.5 does this for some GOs; check `GameObject_C.cpp` and
     `CMapObj`).
   For each route: collision on the client, and what the server needs (vmaps can't read retail WMOs, so is
   movement-only collision acceptable?).
5. **Server side.**
   - What a retail map needs on AzerothCore: Map.dbc/`map_dbc`, `instance_template`, server maps/vmaps/mmaps, and
     whether the extractors can read retail files.
   - Can the server run a map with no extracted terrain? A WMO-only map like map 35 already works without `.map`
     files.
6. **Plan.**
   - Effort and blockers for: neighborhood map, interior assembly, room-kit placement.
   - A recommended order, with the smallest first build step that gets you standing in a real retail housing space.

## Out of scope

Gameplay code, plots and permissions, new editor features, and DB changes.

## Done means

- `docs/housing/maps-recon.md` answers each question above with evidence. Separate what was inspected, what was
  probed, and what was seen in game.
- Every probe tool or command is listed with its exact invocation and real output.
- An in-game checklist for me, if a test console command was added (behind a CVar, default off).
- The Status table in docs/housing/README.md is updated.
- The report ends with what works, what doesn't, files changed, rollback steps, and the next build prompt's scope.
