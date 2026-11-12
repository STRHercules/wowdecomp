#include "ASECM3/Scene.h"
#include "ASECM3/MaterialEvaluation.h"
#include <algorithm>
#include <cctype>
#include <limits>
#include <unordered_set>
namespace ASECM3 {
namespace {
std::string Key(std::string name) {
    for(auto& c:name)c=c=='/'?'\\':char(std::tolower(static_cast<unsigned char>(c)));
    return name;
}
}
Scene::Scene(Document document,bool embeddedEffects):m_document(std::move(document)) {
    m_model=DecodeModel(m_document);
    m_materials=DecodeMaterials(m_document,m_model);
    m_animation=DecodeAnimationBank(m_document);
    try { m_physics=DecodePhysics(m_document,m_model); }
    catch(const Error& e){m_physics=PhysicsData{};m_physics.error=e.what();}
    try { m_collision=DecodeCollision(m_document,m_model); }
    catch(const Error& e){m_collision=CollisionData{};m_collision.error=e.what();}
    try { m_attachments=DecodeAttachments(m_document); }
    catch(const Error& e){m_attachments=AttachmentData{};m_attachments.error=e.what();}
    try { m_effects=DecodeEffects(m_document,m_model); }
    catch(const Error& e){m_effects=EffectsData{};m_effects.error=e.what();}
    std::unordered_set<std::string> textureKeys;
    auto add=[&](const std::string& name) {
        if(!name.empty()&&textureKeys.insert(Key(name)).second)m_textures.push_back(name);
    };
    for(const auto& material:m_materials.materials) {
        for(const auto& layer:material.layers)add(layer.image);
        for(const auto& name:material.bufferTextures)add(name);
    }
    // Exact source names only. A duplicate is ambiguous, even if it occurs at
    // the same numeric slot. No HotS token enum or WoW ID is inferred here.
    for(const auto& sequence:m_model.animations) {
        int32_t match=-1;
        for(size_t i=0;i<m_animation.groups.size();++i)if(m_animation.groups[i].name==sequence.name) {
            if(match>=0){match=-2;break;}match=static_cast<int32_t>(i);
        }
        m_sequenceGroups.push_back(match);
    }
    m_localSequences=m_model.animations;m_localSequenceGroups=m_sequenceGroups;
    if(embeddedEffects)try{
        if(!m_effects.error.empty())throw Error(m_effects.error);
        if((!m_effects.particles.empty()||!m_effects.ribbons.empty())&&!m_localSequences.empty()){
            if(m_effects.particles.size()>4096)throw Error("Embedded effects exceed 4096 particle owners");
            bool playable=false;for(auto group:m_localSequenceGroups)playable|=group>=0;
            if(!playable)throw Error("Embedded effects have no uniquely associated playable source clips");
            // These records and the animation IDs come from one Document.
            // No cross-file name/signature matching or bone remap is needed.
            for(uint32_t i=0;i<m_effects.particles.size();++i){
                const auto bone=m_effects.particles[i].bone;
                if(bone>=m_model.skeleton.size())throw Error("Embedded particle owner bone exceeds skeleton");
                m_effectBinding.particles.push_back({i,i,bone,bone});
            }
            m_embeddedEffects=true;m_effectAnimationSource="<embedded>";
            try{m_effectMaterials=BindEmbeddedEffectMaterials(m_materials,m_animation);}
            catch(const Error& e){m_effectMaterials={};m_effectMaterialError=e.what();}
        }
    }catch(const Error& e){m_effectSetupError=e.what();m_embeddedEffects=false;m_effectBinding={};m_effectAnimationSource.clear();}
}
std::shared_ptr<const Scene> Scene::Parse(std::vector<uint8_t> bytes,bool embeddedEffects) {
    return std::shared_ptr<const Scene>(new Scene(Document::Parse(std::move(bytes)),embeddedEffects));
}
std::shared_ptr<const Scene> Scene::FromDocument(Document document,bool embeddedEffects){
    document.ValidateReferences();return std::shared_ptr<const Scene>(new Scene(std::move(document),embeddedEffects));
}
std::shared_ptr<const Scene> Scene::WithAnimationBank(std::vector<uint8_t> bytes,const std::string& source,bool meshLocalTracks) const {
    auto bank=Parse(std::move(bytes));
    auto binding=BindAnimationBank(m_model,bank->m_model,bank->m_animation,meshLocalTracks);
    auto out=std::shared_ptr<Scene>(new Scene(*this));
    out->m_externalBank=std::move(bank);out->m_binding=std::move(binding);out->m_animationSource=source;
    if(meshLocalTracks)for(size_t i=0;i<out->m_model.skeleton.size();++i){
        const auto index=out->m_binding.meshToBank[i];if(index<0)continue;
        auto& target=out->m_model.skeleton[i];const auto& authored=out->m_externalBank->m_model.skeleton.at(size_t(index));
        target.translation.interpolation=authored.translation.interpolation;
        target.rotation.interpolation=authored.rotation.interpolation;
        target.scale.interpolation=authored.scale.interpolation;
    }
    out->m_model.animations=out->m_externalBank->m_model.animations;
    out->m_sequenceGroups=out->m_externalBank->m_sequenceGroups;
    bool playable=false;for(auto group:out->m_sequenceGroups)playable|=group>=0;
    if(!playable)throw Error("M3A has no uniquely associated playable source clips");
    return out;
}
int32_t Scene::SequenceGroup(uint32_t sequence) const {
    if(sequence>=m_sequenceGroups.size())throw Error("M3 source sequence index is unavailable");
    return m_sequenceGroups[sequence];
}
int32_t Scene::EffectSequenceGroup(uint32_t sequence)const{
    if(m_effectBank)return m_effectBank->SequenceGroup(sequence);
    if(!m_embeddedEffects)throw Error("No M3 effect animation source");
    if(sequence>=m_localSequenceGroups.size())throw Error("Embedded M3 effect clip is unavailable");
    return m_localSequenceGroups[sequence];
}
std::shared_ptr<const Scene> Scene::WithEffectAnimationBank(std::vector<uint8_t> bytes,const std::string& source)const{
    auto bank=Parse(std::move(bytes));
    auto binding=BindEffectBank(m_model,m_effects,bank->m_model,bank->m_effects,bank->m_animation);
    bool playable=false;for(auto group:bank->m_sequenceGroups)playable|=group>=0;
    if(!playable)throw Error("M3 effect dependency has no uniquely associated playable clips");
    auto out=std::shared_ptr<Scene>(new Scene(*this));out->m_effectBank=std::move(bank);
    out->m_embeddedEffects=false;out->m_effectSetupError.clear();
    out->m_effectBinding=std::move(binding);out->m_effectAnimationSource=source;
    try{out->m_effectMaterials=BindEffectMaterials(out->m_materials,out->m_effectBank->m_materials,out->m_effectBank->m_animation);out->m_effectMaterialError.clear();}
    catch(const Error& e){out->m_effectMaterials={};out->m_effectMaterialError=e.what();}
    return out;
}
void Scene::ApplyEffectMaterialValues(MaterialEvaluator& values,uint32_t sequence,uint64_t elapsed,uint32_t material,const Material* adapted)const{
    const auto* animation=EffectAnimation();if(!animation)throw Error("No M3 effect material animation source");
    if(!m_effectMaterialError.empty())return; // isolated failure preserves mesh and particle lane
    if(sequence>=EffectSequences().size())throw Error("M3 effect material clip is unavailable");
    if(adapted&&(material>=m_materials.materials.size()||adapted->type!=m_materials.materials[material].type||adapted->name!=m_materials.materials[material].name))throw Error("Adapted effect material owner changed");
    const auto& clip=EffectSequences()[sequence];
    if(clip.end<clip.start||clip.end>uint32_t(INT32_MAX))throw Error("M3 effect material timeline is invalid");
    const uint64_t duration=uint64_t(clip.end)-clip.start;
    const uint64_t offset=clip.flags&1?std::min(elapsed,duration):duration?elapsed%duration:0;
    const int32_t time=int32_t(clip.start+offset);const auto group=EffectSequenceGroup(sequence);
    MaterialEvaluator source(*animation,group,time);
    for(const auto& link:m_effectMaterials.records){
        if(adapted&&link.mesh.material!=material)continue;
        if(adapted&&link.mesh.kind==MaterialRecordKind::Composite){const auto& original=m_materials.materials[material];if(link.mesh.index>=adapted->composite.size()||adapted->composite[link.mesh.index].materialReference!=original.composite.at(link.mesh.index).materialReference)throw Error("Adapted composite ownership changed");}
        const auto& owner=adapted?LocateMaterialRecord(*adapted,link.mesh):LocateMaterialRecord(m_materials,link.mesh);
        for(const auto& field:link.fields)if(const auto* track=source.Track(field.type,field.id))values.AddOverride(owner,field.name,*track,time);
    }
}
float Scene::SampleEffectScalar(uint32_t emitter,const std::string& field,int32_t group,int32_t time)const{
    const auto* animation=EffectAnimation();if(!animation)throw Error("No M3 effect animation source");
    bool mapped=false;for(const auto& link:m_effectBinding.particles)if(link.meshEmitter==emitter)mapped=true;
    if(!mapped||emitter>=m_effects.particles.size())throw Error("M3 effect parameter owner is not bound");
    const auto& record=m_effects.particles[emitter].record;
    const auto* initial=record.Find(field+".default");
    if(!initial||initial->kind!="float"||!record.Find(field+".header.id"))throw Error("M3 effect parameter is not an animated scalar");
    return MaterialEvaluator(*animation,group,time).Scalar(record,field,0);
}
const TransformTrack* Scene::EffectScalarTrack(uint32_t emitter,const std::string& field,int32_t group)const{
    const auto* animation=EffectAnimation();if(!animation)throw Error("No M3 effect animation source");
    bool mapped=false;for(const auto& link:m_effectBinding.particles)if(link.meshEmitter==emitter)mapped=true;
    if(!mapped||emitter>=m_effects.particles.size())throw Error("M3 effect parameter owner is not bound");
    const auto& record=m_effects.particles[emitter].record;
    const auto* initial=record.Find(field+".default");const auto* id=record.Find(field+".header.id");
    const auto* value=id?std::get_if<uint64_t>(&id->value):nullptr;
    if(!initial||initial->kind!="float"||!id||id->kind!="uint32"||!value||*value>UINT32_MAX)throw Error("M3 effect parameter is not an animated scalar");
    const auto& bank=*animation;
    if(group<-1||(group>=0&&size_t(group)>=bank.groups.size()))throw Error("M3 effect animation group is unavailable");
    if(group<0)return nullptr;
    const TransformTrack* selected=nullptr;uint32_t priority=0;
    for(auto index:bank.groups[group].collections){
        if(index>=bank.collections.size())throw Error("M3 effect animation collection is unavailable");
        const auto& collection=bank.collections[index];const auto found=collection.materialValues.find((uint64_t(5)<<32)|uint32_t(*value));
        if(found!=collection.materialValues.end()&&(!selected||collection.priority>=priority)){selected=&found->second;priority=collection.priority;}
    }
    return selected;
}
const Signed16Track* Scene::EffectCountTrack(uint32_t emitter,const std::string& field,int32_t group)const{
    const auto* animation=EffectAnimation();if(!animation)throw Error("No M3 effect animation source");
    bool mapped=false;for(const auto& link:m_effectBinding.particles)if(link.meshEmitter==emitter)mapped=true;
    if(!mapped||emitter>=m_effects.particles.size())throw Error("M3 effect parameter owner is not bound");
    const auto& record=m_effects.particles[emitter].record;
    const auto* initial=record.Find(field+".default");const auto* id=record.Find(field+".header.id");
    const auto* value=id?std::get_if<uint64_t>(&id->value):nullptr;
    if(!initial||initial->kind!="int16"||!id||id->kind!="uint32"||!value||*value>UINT32_MAX)throw Error("M3 effect parameter is not an animated signed count");
    const auto& bank=*animation;
    if(group<-1||(group>=0&&size_t(group)>=bank.groups.size()))throw Error("M3 effect animation group is unavailable");
    if(group<0)return nullptr;
    const Signed16Track* selected=nullptr;uint32_t priority=0;
    for(auto index:bank.groups[group].collections){
        if(index>=bank.collections.size())throw Error("M3 effect animation collection is unavailable");
        const auto& collection=bank.collections[index];const auto found=collection.signed16Values.find(uint32_t(*value));
        if(found!=collection.signed16Values.end()&&(!selected||collection.priority>=priority)){selected=&found->second;priority=collection.priority;}
    }
    return selected;
}
SceneFrame Scene::EvaluateGroup(int32_t group,int32_t sourceTime,uint32_t division,const std::vector<LocalTransform>* blendFrom,double blend) const {
    SceneFrame out;
    // Bone-bank compatibility does not establish material-ID compatibility.
    // External skeletal banks never animate a mesh material by ID coincidence.
    out.materialGroup=m_externalBank?-1:group;out.sourceTime=sourceTime;
    const bool bound=bool(m_externalBank)&&group>=0;
    const auto& animationModel=bound&&!m_binding.meshLocalTracks?m_externalBank->m_model:m_model;
    const auto& animationBank=bound?m_externalBank->m_animation:m_animation;
    auto source=EvaluatePoseAtSourceTime(animationModel,animationBank,group,sourceTime);
    if(blendFrom)source=BlendLocalPoses(animationModel,*blendFrom,std::move(source),blend);
    out.animationLocals=std::move(source.local);
    out.pose=bound&&!m_binding.meshLocalTracks?BindEvaluatedPose(m_model,m_binding,source):std::move(source);
    if(m_model.divisions.empty())return out;
    if(division>=m_model.divisions.size())throw Error("M3 scene division is unavailable");
    const auto skinned=SkinModel(m_model,out.pose);
    for(const auto& skin:skinned) {
        if(skin.division!=division)continue;
        const auto& source=m_model.divisions[division].regions.at(skin.region);
        SceneRegion region;region.division=division;region.region=skin.region;
        region.vertices.reserve(skin.positions.size());
        for(size_t i=0;i<skin.positions.size();++i) {
            SceneVertex vertex;vertex.position=skin.positions[i];vertex.normal=skin.normals[i];
            vertex.tangent=skin.tangents[i];vertex.tangentSign=skin.tangentSigns[i];
            std::copy_n(m_model.vertices.at(source.firstVertex+i).color,4,vertex.color);
            vertex.uv=m_model.vertices.at(source.firstVertex+i).uv;
            for(auto& value:vertex.uv)value=value*source.uvMultiply+source.uvOffset;
            region.vertices.push_back(std::move(vertex));
        }
        const auto& indices=m_model.divisions[division].indices;
        region.indices.assign(indices.begin()+source.firstIndex,indices.begin()+source.firstIndex+source.indexCount);
        out.regions.push_back(std::move(region));
    }
    return out;
}
SceneFrame Scene::EvaluateSequence(uint32_t sequence,uint64_t elapsedMs,uint32_t division,const std::vector<LocalTransform>* blendFrom,double blend) const {
    const auto group=SequenceGroup(sequence);
    if(group==-2)throw Error("M3 source sequence has ambiguous transformation groups");
    if(group<0)throw Error("M3 source sequence has no exact transformation group in this document");
    const auto& clip=m_model.animations.at(sequence);
    const uint64_t duration=uint64_t(clip.end)-clip.start;
    const uint64_t offset=clip.flags&1 ? std::min(elapsedMs,duration) : (duration?elapsedMs%duration:0);
    const uint64_t time=uint64_t(clip.start)+offset;
    if(time>uint64_t(std::numeric_limits<int32_t>::max()))throw Error("M3 sequence timeline exceeds signed source time range");
    return EvaluateGroup(group,static_cast<int32_t>(time),division,blendFrom,blend);
}
}
