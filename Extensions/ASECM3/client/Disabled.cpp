// Link boundary for hosts built with WOWDECOMP_ENABLE_ASEC_M3=OFF.
// No parser, scene, physics, GPU resources or mutable registry are instantiated.
#include "UnitWorldBinding.h"
#include "AttachmentRegistry.h"
#include "PlayerCamera.h"
namespace ASECM3::Client {
bool QueueUnitWorldModel(CM2Scene*,CM2Model*,uint32_t&){return false;}
void DrawUnitWorldModel(CM2Model*,int32_t,uint32_t,uint32_t){}
bool HasUnitWorldModel(const CGUnit_C*){return false;}
bool UnitCameraBounds(const CGUnit_C*,float& lower,float& upper,bool& pending){lower=upper=0;pending=false;return false;}
PlayerCameraFallback GetPlayerCameraFallback(CGUnit_C*,float,float& height){height=0;return PlayerCameraFallback::Native;}
bool UnitModelBounds(const CM2Model*,CAaBox&,bool,bool){return false;}
bool UnitModelSphere(const CM2Model*,CAaSphere&,bool,bool){return false;}
bool UnitPickingSphere(const CM2Model*,CAaSphere&){return false;}
bool UnitPickGeometry(const CM2Model*,const C3Vector&,const C3Vector&,float,float&,bool&,bool){return false;}
void UpdateWorldBindings(uint32_t){}
const char* PrepareUnitModel(CGUnit_C*,const char* path){return path;}
void AttachUnitModel(CGUnit_C*){}
void RemoveUnitModel(CGUnit_C*){}
const char* PrepareMountModel(CGUnit_C*,const char* path,int32_t,int32_t){return path;}
void AttachMountModel(CGUnit_C*){}
void RemoveMountModel(CGUnit_C*,CM2Model*){}
CM2Model* CreatePreviewModel(CM2Scene*,const char*){return nullptr;}
void UpdatePreviewModel(CM2Model*){}
void RemovePreviewModel(CM2Model*){}
void SetUnitAction(CGUnit_C*,uint32_t){}
void OnUnitBaseAnimation(CGUnit_C*,uint32_t,int32_t){}
void OnUnitSwing(CGUnit_C*,uint32_t){}
void SetUnitEmote(CGUnit_C*,uint32_t){}
void ClearUnitEmote(CGUnit_C*){}
void ResetWorldBindings(){}
bool SetUnitEffectSequence(CGUnit_C*,int32_t,std::string&){return false;}
void RegisterWorldEffectScriptFunctions(){}
void RegisterDBCModelScriptFunctions(){}
void RegisterPlayerModelScriptFunctions(){}
const char* PlayerModelOverride(){return nullptr;}
bool SetUnitEffectSequence(CGUnit_C*,int32_t,std::string&){return false;}
void RegisterWorldEffectScriptFunctions(){}
void RegisterDBCModelScriptFunctions(){}
UnitWorldInfo GetUnitWorldInfo(const CGUnit_C*){return {};}
std::shared_ptr<const NativeAttachmentSnapshot> CaptureModelAttachments(const CM2Model*){return {};}
NativeAttachment SnapshotAttachment(const NativeAttachmentSnapshot&,uint32_t id,bool){NativeAttachment out;out.id=id;out.local.Identity();out.pivot=C3Vector(0,0,0);return out;}
AttachmentCounters GetAttachmentCounters(){return {};}
void RecordAttachmentChild(){}
void RegisterModelAttachments(CM2Model*,const AttachmentData&,const C44Matrix&){}
void PublishModelAttachments(CM2Model*,const AttachmentPose&,const C44Matrix&,const C3Vector*){}
void UnregisterModelAttachments(CM2Model*){}
void ClearModelAttachments(){}
bool HasModelAttachments(const CM2Model*){return false;}
bool ModelAttachment(const CM2Model*,uint32_t,bool,NativeAttachment&){return false;}
bool ModelAttachmentCameraFrame(const CM2Model*,uint32_t,bool,C44Matrix&,bool&){return false;}
bool ModelAttachmentWorldFrame(const CM2Model*,uint32_t,C44Matrix&,bool&){return false;}
bool ModelAttachmentHidden(const CM2Model*,const CM2Model*,bool&){return false;}
bool AnimateModelAttachments(CM2Model*){return false;}
void SetPendingAttachmentPosition(CM2Model*,CM2Model*,const C3Vector&){}
void ClearPendingAttachmentPosition(CM2Model*){}
bool ApplyPendingAttachmentPosition(CM2Model*,CM2Model*,const C44Matrix&){return true;}
}
