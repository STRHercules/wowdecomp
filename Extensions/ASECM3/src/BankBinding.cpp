#include "ASECM3/BankBinding.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <unordered_set>
namespace ASECM3 {
namespace {
uint64_t Channel(const AnimatedValue& ref,uint32_t type){return (uint64_t(type)<<32)|ref.id;}
std::vector<uint64_t> Channels(const Bone& b){return {Channel(b.translation,2),Channel(b.rotation,3),Channel(b.scale,2)};}
Matrix Iref(const Bone& b){Matrix m;for(unsigned i=0;i<16;++i)m.values[i]=b.inverseBind[i];return m;}
}
BankBinding BindAnimationBank(const ModelData& mesh,const ModelData& bank,const AnimationBank& animation,bool meshLocalTracks){
    if(bank.animations.empty()||animation.groups.empty()||animation.collections.empty())throw Error("M3A has no complete source clip bank");
    std::unordered_map<std::string,uint32_t> names;
    std::unordered_set<std::string> meshNames;
    std::unordered_set<uint64_t> animated;
    for(const auto& collection:animation.collections)for(const auto& track:collection.transforms)animated.insert(track.first);
    std::unordered_map<uint64_t,std::string> owners;
    for(uint32_t i=0;i<bank.skeleton.size();++i){const auto& bone=bank.skeleton[i];if(!names.emplace(bone.name,i).second)throw Error("M3A has duplicate bone names");for(auto id:Channels(bone))if(animated.count(id)&&!owners.emplace(id,bone.name).second&&owners[id]!=bone.name)throw Error("M3A has ambiguous typed channel ownership");}
    BankBinding out;out.meshLocalTracks=meshLocalTracks;out.meshToBank.assign(mesh.skeleton.size(),-1);
    const auto bankRest=meshLocalTracks?Pose{}:EvaluatePoseAtSourceTime(bank,animation);
    auto restStatic=[&](uint32_t index){
        const auto& bone=bank.skeleton[index];
        for(unsigned k=0;k<16;++k)if(std::fabs(bankRest.skin[index].values[k]-(k%5==0?1.0:0.0))>1e-4)return false;
        const AnimatedValue* refs[]={&bone.translation,&bone.rotation,&bone.scale};
        const uint32_t types[]={2,3,2};
        for(unsigned r=0;r<3;++r)for(const auto& collection:animation.collections){auto found=collection.transforms.find(Channel(*refs[r],types[r]));if(found==collection.transforms.end())continue;for(const auto& value:found->second.values){double same=0,opposite=0;for(unsigned c=0;c<(types[r]==3?4u:3u);++c){same+=std::fabs(value[c]-refs[r]->value[c]);opposite+=std::fabs(value[c]+refs[r]->value[c]);}if(same>1e-5&&(types[r]!=3||opposite>1e-5))return false;}}
        return true;
    };
    for(uint32_t i=0;i<mesh.skeleton.size();++i){const auto& bone=mesh.skeleton[i];if(!meshNames.insert(bone.name).second)throw Error("Mesh has duplicate bone names");auto found=names.find(bone.name);if(found==names.end())continue;const auto& other=bank.skeleton[found->second];if(Channels(bone)!=Channels(other))throw Error("M3A bone channel identity mismatch: "+bone.name);out.meshToBank[i]=found->second;}
    // Banks can omit static scene/container nodes. Compare the nearest shared
    // ancestor, and omit unmatched static containers from the binding itself.
    bool changed;
    if(!meshLocalTracks)do {
        changed=false;std::unordered_set<std::string> shared;
        for(size_t i=0;i<out.meshToBank.size();++i)if(out.meshToBank[i]>=0)shared.insert(mesh.skeleton[i].name);
        auto parent=[&](const ModelData& model,int32_t p){while(p>=0){const auto& b=model.skeleton.at(p);if(shared.count(b.name))return b.name;p=b.parent;}return std::string();};
        for(size_t i=0;i<out.meshToBank.size();++i)if(out.meshToBank[i]>=0){const auto& a=mesh.skeleton[i];const auto& b=bank.skeleton[out.meshToBank[i]];if(parent(mesh,a.parent)!=parent(bank,b.parent)){if(!restStatic(uint32_t(out.meshToBank[i])))throw Error("M3A shared bone topology mismatch: "+a.name);out.meshToBank[i]=-1;changed=true;}}
    }while(changed);
    for(size_t i=0;i<out.meshToBank.size();++i)if(out.meshToBank[i]>=0){++out.matchedBones;bool moves=false;for(auto id:Channels(mesh.skeleton[i])){moves|=animated.count(id)!=0;auto owner=owners.find(id);if(owner!=owners.end()&&owner->second!=mesh.skeleton[i].name)throw Error("M3A channel belongs to another bone");}if(moves)++out.animatedBones;}
    if(out.animatedBones<(meshLocalTracks?1u:3u))throw Error("M3A lacks verified animated mesh bones");
    std::vector<int32_t> ancestor(mesh.skeleton.size(),-1);
    for(size_t i=0;i<ancestor.size();++i){int32_t p=static_cast<int32_t>(i);while(p>=0&&out.meshToBank[p]<0)p=mesh.skeleton[p].parent;ancestor[i]=p;}
    uint64_t total=0,covered=0,direct=0;
    for(const auto& div:mesh.divisions)for(const auto& region:div.regions)for(size_t i=region.firstVertex;i<size_t(region.firstVertex)+region.vertexCount;++i){const auto& v=mesh.vertices[i];for(unsigned j=0;j<4;++j)if(v.weights[j]){const auto bone=mesh.bonePalette.at(region.firstBoneLookup+v.boneLookup[j]);total+=v.weights[j];if(ancestor.at(bone)>=0)covered+=v.weights[j];if(out.meshToBank.at(bone)>=0)direct+=v.weights[j];}}
    out.influencedWeightCoverage=total?double(covered)/total:0;
    if(out.influencedWeightCoverage<0.90)throw Error("M3A does not cover 90 percent of mesh skin weights through verified ancestors");
    uint32_t bankAnimated=0;for(const auto& bone:bank.skeleton){bool used=false;for(auto id:Channels(bone))used|=animated.count(id)!=0;if(used)++bankAnimated;}
    if(!meshLocalTracks&&(!bankAnimated||double(out.animatedBones)/bankAnimated<0.75)&&(!total||double(direct)/total<0.90))throw Error("M3A lacks sufficient direct armature or source-bank coverage");
    return out;
}
Pose EvaluateBoundPose(const ModelData& mesh,const ModelData& bank,const AnimationBank& animation,const BankBinding& binding,int32_t group,int32_t time){
    if(binding.meshToBank.size()!=mesh.skeleton.size())throw Error("M3A binding does not match mesh skeleton");
    if(binding.meshLocalTracks)return EvaluatePoseAtSourceTime(mesh,animation,group,time);
    const auto source=EvaluatePoseAtSourceTime(bank,animation,group,time);
    return BindEvaluatedPose(mesh,binding,source);
}
Pose BindEvaluatedPose(const ModelData& mesh,const BankBinding& binding,const Pose& source){
    if(binding.meshToBank.size()!=mesh.skeleton.size())throw Error("M3A binding does not match mesh skeleton");
    Pose out;out.skin.resize(mesh.skeleton.size());out.world.resize(mesh.skeleton.size());out.normal.resize(mesh.skeleton.size());
    for(size_t i=0;i<mesh.skeleton.size();++i){int32_t p=static_cast<int32_t>(i);while(p>=0&&binding.meshToBank[p]<0)p=mesh.skeleton[p].parent;const auto deform=p<0?Matrix::Identity():source.skin.at(binding.meshToBank[p]);out.skin[i]=deform;out.world[i]=Multiply(deform,InverseAffine(Iref(mesh.skeleton[i])));try{const auto inverse=InverseAffine(deform);for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)out.normal[i].At(r,c)=inverse.At(c,r);}catch(const Error&){/* zero-scale source bones intentionally hide geometry */}}
    return out;
}
std::vector<std::string> AnimationBankCandidates(const std::string& modelPath){
    std::string path=modelPath;std::replace(path.begin(),path.end(),'/','\\');
    const auto slash=path.find_last_of('\\'),dot=path.find_last_of('.');
    if(dot==std::string::npos)return {};
    const std::string folder=slash==std::string::npos?"":path.substr(0,slash+1);
    std::string container=folder;if(!container.empty())container.pop_back();const auto parentSlash=container.find_last_of('\\');const std::string parent=parentSlash==std::string::npos?"":container.substr(0,parentSlash+1);
    std::string stem=path.substr(slash==std::string::npos?0:slash+1,dot-(slash==std::string::npos?0:slash+1));
    std::vector<std::string> out;std::unordered_set<std::string> seen;
    auto add=[&](std::string name){auto key=name;for(auto& c:key)c=char(std::tolower(static_cast<unsigned char>(c)));if(seen.insert(key).second)out.push_back(std::move(name));};
    while(!stem.empty()){
        const auto bank=stem+"_requiredanims";
        add(folder+bank+".m3a");add(parent+bank+"\\"+bank+".m3a");add(parent+bank+".m3a");
        const auto separator=stem.find_last_of('_');if(separator==std::string::npos)break;stem.resize(separator);
    }
    return out;
}
}
