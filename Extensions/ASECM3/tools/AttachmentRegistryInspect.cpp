#include "AttachmentRegistry.h"
#include "ASECM3/Attachments.h"
#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
#include <thread>
using namespace ASECM3;
using namespace ASECM3::Client;
void Require(bool condition,const char* text){if(!condition)throw Error(text);}
int main(){try{
    // Opaque registry key only: never treated as a constructed/dereferenced model.
    alignas(16) std::array<unsigned char,16> storage{};auto* key=reinterpret_cast<CM2Model*>(storage.data());
    C44Matrix basis;basis.Identity();basis.a0=0;basis.a1=1;basis.b0=-1;basis.b1=0;
    AttachmentData data;AttachmentPoint p;p.wowId=11;p.bindValid=true;p.bindPivot={1,2,3};data.points.push_back(p);RegisterModelAttachments(key,data,basis);NativeAttachment pending;
    Require(ModelAttachment(key,11,false,pending)&&pending.present&&!pending.ready,"Pending attachment was discarded");
    Require(pending.pivot.x==-2&&pending.pivot.y==1&&pending.pivot.z==3,"Pending attachment lost its source bind pivot");
    NativeAttachment missing;Require(ModelAttachment(key,100,false,missing)&&!missing.present&&!missing.ready,"Missing attachment fell back");
    AttachmentPose pose;pose.rootReady=true;AttachmentFrame frame;frame.id=11;frame.ready=true;frame.transform=Matrix::Identity();frame.bindPivot={1,2,3};pose.points.push_back(frame);
    const auto publish=[&](int value){pose.root=Matrix::Identity();pose.root.At(0,3)=value;pose.root.At(1,3)=2*value;pose.root.At(2,3)=3*value;pose.points[0].transform=pose.root;PublishModelAttachments(key,pose,basis);};
    publish(1);const auto held=CaptureModelAttachments(key);const auto first=SnapshotAttachment(*held,11,false);
    Require(first.local.d0==-2&&first.local.d1==1&&first.local.d2==3&&first.pivot.x==-2&&first.pivot.y==1&&first.pivot.z==3,"Source/native basis conversion mismatch");
    auto oriented=pose;auto& source=oriented.points[0].transform;source.At(0,0)=0;source.At(0,1)=-3;source.At(1,0)=2;source.At(1,1)=0;source.At(2,2)=-4;
    PublishModelAttachments(key,oriented,basis);const auto rotated=SnapshotAttachment(*CaptureModelAttachments(key),11,false);
    const auto actual=(C3Vector(1,2,3)*basis)*rotated.local;const auto expectedSource=TransformPoint(source,{1,2,3});const auto expected=C3Vector(expectedSource.x,expectedSource.y,expectedSource.z)*basis;
    Require(std::abs(actual.x-expected.x)<1e-5&&std::abs(actual.y-expected.y)<1e-5&&std::abs(actual.z-expected.z)<1e-5,"Attachment rotation/scale/reflection basis conjugation failed");
    publish(2);Require(SnapshotAttachment(*held,11,false).local.d0==-2,"Published attachment snapshot mutated");
    std::atomic<bool> stop{false},bad{false};std::atomic<unsigned> started{0};std::atomic<uint64_t> samples{0};std::vector<std::thread> workers;
    for(unsigned worker=0;worker<4;++worker)workers.emplace_back([&]{started.fetch_add(1);while(!stop.load()){
        const auto snapshot=CaptureModelAttachments(key);if(!snapshot){bad.store(true);break;}const auto value=SnapshotAttachment(*snapshot,11,false);
        if(!value.ready||value.local.d0!=-2*value.local.d1||value.local.d2!=3*value.local.d1||snapshot->root.d1!=value.local.d1)bad.store(true);samples.fetch_add(1);
    }});
    while(started.load()!=4)std::this_thread::yield();for(int value=3;value<2003;++value)publish(value);stop.store(true);for(auto& worker:workers)worker.join();
    Require(!bad.load()&&samples.load()>0,"Concurrent attachment snapshots were inconsistent");
    const C3Vector rider(.25f,-.1f,1.5f);PublishModelAttachments(key,oriented,basis,&rider);
    const auto seat=SnapshotAttachment(*CaptureModelAttachments(key),0,false);
    Require(seat.present&&seat.ready&&seat.local.a0==1&&seat.local.b1==1&&seat.local.c2==1&&
        seat.local.d0==rider.x&&seat.local.d1==rider.y&&seat.local.d2==rider.z,"Mounted rider lost its level frame or seat position");
    auto riding=oriented;riding.points[0].id=0;riding.points[0].bindPivot={0,0,0};
    riding.points[0].transform=Matrix::Identity();riding.points[0].transform.At(2,3)=.25;
    PublishModelAttachments(key,riding,basis,&rider);const auto bob=SnapshotAttachment(*CaptureModelAttachments(key),0,false);
    Require(bob.local.d2==rider.z+.25f&&bob.local.a0==1&&bob.local.b1==1,"Rider did not follow the animated mount attachment");
    UnregisterModelAttachments(key);Require(!HasModelAttachments(key)&&!ModelAttachment(key,11,false,missing),"Attachment registry retained removed model");
    Require(SnapshotAttachment(*held,11,false).ready,"Reader snapshot invalidated by removal");ClearModelAttachments();
    std::cout<<"{\"ok\":true,\"workers\":4,\"publishes\":"<<GetAttachmentCounters().publishes<<",\"readerSamples\":"<<samples.load()<<",\"scope\":\"Immutable registry/basis/pending/removal fixtures; no native child animation or live world proof\"}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
