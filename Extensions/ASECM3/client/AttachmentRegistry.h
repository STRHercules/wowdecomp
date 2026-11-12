#pragma once
#include "Engine/Source/Tempest/C44Matrix.h"
#include <memory>
#include <vector>
class CM2Model;
namespace ASECM3 {struct AttachmentData;struct AttachmentPose;}
namespace ASECM3::Client {
struct NativeAttachment {uint32_t id=0;bool present=false,ready=false;C44Matrix local;C3Vector pivot;};
struct NativeAttachmentSnapshot {std::vector<NativeAttachment> points;C44Matrix root;bool rootReady=false;};
std::shared_ptr<const NativeAttachmentSnapshot> CaptureModelAttachments(const CM2Model* model);
NativeAttachment SnapshotAttachment(const NativeAttachmentSnapshot& snapshot,uint32_t id,bool rootFallback);
struct AttachmentCounters {uint64_t reads=0,publishes=0,children=0,positions=0;};
AttachmentCounters GetAttachmentCounters();
void RecordAttachmentChild();
void RegisterModelAttachments(CM2Model* model,const AttachmentData& data,const C44Matrix& sourceBasis);
void PublishModelAttachments(CM2Model* model,const AttachmentPose& pose,const C44Matrix& sourceBasis,const C3Vector* rider=nullptr);
void UnregisterModelAttachments(CM2Model* model);
void ClearModelAttachments();
bool HasModelAttachments(const CM2Model* model);
// Safe on native animation workers: readers hold an immutable pose snapshot.
bool ModelAttachment(const CM2Model* model,uint32_t id,bool rootFallback,NativeAttachment& out);
bool ModelAttachmentCameraFrame(const CM2Model* model,uint32_t id,bool rootFallback,C44Matrix& out,bool& available);
bool ModelAttachmentWorldFrame(const CM2Model* model,uint32_t id,C44Matrix& out,bool& available);
bool ModelAttachmentHidden(const CM2Model* parent,const CM2Model* child,bool& hidden);
bool AnimateModelAttachments(CM2Model* parent);
void SetPendingAttachmentPosition(CM2Model* child,CM2Model* parent,const C3Vector& worldPosition);
void ClearPendingAttachmentPosition(CM2Model* child);
bool ApplyPendingAttachmentPosition(CM2Model* child,CM2Model* parent,const C44Matrix& cameraFrame);
}
