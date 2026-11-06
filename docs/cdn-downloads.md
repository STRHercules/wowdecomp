# Independent Retail and Forever acquisition

The client has two independent sources. `retail/` uses `WOW_RETAIL_CONFIG` /
`RetailCasc.ini`; `forever/` uses `WOW_FOREVER_CONFIG` / `ForeverCasc.ini`.
Each owns its CASC handle, mutex, aliases, dependencies, DB2 tables and load flags.
The same FileDataID in two products cannot share an asset entry. Prepared model
textures and animations retain their source namespace through SFile and Model2.

Forever is opt-in: set its environment variable or install
`Data/Retail/ForeverCasc.ini` beside the executable (`Data/Forever` remains a
fallback for older installations). This enables the existing
target races **52 -> Forever 95** and **53 -> Forever 96** (Skyborne). An optional
`WOW_FOREVER_RACES=52:95,53:96` overrides this mapping; an empty value disables it.
Retail's mapping/config remains independent. This provides the existing native
race model/customization pipeline for Skyborne; it does not add server classes,
class spells or unlock rules. Existing packed appearance codecs remain unchanged.

Creator choices are filtered separately from the raw DB2 catalog and saved appearance
indexes. Forever Skyborne ChrModels 218/219 exclude their inherited Blood Elf entries
under source requirement 12, retaining the playable Skyborne choices and authored
accessory requirements. Both bodies expose 10 playable faces and 9 skin palettes.
Internal Transmog placeholders (source requirement 10, including unnamed Forever
copies) are excluded from every race's creator. Dropdowns, arrows, preview, defaults
and randomization share the same mapping back to the original choice indexes.
Existing characters and raw source/saved IDs are not remapped by this UI policy.

Retail/Forever characters now have an independent **Eye Lightning: Off / On**
creator option when the loaded model contains effect mesh 1701. Off is the default,
including existing characters. Randomization keeps this explicit selection; hover
previews restore it on cancel. The final geometry pass clears inherited group-17
effects after stock and Death Knight selectors, then enables exactly 1701 when
requested. Normal eyes (group 33), eyesight/blindness (group 51), and helmet hides
remain independent. This includes Night Elves and Skyborne. Cached originals and
the order/meaning of every existing appearance byte are unchanged.

The matched server persists the toggle in bit 63 of the existing unsigned
`characters.extraAppearance`. The lower appearance payload remains intact, including
Haranir; no schema migration is required. An optional five-byte `EYE1` footer carries
Off/On after the normal creation/customization payload (after `HRC1` on Haranir
creation). Absent legacy footers preserve the old contract; malformed/duplicate
footers are rejected. Character enumeration reuses HXE2 for flagged characters;
live/nearby-player state uses bit 31 of the already-public Object padding high word.
The native decoder removes that flag before decoding the frozen profile bytes.
Client/server copies of `EyeEffects.h` must match. Deploy the matching server before
installing this native client; client-only installation cannot promise saved choices.

Copy [ForeverCasc.example.ini](../config/ForeverCasc.example.ini) into the Wrath
client's `Data/Retail` folder. The relative paths keep both products' acquired
assets inside that game folder:

```text
Data/Retail/
  RetailCasc.ini
  ForeverCasc.ini
  Cache/Retail/<product-build>/           # Retail encoded CASC + decoded originals
  Cache/Forever/<product-build>/          # Forever encoded CASC
  Cache/ForeverDecoded/<product-build>/   # Forever decoded originals
```

Each source has its own build directory and lock. Moving the game folder preserves
these caches; configs and launchers should use relative paths. When relocating an
existing cache, move its complete storage and decoded directories with the client
and downloaders closed, then update both config paths before restarting. Imported
offline packs under `Data/Retail/Assets` and their supplemental registries remain
persistent too. Do not nest these caches inside a modern CASC installation with
an ancestor `.build.info`, `.build.db`, `.build.config` or `versions` manifest;
the MPQ-based Wrath client folder is supported.

The sample pins the installed Forever build
1.60.1.70124. `build` is its build-config key; `cdn` is the **matching CDN-config
key**, and `version` is that build's version. Obtain all three from the same
installation's `.build.info` or version manifest. With `cdn`, the adapter persists
an exact `versions` manifest: CascLib otherwise combines the pinned older build
with the current online archive index, which can fail on uncached files.
This setting is optional for existing Retail configs.

## Background processes

Build `CdnAssetDownload` and run one process per source:

```powershell
CdnAssetDownload.exe retail C:/config/RetailCasc.ini 70
CdnAssetDownload.exe forever C:/config/ForeverCasc.ini 95 96
```

Arguments are **source race IDs**. Both authored sexes are included. The tool
collects all reachable customization materials and collections, including
related-choice combinations, then follows M2 skins, fixed textures, skeletons,
skeleton parents and external animations. It stores original decoded bytes;
download completion does not claim renderer or in-game acceptance.

[DownloadForever.ps1](../Tools/RetailAssets/DownloadForever.ps1) starts the Forever
process hidden at below-normal priority and writes separate progress/error logs.
[Launch.ps1](../Tools/RetailAssets/Launch.ps1) starts it alongside the client when a
Forever config is supplied/installed and `CdnAssetDownload.exe` is beside the
client. A worker mutex prevents duplicate processes for the same source/config;
Retail's running worker is unaffected. Library cache locks coordinate individual
CASC acquisitions across clients/workers using the same storage directory.

Failed files are reported and the process exits nonzero after finishing other
files. Restart with the same arguments to retry; completed files are verified
and reused. Progress reports `reused`, `acquired` and `failed`. `acquired` can mean
decoding CascLib's existing encoded cache or reading a local installation as well
as downloading from the network. It does not falsely report all reads as downloads.

## Durable originals

### Packed distribution

`Tools/RetailAssets/pack_cache.py` stages the strict decoded cache as compressed,
size-capped `patch-Retail-*.MPQ` and `patch-Forever-*.MPQ` files. It runs the native
cache integrity checker first, strips only the 16-byte WDC1 cache envelope, and
uses the existing `EsteriaRetail/<product>-<build>/<locale>/<FileDataID>.bin`
contract. Every packed original is read back and SHA-256 checked. It never edits
the client; install verified archives and move source caches outside Data only
after checking the current source inventory again.

The native client uses its existing SFile packed reader. Standalone tools,
including `CdnAssetDownload`, now discover root `Data/patch-*.MPQ` files when their
config or storage path is beneath that Data directory. They read higher lexical
patch names first and use the same packed namespace before loose originals or
CASC. Ship the matching x64 `StormLib.dll` and its MIT license beside the client
(or the standalone tools). Missing libraries, unreadable archives and failed
packed reads are errors, never an excuse to redownload an existing packed entry.
Loose-cache and bounded CDN acquisition remain available for genuinely missing
entries. The native client does not depend on StormLib.dll for its own MPQ reads.

Keep source INIs, aliases/equipment/registry files, custom support assets and the
32 lenient DB2 table-cache entries in the bootstrap distribution. Encoded CASC
files and archive indexes are working data, not CDN payloads; retain the pinned
versions/config manifests when retiring those working directories. A future new
asset may require reacquiring CASC metadata/indexes, but never whole asset
archives. Packed originals are immutable; newly acquired files remain writable
loose cache entries until the next pack export.
This base exporter refuses to run over installed base Retail/Forever MPQs. Future
incremental exports must use distinct, higher-priority archive names so they cannot
accidentally replace a base chunk with only the newly acquired files.

Archived online assets are acquired with an HTTP byte-range request and saved as
individual encoded files under their own EKey. The client and `CdnAssetDownload`
share this path. A server must return HTTP 206 with the exact Content-Range and
Content-Length; ignored or malformed ranges fail without a whole-archive fallback.
The EKey is checked before atomic publication, and CascLib retains its BLTE and
decoded-content validation. Existing local archives can supply individual slices
without network access while warming a cache.

After acquiring the installed content's dependency graphs, close clients and
downloaders and run `CdnCacheTrim <Data/Retail/Cache>` to review archive containers.
`--apply` first validates every decoded original's length and checksum, then
removes only hash-named archive payloads with corresponding archive indexes.
It preserves decoded originals, standalone encoded files, indexes, manifests and
configuration. It does not establish dependency completeness: acquire/verify the
installed races, customizations and packs before applying. Older client or worker
binaries must be replaced too, or they can resume whole-archive acquisition.

Online sources always persist decoded assets under
`path/<product>-<build>/decoded/<locale>/<policy>/`. An explicit `cache=` chooses
`cache/<product>-<build>/<locale>/<policy>/` instead. Pinned local sources can use
the same `cache=` to seed originals from an installed game, then switch to online
without reacquiring them. Unpinned local sources retain installed-storage behavior.
Offline `mode=files` packs keep their existing layout.

Cache entries have a format tag, bounded size, exact-length check and payload
checksum. Writes publish a completed temporary file atomically. Incomplete or
corrupt entries cannot masquerade as cached assets. Originals are never modified
by model/skin preparation. Successful reads fail explicitly if persistence fails.
The cache is checked **before opening CASC**, so warm runs need neither metadata
downloads nor a working network. Product, build and locale separate entries;
lenient DB2 reads are separate from strict model/texture reads. Changing an
explicit encryption-key file invalidates its lenient-table partition.

The notice appears only for slow active work. Durable cache hits create no
acquisition notice. Misses say `Retail: acquiring missing data` or
`Forever: acquiring missing data`; CPU work says `Preparing character appearance`.
Nested notices restore the active parent's message, and completed notices hide.

## Creator zoom

The create/customize model viewport accepts mousewheel zoom for all races, including
legacy models. Wheel up approaches the face; wheel down restores the full-body
camera. The native camera uses head key bone 6 plus the actual model's pedestal
transform/scale, with a bounds-based fallback for models without that bone. Race
and body switches recalculate the target. Leaving the creator resets zoom. Choice
pickers retain their wheel scrolling. Apply `glue/patch_character_create.py` together
with the matching native client; the patch updates both locale override archives.

## Dracthyr

Retail races 52/70 select **ChrModel 127 (male Visage, M2 4395382)** or
**ChrModel 128 (female Visage, M2 4220448)**. Both model and customization options
use the selected Visage form. Dragon-only saved/debug choice IDs cannot apply
dragon geosets or collection actions to a Visage body. Target race identity and
stored character bytes are preserved; old dragon-only selections use Visage
defaults. No dragon-to-Visage saved-choice migration is implied.

## Verification

`ctest --test-dir build/retail-assets -C Release --output-on-failure` covers cache
integrity, restart loads with CASC storage absent, product/build/locale isolation,
read-policy separation and the Forever empty-WDC5-table layout.

`CdnSourceSmoke <retail-config> <forever-config>` checks concurrent source
initialization and alternating male/female model preparation using warm caches,
requiring zero acquisition. `RetailRaceProbe --forever` and
`RetailComposeProbe --forever` inspect the separate Forever store. The normal
asset probe accepts `forever/<ID>.m2` with `--character`.

For the in-client diagnostic preview, `retailmodel forever/7478487` selects the
Forever namespace. Set `WOW_RETAIL_PREVIEW_CHARACTER=1` when previewing a player
body so it uses the same character material policy as the normal race loader.
The automatic `WOW_RETAIL_PREVIEW` accepts the same Forever-prefixed ID.

On 2026-10-20, Skyborne acquisition completed **1,116 assets**; a fresh process
reused **1,132 entries including DB2s**, with **zero acquisition/failures**. Both
Skyborne bodies and both Visage bodies passed client preparation. A cold
Forever `ChrRaces.db2` request succeeded from the CDN with its paired archive
index pinned. Live creation, in-world animations, equipment and relog remain
separate acceptance checks.
