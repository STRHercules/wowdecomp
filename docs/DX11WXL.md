# DX11 + WXL integration plan for Esteria wowdecomp

**Status:** RESEARCH / IMPLEMENTATION PLAN ONLY. **Do not implement, merge, configure, build, install, or deploy as a result of this document.**  
**Prepared:** 2026-10-20.  
**Local checkout:** `STRHercules/wowdecomp`, branch `Esteria`, inspected HEAD `3611992ebe44f7d522f018b94ff9f8119e6e15a3`.  
**Donor:** [Furioz420/wowdecomp-dx11](https://github.com/Furioz420/wowdecomp-dx11), `main` at `fcf6f8900634290ae2716bdc4b7e4173c071e1ae`. Latest **compiled/tested code** checkpoint `69b5ba38be507fcd41c2877c399d5748fee0f8d0` (2026-10-18). Donor `main` is a later documentation commit, not a newly validated binary.  
**Upstream donor merge base:** SatyPardus/wowdecomp `b1c71b8bd108823bca952f16c6f83a7ad33b9648` according to the donor's handoff. **Do not assume this is the merge base of our Esteria branch.**

## 1. Decision and non-negotiable constraints

**Recommended approach: controlled, source-level port.** Integrate Furioz420's native DX11 implementation as the selected DX11 backend, alongside the existing D3D9, D3D9Ex, OpenGL and Vulkan backends. Treat WXL as a **separate modular feature portfolio**, with adapters to Esteria's current Retail importer, custom races, server protocol and graphics interfaces. Do not replace the whole repository or merge the fork wholesale.

Desired steady state:

| Area | Design requirement |
| --- | --- |
| D3D9 / D3D9Ex | Leave existing source, device factory, default behavior and launch modes functional |
| OpenGL | Preserve existing backend |
| Vulkan | Preserve `GxApi_Vulkan = 6`, dedicated `GxVulkan` target, C++20 boundary, Vulkan launch and Retail default |
| DX11 | Furioz420's native `CGxDeviceD3d11` using the Wrath shader corpus and Microsoft converter |
| Current Esteria DX11 | Keep an independently buildable fallback until acceptance; only one DX11 factory linked per binary |
| Retail CASC / models / races | Retain Esteria data source, existing race mapping, model/rig/equipment/animation fixes and unfinished persistence work |
| WXL | Native x64 modules incrementally enabled, with explicit feature ownership and runtime checks |
| Server | Existing AzerothCore/custom protocol remains authoritative; do not activate new WXL opcodes without server parity |
| Distribution | Do not package game MPQs, server secrets, personal CASC configs, or unlicensed binaries |

**No code modifications were authorized with this document.** All options, filenames and integration wiring marked *proposed* below are designs for a later implementation phase, not existing Esteria behavior.

## 2. Verified existing code and why a merge is risky

### 2.1 Current Esteria implementation (inspected locally)

- `Engine/Source/Gx/CGxDeviceD3d11/` **already implements native DX11** with `CGxDeviceD3d11.cpp`, `CGxD3d11Resources.cpp`, `CGxD3d11State.cpp`, `CGxD3d11FixedFunction.cpp` and `ShaderTranslator.cpp/.h`. It uses a local SM2/SM3-to-HLSL translator and DX11 device/resource handling. **This is not an empty placeholder.**
- `Engine/Source/Gx/Gx.cpp` already routes `GxApi_D3d11 = 4` through `CreateCGxDeviceD3d11()`, and `GxApi_Vulkan = 6` through `CreateCGxDeviceVulkan()`. `GxApiSupported` currently advertises both.
- `WoW/Source/ConsoleDevice/ConsoleDevice.cpp` already supports `-d3d11` and `-vulkan`, plus `gxApi` selection. The current command-line checks evaluate Vulkan **after** D3D11. Preserve existing behavior unless launch precedence is deliberately specified and tested.
- `Engine/Source/Gx/CMakeLists.txt` uses a recursive source glob for Gx, excludes `CGxDeviceVulkan/` and links `GxVulkan` separately. Blindly adding a second DX11 source tree inside Gx would compile duplicate methods, factory symbols and/or incompatible headers.
- `Engine/Source/Gx/CGxDeviceVulkan/CMakeLists.txt` builds the independent C++20 Vulkan target; the rest of Gx uses C++17.
- `GxDefaultApi()` selects **Vulkan** under `WOWDECOMP_RETAIL_ASSETS` and otherwise D3D9. **Do not change the Retail default to DX11 as a side effect.**
- `cmake/RetailAssets.cmake` and `Engine/Source/Retail/` provide CASC/CDN, DB2, shaders, models, texture composition and race customization. `WoW/Common/ComponentCore/RetailRace.cpp` controls creator choices and the `WOW_RETAIL_RACES` mapping.
- `docs/retail-race-handoff.md` documents the current installed custom race work. It explicitly notes ongoing uncommitted changes, incomplete extra-choice persistence, and specific completed fixes (e.g. Orc posture, Elf hands, Blood Elf blindfolds). Preserve all of this.
- `Tools/RetailAssets/shader_probe.cpp` and `cmake/RetailAssets.cmake` compile the **existing** `CGxDeviceD3d11/ShaderTranslator.cpp` directly. Replacing or removing that translator without migrating the test target is a build break.
- `CMakeLists.txt` also wires ExtendedLimits, RetailAssets, Extensions, resource art and Windows x86/x64 targets. The donor root CMake does **not** include Esteria's full feature tree. Never overwrite it.

**Working tree warning:** the local `Esteria` checkout was already dirty during inspection, including active modifications to Retail CASC/DB2/appearance/model files, Model2, ComponentCore and CMake, plus new untracked Retail files. Some files changed during this research session, consistent with other ongoing work. Do not reset, stash, clean, overwrite or commit that work without explicit approval. Preserve existing user-built binaries and local client data separately.

### 2.2 Donor implementation (inspected on GitHub)

- DX11 source: `Engine/Source/Gx/CGxDeviceD3d11/{CGxDeviceD3d11.h,Internal.h,Device.cpp,Resources.cpp,State.cpp,Shaders.cpp,FixedFunction.cpp,Copy.cpp,Outline.cpp}`.
- Wrath shader parsing/translation: `Engine/Source/Gx/ShaderContainer/`, including `LegacyContainer.*`, `LegacyShaderTranslator.*` and `ShaderContainer.*`.
- Microsoft D3D9 shader converter: `ThirdParty/D3D9ShaderConverter/`, compiled as `GxD3D9ShaderConverter`. Includes the shader-conversion subset, **not** a D3D12 device implementation.
- DX11 research/tests/pinned dependency tooling: `Tools/Dx11Research/`. On the donor: `WXL_ENABLE_DX11=OFF` by default, x64-only if enabled, with WDK headers and Microsoft's AMD64 `dxbcSigner.dll` needed for shader conversion/signing.
- WXL features: public headers under `WXL/`, implementations under `WXL/modules/`, portable source under `WXL/vendor/`; modular object-library targets collect into `WxlNative`/`WXL::Native`. The donor builds WXL modules into the executable even when `WXL_ENABLE_NATIVE=OFF` suppresses behavior.
- Donor `Gx` and root CMake lack Esteria's Vulkan-specific linkage, RetailAssets, ExtendedLimits, and extension wiring. A whole-file transplant would regress existing backends/features.
- Donor `CGxDeviceD3d11` has graphics integration points for WXL outlines, refraction, material state, WMO and scene operations. Merely transplanting its header and `*.cpp` without those dependencies will not link or function.
- Donor reports a passed **11-check WARP presentation/Apply regression** at code checkpoint `69b5ba3`; this is **not** verified hardware in-game renderer acceptance.
- WXL encompasses far more than rendering: modern M2/SKIN, BLP, DB2, equipment, appearances, particles, spell visuals, WMO/split ADT, character creation, custom protocol, area loot, challenge modes, quests, radial pings and Skyriding. Integrating everything simultaneously maximizes conflicts.

## 3. Proposed architecture

### 3.1 A selectable DX11 provider, not replacement of all renderers

Add a **proposed** CMake cache selector such as `WOWDECOMP_DX11_BACKEND=ESTERIA|FURIOZ|OFF`, initially defaulting to `ESTERIA`. After passing all acceptance gates, the default may switch to `FURIOZ`; keep the old provider buildable for rollback. The exact option name is not yet implemented.

- `ESTERIA`: compile only the existing local DX11 files and translator.
- `FURIOZ`: compile only the imported native DX11 code, shader containers and pinned converter dependencies. Link exactly **one** `CreateCGxDeviceD3d11()` definition.
- `OFF`: compile neither implementation; make `GxApiSupported(GxApi_D3d11)` return false and reject/handle `-d3d11` cleanly. **Do not** advertise an unavailable API to the factory.
- All settings must preserve the existing D3D9/D3D9Ex/OpenGL/Vulkan implementations, numeric enum values and runtime selection. Do not insert a new enum before Vulkan or shorten `GxApis_Last` or the `g_gxApiNames` array.
- Preserve `GxDefaultApi()` behavior: stock default D3D9; Retail builds default Vulkan; explicit `-d3d11` selects the active compiled DX11 provider.
- Keep Vulkan's C++20 `GxVulkan` build contract unchanged. It must not depend on Furioz's converter/signer just to build or run.

**Build isolation detail:** because Gx currently glob-compiles its child DX11 directory, explicitly include/exclude the providers' source lists. Avoid placing both implementations in the same globbed path. One possible final layout is provider-specific source directories compiled into mutually exclusive target/object sets, with only the chosen provider wired to Gx and to the relevant executable. Do not introduce a circular static-library dependency with `Gx`, `WxlNative` or `GxVulkan`. Resolve the object and final-link ownership in a minimal x64 linker test before widening feature integration.

**Do not rewrite** `CGxDevice/CGxDevice.h` or `Types.h` using a donor's different layout. Verify all virtual overrides against the actual local `CGxDevice`, including device create/destroy, present/resize, gamma, pools/buffers, texture copy/stretch/resolve, shader lifecycle, fixed-function state, readback, queries, cursor, adapter discovery and undocumented no-op extension slots. Do an ABI/contract checklist before substituting the class.

### 3.2 WXL native feature ownership and adapter seams

Keep `WXL/` as the donor's modular source provenance and build modules, but add a small Esteria integration layer at the owning native call sites. **Do not create a second independent owner for the same mounted asset or player choice.**

| Feature boundary | Existing Esteria owner | Proposed WXL integration |
| --- | --- | --- |
| Mounted Retail/Forever bytes | `Engine/Source/Retail/CascSource.*`, `SFile2-Core.*` | Share an existing mounted-file read interface; add WXL storage handling only for unsupported asset families, not a competing process-global mount |
| DB2, FDIDs and appearance tables | `Retail/Db2.*`, `ClientTables.*`, `Appearance.*` | Compare schemas and decode/error paths, reuse or adapt existing lookups; avoid duplicate inconsistent caches |
| Character creator/extra choices | `ComponentCore/RetailRace.cpp`, `CharacterComponent.cpp` | Esteria creator remains authoritative. Add WXL-only optional advanced choices/appearance transport behind adapter methods and explicit ownership |
| M2/SKIN, skeleton and animations | `Model2/M2Shared.cpp` and `Retail/RetailModel.cpp` | Integrate missing WXL model/particle/skin capabilities through targeted native hooks without replacing proven rig/bone/helmet/animation fixes |
| Texture decode/materials | local Retail model/shader/compose and native BLP | Gate WXL BLP/material recipes by source asset type; keep classic WotLK behavior |
| Graphics state / outlines / refraction | `Gx` and chosen backend | Define capability hooks: Furioz DX11 implementation where supported; Vulkan adapter only after explicit parity work; DX9 uses supported fallback or feature disabled |
| WMO / ADT world bytes | native world loader and existing archive layer | Route WXL byte providers behind explicit format matching and fail-closed fallback; preserve classic world parsers |
| UI/quests/challenge/transmog | Esteria UI, Extensions, Addons and custom modules | Opt-in individual features; check duplicate commands, UI names, events and gameplay contracts |
| Custom packets and appearance saving | Esteria/AzerothCore networking and server | Require handshake, opcode availability, message bounds, versioned contracts and logout/relog verification before enabling |

**Policy:** one owner per resource lifetime, one authoritative source for each model identity, row cache and appearance selection; never silently let WXL supersede `WOW_RETAIL_RACES`, account data, addon storage or the existing native creator.

Start with WXL functions that can operate independently of renderers or server persistence. Defer combined model/material/particle/world replacements and custom network features until targeted compatibility tests pass. For WXL effects not yet supported by Vulkan, return a feature-unavailable result or use explicitly validated fallback, never feed D3D11 objects/shaders to Vulkan.

### 3.3 WXL packaging/build specifics

- Import the donor `WXL/CMakeLists.txt` and referenced `WXL/modules/`, public interfaces, `WXL/vendor/` **with provenance and required notices**. WXL's modules are C++20 object libraries in the donor. Aggregate them once into `WxlNative`. They are source-linked, **not injected Win32 DLL plugins**.
- Introduce a **proposed** build-enablement option distinct from donor's runtime master gate so stock builds and optional Win32 builds can omit WXL completely. Do not equate `WXL_ENABLE_NATIVE=OFF` with no linkage: the donor explicitly states otherwise.
- Port only the necessary native glue at first. The donor explicitly links `WxlNative` from `Model2`, `BLPFile` and `WowClient` (and `Wow` when available). Review dependencies and the one-link-object rule to avoid duplicate object definitions or circular archive failures.
- Keep WXL per-feature flags (ASSETS, DB2, BLP, MATERIALS, PARTICLES, APPEARANCES, PROTOCOL, APPEARANCE_SAVE, WMO, SPLIT_ADT, CLIENT_EXTENSIONS, plus optional UI/quest/creation/Skyriding features). Feature flags must have consistent compile definitions and safe disabled implementations.
- Preserve Esteria `WOWDECOMP_RETAIL_ASSETS` and `WOWDECOMP_EXTENDED_LIMITS` in all configurations. Establish deterministic precedence and settings migration for feature combinations. Never let an unchecked CVar change reallocate live character data.
- WXL protocol transport is optional and may compile with disabled behavior. Do not assign custom opcodes to the server merely because source code includes them.
- Audit **licensing before redistribution**. Donor WXL provenance is `WXL/VENDORED-SOURCE.json` and `WXL/APPEARANCE-VENDOR-SOURCE.json`; Microsoft's converter carries an MIT notice and NuGet dependencies have their own terms. Esteria's `docs/retail-race-handoff.md` already identifies GPL-3.0-derived `Retail/Db2.*` / `Appearance.*`. Do not imply that the entire combined codebase can be relicensed or distributed without review.

## 4. Ordered implementation process (future work only)

### Phase 0: Freeze scope and establish a reversible baseline

1. Confirm a named integration branch or isolated worktree **after explicit permission to implement**. Record current `Esteria` HEAD, local modifications, upstream/donor HEADs, existing test results, executable checksums and client-data installation.
2. Do **not** touch ongoing Retail race edits, `bin/Win/`, `build/`, `G:/3.3.5a - Dev`, server files, account data or launcher resources for the research phase.
3. Create a per-file, three-way comparison between Esteria, SatyPardus donor ancestry and Furioz `69b5ba3`. Identify files where both Esteria and donor changed the same native function. Do not replay the donor's earlier historical merge or bulk-copy shared files.
4. Catalog `// OFFSET:` annotations and existing Esteria extensions whose code must be retained. Capture current D3D9, DX11 and Vulkan smoke baselines on a disposable test client once builds are authorized.

**Exit:** complete conflict matrix and untouched production checkout.

### Phase 1: Bring in pinned DX11 support artifacts without selecting the provider

1. Stage, in a future isolated integration branch, the donor's `ThirdParty/D3D9ShaderConverter/`, `Engine/Source/Gx/ShaderContainer/` and `Tools/Dx11Research/` together with notices and provenance.
2. Pin donor source commit `69b5ba3` for code, documenting `fcf6f89` handoff docs as a separate reference. Record any later donor fixes as individually reviewed commits.
3. Preserve the existing `ThirdParty/CMakeLists.txt`, especially `VulkanHeaders`. Add converter only as an optional subdirectory/target, not a replacement CMake file.
4. Use donor `Tools/Dx11Research/fetch_dependencies.py` on a development machine, following its pinned hash verification. Dependencies are **not** checked into source. `WXL_DX11_WDK_ROOT` points at the pinned WDK Include version directory and `WXL_DX11_SIGNER_DLL` at the official AMD64 `dxbcSigner.dll`. No unofficial signer and no Win32 D3D9 proxy.
5. Establish isolated converter/container tests. Use real mounted **WotLK GXSH v3** shader bytes and true permutations. The donor's independent GXSH v4 parser is not a license to use Cataclysm/other-expansion shaders for Wrath.
6. Audit all signed DXBC loads, bounds checks and caching. Unsupported/corrupt shader families should fail closed, not show a misleading success state.

**Exit:** standalone shader parsing/conversion regression tests pass; no renderer selection or gameplay changed.

### Phase 2: Add Furioz DX11 as an alternate provider

1. Add Furioz's DX11 source as a distinct provider. Do not overwrite the local `CGxDeviceD3d11.cpp`, `ShaderTranslator.cpp` or local DX11 files.
2. Adapt the donor header and implementation to the **current** `CGxDevice`, `CGxShader`, `CGxPool`, `CGxTex` and `CGxFormat` contracts; isolate WXL-specific GPU capabilities behind narrow interfaces/stubs until WXL is connected.
3. Wire the **proposed** DX11 provider selector. Resolve double-definition hazards around `CreateCGxDeviceD3d11`, header guards, global statics and `file(GLOB_RECURSE)`. Keep `GxVulkan` out of the DX11 source set.
4. Preserve `GxApi_D3d11 = 4`, `GxApi_Vulkan = 6`, `GxApis_Last = 7` and existing CVar strings. Test command-line combinations, unsupported-provider behavior and clean shutdown.
5. Fix the `RetailShaderProbe` build seam: compile/test the old translator only for the local provider or update it to use a renderer-neutral API. Do not leave it unconditionally compiling or including removed files.
6. Add donor dependency link/copy rules **only** to the Furioz x64 provider/test executables. Place required `dxbcSigner.dll` and matching `XvidDecoder.dll` with a **test** executable, not the live client.
7. Run Furioz GPU tests first using WARP, then actual hardware. Preserve GPU resource ownership/teardown, swapchain presentation-only Apply behavior, recovery, and frame retirement; no dependence on optional FPS caps.

**Exit:** Furioz DX11 can be built/selected independently, local DX11 and Vulkan still build as before, stock D3D9 boots unchanged.

### Phase 3: Integrate WXL core behind isolated adapters

1. Stage `WXL/` and its vendored source/license manifests with the donor's declared CMake object modules.
2. First link `WxlRuntime`, `WxlStorage`, `WxlDB2` and `WxlBlp` with **features disabled**, implementing adapter seams to existing Esteria storage/DB2 rather than registering competing global services.
3. Add source-normalization/asset capability negotiation. Verify whether the asset is classic, Retail CASC, Forever or another mounted family **before** invoking WXL conversion.
4. Add one feature at a time with per-feature disabled tests. Preserve local `Retail/DecodedCache.*`, `Retail/SourceContext.h`, `RetailRace` and live customization behavior.
5. Configure independently of DX11. WXL should not force DX11 selection or make Vulkan/D3D9 impossible to build.

**Exit:** basic WXL API/modules load in x64 without changing visible stock behavior or violating existing source ownership.

### Phase 4: Merge complementary modern asset and effect features

Recommended order, each with an independent acceptance gate:

1. Modern BLP and DB2 formats not supported locally, without replacing existing lookup/texture paths.
2. M2/SKIN extensions, animation companions, skeleton mapping, equipment attachments and material semantics, after comparing to Esteria's real race-creation and in-world renderer tests.
3. WXL appearance and texture features only where the current `RetailRace`/`Appearance` system lacks capability. Preserve explicit extra-choice IDs, palettes, baked NPC textures, Orc posture, existing helmets/rigs and stable component lifetime.
4. Mesh materials, particle layering, cutoff, refraction and spell visual effects with DX11 first. Add Vulkan-specific shader/state support only after validation; do not assume donor DX11 shaders run on Vulkan.
5. Native WMO/split-ADT byte providers and world effects using bounded format detection and classic fallback.

**Exit:** targeted asset/visual tests demonstrate parity or a documented opt-out on each renderer.

### Phase 5: Optional WXL client gameplay/features and protocol

- Decide feature-by-feature which WXL additions Esteria actually needs: Retail UI/transmog, quest markers, creation effects, radial ping, challenge/mythic mode, Skyriding, spell charges, area loot, garrison/other custom consumers.
- Compare each with current AzerothCore modules and client extensions. Avoid duplicate names, callbacks, saved settings and protocol handlers.
- Donor `WXL/NativeProtocol.md` documents bounded native transport and specific example opcodes, including `0x526`, `0x540` through `0x545`, `0x550` through `0x553`. These are **donor contracts, not automatically reserved Esteria opcodes**. Validate conflicts against the exact installed server tree before activating.
- Keep `WXL_ENABLE_PROTOCOL` and `WXL_ENABLE_APPEARANCE_SAVE` OFF until a matching server capability negotiation/implementation exists. A successful client send does not constitute an ACK or persistence.
- Test newly created characters, other nearby players, the barber, logout/relog, reconnect, malformed packets and cross-client compatibility with old clients. No live character/database migrations without explicit approval.

**Exit:** versioned server/client contracts and full round-trip persistent behavior have been verified.

### Phase 6: Matrix tests, rollout and fallback

Minimum build matrix:

| Build | DX11 provider | WXL features | Retail | Expected invariant |
| --- | --- | --- | --- | --- |
| Stock x64 | current/default | off/omitted | off | D3D9/D3D9Ex/OpenGL baseline still builds and launches |
| Retail Vulkan x64 | current/default | off/omitted | on | Esteria Retail custom races, shaders and Vulkan untouched |
| Local DX11 x64 | Esteria | off/omitted | on/off | Existing DX11 fallback remains reproducible |
| Furioz DX11 x64 | Furioz | off/omitted or minimal shim | off | Real WotLK shader conversion, UI, 3D world and DX11 tests |
| Furioz DX11 + WXL x64 | Furioz | selected on | on | Modern models, appearance, world, effects with no double ownership |
| Vulkan + selected WXL x64 | Furioz compiled but not selected | supported subset on | on | Vulkan selected and healthy; DX11 resources never used |
| D3D9 + selected WXL x64 | provider irrelevant | supported subset on | off/on | Legacy rendering unaffected; unsupported effects gracefully disabled |
| Win32 compatibility | off or local as applicable | omitted | off | No unconditional x64 WXL/signer dependency in Win32 layout checks |

Renderer acceptance tests: cold start, launcher args and persisted `gxApi`; login/character creation; all mapped RaceIDs and both sexes; skin/hair/eyes/tattoos/extra choices; armor/helmets/weapons/geosets; facial animation, mounts and NPC guards; terrain, water, weather, particle, shadows, fog, WMO, UI and cutscenes; shader permutation/invalid-fixture handling; windowed/fullscreen/multimonitor, resize, alt-tab, VSync Apply and genuine render-target reset; GPU memory/resource/frame retirement under high FPS; device lost/recovery; logout/relog and old-server compatibility.

Donor tests, when **implemented and explicitly authorized**, include:

```powershell
# These binaries/configurations do not exist in Esteria yet.
# Run from a disposable working directory, never the installed WoW Data root.
NativeDx11BackendTests.exe --presentation-apply warp
NativeDx11BackendTests.exe --frame-resources warp
# Additional real WotLK shader-fixture and hardware/window modes:
# See Tools/Dx11Research/README.md from the pinned donor revision.
```

Keep a known-good Esteria executable and client-data backup. Only promote Furioz DX11 as the selected default after **actual in-game** acceptance; no need to remove the local legacy DX11 provider from source. Roll back by selecting the proven provider/build and disabling new WXL features. Do not reset race data or delete user settings as a rollback mechanism.

## 5. Conflict checklist for implementation reviewers

- [ ] **Gx source glob** explicitly selects one DX11 provider; no dual `CreateCGxDeviceD3d11()` or translator symbols.
- [ ] Vulkan `GxVulkan`, `VulkanHeaders`, enum index 6, C++20 compilation and runtime discovery survive.
- [ ] D3D9/D3D9Ex/OpenGL shader/resource code and default selection remain unchanged.
- [ ] All `CGxDevice` virtual method contracts, shader constants/permutations, vertex layouts, atlas/bone palette budgets and buffer lifetimes match Esteria.
- [ ] `RetailShaderProbe` is updated for the selected translator and remains testable.
- [ ] Root `CMakeLists.txt` retains RetailAssets, ExtendedLimits, Extensions, cursor resources and client output names.
- [ ] Existing in-progress `Retail`, `Model2`, `ComponentCore` and `SFile2-Core` modifications are preserved, reconciled and re-tested rather than overwritten.
- [ ] WXL owns no duplicate CASC source, FDID cache, texture lifetime, appearance selection, create-session or network opcode.
- [ ] WXL modules and DX11 compile independently; Vulkan and D3D9 do not require a signer DLL.
- [ ] Source/vendor license notices, upstream SHAs and Microsoft pinned package hashes retained.
- [ ] No account/character state write is enabled without matching AzerothCore server behavior.
- [ ] GPU pixel tests **and** in-game visual/function/performance parity pass; old renderer launchers tested.

## 6. Immediate next actions, once permission to edit code is given

1. Resolve ongoing uncommitted work and choose a truly isolated integration worktree; never use a destructive clean/reset.
2. Generate a **file/function level** three-way delta against pinned donor `69b5ba3`, starting with root/ThirdParty/Gx CMake, both DX11 implementations, shader translator, `Gx.cpp`, ConsoleDevice/CmdLine and `RetailShaderProbe`.
3. Implement Phase 1 and Phase 2 **without WXL features** other than minimal explicit adapter stubs needed by donor DX11; prove stock D3D9 and Vulkan untouched.
4. Introduce WXL object libraries and adapter seams, then onboard selected feature groups only after repeatable tests.
5. Promote nothing to installed Esteria clients or AzerothCore until reviewed build outputs, backups and acceptance are available.

## 7. Source references

- Donor repository: https://github.com/Furioz420/wowdecomp-dx11
- Donor developer handoff: https://github.com/Furioz420/wowdecomp-dx11/blob/main/docs/DX11-DEVELOPER-HANDOFF.md
- Donor native DX11 research: https://github.com/Furioz420/wowdecomp-dx11/blob/main/Tools/Dx11Research/README.md
- Donor pinned dependencies: https://github.com/Furioz420/wowdecomp-dx11/blob/main/Tools/Dx11Research/DEPENDENCIES.md
- Donor WXL module map: https://github.com/Furioz420/wowdecomp-dx11/blob/main/WXL/README.md
- Donor WXL protocol: https://github.com/Furioz420/wowdecomp-dx11/blob/main/WXL/NativeProtocol.md
- Donor converter provenance: https://github.com/Furioz420/wowdecomp-dx11/tree/main/ThirdParty/D3D9ShaderConverter
- Esteria local docs: `docs/retail-race-handoff.md`, `docs/retail-native-mounts.md`, `docs/retail-casc.md` and `Tools/VulkanShaders/README.md`.
- Primary local source inspected: `CMakeLists.txt`, `Engine/Source/Gx/{Gx.cpp,Types.h,CMakeLists.txt,CGxDeviceD3d11/,CGxDeviceVulkan/}`, `cmake/RetailAssets.cmake`, `WoW/Source/ConsoleDevice/ConsoleDevice.cpp`, `WoW/Source/CMakeLists.txt`.

**Research-only delivery:** This markdown file is the *sole authorized file addition*. No implementation, repo merge, dependency download, build, settings change, game installation or server change was performed for this plan.
