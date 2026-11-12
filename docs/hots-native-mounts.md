# Native Heroes of the Storm mounts

HotS uses the existing CascLib/CDN acquisition and decoded-cache machinery with
an independent `hots/` namespace, `SourceKind::HotS`, `WOW_HOTS_CONFIG` and
`Data/HotS/HotSCasc.ini`. The production product is `hero`. WoW providers do not
accept it. The pinned source is 2.57.0.98348, build config
`a64c9b7ebf1c6a811ffefad97865ed80`, CDN config
`5146a61f539c29687d886efcc8de1e2d`.

M3, M3A and DDS runtime reads return original decoded CASC bytes. ASECM3 owns
format validation and rendering; no M2 or BLP conversion occurs. Source names
are preserved, including numeric filenames. Optional `paths=hots-paths.csv`
maps `hots/<virtual-reference>;<source-CASC-path>` for inherited texture paths.
The native world M3 texture callback retains the model's HotS namespace.
Named offline packs use `mode=files` and store source paths below `path`.
Windows file IO supports long source/cache paths without weakening path checks.

## Reproduce the stage

Build `HotSAssetTool`, `HotSSourceTests`, `ASECM3Client` and `ASECM3Inspect` in a
build configured with `WOWDECOMP_RETAIL_ASSETS=ON` and `WOWDECOMP_ENABLE_ASEC_M3=ON`.
From the repository root, using the existing `build/retail-assets` configuration:

```powershell
rtk proxy cmake --build build/retail-assets --config Release --target HotSAssetTool HotSSourceTests ASECM3Client ASECM3Inspect --parallel 6
```

Create `build/hots-mounts`, copy `config/HotSCasc.example.ini` to
`build/hots-mounts/HotSCasc.ini`, then:

```powershell
rtk proxy build/retail-assets/Release/HotSAssetTool.exe build/hots-mounts/HotSCasc.ini list '*' build/hots-mounts/source-files.txt
rtk proxy python Tools/HotSMounts/import_mounts.py stage --catalog G:/Downloads/HDWoWModels/HotSMounts-0.6/HotSMounts/mounts.json --index build/hots-mounts/source-files.txt --output build/hots-mounts/stage --tool build/retail-assets/Release/HotSAssetTool.exe --config build/hots-mounts/HotSCasc.ini --inspect build/retail-assets/bin/Release/ASECM3Inspect.exe
```

The supplied catalog is a seed, not roster authority. The importer also reads
607 source catalog XML files, follows mount/model inheritance and stages
additional resolvable models and explicit RequiredAnims banks. Every staged
original has a SHA-256 entry in `mount-inventory.json`. Repeated acquisition
uses the source cache and refuses to replace a differing staged file.

Outputs under `build/hots-mounts/stage`:

- `originals/`: unchanged decoded source files, outside Git.
- `mount-inventory.json`: model/display inventory, source/index/config provenance,
  staged hashes, parser counts and pending integration work.
- `catalog-audit.json`: concrete source records, inherited references, additional
  IDs and missing catalogs. Scope explicitly excludes other catalog directories.
- `inspection.json`: each model's actual parser result and dependencies.
- `HotSCasc.ini`, `hots-paths.csv`: offline native source configuration and mappings.
- Request lists and fetch logs: exact attempted files and failures.

## Verified stage on 2026-11-11

The CDN source opened and enumerated 1,614,681 paths. All 185 supplied model
references resolved. The supplied catalog has 562 unique variant IDs; all occur
in the inspected source catalogs. Those catalogs contain 595 concrete mount
records, including 33 IDs absent from the supplied catalog. The stage expands
to 199 models and 585 variant records, including resolvable hero-specific forms.
It contains 2,281 source files, 1,087,454,470 bytes. All requested acquisitions
completed. This is not proof of complete dependency closure for every effect.

The freshly built M3 inspector accepts 188/199 models. Eleven fail bounded
section validation with `Truncated/overlapping M3 section PHCL v2`: Butcher's
Beast, base and Nexus Battle Beast, Ironsides Direwolf, base/saddled/unicorn
horses, Illidan's horse, Money Pig, base Spider and Golden Tiger source models.
Their original files remain intact. Do not relax bounds checks or silently
disable cloth to make this count pass.

Ten source records have no resolved standalone model in this catalog scope:
AzmodanFireFeet, DiabloRun, MarauderMuradinJetpackForm, MountFenixBase,
MountMephistoBase, MountRagnarosWake/Var1/Var2, Random and TyraelMechaJetpackForm.
They remain explicit audit entries; actor-only effects, invisible models and
the Random selector need classification before assigning summon behavior.

Builds passed for the CASC tooling, ASECM3 client adapter and current inspector.
The dedicated HotS test checks source isolation, byte preservation, named
dependencies, path validation and a long-path cache round trip. Real staged M3,
M3A and mapped DDS reads also returned byte-identical originals offline.
HotS source, cache-trim and packed-original tests passed; the latter requires
`STORMLIB_DLL` pointing to a local StormLib DLL. Three Python tests cover the
inventory, inherited catalog records, duplicate IDs and unsafe paths.
The broader RetailAssetTests executable stopped at an unrelated Ethereal
bare-foot-policy assertion; a complete Retail suite pass is not claimed.

## Remaining integration

Resolve PHCL v2 layout coverage from source evidence, audit catalogs outside
the mount directory, and close original effect/material dependencies. Variant
texture selections, rider positions/poses and animation semantics still need
native implementation/validation; the converted pack's offsets are reference
data, not unquestioned transforms for the native models.

Allocate additive model/display/creature/spell/collection IDs against the current
Esteria client and server tables. No target IDs have been allocated, SQL applied,
client executable installed or services restarted by this stage. Finish matched
client/server integration, then verify learning, collection previews, mounted
world rendering, movement, effects, rider placement, dismounting, persistence,
performance and clean teardown in game. Source acquisition and parser acceptance
are not gameplay or visual acceptance.

## Server installation on 2026-11-12

The existing `ac-database`, `ac-authserver` and `ac-worldserver` containers were
started after the reboot. No Docker image or server executable was rebuilt.

`Tools/HotSMounts/server_mounts.py` allocated stable IDs against the current
client archives, active server DBCs/continuations, SQL overlays and shared mount
allocation registry. It staged seven client tables and four server continuations,
plus guarded SQL using the existing native-import ownership receipts. The donor
is spell 23229 (+100% ground speed); surf mounts use the active carpet visual
from spell 61451. All 585 resolvable mount records have server registrations,
including the 38 variants whose eleven models still fail PHCL v2 parsing.
Those registrations are not a statement that the client can render them.

Installed and read back 585 learning items, item displays, creatures and their
model mappings. Worldserver startup explicitly confirms 585 rows loaded from
each of CreatureModelData, CreatureDisplayInfo, Spell and SkillLineAbility's
`dbc1-native-hots-mounts` continuation. The console reached `AC>`; database,
authserver and worldserver remained running. No existing character was taught
mount spells. No startup error matching these allocated IDs was found in the
captured startup log; unrelated pre-existing creature/config warnings remain.

The authoritative receipt and exact staged payloads are in
`build/hots-mounts/server-install-20261112-v3/`. `install-receipt.json` records
`server-installed-runtime-loaded`, while `clientInstalled` remains false.
`docs/hots-mount-learn-commands.md` lists every allocated spell/item individually;
the spell range contains gaps reserved by other imports. The pending migration
is `EsteriaWoW/data/sql/updates/pending_db_world/rev_20261112010000000_hots_native_mounts.sql`.
Canonical copies of the four continuation files also live under EsteriaWoW's
`modules/mod-custom-server/data/dbc-continuations/`.

Four focused Python tests pass. SQL was accepted by the live database and all
content rows were compared with the stage. The repository SQL style linter did
not pass: it flags the existing guarded-insert/CONVERT style, and its subsequent
`git fetch origin master` failed. Details are in `build/hots-mounts/sql-lint.log`.

The client is not installed or gameplay accepted. Mounted M3 models use a
different client lifecycle from the unit-body bridge; that bridge, matched
client tables, variant material selections, rider setup and live checks still
need completion. The ten unresolved source catalog records remain explicit in
`catalog-audit.json`, without invented standalone models.

## Native client integration and rider correction on 2026-11-12

The native client is now installed in `G:/3.3.5a - Dev`. Its additive registry
contains all seven matched HotS tables alongside the previous Retail registry
rows. `Data/HotS/HotSCasc.ini` reads unchanged originals from an offline store;
per-mount virtual paths select the source mesh, declared animation bank and
color-specific DDS dependencies. Existing unrelated tables and archive files
were preserved. The previous executable/config are backed up under
`_backups/hots-native-20261112-052622`.

All 199 meshes parse and bind playable animation sequences. Eleven original
files use a 128-byte PHCL v2 layout; a bounded alternate layout preserves its
unknown coefficient bytes rather than reading into the next section. A
truncation regression still rejects an overlong PHCL section. MADD optional
null slots and repeated graph-group instances are decoded without relaxing
payload extents or typed identifier checks.

HotS catalog-declared mount banks can evaluate matching named/typed tracks on
the mesh's own hierarchy. This handles source-authored reparented bones without
transferring another armature's world transforms. Generic M3 deformation-bank
validation remains unchanged. RequiredAnimsEx declarations are included, fixing
the missing Tiger and Guardian Goat banks. The final source dependency audit
has no unresolved model/animation/texture entries for the 585 registrations.

Mounted M3 models have separate bindings from their owner's body, and attach,
replacement, dismount and world teardown remove only the corresponding model.
The M2 attachment getters, child animation, visibility and model-reload paths
dispatch to immutable native M3 attachment snapshots. Native M2 branches remain
in place. The rider uses an upright frame at the original Ref_Mount pivot,
including its animated translation. The earlier converted-pack seat offset was
too low for Butcher's Beast (1.5 versus the original 2.0); that mistake and the
fixed-seat animation mistake are corrected. Attachment tests cover an upright
seat following a 0.25-unit animated displacement.

Some HotS MADD graphs remain unimplemented. Unambiguous named mount texture
sets have a logged diffuse/normal/specular/emissive surface approximation.
Original graph payloads remain intact. This is not source shader/VFX parity.

Live checks on Zach confirmed learning Butcher's Beast, collection registration,
summoning its textured model, an upright rider above the body, mounted movement
with a Stand-to-Walk transition, a clean quit/relaunch and retained learned and
mounted state. The post-correction draw log records native attached-child
updates and zero render-boundary failures. The user caught the initial low,
static rider; the final installed build includes both corrections. Individual
acceptance of all 585 variants, all rider races and the special hero additions
is not claimed. The exact current executable SHA and evidence are in
`build/hots-mounts/client-playable-v3/install-receipt.json`.

Final data and receipt are in `build/hots-mounts/client-playable-final/`.
All 585 registered variants have texture mappings and rider-seat metadata.
576 use the native passenger point or supplied rider metadata; nine hero actor
additions without a passenger point use explicitly authored root seats above
their mesh or at the registered target mount height. These nine approximations
are identified in `validation.json` and need individual visual placement checks.
The package itself supplies rider coordinates for 184/185 models; Azmodan's
Mad Axe model is the null entry. Converted M2 offsets must not be treated as
unscaled native M3 attachment coordinates.

Color substitution searches all catalog variants for the source bitmap, rather
than assuming the first alphabetic variant is the source model's default. This
preserves distinct standard-horse palettes as well as the Butcher's Beast
green/yellow diffuse and emission maps. The installed final mappings load at
the next client start. The user explicitly confirmed the corrected Butcher's
Beast placement and rider following looked good. Full-roster visual acceptance
is still separate from these source/bank/dependency and gameplay checks.

Six focused Python regressions, the MADD structural fixture and the concurrent
attachment/rider fixture pass. Client and inspector builds pass. No server
image rebuild was required for client integration.

## Visual fixes on 2026-11-12 (not yet checked in game)

From the user's report (dark Alterac Iron Kite, invisible Angry Cloud, static
Ancient Dark Nexus Hound, bikes using the horse pose, broken mount-window
preview, jittery animation):

- The named-texture HotS surface sets `blend_mode_emis1=2`. The default (0)
  multiplied the body by the mostly-black `_Emis` map.
- Particle `size/rotation/color/alpha_hold` now holds the mid value from the
  midpoint for that fraction of the lifespan. Angry Cloud is built from these
  particles. The exact SC2 hold placement is unverified.
- HotS mounts always try their catalog bank, even with embedded VFX clips. Mesh-local
  tracks use the bank's interpolation modes, and Stand/Walk/Run loop.
- The mount window and character model frames route `.m3` paths to a preview
  M3 binding instead of the M2 loader.
- `client_mounts.py poses` writes `Data/HotS/MountSkins.csv`: 62 bike displays
  get rider anim 484 (ReclinedMount), merged into `EsteriaMountSkin`.

## Second fix pass on 2026-11-12 (not yet checked in game)

- Celestial mounts: M3 layers with generated UVs (planar 4/5/11-14, screen 15, triplanar 16-18) used to fail the standard
  surface and drop to the magenta diagnostic. `GxSurface` now computes them per vertex into a spare UV channel; sphere
  environment (8) uses the existing sphere projection. MADD shells that name a `starfield` texture draw it as screen-space
  emissive (`BuildMountSurface`). Approximations; exact SC2 projections are unverified.
- Undecoded MADD materials with no recoverable texture set (glows, shells, beams) are skipped instead of drawn as an opaque
  white sheet (Heart Stone, Tracer bike wheels).
- Clips named `... Ridenone` (morph forms) map as unmounted body clips, so Stand/Walk play.
- The mount window preview no longer fails when a model's Stand effect clip has no bound particle owners (Ripper).
- `Data/HotS/MountSkins.csv` (from `client_mounts.py poses`): `r;484;484` for any mount whose name or HotS ID contains
  Bike, Hovercycle or Vulture (plus category Ridebike); `h;0;0` hides the rider for MorphForm/BatForm mounts.
- `Tools/HotSMounts/movement.csv` holds the hand-made Flying/Remove list (everything else is ground).
  `movement.py --apply` gives flying mounts the flying-carpet donor's (61309) speed effects and attributes and drops removed
  mounts' Spell/SkillLineAbility rows, in the client registry and the server continuations; it restarts the worldserver
  only when nobody is online. Applied 2026-11-12: 137 flying, 7 removed (backup `_backups/hots-movement-20261112-080214`).
  The removed mounts' items and creature rows remain.

## Third pass on 2026-11-12

- Mount seats: HotS models carry `Ref_Shield` as WoW attachment 0 as well as `Ref_Mount` (29). The binding remapped 29 to 0
  but kept the shield point, which the rider lookup found first, so riders stayed at the static shield bone (DERPA hop).
  Mount bindings now drop the source's own attachment 0 before remapping.
- Wide-stance mounts also get SpellVisual 14418 (the kit Eclipse bikes use to seat the rider in 484) via `movement.py`;
  the skin rows alone did not change the pose. The client registry was patched; the server continuation picks it up on the
  next `movement.py --apply` run with nobody online.
- The wide seat players expect is retail pose 806 (Incognitro, the Indecipherable Felcycle), not WotLK's ReclinedMount 484.
  HotS bike rows are now `r;806;484`; the skin row overrides the spell kit's 484. Unit_C logs `rider pose:` lines to
  `Logs/RetailAssets.log` on every mount (requested/available/playing), which is how this was found.
- Wide-stance seats sit 0.35 further back (native -X): `client_mounts.py poses` rewrites `Data/HotS/MountRiders.csv` from `client-playable-final` (SEAT_BACK).
