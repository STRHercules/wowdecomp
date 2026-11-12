#pragma once
#include <cstdint>
#include <string>
#include <vector>
class CGUnit_C;
class CM2Model;
class CM2Scene;
class CAaBox;
class CAaSphere;
class C3Vector;
namespace ASECM3::Client {
// Dedicated native queue entry; never takes over the player-name callback.
inline constexpr int UnitWorldElementType=6;
bool QueueUnitWorldModel(CM2Scene* scene,CM2Model* model,uint32_t& elementIndex);
void DrawUnitWorldModel(CM2Model* model,int32_t packet,uint32_t flags,uint32_t pass);
bool HasUnitWorldModel(const CGUnit_C* unit);
bool UnitCameraBounds(const CGUnit_C* unit,float& lowerZ,float& upperZ,bool& pending);
// Main-thread native bounds bridge. No parsing, animation or shared M2 writes.
bool UnitModelBounds(const CM2Model* model,CAaBox& box,bool worldSpace=false,bool maximum=false);
bool UnitModelSphere(const CM2Model* model,CAaSphere& sphere,bool worldSpace=false,bool maximum=false);
// True means the M3 binding owns this query, even if loading or no hit.
// Native mode3 uses source collision geometry, other modes use render triangles.
bool UnitPickingSphere(const CM2Model* model,CAaSphere& cameraSphere);
bool UnitPickGeometry(const CM2Model* model,const C3Vector& start,const C3Vector& dir,float maximum,float& distance,bool& hit,bool worldSpace=false);
void UpdateWorldBindings(uint32_t time);
const char* PrepareUnitModel(CGUnit_C* unit,const char* path);
void AttachUnitModel(CGUnit_C* unit);
void RemoveUnitModel(CGUnit_C* unit);
const char* PrepareMountModel(CGUnit_C* unit,const char* path,int32_t displayId,int32_t modelId);
void AttachMountModel(CGUnit_C* unit);
void RemoveMountModel(CGUnit_C* unit,CM2Model* expected=nullptr);
CM2Model* CreatePreviewModel(CM2Scene* scene,const char* path);
void UpdatePreviewModel(CM2Model* model);
void RemovePreviewModel(CM2Model* model);
void SetUnitAction(CGUnit_C* unit,uint32_t action);
// Called when native code starts a base animation (attacks, casts, emotes): the M3 plays its matching clip once.
void OnUnitBaseAnimation(CGUnit_C* unit,uint32_t animId,int32_t flags);
// One melee swing (the server's attacker round): restarts the attack clip every time, even while one is playing.
void OnUnitSwing(CGUnit_C* unit,uint32_t animId);
void SetUnitEmote(CGUnit_C* unit,uint32_t action);
void ClearUnitEmote(CGUnit_C* unit);
void ResetWorldBindings();
bool SetUnitEffectSequence(CGUnit_C* unit,int32_t sequence,std::string& error);
void RegisterWorldEffectScriptFunctions();
void RegisterDBCModelScriptFunctions();
void RegisterPlayerModelScriptFunctions();
// Path set by ASECM3_SetPlayerModel for the local player, or nullptr.
const char* PlayerModelOverride();
struct UnitWorldInfo {bool bound=false,loading=false,ready=false,fromDBC=false;uint32_t objectType=0,effectSequences=0,effectMaterialRecords=0;int32_t displayId=0,modelId=0,effectClip=-1;uint64_t draws=0,checks=0,failures=0,effectParticles=0,effectSpawned=0,effectDropped=0,effectDraws=0,effectBirthSamples=0;std::string path,error,animationSource,effectAnimationSource,effectError,effectSimulationError,effectDrawError,effectMaterialError;std::vector<std::string> effectClips;};
UnitWorldInfo GetUnitWorldInfo(const CGUnit_C* unit);
}
