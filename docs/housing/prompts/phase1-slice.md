# Esteria Housing — Phase 1: Vertical slice

Read docs/housing/README.md and every Phase 0 doc in docs/housing/ first; follow its recommended
architecture and protocol. Same environment and rules as Phase 0 (prompts/phase0-recon.md).
Code changes allowed in client + server repos on a new branch `housing-slice` in each.
DB changes only to the `housing_dev_*` clones (create them per the README's "Decisions after Phase 0"; never
`acore_*`), after showing me the SQL. Run the slice on a separate stage worldserver container, not `ac-worldserver`. Back up any G:\3.3.5a - Dev file before replacing it.

## Scope (nothing more)
- One instanced interior (reuse an existing WMO) with entry portal + exit.
- One house per account, claimed via NPC/gossip.
- 20 décor items picked from docs/housing/existing-assets.md (loadable, spread across categories,
  include at least one rug, wall-hung item, light, and a table with something placed on it).
  decor_catalog rows reference FileDataIDs; GameObjectDisplayInfo rows use `retail\<FDID>.m2` (CDN source).
  Start with the 20 items proposed in existing-assets.md; probe each with RetailAssetProbe and swap failures.
- Visitors enter the owner's instance through a small, flag-guarded core hook in MapInstanced (README decision 5).
- Place / move (XY + Z offset) / rotate (15° snap) / remove, server-validated and acknowledged.
- Persist across logout + server restart; a second character sees the same layout.
- Simple Lua panel: catalog list, place button, remove button, error toasts. No polish.
- Feature flag (worldserver.conf + client CVar), default off.

## Out of scope
Gizmos, scale, dyes, rooms, neighborhoods, blueprints, launcher packaging, new asset conversion.

## Done means
- Both projects build; show the commands and output.
- Automated: SQL migration applies + rolls back; placement validation unit tests
  (bad IDs, out-of-bounds, over-budget, non-owner, duplicate seq).
- Manual checklist for me: owner + visitor clients, relog, restart, rapid placement.
- Update the Status table in docs/housing/README.md.
- Report: files changed, flags, rollback steps, and what isn't working.
