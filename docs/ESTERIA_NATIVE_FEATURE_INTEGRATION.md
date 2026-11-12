# Native Esteria feature integration and Eclipse assessment

Prepared: **2026-10-25**. Scope: source investigation and implementation design. **No feature code, client installation, server configuration, database, or deployed binary was changed.**

## 1. Recommended approach

All five requested features are feasible in this source-built client. Port their behavior into Esteria's existing engine and compiled extension system. The two WXL projects are references for behavior and algorithms; their stock-client addresses, DLL entry points, private SDK headers, and D3D9 device assumptions are not compatible integration interfaces for our x64 executable.

Recommended order:

1. **Performance overlay**, to provide a baseline for the remaining work.
2. **No close-camera transparency**, as a small camera-specific source change.
3. **Unit outline**, followed by explicit D3D11 and Vulkan support.
4. **Loot beam**, sharing the world-depth work and the server module already present in Esteria.
5. **Gear recolor**, including server persistence, observer synchronization, and both equipment render paths.
6. Extract selected **Eclipse** capabilities into the same native systems, starting with wardrobe/loadouts, text, cosmetics, and asset tooling.
   **Wardrobe/morph dropped (2026-10-27):** server transmog (`mod-transmog` + `Manastorm_Transmog`) is the only appearance
   system; see `docs/MANASTORM_INTEGRATION.md`. The Eclipse wardrobe rows below are kept for reference only.

Do not merge either client fork wholesale. Our Retail importer, Vulkan renderer, M3 support, extended limits, race choices, and appearance wire format already differ from the donors.

### Findings that change the plan

- The wooooow outline is **D3D9/D3D9Ex-only as written**. Copying its files will not deliver outlines on Esteria's Vulkan or D3D11 backends.
- Loot rarity before opening a corpse requires server knowledge. **Esteria already contains the matching `mod-loot-beam` source and configuration template.** Its deployment/runtime status was not checked.
- Esteria's server already declares and dispatches the principal gear-recolor hooks. That improves compatibility, but is not a successful module build.
- The no-transparency module reverses one particular WXL instruction change. Esteria has the corresponding source function and currently writes the camera alpha. Implement the camera behavior directly.
- Eclipse has a second, substantial gear-color system. Its OKLab/OKLCh recoloring is **not equivalent** to WXL's solid/selective/gradient modes. Preserve the requested modes rather than silently substituting Eclipse's algorithm.
- The local Eclipse checkout contains source, references, tests, and generated includes, but **no `Eclipse/`, `work/`, or `build/` output directory**. Its documented packs are not ready-made local deliverables.

## 2. Inspected snapshots and evidence boundaries

| Repository | Inspected commit |
| --- | --- |
| Esteria client, this checkout | `099b416bc74e9aec56d30b8b389d766b49b77530` |
| [manatactical/wooooow](https://github.com/manatactical/wooooow/tree/93aec3de1a148736598d2d15c7953cba0a587669) | `93aec3de1a148736598d2d15c7953cba0a587669` |
| [notacoder-dev/wxl-no-transparent-characters](https://github.com/notacoder-dev/wxl-no-transparent-characters/tree/d45af912158b4453a914e470bcd03ce1087337de) | `d45af912158b4453a914e470bcd03ce1087337de` |
| [nicolalol3/wxl-gear-recolor](https://github.com/nicolalol3/wxl-gear-recolor/tree/0461a2242ef5c7835395735cd02f593859f866c7) | `0461a2242ef5c7835395735cd02f593859f866c7` |
| `R:\Users\Zach\Documents\GitHub\Eclipse-DEV` | `abf3a09bfd430954539712de4670a8350fc6fa4b` |
| `R:\Users\Zach\Documents\GitHub\EsteriaWoW` server | `b915c2b1b5190f0163bc48f84e6f1ea303e99bd6` |

The three public donors were shallow-cloned for inspection under `C:\Users\Zach\AppData\Local\Temp\esteria-port-review-20261025\`. They were not added as remotes, submodules, or vendored source in this repository.

The client initially had two untracked documents, `DX11WXL.md` and `TWOFORMS.md`; both were preserved. Eclipse reported a clean working tree when inspected.

**Evidence levels used here:**

- **Source verified:** a file, function, protocol, build declaration, or checked local directory was inspected.
- **Donor documented:** an asset census, earlier test result, performance claim, or known issue is recorded in the donor's pipeline notes; it was not independently reproduced here.
- **Proposed:** a future filename, extension, seam, migration, or implementation step.
- **Unverified:** builds, installed server modules, live rendering, multiplayer behavior, and performance. No compilation, asset rebuild, test suite, or game session was run for this report.

The earlier `DX11WXL.md` is a separate research plan. Current source confirms that native D3D11 and Vulkan implementations exist; that older plan is not proof that its proposed WXL integration has landed.

## 3. Integration surfaces we already have

| Existing surface | How to use it |
| --- | --- |
| `Extensions/CMakeLists.txt` | Discovers `Extensions/<Name>/src/*.cpp`, generates entry points, and supplies `WOWDECOMP_EXTENSION_<NAME>` options. Self-contained modules can own a CMakeLists. |
| `WoW/Source/Extension/ExtensionMgr.h` | CVar, Lua function/event, custom message registration, lifecycle, world, object, unit, texture, and model callbacks. |
| `RenderScript` | Frame begin, world rendered, and frame end callbacks. Useful for overlay and lifecycle work; insufficient alone for per-batch silhouettes or compositor ownership. |
| `CM2SceneRender`, `CM2Scene` | Typed model/batch render seams for outline and attached-equipment color. |
| `CCharacterComponent` | Equipment layer composition, synchronous RenderPrep, and asynchronous component requests. |
| `Engine/Source/Gx/` | Own graphics state/resources here. D3D9, D3D9Ex, OpenGL, D3D11, and Vulkan have different implementations. |
| `Engine/Source/Retail/`, `ComponentCore/RetailRace.*` | Existing Retail models, textures, customization, equipment, and identity mapping remain authoritative. |
| `Extensions/BodyShapeSync/src/BodyShapeSync.cpp` | Existing appearance message consumers; coordinate new visual state with this machinery. |
| `Addons/` and existing UI packaging | Place the small controls/overlay in the established client UI distribution. Importing donor FrameXML wholesale would replace unrelated settings. |

**Specific seam caution:** `RenderScript::OnWorldRendered` runs after `OnWorldRender` and world text. Inside `OnWorldRender`, `FFX::Finish()` has already executed. The donor outline/beam placements are inside that render sequence while the intended depth and targets are bound. Do not assume the existing late callback is an interchangeable insertion point.

Keep high-level policy/state in compiled extensions and add only the missing typed engine calls at their owning sites. Do not build a general hook framework for these five features.

## 4. Unit Outline

### What the donor actually does

The feature is split between:

- [Engine/Source/UnitOutline/UnitOutline.cpp](https://github.com/manatactical/wooooow/blob/93aec3de1a148736598d2d15c7953cba0a587669/Engine/Source/UnitOutline/UnitOutline.cpp) and `UnitOutline.h`: mask rendering, depth, blur, and edge composite.
- [WoW/Source/Ui/UnitOutlineGather.cpp](https://github.com/manatactical/wooooow/blob/93aec3de1a148736598d2d15c7953cba0a587669/WoW/Source/Ui/UnitOutlineGather.cpp): selections, reactions, bodies, mounts, and item attachments.

It redraws selected model batches into masks using their actual animated pose, subtracts the local player's silhouette, softens the mask, and composites the edge before interface drawing. It includes foot fading to avoid painting the ground.

The source does more than the README's target/mouseover summary: it also gathers a pet target, selected interactive game objects, quest givers, and flight masters. The quest-giver cache is capped at 24 GUIDs; it resolves those GUIDs again instead of retaining model pointers. Worn gear is identified through an `ObjectComponents` path test, while spell-effect attachments are excluded.

### Required port

1. Bring over the outline algorithm and gather policy as scoped source, preserving provenance.
2. Wire gathering before world/model draws in `CGWorldFrame::OnWorldRender`.
3. Add the silhouette calls at the donor-equivalent sites in `Engine/Source/Model2/CM2SceneRender.cpp`.
4. Port the selected-model ordering changes in `M2Scene.cpp`: stamps need world occlusion depth to be present.
5. Bracket the world draw with the correct shared depth, then composite at the intended point before UI/postprocessing.
6. Register `unitOutline` early enough for login/video settings, and add only its controls to Esteria's UI.
7. Implement resize/device-reset cleanup and clear selection/model references on lifecycle changes.

Build changes reach root/engine CMake, Model2 dependencies, and UI source collection. The donor's `UnitOutline` target links `d3dcompiler.lib`; preserve a single owner for shader/resource lifetime and avoid circular static-library dependencies.

### Dependencies that must be separated

The donor now shares depth with SSAO, sun glow, depth fog, and water:

- `UnitOutlineGather.cpp` includes `AmbientOcclusion.h` and updates its camera distance.
- `CM2SceneRender.cpp` uses a helper that stamps both outline and ambient-occlusion masks.
- The depth allocator checks other postprocess CVars and can use the donor-added `GxTex_INTZ` format.

**Extract the outline-related code only.** These links do not require importing the donor's water, AO, lighting, or entire world renderer. Esteria's current texture enum has `D24X8` but no `INTZ`; adding a format blindly would require every backend's format/capability handling to change. Outline needs depth testing, not inherently sampleable INTZ depth.

### Backend and content work

| Backend/content | Actual starting point | Required completion |
| --- | --- | --- |
| D3D9/D3D9Ex | Donor explicitly supports these APIs | Adapt resources/state to this tree and validate occlusion/reset behavior. |
| D3D11 | Current Esteria shader translation exists | Port mask/depth/render-target setup and verify the particular shaders/constants translate correctly. Removing the donor API guard is insufficient. |
| Vulkan | Native backend already exists | Supply matching shaders/pipelines, depth-compatible targets, synchronization, and resize handling. |
| OpenGL | Donor declines rendering | Add a GL implementation or explicitly report unsupported. |
| Retail M2/equipment | Existing custom importer/material paths | Confirm alpha-tested materials and all attached gear are included without outlining particles. Path heuristics may miss differently named Retail assets. |
| ASECM3 | Separate native module/render packets | M2 batch interception alone does not establish M3 silhouettes. Add a compatible silhouette draw in its renderer or document the limitation. |

The donor refuses shared-depth occlusion when MSAA is active; its fallback can draw outlines over occluding scenery. An acceptable native implementation should use matching multisample resources/resolve or disable the unsupported occlusion-dependent feature clearly. Do not advertise wall occlusion merely because an outline is visible.

**Relative effort:** medium for a D3D9 reference port; high for complete D3D11/Vulkan/custom-model support.

## 5. Loot Beam

### What the donor actually does

[WoW/Source/Ui/LootBeam.cpp](https://github.com/manatactical/wooooow/blob/93aec3de1a148736598d2d15c7953cba0a587669/WoW/Source/Ui/LootBeam.cpp) implements the complete marker: corpse tracking, tier selection, fades, beam geometry, and sparkles.

- Tracks dead, lootable NPCs; excludes players.
- Keys tracked entries by GUID, with 64 simultaneous beacons and up to 32 sparkles each.
- Walks resident visible objects on the main thread.
- Draws additive, untextured, vertex-colored geometry through Gx.
- Reads open-loot rarity through `CGLootInfo` / `CGLootWindow`.
- Also reads the server hint from typed `CGUnitData::m_unitFieldPadding`.
- Reads outline-owned depth availability and disables depth testing if that shared depth is unavailable.
- Uses `lootBeam`, registered early in `WorldParam_CVar.cpp` and defensively in `LootBeam::Init`.

### Server counterpart already found

In `EsteriaWoW`:

- `modules/mod-loot-beam/src/mod_loot_beam.cpp`
- `modules/mod-loot-beam/src/mod_loot_beam_loader.cpp`
- `modules/mod-loot-beam/CMakeLists.txt`
- `modules/mod-loot-beam/conf/mod_loot_beam.conf.dist`

At creature death, this module scans rolled loot and publishes:

| Hint | Meaning |
| --- | --- |
| 0 | No hint |
| 1..8 | Item quality 0..7 plus one |
| 9 | Money without item gear |

It writes `UNIT_FIELD_PADDING`; `LootBeam.Enable` defaults on in the template. The source reserves this use for creatures; player use of that field is a separate existing appearance contract. Do not change player interpretation.

**No new custom opcode or addon query is needed for this design.** Confirm the module is compiled/enabled on the intended server and verify the field arrives in the actual client's update descriptors. Presence of source is not proof it is deployed.

### Required port and corrections

- Import `LootBeam.{h,cpp}` and add a pre-UI world draw at the correct depth lifetime.
- Reuse the outline/world-depth integration, but make beam occlusion work independently of whether `unitOutline` is enabled. The donor's depth-wanted condition does not independently include `lootBeam`.
- Keep all reads typed. Its static assertions are useful; stock heap-offset heuristics are not needed in source.
- Replace the donor fallback that accepts a dead unit when dynamic-flag high bits look unexpected with a strict valid lootability check. Bad metadata must not create arbitrary corpse markers.
- Preserve its geometry, tier colors, fade, distance limits, and enable control.
- Verify open-loot retry/update behavior when item rarity was initially uncached.
- Decide whether color means initial maximum or current remaining loot: the donor only raises a tracked tier, and the current server writes its hint on death. It does not continuously lower the color after partial looting.
- Test per-player tap/loot rights, group loot, quest-only drops, money-only drops, partial loot, despawn, map change, and server hint absence.

The Gx geometry is more portable than the outline shaders, but backend fixed-function emulation and depth ordering still need verification. A loot beam is a marker; it must never send an unsolicited loot request or alter loot rights.

**Relative effort:** low/medium after shared render integration. The existing server module removes a major missing dependency.

## 6. No Transparent Characters

The [module implementation](https://github.com/notacoder-dev/wxl-no-transparent-characters/blob/d45af912158b4453a914e470bcd03ce1087337de/src/NoTransparentCharacters.cpp) compares six bytes at stock x86 address `0x00737441`, replacing `mov [esi+0xCB],dl` with NOPs when the exact enabled instruction is present. It describes this as restoring stock behavior in one branch of the unit alpha function at `0x00737390`. It exports WXL ABI entry points and declares GPL-3.0-or-later.

### Native equivalent

The corresponding local function is `CGUnit_C::SetMaxAlpha` in `WoW/Source/Object/ObjectClient/Unit_C.cpp`, tagged `// OFFSET: 0x00737390`. It currently writes `m_alphaScale` in the ordinary non-vehicle branch.

The complete caller search found:

- `CGWorldFrame::SetPlayerFadeCameraValue` in `WoW/Source/Ui/WorldFrame.cpp`.
- Calls to that wrapper from `WoW/Source/UIUtil/Camera.cpp`.
- Recursive passenger handling in `SetMaxAlpha`.
- A reset to alpha 1 in `UnitVehicle_C.cpp`.

**Recommended implementation:** apply the opt-out at the camera-fade boundary, using an explicit native CVar, rather than disabling all model alpha or copying a memory patch. Preserve the existing first-person/player-render-disable decision in `SetPlayerFadeCameraValue`; alter only the camera alpha supplied to the unit after that decision. A return at the start of that function would skip more than transparency.

When switching off fading, restore the camera alpha scale to 255 so an already faded character does not stay faded. Keep aura alpha, stealth, ghost effects, procedural fades, and vehicle resets separate. Test mounts, possession, vehicles/passengers, first-person transitions, zoom changes, and toggling after a fade has started.

This is backend independent and requires no server change. The WXL binary patch is not portable to either a relocated x86 build or x64.

**Relative effort:** low, with meaningful behavioral regression checks.

## 7. Performance Overlay

### Donor implementation and complete dependency list

The visible UI is a small movable Lua frame, not an ImGui dependency.

| Donor location | Responsibility |
| --- | --- |
| `Engine/Source/Gx/Gx.{h,cpp}` | `GxPerf*` counters; separate current and completed frame totals. |
| `CGxDevice/CGxDevice.cpp` | Latches counters at frame end. |
| D3D9, D3D9Ex, OpenGL draw implementations | Count submitted draws and triangle primitives. |
| `WoW/Source/Ui/GameUI_Scripts.cpp::Script_GetPerformanceStats` | Returns FPS, ms, draw calls, triangles, process CPU percentage. |
| `WoW/Source/Client.h`, `Client_CVar.cpp` | `showPerformance` CVar. |
| `Data/custom.MPQ/Interface/FrameXML/VideoOptionsPanels.lua` | Overlay, drag handling, 0.25-second updates, and control. |

The Lua function is [implemented here](https://github.com/manatactical/wooooow/blob/93aec3de1a148736598d2d15c7953cba0a587669/WoW/Source/Ui/GameUI_Scripts.cpp#L2570). It derives ms as **1000 / averaged FPS**, not a directly measured GPU frame time. CPU uses `GetProcessTimes` deltas divided by wall time and processor count: process share of the whole machine, not a single busy-core percentage.

### Required port

- Bring the counter helpers and Lua API over with their enable gate.
- Instrument Esteria's **D3D11 and Vulkan actual draw paths too**, as well as supported legacy backends. Donor instrumentation has no D3D11/Vulkan coverage.
- Count each actual submission once; account for indexed/unindexed and instanced draws correctly.
- Latch at one consistent completed-frame boundary. Define the numbers as submission workload, including auxiliary outline passes, rather than unique scene geometry.
- Add only the small overlay and controls to our UI/addon packaging.
- Keep it clamped to screen, draggable, and functional through UI reload.
- Preserve failure handling for CPU sampling and guard timer wrap/reset.

**Donor caveat:** comments say the overlay stops polling while hidden, but its actual Lua calls `GetPerformanceStats()` at 4 Hz even when invisible, then skips text. CPU sampling also runs on those calls. Use a CVar callback/native enable state so counting is reliably off without depending on a hidden UI poll. Avoid polling an invisible FontString just to discover a setting changed.

A useful initial overlay can faithfully show the five requested metrics. Frame percentiles, GPU timings, spike logs, memory graphs, and a replacement video settings screen are separate features.

**Relative effort:** low/medium. This is the best first port because it supplies comparative measurements.

## 8. Gear Recolor

### What is actually in the repository

[GearRecolor.cpp](https://github.com/nicolalol3/wxl-gear-recolor/blob/0461a2242ef5c7835395735cd02f593859f866c7/module/wxl-gear-recolor/src/GearRecolor.cpp) is a roughly 6,600-line implementation tied to private WXL core headers and x86 offsets. Alongside it are `Recolor*.inc` fragments; the inspected translation unit does not include them. They must not be assumed to be the authoritative current implementation or compiled a second time.

There are three distinct parts:

1. Client rendering/state and `WXL_Recolor*` Lua functions.
2. Lua interfaces and message recovery under `addon/HorizontalTools/`.
3. AzerothCore `server/mod-item-tint/`, with `custom_item_tint` SQL.

Supported data modes are solid RGB, selective source-to-destination rules, and multi-stop gradients. Client limits include four selective rules and five gradient stops; the standalone addon's selective UI caps at three. Normalize those limits as part of the port.

### The two render paths must both be covered

| Path | Native integration |
| --- | --- |
| Body equipment painted into the character composite | Pass explicit owner, equipped-item identity, slot, and tint revision through component layer preparation and asynchronous requests. Recolor private input layers before blending. |
| Attached item M2 models and body material exceptions such as cloaks | Resolve owner and slot from typed equipment/model attachment context; apply tint in the intended material draw and restore state afterward. |
| Retail customization/composition and collection geometry | Route through existing Retail composition/material ownership as well. A classic `TextureComponents` hook alone does not establish coverage. |
| UI previews | Give dress-up, paperdoll, and glue models their own explicit appearance context; do not identify them through a sticky world-model pointer. |

Local source makes the async issue concrete: `CCharacterComponent::ProcessComponentRequest` pastes base/layer textures from a queued request, outside an ordinary owner-bound RenderPrep stack. A global “current player/component” is insufficient. Store immutable request inputs and reject stale completion after the owner's equipment/tint generation changes.

Do not mutate shared source BLPs, texture-cache palettes, or shared M2 material state globally. Two players wearing the same display must be able to show different colors without coloring their faces, underwear, NPCs, or each other.

### Persistence and visibility contract

The donor's `custom_item_tint` table is keyed by **item instance GUID**, with owner, mode, and data. It is not keyed only by slot or item entry.

Existing `WXL_TINT` messages are:

- Client: `SET\tslot\tmode\tdata`, `CLEAR\tslot`, `REQ`.
- Server: `PUSH\townerGuid\tslot\tmode\tdata\tentry`, or a slot clear; `ACK` confirms writes.
- Resynchronization: login, map changes, visible-item changes, newly visible players, and UI reload.
- The server resolves the item from the authenticated player's equipped slot; preserve that authority.

The principal hooks exist in Esteria's `src/server/game/Scripting/ScriptDefines/PlayerScript.{h,cpp}`: `OnPlayerAfterSetVisibleItemSlot`, `OnPlayerBeforeSendChatMessage`, and `OnPlayerUpdate`. No `mod-item-tint` implementation was found in the searched current modules.

**First complete integration:** adapt the server module, register compatible native Lua functions through `ExtensionMgr`, and package the HorizontalTools communication/recovery logic with an Esteria settings UI. The standalone `addon/WXLRecolor/WXLRecolor.lua` does not contain the `WXL_TINT` communication handling found in HorizontalTools; copying only it is not complete multiplayer support. The Transmog Tints tab additionally depends on its external Horizon/HorizontalTools frame.

The addon transport can be retained initially. A later native message format must use allocated/versioned opcodes; do not repurpose existing appearance opcodes.

### Specific donor issues to address before reuse

- Server parsing narrows `slot` and `mode` to `uint8` before range checking. Values can wrap; validate the complete parsed number first.
- `SanitizeTintData` limits length/characters but does not validate each mode's numeric grammar, finite values, rule/stop counts, or all ranges. Validate structurally at the server and client boundaries.
- Several server maps pack `(ownerLow << 8) | slot` into a `uint32`, retaining only 24 owner bits. Use a full-width owner/slot key.
- The payload allowance is 360 bytes; verify serialized message size against the actual client/server addon transport and introduce bounded framing only if needed.
- Source contains hardcoded developer debug-log paths. Remove those diagnostics from the port.
- Polling visibility every 500 ms and scanning player lists needs load measurement; prefer existing visibility notifications where available.
- Define cleanup/ownership behavior for item deletion, trade, mail, bank moves, character deletion, and reacquisition. The examined server hooks do not establish complete lifecycle coverage.
- Donor notes intentionally exclude character select, dressing room, and paperdoll from reliable tint display to prevent NPC bleed. The older module README claims broader preview support; do not use it as acceptance evidence.
- Preserve cold-login promotion, UI-reload recovery, GUID canonicalization, batching, and selective cache invalidation. Replacing those with forced full-body rebuilds on every update recreates known observer corruption.

**Relative effort:** high. This is a client + UI + server + schema feature, with classic and Retail render coverage.

## 9. What Eclipse-DEV actually is

Eclipse is a **32-bit D3D9 cosmetic overlay and asset production project for stock build 12340**. Its build produces `dinput8.dll` as a proxy and uses ImGui, stock-client offsets/splices, private catalogs, MPQs, and local profiles. It is not a replacement game server or a ready x64 native client library.

The runtime's active navigation, read from `src/eclipse/ui/UiShell.cpp`, is:

**Wardrobe, Mounts, Cosmetics, Spells, Titles, Colour, World, Font, Multiplayer, Settings.**

### Repository inventory

| Area | What is present | Native Esteria value |
| --- | --- | --- |
| `src/eclipse/wardrobe/` | Catalogs, dressing, morph state, previews, loadouts, share codes, specialization bindings, radial shortcut wheel, Stylist | A native wardrobe and outfit workflow. |
| `src/eclipse/races/` | Customization, race textures, masks, garment binding, runtime physics | Equipment/customization fixes and physics inputs; integrate into existing Retail owners. |
| `src/eclipse/color/` | Gear/weapon/character color, world paint policy, outline pass | Reusable color math and policies; competing render/state owners must be consolidated. |
| `src/eclipse/spells/` | Spell/aura visual substitutions, conditional proc looks, spell colors, visual optimization, hearth/enchant/incarnation catalogs | Native cosmetic spell system and configurable effect density. |
| `src/eclipse/mounts/` | Mount catalogs/stages, Retail rendering/particles, mount skin color | Mount appearances and missing material/particle fidelity. |
| `src/eclipse/world/` | Time/weather/sky/fog/distance controls, place catalog/styles, font and world text | Native atmosphere controls, text quality, and per-place styling. |
| `src/eclipse/cosmetics/` | Aura catalog/classification and figure attachments | Wings, back pieces, head/shoulder/hand cosmetics, and auras through the client's effect system. |
| `src/eclipse/social/` | Appearance records, peer synchronization, transport, cosmetic titles | Useful schema and observer behavior; not the preferred Esteria transport. |
| `tools/races/`, `tools/bodies/` | Body/customization conversion, skeletons, face/helm cuts, geometry and pack gates | Offline preparation and regression fixtures. |
| `tools/midnight/`, `tools/warwithin/`, `tools/lib/` | Expansion item scope, download/convert/fit/physics/pack tools and shared file readers | Retail equipment dependency closure and fit validation. |
| `tools/mounts/`, `tools/retailmounts/` | Realm/Retail mount inventory, conversion, kits, sounds, tables, pack/fidelity stages | Content production, independently of overlay injection. |
| `tools/tests/` | Color, layout, fit, packing, physics, world-text and glyph harnesses/oracles | Bring relevant tests with each extracted algorithm. |
| `re/`, `references/` | Stock/retail evidence tooling and reference sources | Reference evidence; avoid treating derived binary addresses as x64 interfaces. |
| `server/eclipse-net/` | JavaScript Cloudflare Worker/Durable Object appearance relay, tests, Wrangler config | A cosmetic relay, not AzerothCore gameplay or persistence. |
| `third_party/`, `assets/`, `src/eclipse/generated/` | ImGui, a patched DX9 backend, art/type resources, 26 generated files observed | Supporting dependencies and generated catalogs; generators/inputs still matter. |

Reference directories present include `saty-source-3.3.5`, `kalibros-extended`, `wxl-client`, `retroport-items`, and `retail-re`. Source paths for the first two and `third_party/imgui/imgui.cpp` were present. This does not establish that the downloaded Retail binary/data or all build inputs are present.

### Important distinctions

- **Cosmetic morphs are not playable-race implementation.** Eclipse documents private race IDs 64..127 and packs a whole customization look into a facial-hair slot byte. Esteria already maps Retail races and has richer appearance messages; importing those IDs/bytes directly would collide with existing meanings.
- **Wardrobe entries are not earned transmog unlocks.** `Morph.h` explicitly describes unrestricted local selection, with no ownership/cost gate.
- **Mount swaps do not grant mount movement abilities.** They replace visual displays; server spells/vehicle behavior remain separate.
- **Titles and typed cosmetic names are not server achievements or player identity changes.**
- **Multiplayer is an optional cosmetic channel.** `Net` uses outbound WebSocket/TLS; `Multi` validates/applies appearances. It does not replace game movement/combat synchronization.
- **Earlier rotation/ESP/cast-telegraph modules are not in the active CMake source list.** The build file describes them as retired. They are not native feature candidates identified by this review.
- CMake also includes Warden/memory-check masking, access/ban behavior, custom heap, and address-space recovery. Those are overlay infrastructure, not required cosmetic features. Do not transplant the proxy, masking, fixed-address detours, or 32-bit memory workarounds into the native client.

### Actual asset availability

Pipeline notes describe these products:

- Race and race-equipment MPQs plus customization manifests.
- Realm item catalogs and art.
- Midnight/War Within weapon, armor, and helm archives with catalog/geometry/physics tables.
- Realm and Retail mount packs, effects, sounds, and display tables.
- Spell/visual catalogs used by cosmetic pools.

The local checkout has none of the `Eclipse/` output tree. Restore inputs and run the owning pipeline before calling any of those assets importable. Recorded counts such as 8,211 cosmetic looks, hundreds of mount models, or multi-GB race packs are **donor documentation**, not a fresh census of available local pack bytes.

## 10. Eclipse capabilities worth building natively

| Priority | Capability | What to extract | Esteria implementation / main dependency |
| --- | --- | --- | --- |
| A | Wardrobe, saved outfits, share codes | `Appearances`, `Dressing`, `Morph`, `Share` | Typed appearance state and native model previews. `ECA1:` shares serialized outfits; imported data needs bounds and ID validation. Server decides unlock restrictions. |
| A | Loadout on spec switch and shortcut wheel | `Spec`, `Wheel` | Use real talent-change events and native input/UI; adopt login state without treating login as a spec switch. |
| A | HD fonts and Retail-style world combat numbers | `Font`, `RetailWorldText.h`, `RetailGlyph.h`, font oracles | Modify native Gxu font atlas/raster/layout and world-text producers; no stock-address patching. Preserve locales and source fonts. |
| A | Aura-based cosmetics | `AuraIndex`, `AuraClass`, `KitFigure`, `SpellFx::WearAura` | Reuse `CGObject_C::PlaySpellVisualKit`, private per-owner rows, attachment APIs, and server-approved persisted choices. |
| A | Asset/fit validation tooling | Race/item/mount converters, readback, closure, fit fixtures | Import narrowly into existing Retail tooling. Prefer native support where it already loads the format rather than converting everything back to MD20. |
| B | Spell/aura visual swaps and colors | `SpellFx`, `Procs`, `SpellColor`, BLP/M2 recolor helpers | One caster/owner-aware resolver over private visual records. Keep spell mechanics unchanged; update all visual-finish/lifetime paths. |
| B | Spell effect-density controls | `Optimize`, `RaidDb` | Source-aware filters with mechanics protected. Rebuild mechanic classification from Esteria's server scripts, including custom encounters. |
| B | Gear/weapon/character/mount recolor refinements | `WeaponColorEngine`, `GearColor`, `CharColor*`, `MountSkin` | Use one owner/cache system shared with requested WXL recolor. Add optional color modes deliberately; preserve WXL semantics and item-instance identity. |
| B | Atmosphere and place-specific world colors | `Misc`, `Places`, `UiColorWorld`, `Color` | Native DayNight/weather/camera APIs and typed terrain/WMO/doodad material phases; settings keyed by Map/Area IDs. |
| B | Mount appearance browser and randomizer | `MountRoom`, `MountDb`, `RetailMounts`, `UiMountRandomizer` | Reuse current mount/model system and server authority; check rider poses, seats, summon loops, sounds, and vehicle cases. |
| B | Outfit Stylist | `StylistCore.h`, `Stylist`, `style_dna.bin` | Pure bounded outfit selection plus existing appearance catalogs. Keep source-specific item availability and fit rules. |
| C | Retail M2 garment physics | `Physics`, `PhysicsLive`, PHY3 generator and oracles | Port portable solver and data reader; replace x86 glue with typed scene/bone lifecycle, animation-thread synchronization, and destruction. |
| C | Broad race/collection/material backport | `Races`, `CharCustom`, `RaceTex`, `GarmentBind`, `RetailRender` | Extract missing capabilities into current Retail importer. A second race loader/compositor would conflict with it. |
| C | Cosmetic titles/name presentation | `Titles`, `TitleDb` | Treat as realm policy/presentation and server-approved state, rather than overwriting authoritative names or earned titles. |

### Shared ownership decisions

**One outline renderer:** use the requested wooooow target/mouseover behavior as the starting policy. Eclipse offers inner/outer borders, per-category coloring, class/health colors, pulse, and matching multisample-mask ideas. Add selected improvements to that renderer; do not run a second mask/composite stack. Eclipse's current outline is also D3D9-specific, and its notes say a new Retail-faithful outline port is not started.

**One recolor owner:** use WXL's requested modes and per-item-instance persistence as the initial contract. Eclipse's pure color math and private-layer approach are useful additions. Its `Tint.item` is an item entry, which alone cannot distinguish two owned instances of the same item; retain the server's instance key.

**One appearance authority:** integrate outfits/cosmetics into Esteria's server, not a second external relay and per-frame watchdog rewriting server-fed descriptors. Use the existing observer synchronization pattern, explicit capabilities, and a versioned visual-state record. Existing `RLK1` looks are 132 bytes; `BodyShapeSync` already consumes `0x524` appearance updates and form acknowledgments. Audit the current opcode table before allocating anything; documentation references old free ranges.

**One asset namespace:** catalog IDs, race IDs, generated display rows, virtual texture names, cache keys, and spell-kit pools must be remapped against current Esteria data. Preserve asset origin/build and resolve aliases explicitly.

**One UI route:** expose high-level features through the existing client UI and Extensions/Lua boundary. Eclipse's ImGui concepts/catalog logic can transfer without bringing its D3D9 overlay, input proxy, and independent settings shell.

### Physics is valuable, but separate from existing M3 physics

Eclipse's M2 item physics is a portable solver plus raw-client glue and generated `.phys` tables. The donor documents private per-item worlds and shared per-scene worlds, with updates after bone animation and before draw.

Esteria's ASECM3 already has a separate physics/rendering implementation. Do not replace it or assume that it supplies Retail M2 garment physics. First prove one M2 item on one body; then shared scenes, preview scenes, threads, model destruction, and multiple items. Keep physics disabled for unsupported data rather than claiming that every Retail item is supported.

## 11. Donor maturity and unresolved evidence

Eclipse has unusually useful validation infrastructure, but its own notes contain unfinished work:

- **Fonts/text:** rendered-string/color/width gates remain required; a reported cut-text issue is unresolved; several Retail-specific inputs are approximated. In-game performance is unmeasured in the text notes.
- **Outline:** current D3D9 implementation exists, while the new Retail outline port is listed as not started. The text pipeline reports a missing old shader generator; the mounts notes name `tools/lib/fxc.py` as a replacement helper. Reconcile the current regeneration command before modifying generated shaders.
- **Items:** notes retain failed helm/body fit combinations, SD/HD clipping cases, an unfinished enchant preview, and requested in-game rechecks.
- **Races:** documentation retains collection/fit/glow caveats and says `races_check.py` predates runtime-look behavior. Passing that old check would not prove current customization.
- **Physics:** notes list contact/jump differences, removal-order differences, unported culling/carry cases, and a live scene-splice confirmation still outstanding.
- **Mounts:** one early section calls four models encrypted; a later note says those were zero-filled provider responses and CDN recovery succeeded. Recompute actual availability from the pinned build rather than copying the older count.
- Header comments sometimes lag pipeline notes, including physics table versions and world type descriptions. Resolve disagreements from current implementation plus targeted tests.

These are reasons to extract features with their tests, not evidence that the whole project is unusable. Numeric file/algorithm gates are valuable; they do not establish hardware rendering, equipment appearance, observer synchronization, or clean shutdown in Esteria.

## 12. Implementation batches and acceptance

All names below are proposed, not files created by this investigation.

| Batch | Concrete change boundary | Done when |
| --- | --- | --- |
| 1: metrics | Gx counter sites, Lua stats, `showPerformance`, small UI | All supported backends count predictably; enable/disable works; baseline workload captured. |
| 2: camera | Camera alpha boundary, one setting | Close-up character remains opaque; first-person and intentional effects retain correct behavior. |
| 3: outline | Gather policy, model draw seams, depth, shaders, UI | Target/mouseover/gear/mount occlusion works with declared backend support and reset/MSAA cases. |
| 4: beams | Corpse tracker/draw, hint reader, existing server configuration | Correct pre-open quality with server hint; fallback works; rights/despawn/partial loot behave correctly. |
| 5a: recolor core | Typed owner/slot/instance state, classic + Retail composition/material paths, all three modes | Two differently colored owners sharing assets never contaminate one another; previews have explicit context. |
| 5b: recolor network | Native Lua API, communication, module, migration | Persistence and observers agree after reload/relog/equip/trade/range/map transitions. |
| 6: Eclipse wardrobe/text | Selected pure catalogs/state/algorithms and tests | Uses current appearance authority; no duplicate renderer/importer/relay. |
| 7: expanded cosmetics/physics | Private visuals, asset tooling, scene/bone integration | Targeted feature and performance gates pass on supported content. |

### Focused verification to carry with implementation

- **Overlay:** known primitive counts, indexed/unindexed/instanced cases, one completed-frame latch, counter disable, CPU sampling failure/time wrap.
- **Camera:** normal close zoom, first person, stealth/ghost/procedural alpha, vehicles/possession, toggling from a faded state.
- **Outline:** hostile/neutral/friendly target and mouseover, alpha-cutout hair/capes, gear/mounts, walls/terrain, UI ordering, MSAA, resolution change/reset, Retail M2 and explicitly supported M3.
- **Beam:** hint decoding 0..9 and invalid values, eligibility, quality initially uncached, tracking cap, rights, partial loot, despawn/zone cleanup, beam-only enable state.
- **Recolor:** solid/selective/gradient numeric fixtures; alpha untouched; two owners sharing one texture/model; queued stale builds; model destruction/reuse; separate preview contexts; malformed packets; item-instance lifecycle; two-client observer behavior.
- **Eclipse extraction:** retain relevant `weaponcolor_test.py`, glyph/worldtext oracles, physics/table tests, fit/pack/closure gates. Restore their required inputs and adapt harnesses to native types.
- **Regression:** x64 primary build, relevant Win32 layout check if supported, Retail assets on/off, affected extension disabled, and each supported rendering backend. Do not repeat expensive unrelated asset rebuilds.
- **Live acceptance:** cold start, actual gameplay/character rendering, equipment appearance, two-client visibility, relog persistence, and clean exit. Record offline, build, and live results separately.

No “zero FPS cost” claim is established by source comments. Compare representative idle, crowded-city, and raid workloads with each feature off/on, using the overlay as an initial tool and proper frame-time measurements where needed.

## 13. Licensing, next action, and limits

The no-transparency source/manifest explicitly declares GPL-3.0-or-later. LootBeam.cpp carries GPL-3.0-or-later language. GearRecolor.cpp declares GPLv3 by reference to the WXL license. No top-level license was found in the inspected root listings of wooooow, gear-recolor, or Eclipse. Preserve individual notices and establish the grant/provenance for imported custom code before redistribution; a public repository by itself is not a complete license grant.

**Recommended first implementation scope:** metrics + camera behavior, then a scoped outline/beam port with backend work explicitly included. Gear recolor follows as one complete client/server feature. Eclipse should supply selected catalogs, algorithms, content tools, and tests to those native systems.

This report does not install WXL DLLs, bring in Eclipse's proxy, rebuild/download multi-GB packs, change the live server, or assert that any requested feature is already integrated. Remaining engineering risk is concentrated in graphics-backend parity, asynchronous/shared-asset recolor isolation, and agreement between client visual state and server persistence.

