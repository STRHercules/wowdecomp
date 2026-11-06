# Esteria Housing — Phase 0: Recon & Design (no code changes)

You are the engine/server engineer for Esteria (AzerothCore 3.3.5a + wowdecomp client, build 12340).
Goal of THIS session: produce design docs for a retail-style player-housing system.
Do not modify code, DBs, MPQs, or game installs.

## Environment (verified, don't re-discover)
- Client source: R:\Users\Zach\Documents\GitHub\wowdecomp (branch Esteria)
- Server source: <path to EsteriaWoW checkout>, build: <how you build worldserver>
- DBs: <host/port>, read-only access via <method>. Do not write.
- Dev client: G:\3.3.5a - Dev. Never touch G:\3.3.5a - Live.
- Existing housing assets: G:\3.3.5a - Dev\Data\Patch-Housing.MPQ (furniture/decoration models).
  Origin: <converted retail / custom / mixed / unknown>. Current load path: <none / GO entries / DBC rows / unknown>.
- Existing tools to reuse: Tools/RetailAssets (CASC/CDN fetch, DBC r/w), Tools/MountStudio,
  wow-retroport skill (Kirazul/RetroPorter/wotlkconv), HD render paths (HdMapObj, Lumenveil).
- Write all output to docs/housing/ in the client repo. Read docs/housing/README.md first.

## Decisions already made
- Phase 1 décor comes from Patch-Housing.MPQ (items confirmed loadable). Stock models only fill gaps.
- Retail-origin assets are dev-only until I decide on distribution. List them; don't solve it.
- Server logic lives in a new module mod-esteria-housing unless you find an existing property system.

## Deliverables (docs/housing/)
1. existing-assets.md — inventory Patch-Housing.MPQ read-only (list, don't extract into the repo):
   model path, textures, .skin present, textures resolve (in this MPQ or base data), origin
   (retail/custom/stock/unknown), category (Furniture/Accents/Lighting/Nature/Structural/Misc),
   and any existing GO/DBC entries pointing at it. Flag broken or unclear-origin items.
2. parity.md — retail housing features: shipped vs announced. Fetch each Blizzard URL below; quote
   the date; mark unreachable ones UNVERIFIED. Columns: feature / MVP / later / engine change / blocker.
   https://worldofwarcraft.blizzard.com/en-us/news/24186690
   https://worldofwarcraft.blizzard.com/en-us/news/24221516
   https://worldofwarcraft.blizzard.com/en-us/news/24244457
   https://worldofwarcraft.blizzard.com/en-us/news/24244464
   https://worldofwarcraft.blizzard.com/en-us/news/24244406
   https://worldofwarcraft.blizzard.com/en-us/news/24295382
   https://worldofwarcraft.blizzard.com/en-us/news/24293281
   https://worldofwarcraft.blizzard.com/en-us/news/24298591
3. feasibility.md — answer from code, with file:line evidence:
   - Can stock GameObjects carry per-object quaternion, scale, dense per-owner placement, collision?
     (Check both server GO spawn/update and client CGameObject render path.)
   - Client: where are M2 instancing, scene picking/raycast, Lua<->C++ bridge, custom packet handling?
   - Server: instancing/phasing options for a private interior map.
   Compare A (stock GO + instance), B (custom client prop layer + custom opcode), hybrid. Recommend one.
4. protocol.md — capability handshake, opcodes (pick unused ones, prove unused), request/ack/seq,
   error codes. Server never trusts client IDs, quantities, transforms, or permissions.
5. schema.sql (draft only, not applied) — house, room, placed_decor, decor_catalog, account_decor,
   permission, revision. Follow the server repo's existing migration conventions.
6. plan.md — milestones with acceptance tests, split into: tests you can run (build, unit, SQL,
   packet harness) vs tests I must run in-game (with the logs you need from me).

## Rules
- Cite file:line for every engine claim. If you can't find it, say "not found" — don't guess.
- Stop and report if recon exceeds what fits in this session; list what's left.
- Update the Status table in docs/housing/README.md.
- End with: what's known, what's unknown, and the questions blocking Phase 1.
