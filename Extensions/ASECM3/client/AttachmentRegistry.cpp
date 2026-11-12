#include "AttachmentRegistry.h"
#include "ASECM3/Attachments.h"
#include "GxGeometryTransform.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
namespace ASECM3::Client {
namespace {
using Snapshot=NativeAttachmentSnapshot;
struct PendingPosition {CM2Model* parent;C3Vector world;};
struct RegistryState {
 std::atomic<uint64_t> reads{0},publishes{0},children{0},positions{0};
 std::shared_mutex mutex;
 std::map<const CM2Model*,std::shared_ptr<const Snapshot>> snapshots;
 std::map<CM2Model*,PendingPosition> pendingPositions;
};
// Native model teardown may outlive C++ static destruction in other units.
// World reset releases all snapshots/requests; the lock itself lives to exit.
RegistryState& State(){static auto* state=new RegistryState;return *state;}
std::shared_ptr<const Snapshot> Read(const CM2Model* model){std::shared_lock<std::shared_mutex> lock(State().mutex);const auto it=State().snapshots.find(model);return it==State().snapshots.end()?nullptr:it->second;}
NativeAttachment Find(const Snapshot& s,uint32_t id,bool root){
    for(const auto& point:s.points)if(point.id==id)return point;
    NativeAttachment out;out.id=id;out.local.Identity();out.pivot=C3Vector(0,0,0);
    if(root){out.present=true;out.ready=s.rootReady;out.local=s.root;}return out;
}
C44Matrix Native(const Matrix& source){C44Matrix out;auto* values=reinterpret_cast<float*>(&out);for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)values[r*4+c]=float(source.At(c,r));return out;}
}
std::shared_ptr<const NativeAttachmentSnapshot> CaptureModelAttachments(const CM2Model* model){auto snapshot=Read(model);if(snapshot)State().reads.fetch_add(1,std::memory_order_relaxed);return snapshot;}
NativeAttachment SnapshotAttachment(const NativeAttachmentSnapshot& snapshot,uint32_t id,bool rootFallback){return Find(snapshot,id,rootFallback);}
AttachmentCounters GetAttachmentCounters(){return {State().reads.load(std::memory_order_relaxed),State().publishes.load(std::memory_order_relaxed),State().children.load(std::memory_order_relaxed),State().positions.load(std::memory_order_relaxed)};}
void RecordAttachmentChild(){State().children.fetch_add(1,std::memory_order_relaxed);}
void RegisterModelAttachments(CM2Model* model,const AttachmentData& data,const C44Matrix& basis){
    auto snapshot=std::make_shared<Snapshot>();snapshot->root.Identity();
    if(data.error.empty())for(const auto& point:data.points)if(point.wowId>=0){NativeAttachment out;out.id=uint32_t(point.wowId);out.present=true;out.local.Identity();out.pivot=point.bindValid?C3Vector(point.bindPivot.x,point.bindPivot.y,point.bindPivot.z)*basis:C3Vector(0,0,0);snapshot->points.push_back(out);}
    std::unique_lock<std::shared_mutex> lock(State().mutex);State().snapshots[model]=std::move(snapshot);
}
void PublishModelAttachments(CM2Model* model,const AttachmentPose& pose,const C44Matrix& basis,const C3Vector* rider){
    auto snapshot=std::make_shared<Snapshot>();snapshot->root.Identity();
    const auto inverse=basis.Inverse(basis.Determinant());GxGeometryTransform validateBasis(basis);
    // Conjugation adapts source attachment frames to native child-model axes.
    // For an M3 child its own source basis cancels this inverse as intended.
    if(pose.rootReady){snapshot->root=inverse*Native(pose.root)*basis;GxGeometryTransform validate(snapshot->root);snapshot->rootReady=true;}
    for(const auto& point:pose.points){if(rider&&point.id==0)continue;NativeAttachment out;out.id=point.id;out.present=true;out.ready=point.ready;out.local.Identity();out.pivot=C3Vector(0,0,0);
        if(point.ready){out.local=inverse*Native(point.transform)*basis;GxGeometryTransform validate(out.local);out.pivot=C3Vector(point.bindPivot.x,point.bindPivot.y,point.bindPivot.z)*basis;}
        snapshot->points.push_back(out);
    }
    if(rider){
        if(!std::isfinite(rider->x)||!std::isfinite(rider->y)||!std::isfinite(rider->z))throw Error("Nonfinite mount rider position");
        NativeAttachment seat;seat.id=0;seat.present=true;seat.ready=pose.rootReady;seat.local.Identity();seat.pivot=*rider;
        C3Vector position=*rider;
        for(const auto& point:pose.points)if(point.id==0&&point.ready){
            const auto animated=Native(point.transform)*basis;
            const auto bind=C3Vector(point.bindPivot.x,point.bindPivot.y,point.bindPivot.z)*basis;
            position=C3Vector(animated.d0,animated.d1,animated.d2)+*rider-bind;seat.ready=true;break;
        }
        seat.local.d0=position.x;seat.local.d1=position.y;seat.local.d2=position.z;snapshot->points.push_back(seat);
    }
    std::unique_lock<std::shared_mutex> lock(State().mutex);
    const auto it=State().snapshots.find(model);if(it!=State().snapshots.end())it->second=std::move(snapshot);
    State().publishes.fetch_add(1,std::memory_order_relaxed);
}
void UnregisterModelAttachments(CM2Model* model){std::unique_lock<std::shared_mutex> lock(State().mutex);State().snapshots.erase(model);for(auto it=State().pendingPositions.begin();it!=State().pendingPositions.end();)if(it->second.parent==model)it=State().pendingPositions.erase(it);else ++it;}
void ClearModelAttachments(){std::unique_lock<std::shared_mutex> lock(State().mutex);State().snapshots.clear();State().pendingPositions.clear();}
void SetPendingAttachmentPosition(CM2Model* child,CM2Model* parent,const C3Vector& world){std::unique_lock<std::shared_mutex> lock(State().mutex);State().pendingPositions[child]={parent,world};}
void ClearPendingAttachmentPosition(CM2Model* child){std::unique_lock<std::shared_mutex> lock(State().mutex);State().pendingPositions.erase(child);}
bool HasModelAttachments(const CM2Model* model){return bool(Read(model));}
bool ModelAttachment(const CM2Model* model,uint32_t id,bool rootFallback,NativeAttachment& out){const auto snapshot=CaptureModelAttachments(model);if(!snapshot)return false;out=Find(*snapshot,id,rootFallback);return true;}
bool ModelAttachmentCameraFrame(const CM2Model* model,uint32_t id,bool rootFallback,C44Matrix& out,bool& available){out.Identity();available=false;NativeAttachment point;if(!ModelAttachment(model,id,rootFallback,point))return false;available=point.ready;if(available)out=point.local*model->m_matrixF4;return true;}
bool ModelAttachmentWorldFrame(const CM2Model* model,uint32_t id,C44Matrix& out,bool& available){if(!ModelAttachmentCameraFrame(model,id,true,out,available))return false;if(available)out*=model->m_scene->m_viewInv;return true;}
bool ModelAttachmentHidden(const CM2Model* parent,const CM2Model* child,bool& hidden){NativeAttachment point;if(!ModelAttachment(parent,child->m_attachmentId,(child->m_fFlags&0x40000)!=0,point))return false;hidden=!point.ready;return true;}
bool ApplyPendingAttachmentPosition(CM2Model* child,CM2Model* parent,const C44Matrix& matrix){
    {std::shared_lock<std::shared_mutex> lock(State().mutex);if(State().pendingPositions.find(child)==State().pendingPositions.end())return true;}
    std::unique_lock<std::shared_mutex> lock(State().mutex);const auto pending=State().pendingPositions.find(child);if(pending==State().pendingPositions.end())return true;
    if(pending->second.parent!=parent){State().pendingPositions.erase(pending);return false;}
    const auto world=matrix*parent->m_scene->m_viewInv;const auto determinant=world.Determinant();if(!std::isfinite(determinant)||std::abs(determinant)<1e-12f)return false;
    const auto local=pending->second.world*world.Inverse(determinant);if(!std::isfinite(local.x)||!std::isfinite(local.y)||!std::isfinite(local.z))return false;
    if(!(child->m_fFlags&0x8000))child->m_worldTransform.Identity();child->m_worldTransform.d0=local.x;child->m_worldTransform.d1=local.y;child->m_worldTransform.d2=local.z;child->m_fFlags|=0x8000;State().pendingPositions.erase(pending);
    State().positions.fetch_add(1,std::memory_order_relaxed);return true;
}
}
