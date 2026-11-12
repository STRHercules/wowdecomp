#include "GxScene.h"
#include "GxGeometryTransform.h"
#include "SceneEnvironment.h"
#include "ASECM3/MaterialEvaluation.h"
#include "ASECM3/Events.h"
#include "ASECM3/ParticleBillboards.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/DrawTrace.h"
#include "Engine/Source/Gx/CGxBuf.h"
#include "Engine/Source/Gx/CGxVertexPCT.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/lua-5.1/src/asecDiagnostics.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <cstdio>
namespace ASECM3::Client {
namespace {
std::string Key(std::string value){for(auto& c:value)c=c=='/'?'\\':char(std::tolower(static_cast<unsigned char>(c)));return value;}
double Value(const MaterialRecord& record,const std::string& name,double fallback){const auto* property=record.Find(name);if(!property)return fallback;return std::visit([](auto v){return double(v);},property->value);}
C3Vector Point(const Vec3& p){return {p.x,p.y,p.z};}
const SceneRegion* Region(const SceneFrame& frame,uint32_t division,uint32_t region){for(const auto& r:frame.regions)if(r.division==division&&r.region==region)return &r;return nullptr;}
const MaterialLayer* Diffuse(const Material& material){for(const auto& layer:material.layers)if(layer.role=="layer_diff")return &layer;return nullptr;}
// Emissive-only effect materials (thrusters, glows) have no diffuse layer; their emissive bitmap is the visible one.
const MaterialLayer* PrimaryLayer(const Material& material){
    const auto* diffuse=Diffuse(material);if(diffuse&&!diffuse->image.empty())return diffuse;
    for(const char* role:{"layer_emis1","layer_emis2","layer_decal"})for(const auto& layer:material.layers)if(layer.role==role&&!layer.image.empty())return &layer;
    return diffuse;
}
// Each distinct line is appended once to Logs\ASECM3-materials.log: which materials fall back and why.
void LogMaterial(const std::string& line){
    static std::set<std::string> seen;if(!seen.insert(line).second)return;
    if(FILE* log=std::fopen("Logs\\ASECM3-materials.log","a")){std::fprintf(log,"%s\n",line.c_str());std::fclose(log);}
}
int UVChannel(uint32_t source){switch(source){case 0:return 0;case 1:return 1;case 9:return 2;case 10:return 3;default:return -1;}}
// A bounded preview recovery convention, not an interpretation of opaque MADD
// bytes: require one exact authored material-name + _Diff.dds reference. Never
// choose the first bitmap, a normal/specular map, or a missing effect gradient.
bool DiffuseRecovery(const Material& source,Material& output){
    if(source.type!=12||source.bufferSurface||source.name.empty())return false;
    const auto expected=Key(source.name+"_Diff.dds");
    std::string image;
    for(const auto& path:source.bufferTextures){
        const auto normalized=Key(path);const auto slash=normalized.find_last_of('\\');
        const auto basename=normalized.substr(slash==std::string::npos?0:slash+1);
        if(basename!=expected)continue;
        if(!image.empty()&&Key(image)!=normalized)return false;
        image=path;
    }
    if(image.empty())return false;
    output.type=1;output.name=source.name;output.record.tag="ASEC_APPROXIMATE_DIFFUSE";
    MaterialLayer layer;layer.role="layer_diff";layer.image=std::move(image);
    layer.record.tag="ASEC_APPROXIMATE_DIFFUSE_LAYER";
    layer.record.properties.push_back({"flags","uint32",uint64_t(12)});
    layer.record.properties.push_back({"color_channels","uint32",uint64_t(0)});
    layer.record.properties.push_back({"uv_source","uint32",uint64_t(0)});
    output.layers.push_back(std::move(layer));return true;
}
}
GxScene::GxScene(std::shared_ptr<const Scene> scene,const std::string& model):m_scene(std::move(scene)) {
    if(!m_scene)throw Error("Cannot render a missing M3 scene");
    if(!m_scene->Collision().error.empty())DebugLog("ASEC M3 collision decode: %s",m_scene->Collision().error.c_str());
    if(!m_scene->Attachments().error.empty())DebugLog("ASEC M3 attachment decode: %s",m_scene->Attachments().error.c_str());
    if(!m_scene->Effects().error.empty()){const auto detail="M3 effects disabled; source="+model+" error="+m_scene->Effects().error;ASEC_LuaDiagnostic(nullptr,detail.c_str(),0,0,0,__FILE__,__LINE__);}
    if(!m_scene->EffectMaterialError().empty()){const auto detail="M3 effect material overlay disabled; source="+model+" error="+m_scene->EffectMaterialError();ASEC_LuaDiagnostic(nullptr,detail.c_str(),0,0,0,__FILE__,__LINE__);}
    for(uint32_t i=0;i<m_scene->Materials().materials.size();++i){
        const auto& source=m_scene->Materials().materials[i];Material recovery;
        if(source.type==12&&source.bufferSurface&&!source.bufferSurfaceError.empty()){
            const auto detail="M3 partial MADD surface; model="+model+" material="+source.name+"; "+source.bufferSurfaceError;
            DebugLog("%s",detail.c_str());
            std::ofstream log(std::filesystem::path(SFile::m_basePath)/"Logs"/"ASECM3.log",std::ios::app);if(log)log<<detail<<'\n';
        }
        if(!DiffuseRecovery(source,recovery)){
            if(source.type==12&&!source.bufferSurface){
                const auto detail="M3 neutral MADD fallback; model="+model+" material="+source.name+"; source shader unsupported; error="+
                    (source.bufferSurfaceError.empty()?source.bufferDecodeError:source.bufferSurfaceError);
                DebugLog("%s",detail.c_str());
                std::ofstream log(std::filesystem::path(SFile::m_basePath)/"Logs"/"ASECM3.log",std::ios::app);if(log)log<<detail<<'\n';
            }
            continue;
        }
        const auto detail="M3 approximate diffuse-only recovery; model="+model+" material="+source.name+" texture="+recovery.layers.front().image+" assumedUV=0; source shader NOT decoded; error="+source.bufferSurfaceError;
        m_diffuseRecovery.emplace(i,std::move(recovery));
        DebugLog("%s",detail.c_str());
        std::ofstream log(std::filesystem::path(SFile::m_basePath)/"Logs"/"ASECM3.log",std::ios::app);if(log)log<<detail<<'\n';
    }
    if(!m_scene->Model().divisions.empty())m_regionVisible.assign(m_scene->Model().divisions[0].regions.size(),true);
    if(!m_scene->Physics().error.empty())DebugLog("ASEC M3 physics decode: %s",m_scene->Physics().error.c_str());
    for(const auto& warning:m_scene->Physics().visibilityWarnings)DebugLog("ASEC M3 cloth region visibility: %s",warning.c_str());
    m_frame=m_scene->EvaluateGroup();
    try{auto cloth=std::make_unique<ClothSimulation>(*m_scene);if(cloth->Cloths()){cloth->Advance(m_frame,0);m_cloth=std::move(cloth);}}
    catch(const std::exception& e){m_physicsError=e.what();DebugLog("ASEC M3 cloth initialization: %s",e.what());}
    try{
        m_actions=ResolveActions(m_scene->Model().animations,model);
        for(size_t i=0;i<m_actions.size();++i)if(m_scene->SequenceGroup(uint32_t(i))<0)m_actions[i].excluded=true;
    }catch(const std::exception& e){m_actionError=e.what();DebugLog("ASEC M3 semantic policy: %s",e.what());}
}
bool GxScene::IsSimulationRegion(uint32_t region)const{
    const auto& helpers=m_scene->Physics().hiddenSimulationRegions;
    return std::binary_search(helpers.begin(),helpers.end(),region);
}
bool GxScene::RegionVisible(uint32_t region)const{return m_regionVisible.at(region)&&!IsSimulationRegion(region);}
void GxScene::SetRegionVisible(uint32_t region,bool visible){
    if(region>=m_regionVisible.size())throw Error("M3 geometry region is out of range");
    if(visible&&IsSimulationRegion(region))throw Error("This region is a physics helper, not render geometry");
    m_regionVisible[region]=visible;
}
void GxScene::SetWoWAction(uint32_t action){
    if(!m_actionError.empty())throw Error(m_actionError);
    const auto clip=SelectAction(m_actions,action);
    if(clip<0)throw Error("No eligible M3 source clip maps to WoW action "+std::to_string(action));
    SetSequence(clip);
}
void GxScene::SetPhysicsEnabled(bool enabled){
    m_pickDirty=true;
    auto frame=EvaluateFrame(m_elapsed);
    if(m_rigid)m_rigid->Advance(frame,0);
    std::unique_ptr<ClothSimulation> cloth;
    if(enabled){auto candidate=std::make_unique<ClothSimulation>(*m_scene);if(candidate->Cloths()){candidate->Advance(frame,0);cloth=std::move(candidate);}}
    m_frame=std::move(frame);m_cloth=std::move(cloth);m_physicsError.clear();
}
void GxScene::SetRigidEnabled(bool enabled){
    m_pickDirty=true;
    auto frame=EvaluateFrame(m_elapsed);
    std::unique_ptr<RigidSimulation> rigid;
    if(enabled){auto candidate=std::make_unique<RigidSimulation>(*m_scene);if(!candidate->Bodies())throw Error("This M3 has no rigid bodies");candidate->Advance(frame,0);rigid=std::move(candidate);}
    if(m_cloth){m_cloth->Reset();m_cloth->Advance(frame,0);}
    m_frame=std::move(frame);m_rigid=std::move(rigid);m_rigidError.clear();m_sourceEventsEnabled=false;m_rigidFromEvent=false;
}
void GxScene::SetEffectSequence(int32_t sequence){
    if(sequence<-1)throw Error("M3 effect sequence must be -1 or a nonnegative source index");
    std::vector<std::unique_ptr<ParticleEmitter>> particles;
    std::vector<Material> materials,materials_ribbon;
    std::vector<std::unique_ptr<RibbonTrail>> ribbons;
    if(sequence>=0){
        if(size_t(sequence)>=m_scene->EffectSequences().size())throw Error("M3 effect sequence is unavailable");
        if(m_scene->EffectBinding().particles.size()>256)throw Error("M3 effect phase exceeds 256 emitters");
        if(sequence<0)ribbons.clear();
        size_t keys=0,capacity=0;const auto group=m_scene->EffectSequenceGroup(uint32_t(sequence));
        for(const auto& link:m_scene->EffectBinding().particles){
            if(const auto* rate=m_scene->EffectScalarTrack(link.meshEmitter,"emit_rate",group))keys+=rate->times.size();
            if(const auto* count=m_scene->EffectCountTrack(link.meshEmitter,"emit_count",group))keys+=count->times.size();
            if(keys>262144)throw Error("M3 effect phase exceeds 262144 compiled control points");
            // An owner this module cannot simulate is skipped and named in the diagnostics; the others still run.
            try{
                auto emitter=std::make_unique<ParticleEmitter>(m_scene,link.meshEmitter,uint32_t(sequence),0x6d2b79f5u+link.meshEmitter);
                if(capacity+emitter->Capacity()>32768)throw Error("M3 effect phase exceeds 32768 live particles");
                auto material=ParticleSurfaceMaterial(m_scene->Materials().materials.at(m_scene->Effects().particles.at(link.meshEmitter).material));
                capacity+=emitter->Capacity();materials.push_back(std::move(material));particles.push_back(std::move(emitter));
            }catch(const std::exception& e){
                const auto detail="M3 particle owner "+std::to_string(link.meshEmitter)+" skipped: "+e.what();
                ASEC_LuaDiagnostic(nullptr,detail.c_str(),0,0,0,__FILE__,__LINE__);
            }
        }
        // Ribbons come from the same embedded timeline; a model may have only these.
        if(m_scene->EffectAnimationSource()=="<embedded>")for(uint32_t i=0;i<m_scene->Effects().ribbons.size()&&ribbons.size()<64;++i){
            const auto& definition=m_scene->Effects().ribbons[i];
            if(definition.material>=m_scene->Materials().materials.size()||definition.bone>=m_scene->Model().skeleton.size())continue;
            try{materials_ribbon.push_back(ParticleSurfaceMaterial(m_scene->Materials().materials.at(definition.material)));ribbons.push_back(std::make_unique<RibbonTrail>(m_scene,i));}
            catch(const Error&){if(materials_ribbon.size()>ribbons.size())materials_ribbon.pop_back();}
        }
        if(particles.empty()&&ribbons.empty())throw Error("M3 effect phase has no bound particle or ribbon owners");
    }
    m_ribbons=std::move(ribbons);m_ribbonMaterials=std::move(materials_ribbon);
    m_particles=std::move(particles);m_particleMaterials=std::move(materials);m_worldParticles.clear();m_effectSequence=sequence;m_effectElapsed=0;m_effectFraction=0;m_particleDraws=0;m_worldParticleDraws=0;m_particleError.clear();m_particleDrawError.clear();
    m_particlePreviousWorld=m_particleWorld;
    m_particleBirthSamples=0;
    m_particleSurfaceSamples=0;
}
void GxScene::LoadNextTexture(const std::function<std::vector<uint8_t>(const std::string&)>& provider){
    if(m_textureCursor>=m_scene->Textures().size())return;
    const auto& name=m_scene->Textures()[m_textureCursor++];
    try{
        auto image=DDSImage::Parse(provider(name));
        auto texture=GxDDS::Create(std::move(image),"ASECM3:"+name,true,true);
        m_textures.emplace(Key(name),std::move(texture));
    }catch(const std::exception& e){m_textureErrors.push_back(name+": "+e.what());DebugLog("ASEC M3 DDS: %s",m_textureErrors.back().c_str());std::ofstream log(std::filesystem::path(SFile::m_basePath)/"Logs"/"ASECM3.log",std::ios::app);if(log)log<<m_textureErrors.back()<<'\n';}
}
size_t GxScene::UploadedTextures() const {size_t count=0;for(const auto& entry:m_textures)if(entry.second->Uploaded())++count;return count;}
size_t GxScene::CubeTexturesReady() const {size_t count=0;for(const auto& entry:m_textures)if(entry.second->Image().faces==6&&entry.second->Uploaded()&&entry.second->UploadedFaces()==63&&!entry.second->InvalidUploadRequest())++count;return count;}
size_t GxScene::InvalidDDSUploads() const {size_t count=0;for(const auto& entry:m_textures)if(entry.second->InvalidUploadRequest())++count;return count;}
uint64_t GxScene::UploadLatches() const {uint64_t count=0;for(const auto& entry:m_textures)count+=entry.second->UploadLatches();return count;}
SceneFrame GxScene::EvaluateFrame(uint64_t elapsed) const {
    if(m_sequence<0)return m_scene->EvaluateGroup();
    if(m_loopSequence){const auto& clip=m_scene->Model().animations.at(m_sequence);
        const auto duration=uint64_t(clip.end)-clip.start;elapsed=duration?elapsed%duration:0;}
    return m_scene->EvaluateSequence(uint32_t(m_sequence),elapsed,0,m_blendFrom.empty()?nullptr:&m_blendFrom,TransitionWeight());
}
void GxScene::SetSequence(int32_t sourceIndex,bool loop){
    m_pickDirty=true;
    if(sourceIndex<-1)throw Error("M3 source clip index must be -1 or a nonnegative source index");
    SceneFrame frame;
    if(sourceIndex<0)frame=m_scene->EvaluateGroup();
    else frame=m_scene->EvaluateSequence(static_cast<uint32_t>(sourceIndex),0);
    std::vector<LocalTransform> from;
    const auto blendMs=sourceIndex>=0?m_scene->Model().animations.at(sourceIndex).blendMs:0;
    // First load and explicit rigid-state changes establish a new pose. For
    // ordinary clips, interruptions start from the currently blended local
    // transforms, never the previous clip's unblended target or cloth output.
    if(sourceIndex>=0&&m_sequence>=0&&blendMs&&!m_rigid){
        from=m_frame.animationLocals;
        frame=m_scene->EvaluateSequence(uint32_t(sourceIndex),0,0,&from,0);
    }
    if(m_cloth){m_cloth->Rebase(frame);m_cloth->Advance(frame,0);}
    m_blendFrom=std::move(from);m_blendMs=m_blendFrom.empty()?0:blendMs;m_blendElapsed=0;
    m_frame=std::move(frame);m_sequence=sourceIndex;m_loopSequence=loop;m_elapsed=0;m_fraction=0;m_animationError.clear();
    m_includeEventStart=true;m_lastEvent.clear();m_eventError.clear();
    if(m_rigidFromEvent){m_rigid.reset();m_rigidFromEvent=false;}
    if(m_rigid)m_rigid->Reset();
}
void GxScene::Advance(double seconds){
    if((m_sequence<0&&!m_cloth&&!m_rigid&&m_effectSequence<0)||!std::isfinite(seconds)||seconds<0)return;
    if(seconds>86400){m_animationError="M3 frame interval exceeds one day";m_sequence=-1;return;}
    m_fraction+=seconds*1000;
    const auto delta=static_cast<uint64_t>(m_fraction);m_fraction-=delta;
    if(delta>std::numeric_limits<uint64_t>::max()-m_elapsed){m_animationError="M3 elapsed clock overflow";m_sequence=-1;return;}
    const auto previousElapsed=m_elapsed;m_elapsed+=delta;
    if(!m_blendFrom.empty())m_blendElapsed=std::min(double(m_blendMs),m_blendElapsed+seconds*1000);
    m_pickDirty=true;
    SceneFrame previousFrame;
    try{
        auto frame=EvaluateFrame(m_elapsed);
        // Move the previous solved frame only while effects are active. No
        // geometry/UV deep copy or persistent second frame is needed.
        if(m_effectSequence>=0)previousFrame=std::move(m_frame);
        m_frame=std::move(frame);++m_poseSamples;
    }
    catch(const std::exception& e){m_animationError=e.what();m_sequence=-1;DebugLog("ASEC M3 animation: %s",e.what());return;}
    if(m_blendElapsed>=m_blendMs)m_blendFrom.clear();
    double rigidSeconds=seconds;
    if(m_sequence>=0&&m_eventError.empty())try{
        auto clip=m_scene->Model().animations.at(m_sequence);
        if(m_loopSequence)clip.flags&=~1u;
        const auto events=EventsBetween(m_scene->Animation(),m_scene->SequenceGroup(uint32_t(m_sequence)),clip,previousElapsed,m_elapsed,m_includeEventStart);
        m_includeEventStart=false;
        for(const auto& occurrence:events){
            ++m_eventsDelivered;m_lastEvent=occurrence.event.name;
            if(m_lastEvent!="Evt_Simulate"||!m_sourceEventsEnabled)continue;
            const auto& event=occurrence.event;
            bool identity=true;for(unsigned i=0;i<16;++i)identity&=std::abs(event.matrix[i]-(i%5==0?1.0f:0.0f))<1e-5f;
            if(event.id!=-1||event.bone!=-1||event.flags!=4||!identity||!event.payload.empty()||event.param0||event.param1)throw Error("Scoped/parameterized Evt_Simulate requires a source actor adapter");
            ++m_simulateEvents;if(m_rigid)continue; // activating an active world is idempotent
            auto rigid=std::make_unique<RigidSimulation>(*m_scene);if(!rigid->Bodies())throw Error("Evt_Simulate targets a model without rigid bodies");
            const auto group=m_scene->SequenceGroup(uint32_t(m_sequence));const auto activation=m_scene->EvaluateGroup(group,event.time);
            const auto priorTime=std::max(int64_t(clip.start),int64_t(event.time)-8);
            if(priorTime<event.time){const auto prior=m_scene->EvaluateGroup(group,int32_t(priorTime));rigid->Activate(activation,&prior,double(event.time-priorTime)/1000);}else rigid->Activate(activation);
            m_rigid=std::move(rigid);m_rigidFromEvent=true;rigidSeconds=double(m_elapsed-occurrence.elapsed)/1000;if(m_cloth)m_cloth->Reset();
        }
    }catch(const std::exception& e){m_eventError=e.what();DebugLog("ASEC M3 source event: %s",e.what());}
    if(m_rigid)try{m_rigid->Advance(m_frame,rigidSeconds);}catch(const std::exception& e){m_rigidError=e.what();m_rigid.reset();m_rigidFromEvent=false;m_frame=EvaluateFrame(m_elapsed);if(m_cloth)m_cloth->Reset();DebugLog("ASEC M3 rigid simulation stopped: %s",e.what());}
    if(m_cloth)try{m_cloth->Advance(m_frame,seconds);}catch(const std::exception& e){m_physicsError=e.what();m_cloth.reset();DebugLog("ASEC M3 cloth simulation stopped: %s",e.what());}
    if(m_effectSequence>=0)try{
        m_effectFraction+=seconds*1000;const auto delta=uint64_t(m_effectFraction);m_effectFraction-=delta;
        if(delta>std::numeric_limits<uint64_t>::max()-m_effectElapsed)throw Error("M3 effect clock overflow");
        const auto previousEffect=m_effectElapsed;m_effectElapsed+=delta;
        AdvanceParticleEmitters(previousEffect,previousFrame);
    }catch(const std::exception& e){
        m_particleError=e.what();m_particles.clear();m_effectSequence=-1;
        const auto detail="M3 particle motion stopped; bank="+m_scene->EffectAnimationSource()+" error="+m_particleError;
        ASEC_LuaDiagnostic(nullptr,detail.c_str(),0,0,0,__FILE__,__LINE__);
    }
}
bool GxScene::Pick(const PickRay& ray,uint32_t nativeMode,PickHit& hit){
    if(nativeMode==3){double distance;if(!IntersectCollision(m_scene->Collision(),m_frame.pose,ray,distance))return false;hit={distance,UINT32_MAX,UINT32_MAX};return true;}
    if(m_pickDirty){m_picking.Refit(m_frame);m_pickDirty=false;}
    std::vector<bool> visible(m_frame.regions.size(),true);
    for(size_t i=0;i<m_frame.regions.size();++i)if(m_frame.regions[i].division==0)visible[i]=RegionVisible(m_frame.regions[i].region);
    MaterialEvaluator values(m_scene->MaterialAnimation(),m_frame.materialGroup,m_frame.sourceTime);
    if(m_effectSequence>=0)m_scene->ApplyEffectMaterialValues(values,uint32_t(m_effectSequence),m_effectElapsed);
    const auto eligible=PickableRegions(m_frame,m_scene->Materials(),EvaluateMaterialPasses(m_scene->Materials(),values),visible,nativeMode);
    return m_picking.Intersect(m_frame,eligible,ray,hit);
}
bool GxScene::CollisionBounds(Vec3& lower,Vec3& upper)const{return ASECM3::CollisionBounds(m_scene->Collision(),m_frame.pose,lower,upper);}
bool GxScene::FrameBounds(Vec3& lower,Vec3& upper)const{
    bool found=false;
    for(const auto& region:m_frame.regions){
        if(region.division==0&&!RegionVisible(region.region))continue;
        for(const auto& vertex:region.vertices){const auto p=vertex.position;
            if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))throw Error("Nonfinite M3 render bound");
            if(!found){lower=upper=p;found=true;}else{lower.x=std::min(lower.x,p.x);lower.y=std::min(lower.y,p.y);lower.z=std::min(lower.z,p.z);upper.x=std::max(upper.x,p.x);upper.y=std::max(upper.y,p.y);upper.z=std::max(upper.z,p.z);}
        }
    }
    return found;
}
std::string GxScene::Status() const {
    std::ostringstream out;out<<"M3 surface passes "<<m_materialPasses<<" | DDS "<<m_textures.size()<<'/'<<m_scene->Textures().size();
    if(!m_textureErrors.empty())out<<" | "<<m_textureErrors.size()<<" texture errors: "<<m_textureErrors.front();
    if(!m_diffuseRecovery.empty())out<<" | Approximate diffuse-only recovery: "<<m_diffuseRecovery.size()<<" material(s), "<<m_diffuseRecoveryDraws<<" draws, UV0; source shader unsupported";
    if(!m_animationError.empty())out<<" | Animation: "<<m_animationError;
    if(!m_scene->Physics().visibilityWarnings.empty())out<<" | Cloth helper visibility: "<<m_scene->Physics().visibilityWarnings.front();
    if(!m_physicsError.empty())out<<" | Cloth: "<<m_physicsError;
    if(!m_rigidError.empty())out<<" | Ragdoll: "<<m_rigidError;
    if(!m_eventError.empty())out<<" | Event: "<<m_eventError;
    if(!m_actionError.empty())out<<" | Action policy: "<<m_actionError;
    if(!m_scene->Collision().error.empty())out<<" | Collision: "<<m_scene->Collision().error;
    if(!m_scene->Attachments().error.empty())out<<" | Attachments: "<<m_scene->Attachments().error;
    if(!m_scene->Effects().error.empty())out<<" | Effects: "<<m_scene->Effects().error;
    if(!m_scene->EffectMaterialError().empty())out<<" | Effect material: "<<m_scene->EffectMaterialError();
    if(!m_particleError.empty())out<<" | Particle motion: "<<m_particleError;
    if(m_effectSequence>=0)out<<" | Particles: "<<EffectParticles()<<" alive / "<<EffectSpawned()<<" born / "<<EffectDropped()<<" capped / "<<m_particleDraws<<" preview / "<<m_worldParticleDraws<<" world draws";
    if(!m_particleDrawError.empty())out<<" | Particle surface: "<<m_particleDrawError;
    if(!m_surface.ErrorText().empty())out<<" | Surface: "<<m_surface.ErrorText();
    for(const auto& material:m_scene->Materials().materials)if(material.type==12) {
        if(!material.bufferDecodeError.empty())out<<" | MADD buffer: "<<material.bufferDecodeError;
        else if(!material.bufferSurface)out<<" | MADD surface: "<<material.bufferSurfaceError;
        else if(!material.bufferSurfaceError.empty())out<<" | MADD partial surface: "<<material.bufferSurfaceError;
        else out<<" | MADD named-input preview; ECSpec/HDR/world effects pending";
    }
    if(m_rigid)out<<" | Physics: "<<m_rigid->DynamicBodies()<<" dynamic / "<<(m_rigid->Bodies()-m_rigid->DynamicBodies())<<" animation anchors";
    out<<" | Layer shader draws "<<m_surface.Draws();
    if(m_sequence>=0)out<<" | Clip "<<m_sequence<<": "<<m_scene->Model().animations.at(m_sequence).name;
    if(!m_scene->AnimationSource().empty())out<<" | Bank: "<<m_scene->AnimationSource();
    out<<" | Full materials / effects pending";return out.str();
}
void GxScene::Draw(const C3Vector& eye,const C3Vector& center,float modelScale,bool lit){DrawGeometry(eye,center,modelScale,lit,nullptr);}
const std::vector<WorldDrawPacket>& GxScene::PrepareWorldPackets(bool mergeOpaque){
    m_worldValues=std::make_unique<MaterialEvaluator>(m_scene->MaterialAnimation(),m_frame.materialGroup,m_frame.sourceTime);
    if(m_effectSequence>=0)m_scene->ApplyEffectMaterialValues(*m_worldValues,uint32_t(m_effectSequence),m_effectElapsed);
    m_worldPasses=EvaluateMaterialPasses(m_scene->Materials(),*m_worldValues);m_worldPackets.clear();
    for(const auto& packet:PackMaterialPasses(m_worldPasses)){
        const auto& batch=m_scene->Materials().batches.at(packet.batch);
        if(batch.division==0&&!RegionVisible(batch.region))continue;
        const auto* region=Region(m_frame,batch.division,batch.region);if(!region||region->indices.empty())continue;
        WorldDrawPacket out;out.passes=packet;bool found=false;
        for(const auto& vertex:region->vertices){const auto p=vertex.position;
            if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))throw Error("Nonfinite M3 world packet bounds");
            if(!found){out.lower=out.upper=p;found=true;}else{out.lower.x=std::min(out.lower.x,p.x);out.lower.y=std::min(out.lower.y,p.y);out.lower.z=std::min(out.lower.z,p.z);out.upper.x=std::max(out.upper.x,p.x);out.upper.y=std::max(out.upper.y,p.y);out.upper.z=std::max(out.upper.z,p.z);}}
        if(found){
            if(mergeOpaque&&!out.passes.deferred&&!m_worldPackets.empty()&&!m_worldPackets.back().passes.deferred&&
               m_worldPackets.back().passes.first+m_worldPackets.back().passes.count==out.passes.first){
                auto& previous=m_worldPackets.back();previous.passes.count+=out.passes.count;
                previous.lower.x=std::min(previous.lower.x,out.lower.x);previous.lower.y=std::min(previous.lower.y,out.lower.y);previous.lower.z=std::min(previous.lower.z,out.lower.z);
                previous.upper.x=std::max(previous.upper.x,out.upper.x);previous.upper.y=std::max(previous.upper.y,out.upper.y);previous.upper.z=std::max(previous.upper.z,out.upper.z);
            }else m_worldPackets.push_back(out);
        }
    }
    AppendWorldParticlePackets();return m_worldPackets;
}
void GxScene::DrawCameraSpace(const GxGeometryTransform& geometry,bool lit,const SceneEnvironment* environment,int32_t packet,const Matrix* particleCamera){DrawGeometry(C3Vector(0,0,0),C3Vector(0,0,0),1,lit,&geometry,environment,packet,particleCamera);}
void GxScene::DrawGeometry(const C3Vector& eye,const C3Vector& center,float modelScale,bool lit,const GxGeometryTransform* geometry,const SceneEnvironment* environment,int32_t packet,const Matrix* particleCamera){
    GxDrawSourceScope sourceFamily(GxDrawSourceFamily::M3);
    auto* device=g_theGxDevicePtr;if(!device)return;
    // Composite proxies expand to source child passes. Unsupported leaf shader
    // types/channels remain diagnostic; buffer nodes are not guessed diffuse.
    device->RsPush();
    struct Restore { CGxDevice* device; ~Restore(){device->RsPop();} } restore{device};
    GxRsSet(GxRs_VertexShader,0);GxRsSet(GxRs_PixelShader,0);GxRsSet(GxRs_Lighting,0);GxRsSet(GxRs_Fog,0);GxRsSet(GxRs_DepthTest,1);
    GxRsSet(GxRs_DepthFunc,0); // engine LESSEQUAL: coplanar composite layers
    for(unsigned stage=1;stage<8;++stage){GxRsSet(static_cast<EGxRenderState>(GxRs_Texture0+stage),0);GxRsSet(static_cast<EGxRenderState>(GxRs_ColorOp0+stage),3);GxRsSet(static_cast<EGxRenderState>(GxRs_AlphaOp0+stage),3);}
    if(packet>=0&&size_t(packet)<m_worldPackets.size()&&m_worldPackets[packet].particleEmitter>=0){DrawParticleWorld(uint32_t(m_worldPackets[packet].particleEmitter),particleCamera,environment);return;}
    if(packet>=0&&size_t(packet)<m_worldPackets.size()&&m_worldPackets[packet].ribbon>=0){DrawRibbonWorld(uint32_t(m_worldPackets[packet].ribbon),particleCamera,environment);return;}
    std::unique_ptr<MaterialEvaluator> previewValues;
    std::vector<MaterialPass> previewPasses;
    if(packet<0){previewValues=std::make_unique<MaterialEvaluator>(m_scene->MaterialAnimation(),m_frame.materialGroup,m_frame.sourceTime);if(m_effectSequence>=0)m_scene->ApplyEffectMaterialValues(*previewValues,uint32_t(m_effectSequence),m_effectElapsed);previewPasses=EvaluateMaterialPasses(m_scene->Materials(),*previewValues);}
    else if(!m_worldValues||size_t(packet)>=m_worldPackets.size())throw Error("M3 world draw packet is stale or invalid");
    const auto& values=packet<0?*previewValues:*m_worldValues;
    const auto& passes=packet<0?previewPasses:m_worldPasses;
    const uint32_t first=packet<0?0:m_worldPackets[packet].passes.first;
    const uint32_t count=packet<0?uint32_t(passes.size()):m_worldPackets[packet].passes.count;
    m_materialPasses=0;
    for(uint32_t pi=first;pi<first+count;++pi){const auto& pass=passes.at(pi);
        const auto* batch=&m_scene->Materials().batches.at(pass.batch);
        if(batch->division==0&&!RegionVisible(batch->region))continue;
        const auto* region=Region(m_frame,batch->division,batch->region);if(!region||region->indices.empty())continue;
        ++m_materialPasses;
        const auto& sourceMaterial=m_scene->Materials().materials.at(pass.material);
        const auto textureOf=[&](const std::string& path)->GxDDS*{const auto found=m_textures.find(Key(path));return found==m_textures.end()?nullptr:found->second.get();};
        // Distortion (DIS_) bends the scene behind the mesh; it has its own shader and never takes the layered path.
        if(sourceMaterial.type==2&&!sourceMaterial.bufferSurface){
            const auto before=m_surface.Draws();
            if(m_surface.DrawDistortion(*region,sourceMaterial,pass,values,eye,center,modelScale,textureOf,geometry,environment))m_drawCalls+=m_surface.Draws()-before;
            else LogMaterial("distortion not drawn: "+sourceMaterial.name+" error="+m_surface.ErrorText());
            continue;
        }
        // A material with no texture draws as a flat black/magenta plane.
        if(sourceMaterial.type==1&&!sourceMaterial.bufferSurface){
            bool textured=false;for(const auto& layer:sourceMaterial.layers)if(!layer.image.empty())textured=true;
            if(!textured)continue;
        }
        const auto recovery=m_diffuseRecovery.find(pass.material);
        // An undecoded MADD shader with no recoverable texture set is an effect layer (glows, shells, beams); drawn as a
        // neutral opaque sheet it hides the body, so leave it out.
        if(sourceMaterial.type==12&&!sourceMaterial.bufferSurface&&recovery==m_diffuseRecovery.end())continue;
        const auto& material=sourceMaterial.bufferSurface?*sourceMaterial.bufferSurface:recovery!=m_diffuseRecovery.end()?recovery->second:sourceMaterial;
        const auto before=m_surface.Draws(),beforeTextured=m_surface.TexturedDraws();
        // DiffuseRecovery deliberately replaces an opaque MADD shader with a
        // single authored _Diff.dds layer. Keep that recovery on the basic Gx
        // textured path below instead of sending the synthetic layer through
        // the full standard-surface shader, whose semantics the source MADD
        // did not provide.
        if(recovery==m_diffuseRecovery.end()&&m_surface.Draw(*region,material,pass,values,eye,center,modelScale,lit,[&](const std::string& path)->GxDDS*{const auto found=m_textures.find(Key(path));return found==m_textures.end()?nullptr:found->second.get();},geometry,environment)){
            const auto count=m_surface.Draws()-before;m_drawCalls+=count;m_texturedDrawCalls+=m_surface.TexturedDraws()-beforeTextured;
            if(sourceMaterial.bufferSurface)m_bufferShaderDraws+=count;
            else if(recovery!=m_diffuseRecovery.end())m_diffuseRecoveryDraws+=count;
            if(pass.composite)m_compositeDrawCalls+=count;
            continue;
        }
        if(recovery==m_diffuseRecovery.end())LogMaterial("layered path fell back: "+sourceMaterial.name+" type="+std::to_string(sourceMaterial.type)+" blend="+std::to_string(pass.blend)+" error="+m_surface.ErrorText());
        // The fallback ignores the alpha layers; a mask scaled to zero (an effect that is switched off) hides the whole material.
        bool maskedOut=false;for(const auto& layer:material.layers)if((layer.role=="layer_alpha1"||layer.role=="layer_alpha2")&&!layer.image.empty()&&values.Scalar(layer.record,"color_multiply",1)<=0)maskedOut=true;
        if(maskedOut)continue;
        const auto* diffuse=PrimaryLayer(material);
        const uint32_t flags=uint32_t(Value(material.record,"flags",0));
        const uint32_t layerFlags=diffuse?uint32_t(Value(diffuse->record,"flags",0)):0;
        const uint32_t channels=diffuse?uint32_t(Value(diffuse->record,"color_channels",0)):0;
        const int channel=diffuse?UVChannel(uint32_t(Value(diffuse->record,"uv_source",0))):0;
        bool diagnostic=material.type!=1||channel<0||channels>1;
        CGxTex* native=nullptr;
        if(!diagnostic&&diffuse&&!(layerFlags&0x400)&&!diffuse->image.empty()){
            const auto found=m_textures.find(Key(diffuse->image));
            if(found==m_textures.end())diagnostic=true;
            else if(found->second->Image().faces!=1)diagnostic=true;
            else {found->second->SetWrap((layerFlags&4)!=0,(layerFlags&8)!=0);native=found->second->Get();}
        }
        const bool fadeFromOpaque=!pass.blend&&environment&&environment->opacity<.99999f;
        const uint32_t sourceBlend=fadeFromOpaque?1:pass.blend;
        const int blends[]={GxBlend_Opaque,GxBlend_Alpha,GxBlend_NoAlphaAdd,GxBlend_Add,GxBlend_Mod,GxBlend_Mod2x};
        if(sourceBlend>=6)diagnostic=true;
        int renderBlend=sourceBlend<6?blends[sourceBlend]:GxBlend_Opaque;
        const auto threshold=uint32_t(Value(material.record,"alpha_test_threshold",0));
        if(!sourceBlend&&threshold)renderBlend=GxBlend_AlphaKey;
        GxRsSet(GxRs_BlendingMode,renderBlend);GxRsSet(GxRs_AlphaRef,int32_t(std::min(255u,threshold)));
        GxRsSet(GxRs_DepthWrite,sourceBlend==0||fadeFromOpaque?1:0);GxRsSet(GxRs_Culling,flags&8?0:(geometry&&geometry->CullMirrored()?2:1));
        GxRsSet(GxRs_Texture0,diagnostic?nullptr:native);GxRsSet(GxRs_TextureCoord0,0);
        GxRsSet(GxRs_ColorOp0,0);GxRsSet(GxRs_AlphaOp0,channels==1?0:3);
        float color[4]={1,1,1,1};
        if(diffuse&&(layerFlags&0x400)){const auto sampled=values.Sample(diffuse->record,"color_value",4,{255,255,255,255});for(unsigned i=0;i<4;++i)color[i]=sampled[i]/255.0f;}
        // The basic vertex-color fallback bypasses GxSurface, so propagate the
        // native unit fade here as well. Without it, M3 materials recovered
        // from MADD stay opaque while the camera moves into the player model.
        color[3]*=pass.opacity*(environment?std::clamp(environment->opacity,0.0f,1.0f):1.0f);
        const float multiply=diffuse?values.Scalar(diffuse->record,"color_multiply",1):1;
        const float add=diffuse?values.Scalar(diffuse->record,"color_add",0):0;
        const float brightness=diffuse?values.Scalar(diffuse->record,"color_brightness",1):1;
        const auto tiling=diffuse?values.Sample(diffuse->record,"uv_tiling",1,{1,1,1,0}):KeyValue{1,1,1,0};
        const auto offset=diffuse?values.Sample(diffuse->record,"uv_offset",1,{}):KeyValue{};
        const auto angles=diffuse?values.Sample(diffuse->record,"uv_angle",2,{}):KeyValue{};
        const float tileU=tiling[0],tileV=tiling[1],offsetU=offset[0],offsetV=offset[1],angle=angles[2],cs=std::cos(angle),sn=std::sin(angle);
        for(size_t start=0;start<region->indices.size();start+=60000){
            const uint32_t count=uint32_t(std::min(size_t(60000),region->indices.size()-start));
            auto* vb=device->BufStream(GxPoolTarget_Vertex,sizeof(CGxVertexPCT),count);auto* ib=device->BufStream(GxPoolTarget_Index,sizeof(uint16_t),count);
            // Staged in cached memory; the stream buffers are write-combined and slow to read back.
            std::vector<CGxVertexPCT> stagedVertices(count);std::vector<uint16_t> stagedIndices(count);
            auto* vertices=stagedVertices.data();auto* indices=stagedIndices.data();
            for(uint32_t i=0;i<count;++i){
                const auto& source=region->vertices.at(region->indices[start+i]);auto& v=vertices[i];v.p=(Point(source.position)-center)*modelScale-eye;if(geometry)v.p=geometry->Position(Point(source.position));
                const auto normal=geometry?geometry->Normal(Point(source.normal)):Point(source.normal);
                const float shade=lit&&!(flags&16)?0.3f+0.7f*std::fabs(normal.x*0.35f+normal.y*0.45f+normal.z*0.82f):1;
                float rgb[3];for(unsigned c=0;c<3;++c)rgb[c]=(color[c]*multiply+add)*brightness*shade;
                // Magenta remains useful for unsupported ordinary materials,
                // but it makes newer MADD surfaces look like texture failures.
                // Keep their geometry visible with neutral vertex-lit shading.
                if(diagnostic&&sourceMaterial.type!=12){rgb[0]=1;rgb[1]=0.05f;rgb[2]=0.6f;}
                v.c.r=uint8_t(std::clamp(rgb[0],0.0f,1.0f)*255);v.c.g=uint8_t(std::clamp(rgb[1],0.0f,1.0f)*255);v.c.b=uint8_t(std::clamp(rgb[2],0.0f,1.0f)*255);v.c.a=uint8_t(std::clamp(color[3],0.0f,1.0f)*255);GxFormatColor(v.c);
                const size_t uv=channel<0?0:size_t(channel)*2;float u=0,w=0;if(uv+1<source.uv.size()){u=source.uv[uv]*tileU;w=source.uv[uv+1]*tileV;}v.tc.x=cs*u-sn*w+offsetU;v.tc.y=sn*u+cs*w+offsetV;indices[i]=uint16_t(i);
            }
            std::memcpy(device->BufLock(vb),stagedVertices.data(),size_t(count)*sizeof(CGxVertexPCT));std::memcpy(device->BufLock(ib),stagedIndices.data(),size_t(count)*sizeof(uint16_t));
            GxBufUnlock(ib,0);GxBufUnlock(vb,0);GxPrimVertexPtr(vb,GxVBF_PCT);device->PrimIndexPtr(ib);CGxBatch draw;draw.m_primType=GxPrim_Triangles;draw.m_start=0;draw.m_count=count;draw.m_minIndex=0;draw.m_maxIndex=count-1;device->Draw(&draw,1);++m_drawCalls;if(native&&!diagnostic)++m_texturedDrawCalls;if(pass.composite)++m_compositeDrawCalls;
        }
    }
    if(!geometry&&packet<0&&!m_particles.empty())DrawParticlePreview(eye,center,modelScale);
}
}
