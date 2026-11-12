#include "UnitWorldBinding.h"
#include "WorldInstance.h"
#include "GxGeometryTransform.h"
#include "SceneEnvironment.h"
#include "WorldPlacement.h"
#include "EmoteRequest.h"
#include "RuntimeTiming.h"
#include "AttachmentRegistry.h"
#include "ASECM3/EffectDependencies.h"
#include "ASECM3/CameraHeight.h"
#include "LooseAssetRead.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Model2/M2Cache.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/lua-5.1/src/asecDiagnostics.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/ShaderEffect/ShaderEffect.h"
#include "Engine/Source/Os/TimeManager.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/DB/Rec/CreatureModelDataRec.h"
#include "WoW/Source/DB/Rec/CreatureDisplayInfoRec.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <fstream>
#include <sstream>
#include <limits>
#include <cmath>
namespace ASECM3::Client {
namespace {
using Bytes=std::vector<uint8_t>;
std::string Key(std::string value){for(auto& c:value)c=c=='/'?'\\':char(std::tolower((unsigned char)c));return value;}
bool IsM3(const char* path){return path&&Key(std::filesystem::path(path).extension().string())==".m3";}
Bytes Read(const std::string& name,size_t maximum=128u*1024*1024){SFileImpl* file=nullptr;if(!SFileOpenFile(name.c_str(),&file))throw Error("Cannot open world M3 dependency: "+name);
 struct Close{SFileImpl* p;~Close(){SFileCloseFile(p);}} close{file};uint32_t high=0;const auto n=SFileGetFileSize(file,&high);if(high||!n||n>128u*1024*1024)throw Error("World M3 dependency exceeds 128 MiB or is empty: "+name);
 if(n>maximum)throw Error("World M3 dependency exceeds its snapshot budget: "+name);
 Bytes bytes(n);uint32_t read=0;if(!SFileReadFile(file,bytes.data(),n,&read,nullptr,0)||read!=n)throw Error("Short world M3 dependency read: "+name);return bytes;}
struct SourceJob {
 std::string path,error;Bytes model;std::vector<std::pair<std::string,Bytes>> banks;
 std::pair<std::string,Bytes> effectBank;std::string effectError;
 float cameraLower=0,cameraUpper=0;std::string cameraError;
 std::unique_ptr<Document> document;AttachmentData attachmentCatalog;
 std::shared_ptr<const Scene> scene;std::atomic<bool> ready{false};size_t bytes=0;
};
class SourceParser {
 std::mutex mutex;std::condition_variable wake;bool stop=false;size_t queuedBytes=0;
 std::deque<std::shared_ptr<SourceJob>> queue;std::thread worker;
 void Run(){for(;;){std::shared_ptr<SourceJob> job;
  {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return stop||!queue.empty();});if(stop)break;job=std::move(queue.front());queue.pop_front();}
  try{job->scene=Scene::FromDocument(std::move(*job->document),job->effectBank.first.empty()&&job->effectBank.second.empty()&&job->effectError.empty());if(job->effectError.empty())job->effectError=job->scene->EffectSetupError();if(job->scene->Model().animations.empty()||Key(job->path).rfind("hots\\mounts\\",0)==0)for(auto& bank:job->banks){try{job->scene=job->scene->WithAnimationBank(std::move(bank.second),bank.first,Key(job->path).rfind("hots\\mounts\\",0)==0);break;}catch(const std::exception&){}}
   if(!job->effectBank.second.empty())try{job->scene=job->scene->WithEffectAnimationBank(std::move(job->effectBank.second),job->effectBank.first);}catch(const std::exception& e){job->effectError=e.what();}
   try{SceneCameraBounds(*job->scene,job->cameraLower,job->cameraUpper);}catch(const std::exception& e){job->cameraLower=job->cameraUpper=0;job->cameraError=e.what();}
  }catch(const std::exception& e){job->error=e.what();}catch(...){job->error="Unknown world M3 parse failure";}
  job->document.reset();job->model.clear();job->banks.clear();job->effectBank.second.clear();job->ready.store(true,std::memory_order_release);
  {std::lock_guard<std::mutex> lock(mutex);queuedBytes-=job->bytes;}
 }}
public:
 SourceParser():worker([this]{Run();}){}
 ~SourceParser(){{std::lock_guard<std::mutex> lock(mutex);stop=true;}wake.notify_one();if(worker.joinable())worker.join();}
 void Add(const std::shared_ptr<SourceJob>& job){std::lock_guard<std::mutex> lock(mutex);if(job->bytes>256u*1024*1024-queuedBytes)throw Error("World M3 queued snapshots exceed 256 MiB");queuedBytes+=job->bytes;queue.push_back(job);wake.notify_one();}
};
struct Binding {uint64_t guid=0;const CGUnit_C* identity=nullptr;std::string path,error,actionError,collisionError;std::shared_ptr<SourceJob> source;EmoteRequest emote;
 bool fromDBC=false,mount=false,preview=false,previewEffectTried=false,hasRider=false;C3Vector rider;uint32_t objectType=0;int32_t displayId=0,modelId=0;
 bool deviceWaiting=false;uint64_t deviceDeferrals=0;
 bool particleDrawReported=false,particleErrorReported=false,cameraMirrored=false,modelMirrored=false;
 RuntimeTiming tickTiming,queueTiming,drawTiming,pickTiming;uint64_t pickQueries=0,pickHits=0,collisionQueries=0,collisionHits=0;bool collisionReported=false;uint32_t perfTime=0;uint64_t perfQueued=0;double queueHz=0;bool restartEmote=false;
 std::unique_ptr<WorldInstance> instance;CM2Model* proxy=nullptr;uint32_t action=0,lastAttemptedAction=UINT32_MAX,lastFrame=UINT32_MAX,lastTime=0;uint64_t draws=0,queued=0,boundsQueries=0,boundsUpdates=0,opaquePackets=0,transparentPackets=0,splitPackets=0,phaseDraws[3]={};bool reported=false,publishedBounds=false;CAaBox spatialBounds;};
std::map<uint64_t,std::unique_ptr<Binding>> bindings;
std::map<uint64_t,std::unique_ptr<Binding>> mountBindings;
std::map<CM2Model*,std::unique_ptr<Binding>> previewBindings;
// Per-binding automatic effect state, kept beside the binding map so the Binding layout stays as it was.
struct AutoEffects{int32_t followedClip=-2;bool userSelected=false;};
std::map<Binding*,AutoEffects> autoEffects;
// One-shot body animations the native unit started (swings, casts, emotes). The M3 plays its matching clip once, from the
// start, then returns to the continuous state; a new request while one plays is ignored.
struct OneShot{int32_t action=-1;bool pending=false,active=false,clock=false;uint32_t started=0;};
std::map<uint64_t,OneShot> oneShots;
bool IsOneShotAnim(uint32_t a){
    // Melee swings (16..19, 85..88, 117) are driven by OnUnitSwing: native code repeats PlayBaseAnimation for them every frame.
    switch(a){case 2:case 31:case 32:case 33:case 53:case 54:case 55:case 46:case 47:case 49:case 57:case 58:case 59:case 95:return true;
    default:return a>=60&&a<=84;}
}
bool SameName(const std::string& a,const std::string& b){if(a.size()!=b.size())return false;for(size_t i=0;i<a.size();++i)if(std::tolower(static_cast<unsigned char>(a[i]))!=std::tolower(static_cast<unsigned char>(b[i])))return false;return true;}
std::map<CM2Model*,Binding*> proxies;
std::map<std::string,std::weak_ptr<SourceJob>> sources;
std::unique_ptr<SourceParser> parser;std::unique_ptr<WorldRenderContext> context;
// Native M2 workers may query ordinary M2 bounds. They must not read the
// main-thread registry or mutable M3 pose; their models keep native bounds.
thread_local bool bindingHostThread=false;
void Trace(const Binding& b,const char* phase){
 const auto file=std::filesystem::path(SFile::m_basePath)/"Logs"/"ASECM3-world.log";
 std::error_code ec;if(std::filesystem::file_size(file,ec)>2u*1024*1024&&!ec){auto previous=file;previous+=".previous";std::filesystem::remove(previous,ec);ec.clear();std::filesystem::rename(file,previous,ec);}
 const bool ready=b.source&&b.source->ready.load(std::memory_order_acquire);
 const auto clip=b.instance?b.instance->Renderer().CurrentSequence():-1;
 const auto attachmentStats=GetAttachmentCounters();
 const auto* cloth=b.instance?b.instance->Renderer().Cloth():nullptr;
 std::ofstream log(file,std::ios::app);if(log)log<<"phase="<<phase<<" unit="<<b.guid<<" source="<<b.path<<" selection="<<(b.fromDBC?"dbc":"override")<<" objectType="<<b.objectType<<" displayId="<<b.displayId<<" modelId="<<b.modelId<<" bank="<<(ready&&b.source->scene?b.source->scene->AnimationSource():"")<<" effectBank="<<(ready&&b.source->scene?b.source->scene->EffectAnimationSource():"")<<" effectError="<<(ready?b.source->effectError:"")<<" deviceDeferrals="<<b.deviceDeferrals<<" queued="<<b.queued<<" draws="<<b.draws<<" action="<<b.action<<" emoteRequest="<<b.emote.Action()<<" modelQueueHz="<<b.queueHz<<" tickAvgMs="<<b.tickTiming.Average()<<" tickMaxMs="<<b.tickTiming.maximum<<" queueAvgMs="<<b.queueTiming.Average()<<" drawAvgMs="<<b.drawTiming.Average()<<" drawMaxMs="<<b.drawTiming.maximum<<" packedVertices="<<(b.instance?b.instance->Renderer().VerticesPacked():0)<<" submittedIndices="<<(b.instance?b.instance->Renderer().IndicesSubmitted():0)<<" clip="<<clip<<" clipName="<<(clip>=0?b.instance->Renderer().Data().Model().animations.at(clip).name:"")<<" elapsedMs="<<(b.instance?b.instance->Renderer().ElapsedMs():0)<<" transitionMs="<<(b.instance?b.instance->Renderer().TransitionMs():0)<<" transitionWeight="<<(b.instance?b.instance->Renderer().TransitionWeight():1)<<" clothHz="<<(cloth?cloth->FrequencyHz():0)<<" clothBends="<<(cloth?cloth->BendingConstraints():0)<<" clothTethers="<<(cloth?cloth->Tethers():0)<<" clothDisplacement="<<(cloth?cloth->MaximumDisplacement():0)<<" clothResets="<<(cloth?cloth->Resets():0)<<" locomotionFallback="<<(b.actionError.empty()&&clip>=0&&b.instance->Renderer().Actions().at(clip).action!=int32_t(b.action))<<" opaquePackets="<<b.opaquePackets<<" transparentPackets="<<b.transparentPackets<<" splitPackets="<<b.splitPackets<<" phaseDraws="<<b.phaseDraws[0]<<","<<b.phaseDraws[1]<<","<<b.phaseDraws[2]<<" worldEnvironmentDraws="<<(b.instance?b.instance->Renderer().WorldEnvironmentDraws():0)<<" worldFogDraws="<<(b.instance?b.instance->Renderer().WorldFogDraws():0)<<" worldPointLightDraws="<<(b.instance?b.instance->Renderer().WorldPointLightDraws():0)<<" attachmentReadsAll="<<attachmentStats.reads<<" attachmentPublishesAll="<<attachmentStats.publishes<<" attachedChildrenAll="<<attachmentStats.children<<" attachedPositionsAll="<<attachmentStats.positions<<" collisionError="<<b.collisionError<<" collisionQueries="<<b.collisionQueries<<" collisionHits="<<b.collisionHits<<" pickQueries="<<b.pickQueries<<" pickHits="<<b.pickHits<<" pickAvgMs="<<b.pickTiming.Average()<<" pickMaxMs="<<b.pickTiming.maximum<<" boundsQueries="<<b.boundsQueries<<" boundsUpdates="<<b.boundsUpdates<<" boundaryChecks="<<(context?context->BoundaryChecks():0)<<" boundaryFailures="<<(context?context->BoundaryFailures():0)<<" error="<<b.error<<" actionError="<<b.actionError<<'\n';
 if(log)log<<"orientationPhase="<<phase<<" unit="<<b.guid<<" source="<<b.path<<" cameraMirrored="<<b.cameraMirrored<<" modelMirrored="<<b.modelMirrored<<" cullMirrored="<<b.modelMirrored<<'\n';
 if(log&&b.instance){const auto& r=b.instance->Renderer();if(r.EffectSequence()>=0||!r.ParticleError().empty()||!r.ParticleDrawError().empty())log<<"particlePhase="<<phase<<" unit="<<b.guid<<" source="<<b.path<<" effectClip="<<r.EffectSequence()<<" live="<<r.EffectParticles()<<" spawned="<<r.EffectSpawned()<<" capped="<<r.EffectDropped()<<" birthTransformSamples="<<r.ParticleBirthSamples()<<" birthSurfaceSamples="<<r.ParticleSurfaceSamples()<<" surfaceHistory=linear_observed worldParticleDraws="<<r.WorldParticleDraws()<<" simulationError="<<r.ParticleError()<<" drawError="<<r.ParticleDrawError()<<'\n';}
}
void Report(Binding& b,const std::string& message){b.error=message;if(b.reported)return;b.reported=true;Trace(b,"error");const auto detail="M3 world unit="+std::to_string(b.guid)+" source="+b.path+" error="+message;ASEC_LuaDiagnostic(nullptr,detail.c_str(),0,0,0,__FILE__,__LINE__);}
std::shared_ptr<SourceJob> Load(const std::string& path){const auto key=Key(path);const auto found=sources.find(key);if(found!=sources.end())if(auto live=found->second.lock())return live;
 for(auto it=sources.begin();it!=sources.end();)if(it->second.expired())it=sources.erase(it);else ++it;
 if(sources.size()>=64)throw Error("World M3 source budget exceeds 64 distinct assets");
 auto job=std::make_shared<SourceJob>();job->path=path;job->model=Read(path);job->bytes=job->model.size();
 job->document=std::make_unique<Document>(Document::Parse(std::move(job->model),false));
 try{job->attachmentCatalog=DecodeAttachments(*job->document);}catch(const std::exception& e){job->attachmentCatalog.error=e.what();}
 for(const auto& bank:AnimationBankCandidates(path)){if(job->banks.size()>=8)break;if(!SFile::FileExists(bank.c_str()))continue;auto bytes=Read(bank,256u*1024*1024-job->bytes);if(bytes.size()>256u*1024*1024-job->bytes)throw Error("World M3 source snapshots exceed 256 MiB");job->bytes+=bytes.size();job->banks.emplace_back(bank,std::move(bytes));}
 const auto declaration=EffectDeclarationPath(path);if(SFile::FileExists(declaration.c_str()))try{job->effectBank.first=ResolveEffectDeclaration(path,Read(declaration,1024));auto bytes=Read(job->effectBank.first,256u*1024*1024-job->bytes);if(bytes.size()>256u*1024*1024-job->bytes)throw Error("World M3 effect snapshot exceeds 256 MiB budget");job->bytes+=bytes.size();job->effectBank.second=std::move(bytes);}catch(const std::exception& e){job->effectError=e.what();}
 if(!parser)parser=std::make_unique<SourceParser>();parser->Add(job);sources[key]=job;return job;
}
Bytes Texture(const Binding& b,const std::string& name){
 auto normalized=name;std::replace(normalized.begin(),normalized.end(),'/','\\');const auto lower=Key(normalized);
 // Source references stay in the owning product; the HotS manifest resolves mod inheritance.
 if(Key(b.path).rfind("hots\\mounts\\",0)==0)
  return Read("hots\\variants\\"+std::filesystem::path(Key(b.path)).stem().string()+"\\"+normalized);
 if(Key(b.path).rfind("hots\\",0)==0)return Read(lower.rfind("hots\\",0)==0?normalized:"hots\\"+normalized);
 const std::string prefix="assets\\textures\\";
 if(lower.rfind(prefix,0)==0){
  const std::filesystem::path relative(normalized);
  if(relative.is_absolute())throw Error("World M3 texture path must be relative: "+name);
  for(const auto& component:relative)if(component=="..")throw Error("World M3 texture path escapes its asset root: "+name);
  const auto sidecar=std::filesystem::path(SFile::m_basePath)/"ASECM3"/relative;
  std::error_code ec;
  if(std::filesystem::is_regular_file(sidecar,ec))return ReadLooseAssetFile(sidecar);
  try{return Read("ASECM3\\"+normalized);}catch(const std::exception&){}
 }
 try{return Read(name);}catch(const std::exception&){}
 return Read((std::filesystem::path(b.path).parent_path()/std::filesystem::path(normalized).filename()).string());
}
C44Matrix SourceBasis(){
 // Same +90 Z basis as M3_to_M2: HotS -Y forward becomes Wrath +X.
 C44Matrix basis;basis.Identity();basis.a0=0;basis.a1=1;basis.b0=-1;basis.b1=0;return basis;
}
void CollisionProblem(Binding& b,const std::string& message){
 b.collisionError=message;if(b.collisionReported)return;b.collisionReported=true;Trace(b,"collision_error");
 const auto detail="M3 collision disabled for query; unit="+std::to_string(b.guid)+" source="+b.path+" error="+message;
 ASEC_LuaDiagnostic(nullptr,detail.c_str(),0,0,0,__FILE__,__LINE__);
}
bool Ready(Binding& b){
 if(!b.error.empty()||!b.source||!b.source->ready.load(std::memory_order_acquire))return false;
 if(!b.source->error.empty())throw Error(b.source->error);
 if(!b.instance){b.instance=std::make_unique<WorldInstance>(b.source->scene,b.path);
  if(!b.source->cameraError.empty()){const auto detail="M3 camera bounds unavailable; source="+b.path+" error="+b.source->cameraError;ASEC_LuaDiagnostic(nullptr,detail.c_str(),0,0,0,__FILE__,__LINE__);}
  if(!b.source->effectError.empty()){const auto detail="M3 effect dependency disabled; source="+b.path+" error="+b.source->effectError;ASEC_LuaDiagnostic(nullptr,detail.c_str(),0,0,0,__FILE__,__LINE__);}
  if(!b.source->scene->Attachments().error.empty()){
   const auto detail="M3 attachments disabled; source="+b.path+" error="+b.source->scene->Attachments().error;
   ASEC_LuaDiagnostic(nullptr,detail.c_str(),0,0,0,__FILE__,__LINE__);}
  if(!b.source->scene->Collision().error.empty())CollisionProblem(b,b.source->scene->Collision().error);}
 return true;
}
SceneEnvironment Environment(const CM2Model& model,uint32_t flags,uint32_t pass){
 SceneEnvironment out;out.opacity=std::clamp(model.m_effectiveAlpha,0.0f,1.0f);
 if(!model.m_currentLighting)throw Error("M3 world object has no native lighting");
 // Work on a value copy: SetupSunlight may finalize cached native aggregates.
 auto lighting=*model.m_currentLighting;C3Vector direction;
 lighting.GetSunLight(direction,out.ambient,out.diffuse,out.specular);
 out.towardSun=C3Vector(-direction.x,-direction.y,-direction.z);
 for(unsigned i=0;i<std::min(4u,lighting.m_lightCount);++i){
  if(!lighting.m_lights[i])continue;
  auto& point=out.points[out.pointCount++];
  lighting.GetPointLight(i,point.position,point.color,point.attenuation);
  point.position=point.position*model.m_scene->m_view;
 }
 out.fogStart=lighting.m_fogStart;out.fogEnd=lighting.m_fogEnd;
 out.fogDensity=lighting.m_fogDensity;out.fogColor=lighting.m_fogColor;
 out.fogDistanceScale=CShaderEffect::s_fogMul;
 out.fogEnabled=g_theGxDevicePtr->MasterEnable(GxMasterEnable_Fog)&&lighting.m_fogScale>0&&out.fogEnd>out.fogStart;
 if(flags&2){const auto& plane=lighting.m_liquidPlane;const float sign=pass==2?-1.0f:1.0f;
  out.clipPlane={plane.n.x*sign,plane.n.y*sign,plane.n.z*sign,plane.d*sign};out.clipEnabled=true;
  for(float value:out.clipPlane)if(!std::isfinite(value))throw Error("Nonfinite M3 liquid clip plane");}
 const auto finite=[](const C3Vector& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
 if(!finite(out.ambient)||!finite(out.diffuse)||!finite(out.specular)||!finite(out.towardSun)||!finite(out.fogColor)||
    !std::isfinite(out.opacity)||!std::isfinite(out.fogStart)||!std::isfinite(out.fogEnd)||!std::isfinite(out.fogDensity)||!std::isfinite(out.fogDistanceScale))throw Error("Nonfinite native M3 world environment");
 for(unsigned i=0;i<out.pointCount;++i){const auto& p=out.points[i];if(!finite(p.position)||!finite(p.color)||!finite(p.attenuation))throw Error("Nonfinite native M3 world point light");}
 return out;
}
void Draw(CM2Model* model,int32_t packet,uint32_t flags,uint32_t pass){
 const auto proxy=proxies.find(model);if(proxy==proxies.end())return;auto& b=*proxy->second;
 if(!b.preview){auto* unit=static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(WOWGUID{b.guid},TYPEMASK_UNIT,__FILE__,__LINE__));if(!unit||unit!=b.identity||(b.mount?unit->m_mountModel:unit->m_worldModel)!=model)return;}
 TimeSample measured(b.drawTiming);
 if(!b.error.empty()||!b.source||!b.source->ready.load(std::memory_order_acquire))return;
 // m_context is cleared by the native D3D9/D3D9Ex reset path. Do not
 // turn temporary context loss into a permanent binding/isolation error.
 if(!g_theGxDevicePtr||!g_theGxDevicePtr->m_context){++b.deviceDeferrals;if(!b.deviceWaiting){b.deviceWaiting=true;Trace(b,"device_wait");}return;}
 try{
  if(!Ready(b))return;
  if(b.lastFrame!=model->m_scene->m_frameStamp){
   b.lastFrame=model->m_scene->m_frameStamp;
   b.instance->Renderer().LoadNextTexture([&](const std::string& name){return Texture(b,name);});
  }
  if(!context)context=std::make_unique<WorldRenderContext>();
  const auto matrix=SourceBasis()*model->m_matrixF4;
  const auto objectMatrix=SourceBasis()*model->m_worldTransform;
  b.cameraMirrored=matrix.Determinant()<0;
  b.modelMirrored=objectMatrix.Determinant()<0;
  const auto environment=Environment(*model,flags,pass);
   if(!b.instance->Draw(*context,matrix,g_theGxDevicePtr->m_projection,true,&environment,packet,&model->m_scene->m_view,b.modelMirrored))throw Error(b.instance->ErrorText());
  if(b.instance->DrawDeferred()){++b.deviceDeferrals;if(!b.deviceWaiting){b.deviceWaiting=true;Trace(b,"device_wait");}return;}
  ++b.draws;if(pass<3)++b.phaseDraws[pass];if(b.deviceWaiting){b.deviceWaiting=false;Trace(b,"device_resumed");}
  const auto& renderer=b.instance->Renderer();
  if(!b.particleDrawReported&&renderer.WorldParticleDraws()){b.particleDrawReported=true;Trace(b,"particle_draw");}
  if(!b.particleErrorReported&&(!renderer.ParticleError().empty()||!renderer.ParticleDrawError().empty())){b.particleErrorReported=true;Trace(b,"particle_error");}
  if(b.draws==1||b.draws==120||b.draws==600)Trace(b,"draw");
 }catch(const std::exception& e){Report(b,e.what());}
}
void Disconnect(Binding& b,CGUnit_C*){if(b.proxy){UnregisterModelAttachments(b.proxy);proxies.erase(b.proxy);b.proxy=nullptr;}}
}
bool QueueUnitWorldModel(CM2Scene* scene,CM2Model* model,uint32_t& elementIndex){
 const auto p=proxies.find(model);if(p==proxies.end())return false;
 if(p->second->proxy!=model)return false;
 if(++p->second->queued==1)Trace(*p->second,"queued");
 // Returning true suppresses native placeholder batches, even while loading.
 auto& b=*p->second;
 TimeSample measured(b.queueTiming);
 try{
  if(!Ready(b)||model->m_effectiveAlpha<.0001f)return true;
  const auto& packets=b.instance->Renderer().PrepareWorldPackets(model->m_effectiveAlpha>=.99999f);
  const auto transform=GxGeometryTransform(SourceBasis()*model->m_matrixF4);
  const bool firstPackets=!b.opaquePackets&&!b.transparentPackets;
  for(uint32_t i=0;i<packets.size();++i){const auto& packet=packets[i];
   const auto box=transform.Bounds(CAaBox(C3Vector(packet.lower.x,packet.lower.y,packet.lower.z),C3Vector(packet.upper.x,packet.upper.y,packet.upper.z)));
   const auto sphere=GxGeometryTransform::EnclosingSphere(box);
   const float depth=sphere.c.x*sphere.c.x+sphere.c.y*sphere.c.y+sphere.c.z*sphere.c.z;
   if(!std::isfinite(depth))throw Error("M3 transparent sort depth is nonfinite");
   const auto* lighting=model->m_currentLighting;const bool above=!lighting||(lighting->m_flags&0x20)!=0,below=lighting&&(lighting->m_flags&0x40)!=0;
   float distance=0;
   if(above&&below){const auto& plane=lighting->m_liquidPlane;
    if(plane.n.x*plane.n.x+plane.n.y*plane.n.y+plane.n.z*plane.n.z<1e-12f)throw Error("M3 liquid plane has no normal");
    distance=plane.n.x*sphere.c.x+plane.n.y*sphere.c.y+plane.n.z*sphere.c.z+plane.d;
   }
   const auto placement=PlaceWorldPacket(packet.passes.deferred,model->m_effectiveAlpha,above,below,distance,sphere.r,(scene->m_cache->m_flags&2)!=0,scene->m_liquidTypeId!=0);
   if(!placement.passes)continue;
   auto* element=scene->m_elements.New();if(!element)throw Error("Cannot allocate M3 world draw packet");
   *element=M2Element{};element->m_type=UnitWorldElementType;element->m_model=model;element->m_index=int32_t(i);element->m_alpha=model->m_effectiveAlpha;
   // A common model key preserves its explicit material priorities before
   // region depth, matching the native model-level depth-sort convention.
   element->m_sortDepth=model->m_scaleSq;element->m_depth=depth;
   element->m_priorityPlane=int32_t(std::clamp(-int64_t(packet.passes.priority),int64_t(INT32_MIN),int64_t(INT32_MAX)));
   element->m_flags=placement.clip?2:0;
   element->m_vertexPermute=element->m_pixelPermute=UINT32_MAX;
   const auto index=elementIndex++;
   for(unsigned pass=0;pass<3;++pass)if(placement.passes&(1u<<pass))scene->m_passElements[pass].New(index);
   if(placement.transparent)++b.transparentPackets;else ++b.opaquePackets;if(placement.clip)++b.splitPackets;
  }
  if(firstPackets&&(b.opaquePackets||b.transparentPackets))Trace(b,"packets");
 }catch(const std::exception& e){Report(b,e.what());}
 return true;
}
void DrawUnitWorldModel(CM2Model* model,int32_t packet,uint32_t flags,uint32_t pass){Draw(model,packet,flags,pass);}
CM2Model* CreatePreviewModel(CM2Scene* scene,const char* path){
 if(!scene||!IsM3(path))return nullptr;
 auto* proxy=scene->CreateModel("Spells\\ErrorCube.mdx",1);if(!proxy)return nullptr;
 bindingHostThread=true;auto b=std::make_unique<Binding>();b->preview=true;b->proxy=proxy;b->path=path;
 try{b->source=Load(path);}catch(const std::exception& e){Report(*b,e.what());}
 proxies[proxy]=b.get();previewBindings[proxy]=std::move(b);return proxy;
}
void UpdatePreviewModel(CM2Model* model){
 const auto found=previewBindings.find(model);if(found==previewBindings.end())return;auto& b=*found->second;
 try{if(!Ready(b))return;const auto now=OsGetAsyncTimeMs();const auto seconds=b.lastTime?double(uint32_t(now-b.lastTime))/1000:0;b.lastTime=now;
  b.instance->SetWorldTransform(SourceBasis()*model->m_worldTransform);
  if(!b.instance->Advance(seconds,{}))throw Error(b.instance->ErrorText());
  auto& renderer=b.instance->Renderer();if(!b.previewEffectTried&&renderer.EffectSequence()<0&&!renderer.Data().EffectSequences().empty()&&
      (!renderer.Data().EffectBinding().particles.empty()||!renderer.Data().Effects().ribbons.empty())){
   b.previewEffectTried=true; // effects are optional: a clip without bound owners must not hide the body
   const auto& clips=renderer.Data().EffectSequences();
   try{for(size_t i=0;i<clips.size();++i)if(SameName(clips[i].name,"Stand")){renderer.SetEffectSequence(int32_t(i));break;}}
   catch(const std::exception& e){const auto detail="M3 preview effects not started for "+b.path+": "+e.what();ASEC_LuaDiagnostic(nullptr,detail.c_str(),0,0,0,__FILE__,__LINE__);}
  }
 }catch(const std::exception& e){Report(b,e.what());}
}
void RemovePreviewModel(CM2Model* model){
 const auto found=previewBindings.find(model);if(found==previewBindings.end())return;
 autoEffects.erase(found->second.get());Disconnect(*found->second,nullptr);previewBindings.erase(found);
}
bool UnitCameraBounds(const CGUnit_C* unit,float& lower,float& upper,bool& pending){
 lower=upper=0;pending=false;if(!unit||!unit->m_obj)return false;const auto found=bindings.find(unit->m_obj->m_guid.m_guid);
 if(found==bindings.end()||found->second->identity!=unit)return false;const auto& source=found->second->source;
 if(source){pending=!source->ready.load(std::memory_order_acquire);if(!pending&&source->error.empty()){lower=source->cameraLower;upper=source->cameraUpper;}}
 return true;
}
bool HasUnitWorldModel(const CGUnit_C* unit){
 if(!unit||!unit->m_obj)return false;const auto it=bindings.find(unit->m_obj->m_guid.m_guid);
 return it!=bindings.end()&&it->second->identity==unit;
}
bool UnitModelBounds(const CM2Model* model,CAaBox& box,bool worldSpace,bool maximum){
 if(!bindingHostThread)return false;
 const auto proxy=proxies.find(const_cast<CM2Model*>(model));if(proxy==proxies.end())return false;
 auto& b=*proxy->second;
 if(b.proxy!=model||!b.instance||!b.error.empty())return false;
 try{
  auto transform=SourceBasis();
  if(worldSpace)transform=transform*(model->m_attachParent?model->m_matrixF4*model->m_scene->m_viewInv:model->m_worldTransform);
  WorldBounds bounds;if(!b.instance->Bounds(transform,bounds,maximum))return false;
  box=CAaBox(bounds.lower,bounds.upper);
  // Native spatial indexing requires strictly positive extents. A small
  // outward margin also prevents rounding a flat source mesh to an empty box.
  box.b.x-=.01f;box.b.y-=.01f;box.b.z-=.01f;box.t.x+=.01f;box.t.y+=.01f;box.t.z+=.01f;
  if(maximum&&!worldSpace){
   box.b.x=std::floor(box.b.x*20)/20;box.b.y=std::floor(box.b.y*20)/20;box.b.z=std::floor(box.b.z*20)/20;
   box.t.x=std::ceil(box.t.x*20)/20;box.t.y=std::ceil(box.t.y*20)/20;box.t.z=std::ceil(box.t.z*20)/20;
  }
  // Validate before passing source data to native spatial/light structures.
  GxGeometryTransform::EnclosingSphere(box);++b.boundsQueries;return true;
 }catch(const std::exception& e){Report(b,e.what());return false;}
}
bool UnitModelSphere(const CM2Model* model,CAaSphere& sphere,bool worldSpace,bool maximum){
 CAaBox box;if(!UnitModelBounds(model,box,worldSpace,maximum))return false;
 sphere=GxGeometryTransform::EnclosingSphere(box);return true;
}
bool UnitPickingSphere(const CM2Model* model,CAaSphere& sphere){
 if(!bindingHostThread||!model)return false;
 const auto proxy=proxies.find(const_cast<CM2Model*>(model));if(proxy==proxies.end())return false;
 if(proxy->second->proxy!=model)return false;auto& b=*proxy->second;
 sphere.c=C3Vector(0,0,0);sphere.r=0;
 if(!b.instance||!b.error.empty())return true;
 try{WorldBounds bounds;bool found=false;
  const auto matrix=SourceBasis()*model->m_matrixF4;
  if(model->m_hitTestMode==3){Vec3 lower,upper;if(b.instance->Renderer().CollisionBounds(lower,upper)){
   const auto box=GxGeometryTransform(matrix).Bounds(CAaBox(C3Vector(lower.x,lower.y,lower.z),C3Vector(upper.x,upper.y,upper.z)));bounds={box.b,box.t};found=true;}
  }else found=b.instance->Bounds(matrix,bounds);
  if(found){sphere=GxGeometryTransform::EnclosingSphere(CAaBox(bounds.lower,bounds.upper));++b.boundsQueries;}
 }catch(const std::exception& e){if(model->m_hitTestMode==3)CollisionProblem(b,e.what());else Report(b,e.what());}
 return true;
}
bool UnitPickGeometry(const CM2Model* model,const C3Vector& start,const C3Vector& dir,float maximum,float& distance,bool& hit,bool worldSpace){
 if(!bindingHostThread||!model)return false;
 const auto proxy=proxies.find(const_cast<CM2Model*>(model));if(proxy==proxies.end())return false;
 if(proxy->second->proxy!=model)return false;auto& b=*proxy->second;
 hit=false;if(!b.instance||!b.error.empty()||(model->m_hitTestMode!=3&&model->m_effectiveAlpha<=0))return true;
 TimeSample measured(b.pickTiming);++b.pickQueries;if(model->m_hitTestMode==3)++b.collisionQueries;
 try{
  const auto matrix=SourceBasis()*(worldSpace?model->m_worldTransform:model->m_matrixF4);GxGeometryTransform validate(matrix);
  const auto inverse=matrix.Inverse(matrix.Determinant());
  const auto localStart=start*inverse;const auto localDir=TransformDirection(dir,inverse);
  const PickRay ray{{localStart.x,localStart.y,localStart.z},{localDir.x,localDir.y,localDir.z},maximum};
  PickHit result;hit=b.instance->Renderer().Pick(ray,model->m_hitTestMode,result);
  if(hit){distance=float(result.distance);++b.pickHits;if(model->m_hitTestMode==3)++b.collisionHits;}
  if(b.pickQueries==1||(hit&&b.pickHits==1))Trace(b,"pick");
  if(model->m_hitTestMode==3&&(b.collisionQueries==1||(hit&&b.collisionHits==1)))Trace(b,"collision_pick");
 }catch(const std::exception& e){if(model->m_hitTestMode==3)CollisionProblem(b,e.what());else Report(b,e.what());hit=false;}
 return true;
}
void UpdateWorldBindings(uint32_t time){
 for(auto& entry:proxies){auto& b=*entry.second;
  auto* unit=static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(WOWGUID{b.guid},TYPEMASK_UNIT,__FILE__,__LINE__));
  if(!unit||unit!=b.identity||(b.mount?unit->m_mountModel:unit->m_worldModel)!=b.proxy||!b.proxy||!b.proxy->IsLoaded(0,0))continue;
  try{
   if(!Ready(b))continue;
   const auto tickStart=RuntimeTiming::Clock::now();
   uint32_t flags=0;auto chosen=unit->ChooseAnimation(UINT32_MAX,&flags,nullptr);
   if(b.mount)chosen=unit->IsJumping()?38u:(unit->m_movementData.m_flags&0xF)?5u:0u;
   if(b.emote.Action()>=0){
    const auto clip=SelectWorldAction(b.instance->Renderer().Actions(),uint32_t(b.emote.Action()));
    if(clip<0){b.actionError="Explicit M3 emote has no mapped source clip";b.emote.Clear();Trace(b,"emote_unmapped");}
    else{const auto& source=b.instance->Renderer().Data().Model().animations.at(clip);
     const bool interrupted=(unit->m_movementData.m_flags&0xF)||unit->IsJumping()||unit->IsInCombat()||unit->GetClientStandState()||unit->m_mountModel||unit->m_unit->m_unitChannelSpell||(unit->m_animationState&0x400);
     chosen=b.emote.Choose(chosen,interrupted,time,uint32_t(source.end-source.start));}
   }
   bool restartNow=false;
   if(!b.mount){auto shot=oneShots.find(b.guid);
    if(shot!=oneShots.end()){auto& o=shot->second;
     if(o.pending){o.pending=false;o.active=true;o.clock=false;restartNow=true;}
     if(o.active){
      if(unit->m_unit->m_unitFieldHealth<=0)o.active=false; // death takes over
      else{const auto clip=SelectBodyAction(b.instance->Renderer().Actions(),uint32_t(o.action));
       if(clip<0)o.active=false;
       else{const auto& source=b.instance->Renderer().Data().Model().animations.at(size_t(clip));
        if(!o.clock){o.started=time;o.clock=true;}
        if(uint32_t(time-o.started)>=uint32_t(std::max<int64_t>(1,int64_t(source.end)-int64_t(source.start))))o.active=false;
        else chosen=uint32_t(o.action);}}}}}
   if(chosen<=505)b.action=chosen;
   if(b.emote.Action()<0)b.restartEmote=false;
   // Immutable banks cannot acquire a missing action on the next frame.
   // Attempt once per requested action, avoiding repeated exceptions per tick.
   if(b.lastAttemptedAction!=b.action||b.restartEmote||restartNow){
    b.lastAttemptedAction=b.action;
    if(!b.instance->SetWoWAction(b.action,b.restartEmote||restartNow))b.actionError=b.instance->ErrorText();else b.actionError.clear();
    b.restartEmote=false;
    Trace(b,"action");
   }
   auto world=b.mount?b.proxy->m_worldTransform:unit->GetMatrix();if(!b.mount)world.Scale(unit->GetTrueScale());
   b.instance->SetWorldTransform(SourceBasis()*world);
   const double seconds=b.lastTime?double(uint32_t(time-b.lastTime))/1000:0;b.lastTime=time;
   if(!b.instance->Advance(seconds,{}))throw Error(b.instance->ErrorText());
   // HotS actors start a model's particle effects together with its body clip. Without that data the effect timeline
   // follows the body clip by name, falling back to Stand, unless a script chose one (ASECM3_SetPlayerEffectSequence).
   {auto& follow=autoEffects[&b];auto& renderer=b.instance->Renderer();const int32_t clip=renderer.CurrentSequence();
    if(!follow.userSelected&&clip>=0&&clip!=follow.followedClip&&(!renderer.Data().EffectBinding().particles.empty()||!renderer.Data().Effects().ribbons.empty())){
     follow.followedClip=clip;const auto& effects=renderer.Data().EffectSequences();const auto& body=renderer.Data().Model().animations.at(size_t(clip)).name;
     int32_t pick=-1;for(size_t i=0;i<effects.size()&&pick<0;++i)if(SameName(effects[i].name,body))pick=int32_t(i);
     for(size_t i=0;i<effects.size()&&pick<0;++i)if(SameName(effects[i].name,"Stand"))pick=int32_t(i);
     if(pick>=0&&pick!=renderer.EffectSequence()){try{renderer.SetEffectSequence(pick);b.particleDrawReported=b.particleErrorReported=false;}catch(const std::exception& e){const auto detail="M3 effects not started for "+b.path+": "+e.what();ASEC_LuaDiagnostic(nullptr,detail.c_str(),0,0,0,__FILE__,__LINE__);}}
    }}
   auto attachments=b.instance->Renderer().AttachmentFrames();
   // A mount's seat (29) is the rider's attachment 0; the source's own 0 (Ref_Shield) would otherwise win the lookup.
   if(b.mount){auto& points=attachments.points;points.erase(std::remove_if(points.begin(),points.end(),[](const auto& p){return p.id==0;}),points.end());
    for(auto& point:points)if(point.id==29)point.id=0;}
   PublishModelAttachments(b.proxy,attachments,SourceBasis(),b.hasRider?&b.rider:nullptr);
   CAaBox bounds;
   if(UnitModelBounds(b.proxy,bounds,false,true)&&(!b.publishedBounds||
      bounds.b.x!=b.spatialBounds.b.x||bounds.b.y!=b.spatialBounds.b.y||bounds.b.z!=b.spatialBounds.b.z||
      bounds.t.x!=b.spatialBounds.t.x||bounds.t.y!=b.spatialBounds.t.y||bounds.t.z!=b.spatialBounds.t.z)){
    b.spatialBounds=bounds;b.publishedBounds=true;unit->UpdateWorldObject(0);
    if(++b.boundsUpdates==1)Trace(b,"bounds");
   }
   b.tickTiming.Add(tickStart);
   if(!b.perfTime){b.perfTime=time;b.perfQueued=b.queued;}
   else if(uint32_t(time-b.perfTime)>=5000){
    b.queueHz=double(b.queued-b.perfQueued)*1000/uint32_t(time-b.perfTime);Trace(b,"perf");
    b.perfTime=time;b.perfQueued=b.queued;b.tickTiming={};b.queueTiming={};b.drawTiming={};
   }
  }catch(const std::exception& e){Report(b,e.what());}
 }
}
const char* PrepareUnitModel(CGUnit_C* unit,const char* path){
 bindingHostThread=true;
 if(!unit||!unit->m_obj)return path;const auto guid=unit->m_obj->m_guid.m_guid;
 if(!IsM3(path)){RemoveUnitModel(unit);return path;}
 // GetModelFileName returns the exact DBC string pointer on the DBC branch.
 // Compare identity before copying it: equal text in an override is still an
 // override. No DBC or server fields are changed or retained by the binding.
 const auto* modelData=unit->GetModelData();const bool fromDBC=modelData&&path==modelData->m_modelName;
 const int32_t displayId=unit->m_displayInfo?unit->m_displayInfo->m_id:0,modelId=modelData?modelData->m_id:0;
 const auto objectType=unit->m_obj->m_type;
 auto existing=bindings.find(guid);if(existing!=bindings.end()&&existing->second->identity==unit&&Key(existing->second->path)==Key(path)){auto& b=*existing->second;b.fromDBC=fromDBC;b.displayId=displayId;b.modelId=modelId;b.objectType=objectType;Disconnect(b,unit);return "Spells\\ErrorCube.mdx";}
 RemoveUnitModel(unit);if(bindings.size()>=512){ASEC_LuaDiagnostic(nullptr,"World M3 binding budget exceeds 512 units",0,0,0,__FILE__,__LINE__);return "Spells\\ErrorCube.mdx";}
 auto binding=std::make_unique<Binding>();binding->guid=guid;binding->identity=unit;binding->path=path;
 binding->fromDBC=fromDBC;binding->displayId=displayId;binding->modelId=modelId;binding->objectType=objectType;
 try{binding->source=Load(path);}catch(const std::exception& e){Report(*binding,e.what());}
 bindings[guid]=std::move(binding);return "Spells\\ErrorCube.mdx";
}
void AttachUnitModel(CGUnit_C* unit){if(!unit||!unit->m_obj)return;const auto found=bindings.find(unit->m_obj->m_guid.m_guid);if(found==bindings.end()||found->second->identity!=unit||!unit->m_worldModel)return;
 auto& b=*found->second;b.proxy=unit->m_worldModel;proxies[b.proxy]=&b;
 RegisterModelAttachments(b.proxy,b.source?b.source->attachmentCatalog:AttachmentData{},SourceBasis());
 Trace(b,"attached");
 b.proxy->SetGeometryVisible(0,UINT32_MAX,0);b.proxy->SetEmittersEnabled(0);b.proxy->SetRibbonsEnabled(0);
 // The native name/bow callbacks remain owned by their original systems.
}
void RemoveUnitModel(CGUnit_C* unit){if(!unit||!unit->m_obj)return;const auto found=bindings.find(unit->m_obj->m_guid.m_guid);if(found==bindings.end()||found->second->identity!=unit)return;Disconnect(*found->second,unit);autoEffects.erase(found->second.get());oneShots.erase(found->first);bindings.erase(found);}
const char* PrepareMountModel(CGUnit_C* unit,const char* path,int32_t displayId,int32_t modelId){
 bindingHostThread=true;if(!unit||!unit->m_obj)return path;
 RemoveMountModel(unit);if(!IsM3(path))return path;
 if(bindings.size()+mountBindings.size()>=512){ASEC_LuaDiagnostic(nullptr,"World M3 binding budget exceeds 512 models",0,0,0,__FILE__,__LINE__);return "Spells\\ErrorCube.mdx";}
 auto b=std::make_unique<Binding>();b->guid=unit->m_obj->m_guid.m_guid;b->identity=unit;b->path=path;b->mount=true;
 b->fromDBC=true;b->displayId=displayId;b->modelId=modelId;b->objectType=unit->m_obj->m_type;
 try{
  static const auto riders=[] {
   std::map<std::string,C3Vector> out;const auto path=std::filesystem::path(SFile::m_basePath)/"Data"/"HotS"/"MountRiders.csv";
   if(!std::filesystem::exists(path))return out;
   const auto bytes=ReadLooseAssetFile(path,1024*1024);std::istringstream lines(std::string(bytes.begin(),bytes.end()));
   for(std::string line;std::getline(lines,line);){
    if(line.empty()||line[0]=='#')continue;if(line.size()>1024||out.size()>=4096)throw Error("HotS rider metadata exceeds budget");
    std::replace(line.begin(),line.end(),';',' ');std::istringstream row(line);std::string name,extra;C3Vector p;
    if(!(row>>name>>p.x>>p.y>>p.z)||(row>>extra)||!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||
       std::abs(p.x)>100||std::abs(p.y)>100||std::abs(p.z)>100||Key(name).rfind("hots\\mounts\\",0)!=0||
       !out.emplace(Key(name),p).second)throw Error("Invalid HotS rider metadata");
   }return out;
  }();
  const auto seat=riders.find(Key(path));if(seat!=riders.end()){b->rider=seat->second;b->hasRider=true;}
  b->source=Load(path);
 }catch(const std::exception& e){Report(*b,e.what());}
 mountBindings[b->guid]=std::move(b);return "Spells\\ErrorCube.mdx";
}
void AttachMountModel(CGUnit_C* unit){
 if(!unit||!unit->m_obj||!unit->m_mountModel)return;const auto it=mountBindings.find(unit->m_obj->m_guid.m_guid);
 if(it==mountBindings.end()||it->second->identity!=unit)return;auto& b=*it->second;b.proxy=unit->m_mountModel;proxies[b.proxy]=&b;
 auto points=b.source?b.source->attachmentCatalog:AttachmentData{};
 points.points.erase(std::remove_if(points.points.begin(),points.points.end(),[](const auto& p){return p.wowId==0;}),points.points.end());
 for(auto& point:points.points)if(point.wowId==29)point.wowId=0;
 if(b.hasRider&&std::none_of(points.points.begin(),points.points.end(),[](const auto& p){return p.wowId==0;})){
  AttachmentPoint seat;seat.wowId=0;seat.bindValid=true;seat.bindPivot={b.rider.y,-b.rider.x,b.rider.z};points.points.push_back(seat);
 }
 RegisterModelAttachments(b.proxy,points,SourceBasis());
 b.proxy->SetGeometryVisible(0,UINT32_MAX,0);b.proxy->SetEmittersEnabled(0);b.proxy->SetRibbonsEnabled(0);Trace(b,"mount_attached");
}
void RemoveMountModel(CGUnit_C* unit,CM2Model* expected){
 if(!unit||!unit->m_obj)return;const auto it=mountBindings.find(unit->m_obj->m_guid.m_guid);
 if(it==mountBindings.end()||it->second->identity!=unit||(expected&&it->second->proxy!=expected))return;Disconnect(*it->second,unit);autoEffects.erase(it->second.get());mountBindings.erase(it);
}
void OnUnitBaseAnimation(CGUnit_C* unit,uint32_t animId,int32_t){
 if(!unit||!unit->m_obj||!IsOneShotAnim(animId))return;
 const auto found=bindings.find(unit->m_obj->m_guid.m_guid);if(found==bindings.end()||found->second->identity!=unit)return;
 auto& o=oneShots[found->first];if(o.active||o.pending)return;o.action=int32_t(animId);o.pending=true;
}
void OnUnitSwing(CGUnit_C* unit,uint32_t animId){
 if(!unit||!unit->m_obj||animId>505)return;
 const auto found=bindings.find(unit->m_obj->m_guid.m_guid);if(found==bindings.end()||found->second->identity!=unit)return;
 auto& o=oneShots[found->first];o.action=int32_t(animId);o.pending=true;o.active=false;
}
void SetUnitAction(CGUnit_C* unit,uint32_t action){if(!unit||!unit->m_obj||action>505)return;const auto found=bindings.find(unit->m_obj->m_guid.m_guid);if(found!=bindings.end()&&found->second->identity==unit)found->second->action=action;}
void ResetWorldBindings(){ClearModelAttachments();proxies.clear();bindings.clear();mountBindings.clear();previewBindings.clear();autoEffects.clear();oneShots.clear();sources.clear();parser.reset();context.reset();}
void SetUnitEmote(CGUnit_C* unit,uint32_t action){
 if(!unit||!unit->m_obj||action>505)return;const auto it=bindings.find(unit->m_obj->m_guid.m_guid);
 if(it!=bindings.end()&&it->second->identity==unit){it->second->emote.Set(action);it->second->restartEmote=true;Trace(*it->second,"emote_request");}
}
void ClearUnitEmote(CGUnit_C* unit){if(!unit||!unit->m_obj)return;const auto it=bindings.find(unit->m_obj->m_guid.m_guid);if(it!=bindings.end()&&it->second->identity==unit)it->second->emote.Clear();}
bool SetUnitEffectSequence(CGUnit_C* unit,int32_t sequence,std::string& error){
 error.clear();if(!unit||!unit->m_obj){error="There is no active world unit";return false;}
 const auto found=bindings.find(unit->m_obj->m_guid.m_guid);
 if(found==bindings.end()||found->second->identity!=unit){error="This unit has no M3 world binding";return false;}
 auto& b=*found->second;if(!b.instance){error="The M3 world source is not ready yet";return false;}
 try{b.instance->Renderer().SetEffectSequence(sequence);autoEffects[&b].userSelected=true;b.particleDrawReported=b.particleErrorReported=false;Trace(b,"effect_request");return true;}
 catch(const std::exception& e){error=e.what();return false;}
}
UnitWorldInfo GetUnitWorldInfo(const CGUnit_C* unit){UnitWorldInfo out;if(!unit||!unit->m_obj)return out;const auto found=bindings.find(unit->m_obj->m_guid.m_guid);if(found==bindings.end()||found->second->identity!=unit)return out;
 const auto& b=*found->second;out.bound=true;out.path=b.path;out.error=!b.error.empty()?b.error:!b.actionError.empty()?b.actionError:b.collisionError;out.draws=b.draws;out.loading=b.source&&!b.source->ready.load(std::memory_order_acquire);out.ready=bool(b.instance);
 out.fromDBC=b.fromDBC;out.displayId=b.displayId;out.modelId=b.modelId;out.objectType=b.objectType;
 if(b.source&&b.source->ready.load(std::memory_order_acquire)){if(!b.source->error.empty())out.error=b.source->error;out.effectError=b.source->effectError;if(b.source->scene){out.animationSource=b.source->scene->AnimationSource();out.effectAnimationSource=b.source->scene->EffectAnimationSource();out.effectSequences=uint32_t(b.source->scene->EffectSequences().size());out.effectMaterialRecords=uint32_t(b.source->scene->EffectMaterials().records.size());out.effectMaterialError=b.source->scene->EffectMaterialError();}}
 if(b.instance){const auto& r=b.instance->Renderer();out.effectClip=r.EffectSequence();out.effectParticles=r.EffectParticles();out.effectSpawned=r.EffectSpawned();out.effectDropped=r.EffectDropped();out.effectDraws=r.WorldParticleDraws();out.effectBirthSamples=r.ParticleBirthSamples();out.effectSimulationError=r.ParticleError();out.effectDrawError=r.ParticleDrawError();for(const auto& clip:r.Data().EffectSequences())out.effectClips.push_back(clip.name);}
 if(context){out.checks=context->BoundaryChecks();out.failures=context->BoundaryFailures();}return out;
}
}
