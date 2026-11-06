# Feasibility (Phase 0)

Client paths are relative to `R:\Users\Zach\Documents\GitHub\wowdecomp`. Server paths (prefixed `server:`) are
relative to `R:\Users\Zach\Documents\GitHub\EsteriaWoW`. Everything here was inspected only. Nothing was built
or run in game.

## 1. Can stock GameObjects carry what decor needs?

| Need | Server | Client | Verdict |
|---|---|---|---|
| **Full quaternion rotation** | `GameObject::Create` takes `G3D::Quat` (server:`src/server/game/Entities/GameObject/GameObject.cpp:270`). `SetWorldRotation` unitizes it and packs it into 64 bits as x:22/y:21/z:21 (`:2243-2271`). Sent when `UPDATEFLAG_ROTATION` is set, which is the GO default (`:63`; `Object.cpp:515`). | Create block reads the 64-bit value when flag 0x200 is set (`WoW/Source/ObjectMgrClient/CClientObjCreate.cpp:133-136`). It's stored into the passenger (`GameObject_C.cpp:1891`) and decoded as a `C4QuaternionCompressed` (`WoW/Source/Object/Passenger.cpp:86-99`). `UpdateWorldObject` applies Translate·Rotate·Scale (`GameObject_C.cpp:2618-2644`). | **Yes**, with about 21-bit precision per axis. |
| **Rotation change after spawn** | Rotation reaches clients only in the create block. No values-update field carries it (the packed value sits in the movement block). | Same: read once at create. | **Respawn needed.** To move or rotate, destroy the object for viewers and re-create it, or use a custom opcode (see §4). |
| **Per-object scale** | `SetObjectScale` writes `OBJECT_FIELD_SCALE_X` (server:`src/server/game/Entities/Object/Object.h:129-130`). Create sets it from the template's `size` (`GameObject.cpp:321`), and a module can override it per spawn. | `GetScale()` feeds the world matrix (`GameObject_C.cpp:2622`). Spell ghost previews use the template scale (`Spell_C.cpp:8530-8532`). | **Yes.** Live re-scale is a values update, but whether the client rebuilds its matrix and bounds on that change is **unverified**. |
| **Dense per-owner placement** | Each decor is a full GO with update fields and grid visibility. GO GUIDs are map-local (`GenerateLowGuid`, used by mod-guildhouse at `mod_guildhouse_butler.cpp:376`). | Each GO creates its own `CM2Model` and map entity (`GameObject_C.cpp:3199-3213`). The M2 shared data is cached per file name (`Engine/Source/Model2/M2Cache.cpp:232`, `M2Scene.cpp:1561`), so 100 identical chairs share one model load. | **Yes, for interior counts (hundreds).** The cost per GO is network create plus a scene entity. Not measured. |
| **Collision (client)** | n/a | Non-transport GOs create a world object from their model (`CGGameObject_C_Type_WorldObj::CreateWorldObj`, `GameObject_C.cpp:3218-3238`). `CanBeSolid` defaults to true (`:1685-1687`). The world bounds come from the model's collision bounds (`:2636-2637`). | **Probably.** It depends on `PrepareModel` keeping retail collision triangles. **Unverified.** |
| **Collision / LOS (server)** | `GameObjectModel::Create` looks the displayId up in `vmaps/temp_gameobject_models` (server:`src/common/Collision/Models/GameObjectModel.cpp:44-108`). It applies scale (`:132-133`). | n/a | **No server collision for decor.** The extractor can't read MD21 and the displayIds are new. Movement is client-authoritative in 3.3.5 anyway, so this only affects server LOS and pathing. That's acceptable for an MVP. |
| **Display rows** | The template's displayId must exist in `sGameObjectDisplayInfoStore` (server:`src/server/game/Globals/ObjectMgr.cpp:2987`). That store also loads the `gameobjectdisplayinfo_dbc` SQL table (`DataStores/DBCStores.cpp:446`), so rows can be added by SQL. | The client reads `m_modelName` from `GameObjectDisplayInfo.dbc` (`GameObject_C.cpp:153-156`, `:530-533`). The top copy is in `Patch-ZZZZZZZZZZZZZZZZZZZZZZZZCoA.MPQ`. | **Yes.** IDs 60000–69999 are free on both sides (checked read-only). The model name must be `retail\<FDID>.m2` (see existing-assets: stock MD20-only loader, `M2Shared.cpp:1683-1687`). |

## 2. Client building blocks

| Need | Where | Notes |
|---|---|---|
| M2 instancing | `CM2Scene::CreateModel` (`Engine/Source/Model2/M2Scene.cpp:1561`) and the `CM2Cache::CreateShared` per-name cache (`M2Cache.cpp:232`) | Retail models are released per name (`M2Shared.cpp:2115`). |
| Retail model path | SFile routes names the Retail source `Handles` (`Engine/Source/BlizzardCore/SFile2-Core.cpp:708`, `:1172`). `CascSource::Read` prepares the M2, skins and textures (`Engine/Source/Retail/CascSource.cpp:550-626`). | Online CDN fetch with a cache (`Data/Retail/RetailCasc.ini`). It also takes packed `EsteriaRetail\…\<fid>.bin` originals (`CascSource.cpp:180-193`). |
| Ghost preview and placement cursor | Spell "world object" targeting: it creates a ghost M2 from a GO template (`Spell_C.cpp:8508-8513`), applies position, facing and scale each frame (`:8494-8501`), and takes the position from the cursor at `:4852`. | **Stock effect 81 sets a "housing" mode** (`Spell_C.cpp:9101-9110`). In it, right-click rotates 90° instead of cancelling (`WoW/Source/Ui/GameUI.cpp:3865-3871`, `Spell_C.cpp:3335-3340`). That's Blizzard's leftover WotLK housing hook and a ready-made Basic-mode cursor. |
| Raycast / picking | `World::Intersect` → `CMap::Intersect` (`WoW/Source/WorldClient/Map.cpp:5071`), with the hit owner in `s_m2Scene->m_lastHitOwner` (`:5089`). `CGWorldFrame::HitTestModelList` (`WoW/Source/Ui/WorldFrame.cpp:988`). | Enough to pick a decor GO under the cursor and find the surface hit point. |
| Lua ↔ C++ | `sExtensionMgr->RegisterLuaFunction(name, fn, FRAMESCRIPT_CONTEXTS_GAME)` (examples: `Extensions/CharacterSwap/src/CharacterSwap.cpp:432-437`, `Extensions/RetailPreview/src/RetailPreview.cpp:219-220`) | Copy the CharacterSwap extension's shape: Lua functions, request opcode, result handler. |
| Custom packets | `ExtensionMgr::RegisterMessageHandler` for SMSGs past the stock table (`WoW/Source/Extension/ExtensionMgr.h:98-112`; stock `NUM_MSG_TYPES = 0x51F` at `WoW/Source/Net/Types.h:1319`). `SendClientMessage` for CMSGs. | Opcodes in use: 0x521–0x536 (server:`src/server/game/Server/Protocol/Opcodes.h:1343-1364`), and 0x5F0 for HelloWorld (`Extensions/HelloWorld/src/HelloWorldNet.cpp:10`). |
| Free camera, gizmo, terrain | Not in wowdecomp. | `wxl-housing` has `FreeBuildCamera.cpp`, `Placement.cpp` and ImGuizmo, but against the WarcraftXL ABI. Reference only. |

## 3. Server: a private interior

| Option | Evidence | Assessment |
|---|---|---|
| **Phase mask on a shared map** (mod-guildhouse) | `GetGuildPhase = guildId + 10` used directly as a phaseMask (server:`modules/mod-guildhouse/src/mod_guildhouse.cpp:93-96`). It's applied with `SetPhaseMask` (`:653`). | **Unsuitable for per-player houses.** A phase *mask* has 32 bits, and `id+10` isn't a single bit, so guild 1 → 11 = 0b1011 overlaps the normal phase 1 and other guilds. It can't isolate more than about 31 owners. |
| **Instance map per house** | `MapInstanced::CreateInstanceForPlayer` picks the destination via `InstanceSaveMgr::PlayerGetDestinationInstanceId` (server:`src/server/game/Maps/MapInstanced.cpp:140-150`; `Instances/InstanceSaveMgr.cpp:960`). A new instance ID is generated when there's no bind or group (`MapInstanced.cpp:168-174`). | **Right isolation model.** Each owner gets an instance and decor is spawned as temporary GOs when it loads. **Gap:** a visitor who isn't in the owner's group won't route into the owner's instance. That needs either (a) a group-invite visit flow, with no core change, or (b) a small core hook so the module can pick the destination instance ID. |
| **Which map** | `map_dbc` max ID is 1469 (read-only query). A new map needs Map.dbc on both client and server, plus ADT/WDT/WMO. | MVP: reuse an existing small instanced map whose geometry is an interior WMO. Phase 1 picks one. A dedicated housing map comes later. |

## 4. A vs B vs hybrid

| | A: stock GO + instance | B: custom client prop layer + custom opcode | **Hybrid (recommended)** |
|---|---|---|---|
| Rendering | GO → GameObjectDisplayInfo → `retail\<fdid>.m2` | Client spawns M2s itself from a server list | GO rendering as in A |
| Replication / visibility | Free (grid and visibility system) | Must write our own sync, visibility and join-in-progress | Free |
| Transform edits | Respawn per edit (rotation only travels in create) | Exact, live | Edit via custom CMSG. Server validates, persists, then respawns the GO. Client ghost shows the change at once |
| Collision | Client GO world object | Must call `CWorld::ObjectCreate` ourselves | Client GO world object |
| Interaction (sit, use, loot later) | Stock GO types | None | Stock GO types |
| Server authority | Full | Server stores the list, client renders it | Full |
| Engine change | None for display. Opcodes still needed for an editor | Large | Client extension (editor UI and opcodes) plus server module and opcodes |
| Works for non-modded clients | Visible decor if they have the DBC rows | No | Visible decor. Editing needs our client |

**Recommendation: Hybrid.**
- Decor is a temporary server GameObject, one `gameobject_template` per GameObjectDisplayInfo row, with a
  quaternion and scale per spawn, inside a per-house instance.
- Editing is a small set of custom opcodes ([protocol.md](protocol.md)) handled by `mod-esteria-housing`.
- The client side is a new extension that reuses the Spell world-object ghost and `World::Intersect`.
- No existing property system fits: mod-guildhouse is guild-scoped and its phasing is broken for this use.
  So the new module stands as the README planned.

## Open technical risks

1. **Some decor may fail `PrepareModel`** (strict shader rejection for generic models, `CascSource.h:33-35`).
   Mitigation: Phase 1 probes the 20 slice FDIDs first.
2. **Collision triangles after preparation.** Unverified; needs an in-game walk test.
3. **Opcode registration needs a core edit.** Esteria opcodes live in core
   (server:`src/server/game/Server/Protocol/Opcodes.h:1343-1365`, `Opcodes.cpp:1445-1475`), and `NUM_MSG_TYPES`
   must grow from 0x537. A module can't add them alone. Handlers can still forward into the module, following
   the `CharacterSwapHandler.cpp` pattern.
4. **Online-only assets.** `mode=online` means first-time decor needs the CDN.
5. **Stock-path overrides by Patch-Housing.MPQ** ([existing-assets.md#shadowing](existing-assets.md#shadowing)).
   This is a problem today, independent of housing.
