# Native Retail content imports for Esteria

Status: additive weapon and mount backends are implemented, with original-byte
acquisition, persistent IDs, supplemental client records and scoped server installation.
See the [weapon workflow](retail-import-workflow.md), [native mount integration](retail-native-mounts.md)
and [independent Retail/Forever acquisition and caches](cdn-downloads.md).
Weapons use explicit resolved file IDs and authored test gameplay metadata; mounts
use resolved, build-pinned recipes and target-server donor spells. Automatic human-name
item/set discovery, a general armor-set importer and full native format fidelity
remain future work. Implementation and installed/static verification do not establish
in-game acceptance.
The user accepted the native hand attachment proof in character selection on 2026-10-17.

## Intended contract

A request such as "add the latest warrior tier sets" or "add Retail mount X" should
produce new, independently usable Esteria content, with its complete client asset
graph and matching server definitions. These examples specify the desired workflow;
they are not instructions to import a particular asset in this design change.

- Preserve Blizzard's downloaded asset bytes. Do not export a converted M2, skin,
  skeleton, animation, texture or effect, and do not package a legacy substitute.
- Implement missing format/rendering behavior in the client. Unsupported assets
  must produce a concrete capability report, not silently fall back to conversion.
- Allocate new target IDs. Preserve existing items, displays, spells and creatures
  unless the request explicitly identifies an intended replacement.
- Produce real server items and mount definitions, with client records that agree
  with them. A display override alone does not meet the contract.
- Pin and record the source product/build and resolved variants. Repeating the
  same import must not allocate duplicate content or change an installed build.
- Keep downloaded game data, keys, database credentials and caches outside Git.

"Unmodified assets" does not mean identical Retail and Esteria database records.
New Esteria records necessarily contain target IDs and target gameplay definitions.
Retail gameplay systems cannot be assumed to exist on the Esteria server.

## Request to installed content

1. Resolve the human request against source metadata for a pinned Retail build.
   Resolve item/set/mount identities, names, variants, appearances and dependencies.
   "Latest" is resolved at import time and then recorded as an explicit result.
   FileDataIDs and filenames alone are not an item or set catalog. Ambiguous scope
   such as recolors or difficulty variants must be made explicit in the import plan.
2. Build a dependency graph from the matching metadata and asset chunks. Include
   model/skin profiles, skeletons, animations, textures, icons and effects; include
   armor components, texture layers and race/sex choices when the appearance needs
   them. Record dependency roles instead of treating every model as a hand item.
3. Acquire and verify the original files through CASC. Preserve the encoded cache
   and the original decoded asset bytes as applicable, with their source identifiers
   and integrity information. Never write runtime-adapted buffers into this store.
4. Check the graph against native client capabilities before deploying it. Name
   missing features precisely and retain the acquired inputs for implementation.
5. Reserve target IDs against the actual current client and server baselines. Create
   the target records and a manifest linking those records to the original graph.
6. Stage a matched client/server pack, validate its references and install it with
   a receipt. Verify the installed records and perform the appropriate live checks.

## Client storage and identity

The implemented importer layout beneath the development client's Data directory is:

```text
Data/Retail/
  allocations.sqlite
  installed.json
  Assets/<product>-<build>/     # original decoded assets for offline packs
  Packs/<pack-name>/            # manifest.json, receipt.json, originals, tables, SQL
  Registries/<revision>/        # merged supplemental rows, equipment and aliases
  RetailCasc.ini               # mode=files for imported packs
```

The storage design must preserve CascLib's expected cache layout and its ancestor
manifest rules; copying an arbitrary collection of files into Data is insufficient.
Installation paths are configurable and must not be embedded in a distributable pack.

The installer verifies pack manifests and publishes a registry/configuration; the
client reads that configuration, not each pack manifest. Original asset references
must resolve within the pinned build: a naked FileDataID is not a globally immutable
identity across builds. Retail and Forever now have independent source instances,
asset namespaces and product/build/locale-qualified durable caches. Each runtime
source is configured once and requires a restart to change builds. The importer
registry still accepts only one product/build across its installed packs; arbitrary
simultaneous builds within a source remain unsupported. See [cache storage and packed
distribution](cdn-downloads.md) for the online/local acquisition layout and workers.

Packs share downloaded dependencies when their build/content identity agrees.
They must be complete for offline use after acquisition, rather than depending on
a successful network request during equipping or mounting. Missing files are
reported before installation. Acquisition may use the CDN; normal gameplay should
read the verified local cache through the same native loader.

## Target records and server integration

The importer needs a persistent allocation ledger keyed by source build, source
record, variant and target namespace. Scan the effective client tables, current
server records and prior allocations before reserving anything. Use uniqueness
constraints or equivalent reservation checks, and retain IDs across reruns.
Never treat a Retail item ID, display ID or FileDataID as a free Esteria ID.

| Content | New target definitions |
| --- | --- |
| Item/weapon/armor | Server item entry, client item record, appearance/display record and native asset manifest links |
| Armor set | Independent item entries for its pieces, appropriate set metadata/gameplay definitions, and complete body/attachment appearance data |
| Mount | Creature/model/display definitions and a usable summon/learn path with matching client spell data and server behavior; optional acquisition item |

Generate records for the detected Esteria server schema and runtime, not a guessed
generic server. The current tools use the supplied Esteria source checkout, scan
client providers and running server tables, and snapshot the actual database schema.
They generate guarded additive SQL; mounts also publish separate DBC continuations.
This adapter targets the current Esteria Docker deployment, not arbitrary servers.

The native supplemental registry is implemented: the normal file/database loading
path merges supported WDBC additions in memory, preserving existing rows and string
data and rejecting conflicting new IDs or invalid new strings. Weapon packs add Item
and ItemDisplayInfo; mounts also add CreatureModelData, CreatureDisplayInfo, Spell,
SpellIcon and SkillLineAbility. Server item queries, inventory/equipment, tooltips,
inspection and appearance lookups must agree on the new item/display identity;
registration alone does not prove those live behaviors.

Importing the original visual does not import Retail combat or progression code.
Item stats, set bonuses, procs, mount movement and requirements need an explicit
Esteria gameplay policy. Preserve source metadata for provenance; translate only
supported gameplay semantics and report any intentional difference. Do not present
an ordinary Esteria spell as an exact Retail mechanic without implementing it.

## Native client work still required

The current native loader reads original CASC data but prepares a legacy-compatible
buffer in memory, including model version/layout, blend and material adjustments.
It emits no converted art file. It does not yet implement a general native Retail
model/appearance pipeline or satisfy literal "no modification anywhere".

The long-term loader should parse the original supported formats into explicit
runtime structures, retaining their semantics and leaving source data immutable.
Extend the renderer/animation systems when the original features require it.

- Extend the implemented standalone item and creature/mount paths as new graphs require.
  Supplemental item/display registration and mount learning/summon integration exist.
- Shared SKELs and their SKPD parents, model-only AFM2 animations and split
  AFM2/AFSB/AFSA track payloads are supported in acquisition and runtime preparation.
  Continue closing material/effect gaps: some Retail features are approximated or
  omitted, so successful parsing is not a claim of full visual fidelity.
- Complete a general armor-set import path covering texture composition, geosets,
  race/sex variants, attached geometry and visibility on Esteria's actual playable bodies.
- Build on the implemented source/cache isolation, background acquisition workers
  and registry mounting to support explicit pack revisions and multiple simultaneous
  builds within a source.

Armor acceptance must use the existing custom race/body and appearance contracts.
Do not truncate custom race masks or substitute a stock body silently. Exact Retail
fit on a custom body is not guaranteed by downloading the original armor files.

## Deployment and acceptance

A pack contains original assets or verified cache references, its complete manifest,
the generated target records, schema-specific server migrations, validation results
and an installation receipt. Validate dependency closure, target-ID uniqueness and
client/server reference agreement before installation. Installing the same pack
again must be idempotent. Updates to a newer source build are explicit pack revisions.

Rollback removes only additions owned by that pack. Account for already-created
player items and learned mounts; do not blindly delete player data or recycle IDs.
Keep acquisition, registration, installed state and live acceptance separate.

For an item, acceptance includes obtaining the new item entry, tooltip/query data,
equipping and re-equipping, movement, combat/attachment behavior where applicable,
and another correctly provisioned client seeing the same item. Armor additionally
needs its supported race/sex and layering checks. Mounts need learning/summoning,
rider attachment, movement, relevant flying/swimming behavior and dismounting.
The accepted sword screenshot establishes character-selection hand rendering only.

## Implementation sequence

1. Implemented: manifest/ledger and resolved-file weapon imports as additive
   client/server items. Automatic human-name item/set metadata resolution is pending.
2. Implemented: resolved mount recipes with original asset graphs and server
   summon/learn integration. Complete live acceptance per pack; the mount guide records
   learning and mounted-world rendering evidence separately from pending movement,
   flight, persistence and renderer-defect checks.
3. Add the armor appearance resolver and renderer features required for a complete
   selected tier set on the target bodies.
4. Expand named requests to complete sets/variant groups, repeatable pack updates
   and client distribution using the same validated import mechanism.

The converter workflows in the retroport skill are not the backend for this native
mode. Their target-baseline, identity and validation safeguards remain relevant.
