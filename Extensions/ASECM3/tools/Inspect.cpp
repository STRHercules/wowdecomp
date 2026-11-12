#include "ASECM3/Document.h"
#include "ASECM3/Animation.h"
#include "ASECM3/Effects.h"
#include "ASECM3/Scene.h"
#include "ASECM3/EffectDependencies.h"
#include "ASECM3/EmissionSurface.h"
#include "ASECM3/EmissionClock.h"
#include "ASECM3/ParticleEmitter.h"
#include "ASECM3/ParticleBillboards.h"
#include "ASECM3/ParticleSpace.h"
#include "ASECM3/MaterialEvaluation.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <iomanip>
#include <cctype>
static std::string Quote(const std::string& text) {
    std::string out="\""; const char* hex="0123456789abcdef";
    for (unsigned char c:text) {
        if (c=='"' || c=='\\') { out+='\\'; out+=char(c); }
        else if (c<32) { out+="\\u00"; out+=hex[c>>4]; out+=hex[c&15]; }
        else out+=char(c);
    }
    return out+'"';
}
int main(int argc,char** argv) {
    const bool effectDeclarationMode=argc==4&&std::string(argv[2])=="--effect-declaration";
    const bool worldParticleMode=argc==4&&std::string(argv[2])=="--world-particles";
    const bool particleMode=argc==4&&(std::string(argv[2])=="--effect-particles"||worldParticleMode);
    const bool effectClockMode=argc==4&&std::string(argv[2])=="--effect-clock";
    const bool effectMaterialMode=argc==4&&std::string(argv[2])=="--effect-materials";
    const bool effectBankMode=argc==4&&(std::string(argv[2])=="--effect-bank"||effectDeclarationMode||effectClockMode||particleMode||effectMaterialMode);
    const bool signedDetail=argc==3&&std::string(argv[2])=="--signed-tracks";
    const bool surfaceDetail=argc==3&&std::string(argv[2])=="--emission-surfaces";
    if (argc!=2 && !effectBankMode && !signedDetail && !surfaceDetail && !(argc==3 && (std::string(argv[2])=="--geometry"||std::string(argv[2])=="--effects"))) { std::cerr<<"Usage: ASECM3Inspect <model.m3|bank.m3a|directory> [--geometry|--effects|--signed-tracks|--emission-surfaces|--effect-bank bank.m3a|--effect-clock bank.m3a|--effect-materials bank.m3a|--effect-particles bank.m3a|--world-particles bank.m3a|--effect-declaration model.m3.effectbank]\n"; return 2; }
    const bool geometry=argc==3&&std::string(argv[2])=="--geometry",effectsDetail=argc==3&&std::string(argv[2])=="--effects";
    std::cout<<std::setprecision(9);
    std::vector<std::filesystem::path> files; const std::filesystem::path input=argv[1];
    if (std::filesystem::is_directory(input)) {
        for (const auto& item:std::filesystem::recursive_directory_iterator(input)) if (item.is_regular_file()) {
            auto ext=item.path().extension().string(); std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return std::tolower(c);});
            if (ext==".m3" || ext==".m3a") files.push_back(item.path());
        }
    } else files.push_back(input);
    std::sort(files.begin(),files.end()); unsigned failed=0;
    std::cout<<"{\"files\":["; bool first=true;
    for (const auto& file:files) {
        if (!first) std::cout<<','; first=false; std::cout<<"{\"path\":"<<Quote(file.string());
        try {
            const auto length=std::filesystem::file_size(file); if (length>512ull*1024*1024) throw ASECM3::Error("Input exceeds size limit");
            std::ifstream stream(file,std::ios::binary); if (!stream) throw ASECM3::Error("Could not open input");
            std::vector<uint8_t> bytes(static_cast<size_t>(length));
            if (!stream.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(length))) throw ASECM3::Error("Short read");
            const auto doc=ASECM3::Document::Parse(std::move(bytes)); const auto model=ASECM3::DecodeModel(doc);
            std::cout<<",\"ok\":true,\"model\":"<<Quote(model.name)<<",\"mdVersion\":"<<doc.Version()
                <<",\"vertices\":"<<model.vertices.size()<<",\"bones\":"<<model.bones<<",\"materials\":"<<model.materials
                <<",\"particles\":"<<model.particles<<",\"ribbons\":"<<model.ribbons<<",\"sections\":"<<doc.Sections().size();
            const auto effects=ASECM3::DecodeEffects(doc,model);
            if(surfaceDetail){
                const auto scene=ASECM3::Scene::FromDocument(doc);const auto frame=scene->EvaluateGroup();
                std::cout<<",\"meshEmissionSurfaces\":[";bool comma=false;
                for(uint32_t ei=0;ei<effects.particles.size();++ei){
                    const auto& emitter=effects.particles[ei];const auto* shape=emitter.record.Find("emit_shape");
                    if(!shape||!std::holds_alternative<uint64_t>(shape->value)||std::get<uint64_t>(shape->value)!=7)continue;
                    const ASECM3::MeshEmissionSurface surface(emitter,frame);
                    if(comma)std::cout<<',';comma=true;
                    std::cout<<"{\"emitter\":"<<ei<<",\"regions\":[";
                    for(size_t j=0;j<emitter.meshRegions.size();++j){if(j)std::cout<<',';std::cout<<emitter.meshRegions[j];}
                    std::cout<<"],\"faces\":"<<surface.Faces()<<",\"area\":"<<surface.Area()<<",\"weightedArea\":"<<surface.WeightedArea()<<",\"samples\":[";
                    uint32_t state=0x12345678u;
                    for(unsigned j=0;j<32;++j){ASECM3::EmissionPoint point;if(!surface.Sample(state,point))break;if(j)std::cout<<',';
                        std::cout<<"{\"region\":"<<point.region<<",\"triangle\":"<<point.triangle<<",\"position\":["<<point.position.x<<','<<point.position.y<<','<<point.position.z<<"],\"normal\":["<<point.normal.x<<','<<point.normal.y<<','<<point.normal.z<<"],\"barycentric\":["<<point.barycentric[0]<<','<<point.barycentric[1]<<','<<point.barycentric[2]<<"]}";
                    }
                    std::cout<<"]}";
                }
                std::cout<<']';
            }
            if(signedDetail){
                const auto bank=ASECM3::DecodeAnimationBank(doc);
                std::cout<<",\"signed16Tracks\":[";bool comma=false;size_t emittedKeys=0;
                for(uint32_t ci=0;ci<bank.collections.size();++ci){
                    const auto& collection=bank.collections[ci];std::vector<uint32_t> ids;
                    for(const auto& item:collection.signed16Values)ids.push_back(item.first);
                    std::sort(ids.begin(),ids.end());
                    for(auto id:ids){const auto& track=collection.signed16Values.at(id);
                        emittedKeys+=track.times.size();if(emittedKeys>1000000)throw ASECM3::Error("Signed-track detail exceeds one million keys");
                        if(comma)std::cout<<',';comma=true;
                        std::cout<<"{\"collection\":"<<ci<<",\"name\":"<<Quote(collection.name)<<",\"id\":"<<id<<",\"flags\":"<<track.flags<<",\"end\":"<<track.end<<",\"keys\":[";
                        for(size_t k=0;k<track.times.size();++k){if(k)std::cout<<',';std::cout<<'['<<track.times[k]<<','<<track.values[k]<<']';}
                        std::cout<<"]}";
                    }
                }
                std::cout<<']';
            }
            if(effectBankMode){
                std::filesystem::path bankPath=argv[3];
                if(effectDeclarationMode){const auto count=std::filesystem::file_size(bankPath);if(count>1024)throw ASECM3::Error("Effect declaration exceeds 1024 bytes");std::ifstream metadata(bankPath,std::ios::binary);std::vector<uint8_t> bytes(static_cast<size_t>(count));if(!metadata.read(reinterpret_cast<char*>(bytes.data()),count))throw ASECM3::Error("Short effect declaration read");auto owner=bankPath.string();const std::string suffix=".effectbank";if(owner.size()<=suffix.size()||owner.substr(owner.size()-suffix.size())!=suffix)throw ASECM3::Error("Expected model.m3.effectbank filename");owner.resize(owner.size()-suffix.size());bankPath=ASECM3::ResolveEffectDeclaration(owner,bytes);}
                const auto size=std::filesystem::file_size(bankPath);if(size>128u*1024*1024)throw ASECM3::Error("Effect bank exceeds 128 MiB");
                std::ifstream inputBank(bankPath,std::ios::binary);std::vector<uint8_t> bankBytes(static_cast<size_t>(size));if(!inputBank.read(reinterpret_cast<char*>(bankBytes.data()),size))throw ASECM3::Error("Short effect bank read");
                const auto original=ASECM3::Scene::FromDocument(doc);const auto bound=original->WithEffectAnimationBank(std::move(bankBytes),bankPath.string());
                if(effectMaterialMode){
                    if(!bound->EffectMaterialError().empty())throw ASECM3::Error(bound->EffectMaterialError());
                    std::cout<<",\"effectMaterialRecords\":"<<bound->EffectMaterials().records.size()<<",\"effectMaterialSamples\":[";bool comma=false;
                    for(uint32_t si=0;si<bound->EffectSequences().size();++si)for(uint64_t elapsed:{uint64_t(0),uint64_t(233),uint64_t(1000),uint64_t(2000),uint64_t(3333),uint64_t(4333)}){
                        ASECM3::MaterialEvaluator values(bound->MaterialAnimation(),-1,0),plain(bound->MaterialAnimation(),-1,0);bound->ApplyEffectMaterialValues(values,si,elapsed);
                        for(const auto& link:bound->EffectMaterials().records){const auto& record=ASECM3::LocateMaterialRecord(bound->Materials(),link.mesh);auto copy=record;
                            auto adapted=bound->Materials().materials.at(link.mesh.material);ASECM3::MaterialEvaluator adaptedValues(bound->MaterialAnimation(),-1,0);bound->ApplyEffectMaterialValues(adaptedValues,si,elapsed,link.mesh.material,&adapted);
                            for(const auto& field:link.fields){
                                const auto sampled=values.Sample(record,field.name,field.type,{}),initial=plain.Sample(record,field.name,field.type,{});
                                if(values.Sample(copy,field.name,field.type,{})!=initial)throw ASECM3::Error("Effect overlay reached an unrelated record copy");
                                if(plain.Sample(record,field.name,field.type,{})!=initial||sampled!=adaptedValues.Sample(ASECM3::LocateMaterialRecord(adapted,link.mesh),field.name,field.type,{}))throw ASECM3::Error("Effect overlay changed defaults or adapted material sampling");
                                if(comma)std::cout<<',';comma=true;
                                std::cout<<"{\"material\":"<<Quote(bound->Materials().materials[link.mesh.material].name)<<",\"meshMaterial\":"<<link.mesh.material<<",\"bankMaterial\":"<<link.bank.material<<",\"role\":"<<Quote(link.mesh.role)<<",\"field\":"<<Quote(field.name)<<",\"id\":"<<field.id<<",\"type\":"<<field.type<<",\"sequence\":"<<si<<",\"elapsed\":"<<elapsed<<",\"initial\":["<<initial[0]<<','<<initial[1]<<','<<initial[2]<<','<<initial[3]<<"],\"value\":["<<sampled[0]<<','<<sampled[1]<<','<<sampled[2]<<','<<sampled[3]<<"]}";
                            }
                        }
                    }
                    std::cout<<']';
                }
                if(particleMode){
                    const auto frame=bound->EvaluateGroup();std::cout<<",\"particleMotion\":[";bool comma=false;
                    const auto birthWorld=worldParticleMode?ASECM3::Compose({10,-2,5,0},{0,0,float(std::sqrt(.5)),float(std::sqrt(.5))},{2,2,2,0}):ASECM3::Matrix::Identity();
                    const auto currentWorld=worldParticleMode?ASECM3::Compose({23,8,-4,0},{0,float(std::sin(.3)),0,float(std::cos(.3))},{3,3,3,0}):ASECM3::Matrix::Identity();
                    const auto camera=ASECM3::Compose({-3,4,-8,0},{0,0,float(std::sin(-.2)),float(std::cos(-.2))},{1,1,1,0});
                    for(uint32_t si=0;si<bound->EffectSequences().size();++si)for(const auto& link:bound->EffectBinding().particles){
                        const auto& clip=bound->EffectSequences()[si];const uint64_t duration=uint64_t(clip.end)-clip.start;
                        if(duration>600000)throw ASECM3::Error("Particle motion detail exceeds ten-minute clip");
                        for(uint64_t end:{uint64_t(500),uint64_t(1000),uint64_t(2000),duration,duration+10000}){
                            ASECM3::ParticleEmitter whole(bound,link.meshEmitter,si),chunked(bound,link.meshEmitter,si);
                            whole.Advance(end,frame,birthWorld);for(uint64_t t=0;;t=std::min(end,t+17)){chunked.Advance(t,frame,birthWorld);if(t==end)break;}
                            const auto a=whole.Snapshot(currentWorld),b=chunked.Snapshot(currentWorld);
                            if(a.size()!=b.size()||whole.Spawned()!=chunked.Spawned()||whole.Dropped()!=chunked.Dropped()||whole.NoSurface()!=chunked.NoSurface())throw ASECM3::Error("Particle lifetime/capacity depends on frame partition");
                            for(size_t j=0;j<a.size();++j){const auto& x=a[j];const auto& y=b[j];if(x.serial!=y.serial||x.birthMs!=y.birthMs||x.age!=y.age||x.lifetime!=y.lifetime||x.position.x!=y.position.x||x.position.y!=y.position.y||x.position.z!=y.position.z||x.velocity.x!=y.velocity.x||x.velocity.y!=y.velocity.y||x.velocity.z!=y.velocity.z)throw ASECM3::Error("Particle motion depends on static-frame update partition");}
                            for(size_t j=0;j<a.size();++j){const auto& x=a[j].visual;const auto& y=b[j].visual;if(x.size!=y.size||x.rotation!=y.rotation||x.color!=y.color||x.u0!=y.u0||x.v0!=y.v0||x.u1!=y.u1||x.v1!=y.v1)throw ASECM3::Error("Particle appearance depends on static-frame update partition");}
                            if(comma)std::cout<<',';comma=true;
                            std::cout<<"{\"emitter\":"<<link.meshEmitter<<",\"sequence\":"<<si<<",\"elapsed\":"<<end<<",\"capacity\":"<<whole.Capacity()<<",\"worldSpace\":"<<(whole.WorldSpace()?"true":"false")<<",\"spawned\":"<<whole.Spawned()<<",\"dropped\":"<<whole.Dropped()<<",\"noSurface\":"<<whole.NoSurface()<<",\"partitionMatched\":true,\"particles\":[";
                            for(size_t j=0;j<a.size();++j){if(j)std::cout<<',';const auto& p=a[j];const auto& v=p.visual;std::cout<<"{\"serial\":"<<p.serial<<",\"birth\":"<<p.birthMs<<",\"age\":"<<p.age<<",\"lifetime\":"<<p.lifetime<<",\"position\":["<<p.position.x<<','<<p.position.y<<','<<p.position.z<<"],\"velocity\":["<<p.velocity.x<<','<<p.velocity.y<<','<<p.velocity.z<<"],\"size\":"<<v.size<<",\"rotation\":"<<v.rotation<<",\"color\":["<<v.color[0]<<','<<v.color[1]<<','<<v.color[2]<<','<<v.color[3]<<"],\"uv\":["<<v.u0<<','<<v.v0<<','<<v.u1<<','<<v.v1<<"]}";}
                            const auto quads=ASECM3::BuildParticleBillboards(a,{1,0,0},{0,1,0});
                            std::cout<<"],\"billboards\":{\"indices\":"<<quads.indices.size()<<",\"vertices\":[";
                            for(size_t j=0;j<quads.vertices.size();++j){if(j)std::cout<<',';const auto& v=quads.vertices[j];std::cout<<"{\"position\":["<<v.position.x<<','<<v.position.y<<','<<v.position.z<<"],\"color\":["<<unsigned(v.color[0])<<','<<unsigned(v.color[1])<<','<<unsigned(v.color[2])<<','<<unsigned(v.color[3])<<"],\"uv\":["<<v.uv[0]<<','<<v.uv[1]<<"]}";}
                            std::cout<<"]}";
                            if(worldParticleMode){
                                const auto cam=ASECM3::CameraParticles(a,camera);ASECM3::Vec3 lower,upper;const bool bounds=ASECM3::ParticleSourceBounds(a,currentWorld,lower,upper);
                                std::cout<<",\"worldSpaceCheck\":{\"hasBounds\":"<<(bounds?"true":"false");
                                if(bounds)std::cout<<",\"lower\":["<<lower.x<<','<<lower.y<<','<<lower.z<<"],\"upper\":["<<upper.x<<','<<upper.y<<','<<upper.z<<']';
                                std::cout<<",\"cameraPositions\":[";for(size_t j=0;j<cam.size();++j){if(j)std::cout<<',';std::cout<<'['<<cam[j].position.x<<','<<cam[j].position.y<<','<<cam[j].position.z<<','<<cam[j].visual.size<<']';}
                                const auto camQuads=ASECM3::BuildParticleBillboards(cam,{1,0,0},{0,1,0});const auto toSource=ASECM3::Multiply(ASECM3::InverseAffine(currentWorld),ASECM3::InverseAffine(camera));
                                for(const auto& vertex:camQuads.vertices){const auto p=ASECM3::TransformPoint(toSource,vertex.position);if(!bounds||p.x<lower.x-2e-5f||p.y<lower.y-2e-5f||p.z<lower.z-2e-5f||p.x>upper.x+2e-5f||p.y>upper.y+2e-5f||p.z>upper.z+2e-5f)throw ASECM3::Error("World particle bounds exclude a camera-facing quad");}
                                std::cout<<"],\"cameraQuadVertices\":"<<camQuads.vertices.size()<<",\"boundsContainCameraQuads\":true}";
                            }
                            std::cout<<'}';
                        }
                    }
                    std::cout<<']';
                }
                if(effectClockMode){
                    std::cout<<",\"emissionClocks\":[";bool comma=false;
                    for(uint32_t si=0;si<bound->EffectSequences().size();++si)for(const auto& link:bound->EffectBinding().particles){
                        const auto& clip=bound->EffectSequences()[si];
                        const uint64_t duration=uint64_t(clip.end)-clip.start;
                        const uint64_t end=duration*((clip.flags&1)?1:2);
                        if(end>600000)throw ASECM3::Error("Emission clock detail exceeds ten minutes");
                        ASECM3::EmissionClock whole(*bound,link.meshEmitter,si),chunked(*bound,link.meshEmitter,si);
                        const auto births=whole.Advance(end);std::vector<ASECM3::ParticleBirth> chunks;
                        for(uint64_t t=0;;t=std::min(end,t+17)){auto next=chunked.Advance(t);chunks.insert(chunks.end(),next.begin(),next.end());if(t==end)break;}
                        if(births.size()!=chunks.size())throw ASECM3::Error("Emission rate birth count depends on frame partition");
                        for(size_t j=0;j<births.size();++j)if(births[j].elapsedMs!=chunks[j].elapsedMs||births[j].quantity!=chunks[j].quantity||births[j].countKey!=chunks[j].countKey)throw ASECM3::Error("Emission birth timestamp depends on frame partition");
                        if(comma)std::cout<<',';comma=true;
                        std::cout<<"{\"emitter\":"<<link.meshEmitter<<",\"sequence\":"<<si<<",\"clip\":"<<Quote(clip.name)<<",\"elapsed\":"<<end<<",\"integratedRate\":"<<whole.IntegratedRate(end)<<",\"framePartitionMatched\":true,\"births\":[";
                        for(size_t j=0;j<births.size();++j){if(j)std::cout<<',';std::cout<<"{\"time\":"<<births[j].elapsedMs<<",\"quantity\":"<<births[j].quantity<<",\"countKey\":"<<(births[j].countKey?"true":"false")<<'}';}
                        std::cout<<"]}";
                    }
                    std::cout<<']';
                }
                if(bound->Model().animations.size()!=original->Model().animations.size()||bound->Model().skeleton.size()!=original->Model().skeleton.size()||bound->AnimationSource()!=original->AnimationSource())throw ASECM3::Error("Effect binding changed skeletal ownership");
                const auto a=original->EvaluateGroup(),b=bound->EvaluateGroup();if(a.pose.world.size()!=b.pose.world.size())throw ASECM3::Error("Effect binding changed skeletal pose size");
                for(size_t j=0;j<a.pose.world.size();++j)for(unsigned k=0;k<16;++k)if(a.pose.world[j].values[k]!=b.pose.world[j].values[k])throw ASECM3::Error("Effect binding changed skeletal pose");
                std::cout<<",\"skeletalOwnershipPreserved\":true,\"effectEmitterLinks\":[";bool comma=false;for(const auto& link:bound->EffectBinding().particles){if(comma)std::cout<<',';comma=true;std::cout<<"{\"meshEmitter\":"<<link.meshEmitter<<",\"bankEmitter\":"<<link.bankEmitter<<",\"meshBone\":"<<link.meshBone<<",\"bankBone\":"<<link.bankBone<<'}';}
                std::cout<<"],\"effectSequences\":[";comma=false;for(uint32_t j=0;j<bound->EffectSequences().size();++j){if(comma)std::cout<<',';comma=true;std::cout<<"{\"name\":"<<Quote(bound->EffectSequences()[j].name)<<",\"group\":"<<bound->EffectSequenceGroup(j)<<'}';}std::cout<<']';
                std::cout<<",\"effectRateSamples\":[";comma=false;for(uint32_t j=0;j<bound->EffectSequences().size();++j){const auto group=bound->EffectSequenceGroup(j);if(group<0)continue;for(const auto& link:bound->EffectBinding().particles)for(auto time:{0,100,233,666,1500,2666,3333}){if(comma)std::cout<<',';comma=true;std::cout<<"{\"sequence\":"<<j<<",\"emitter\":"<<link.meshEmitter<<",\"sourceMs\":"<<time<<",\"rate\":"<<bound->SampleEffectScalar(link.meshEmitter,"emit_rate",group,time)<<'}';}}std::cout<<']';
            }
            size_t effectBytes=0;for(const auto& payload:effects.payloads)effectBytes+=payload.bytes.size();
            std::cout<<",\"effectParticles\":"<<effects.particles.size()<<",\"effectCopies\":"<<effects.copies.size()<<",\"effectRibbons\":"<<effects.ribbons.size()<<",\"effectRetainedBytes\":"<<effectBytes;
            if(effectsDetail){
                auto hex=[](const std::vector<uint8_t>& bytes){if(bytes.size()>8u*1024*1024)throw ASECM3::Error("Effect detail output exceeds 8 MiB per record; use summary mode");const char* digits="0123456789abcdef";std::string out;out.reserve(bytes.size()*2);for(auto b:bytes){out+=digits[b>>4];out+=digits[b&15];}return out;};
                auto definitions=[&](const char* name,const std::vector<ASECM3::EffectDefinition>& values){std::cout<<",\""<<name<<"\":[";bool comma=false;for(const auto& e:values){if(comma)std::cout<<',';comma=true;std::cout<<"{\"bone\":"<<e.bone<<",\"material\":"<<e.material<<",\"version\":"<<e.record.version<<",\"bytes\":"<<Quote(hex(e.record.bytes))<<",\"properties\":[";bool prop=false;for(const auto& p:e.record.properties){if(prop)std::cout<<',';prop=true;std::cout<<"{\"name\":"<<Quote(p.name)<<",\"kind\":"<<Quote(p.kind)<<",\"value\":";std::visit([](auto value){std::cout<<value;},p.value);std::cout<<'}';}std::cout<<"]}";}std::cout<<']';};
                definitions("particleDefinitions",effects.particles);definitions("particleCopies",effects.copies);definitions("ribbonDefinitions",effects.ribbons);
                std::cout<<",\"effectPayloads\":[";bool comma=false;for(const auto& p:effects.payloads){if(comma)std::cout<<',';comma=true;std::cout<<"{\"section\":"<<p.section<<",\"tag\":"<<Quote(p.tag)<<",\"count\":"<<p.count<<",\"version\":"<<p.version<<",\"bytes\":"<<Quote(hex(p.bytes))<<'}';}std::cout<<']';
            }
            const auto bank=ASECM3::DecodeAnimationBank(doc);
            size_t tracks=0,keys=0,samples=0,other=0;
            for (const auto& collection:bank.collections) {
                for (auto count:collection.otherChannelTypes) other+=count;
                for (const auto& item:collection.transforms) {
                    const auto& track=item.second; ++tracks; keys+=track.times.size();
                    if (!track.times.empty()) {
                        const auto midpoint=static_cast<int32_t>((int64_t(track.times.front())+track.times.back())/2);
                        const auto value=ASECM3::SampleTransform(track,midpoint,1,{});
                        (void)value; ++samples;
                    }
                }
            }
            std::cout<<",\"transformTracks\":"<<tracks<<",\"transformKeys\":"<<keys<<",\"sampledTracks\":"<<samples
                <<",\"otherTracks\":"<<other<<",\"animationGroups\":"<<bank.groups.size();
            std::cout<<",\"textureLayers\":["; bool layerComma=false;
            for (uint32_t si=0;si<doc.Sections().size();++si) {
                const auto& section=doc.Sections()[si]; if (section.tag!="LAYR" || !section.layout) continue;
                for (uint32_t li=0;li<section.count;++li) {
                    const ASECM3::Record layer(doc,si,li);
                    const auto bitmap=layer.String("color_bitmap"); if (bitmap.empty()) continue;
                    if (layerComma) std::cout<<','; layerComma=true;
                    std::cout<<"{\"bitmap\":"<<Quote(bitmap)<<",\"flags\":"<<layer.UInt("flags")<<",\"uvSource\":"<<layer.UInt("uv_source")<<'}';
                }
            }
            std::cout<<']';
            if (geometry) {
                // Detailed geometry remains opt-in; dependency metadata below is small and always emitted.
                std::cout<<",\"vertexData\":["; bool comma=false;
                for (const auto& v:model.vertices) {
                    if (comma) std::cout<<','; comma=true;
                    std::cout<<"{\"position\":["<<v.position.x<<','<<v.position.y<<','<<v.position.z<<"],\"normal\":["<<v.normal.x<<','<<v.normal.y<<','<<v.normal.z<<"],\"weights\":[";
                    for (int j=0;j<4;++j) { if (j) std::cout<<','; std::cout<<unsigned(v.weights[j]); }
                    std::cout<<"],\"lookup\":[";
                    for (int j=0;j<4;++j) { if (j) std::cout<<','; std::cout<<unsigned(v.boneLookup[j]); }
                    std::cout<<"],\"uv\":[";
                    for (size_t j=0;j<v.uv.size();++j) { if (j) std::cout<<','; std::cout<<v.uv[j]; }
                    std::cout<<"],\"tangent\":["<<v.tangent.x<<','<<v.tangent.y<<','<<v.tangent.z<<"],\"tangentSign\":"<<unsigned(v.tangentSign)<<",\"hasTangent\":"<<(v.hasTangent?"true":"false")<<",\"hasColor\":"<<(v.hasColor?"true":"false")<<",\"color\":[";
                    for(unsigned j=0;j<4;++j){if(j)std::cout<<',';std::cout<<unsigned(v.color[j]);}std::cout<<"]}";
                }
                std::cout<<']';
            }
            std::cout<<",\"skeleton\":["; bool boneComma=false;
            for (const auto& bone:model.skeleton) {
                if (boneComma) std::cout<<','; boneComma=true;
                std::cout<<"{\"name\":"<<Quote(bone.name)<<",\"parent\":"<<bone.parent<<",\"flags\":"<<bone.flags
                    <<",\"translationId\":"<<bone.translation.id<<",\"rotationId\":"<<bone.rotation.id<<",\"scaleId\":"<<bone.scale.id<<'}';
            }
            std::cout<<']';
            std::cout<<",\"unknownSections\":["; bool comma=false;
            for (const auto& section:doc.Sections()) if (!section.layout) { if (comma) std::cout<<','; comma=true; std::cout<<Quote(section.tag+" v"+std::to_string(section.version)); }
            std::cout<<"],\"regions\":["; comma=false;
            for (const auto& division:model.divisions) for (const auto& region:division.regions) {
                if (comma) std::cout<<','; comma=true; std::cout<<"{\"vertices\":"<<region.vertexCount<<",\"indices\":"<<region.indexCount<<'}';
            }
            std::cout<<"],\"nativeLayers\":[";bool nativeComma=false;
            const auto materials=ASECM3::DecodeMaterials(doc,model);
            auto emitLayer=[&](const auto& layer){if(layer.image.empty())return;if(nativeComma)std::cout<<',';nativeComma=true;
                std::cout<<"{\"role\":"<<Quote(layer.role)<<",\"bitmap\":"<<Quote(layer.image)<<'}';};
            for(const auto& material:materials.materials){for(const auto& layer:material.layers)emitLayer(layer);
                if(material.bufferSurface)for(const auto& layer:material.bufferSurface->layers)emitLayer(layer);
                for(const auto& name:material.bufferTextures){if(nativeComma)std::cout<<',';nativeComma=true;std::cout<<"{\"role\":\"buffer\",\"bitmap\":"<<Quote(name)<<'}';}}
            std::cout<<"],\"attachmentPoints\":[";bool pointComma=false;
            const auto attachments=ASECM3::DecodeAttachments(doc);
            for(const auto& point:attachments.points){if(pointComma)std::cout<<',';pointComma=true;
                std::cout<<"{\"name\":"<<Quote(point.name)<<",\"wowId\":"<<point.wowId<<",\"pivot\":["<<point.bindPivot.x<<','<<point.bindPivot.y<<','<<point.bindPivot.z<<"]}";}
            std::cout<<"],\"animations\":["; comma=false;
            for (const auto& animation:model.animations) {
                if (comma) std::cout<<','; comma=true;
                std::cout<<"{\"name\":"<<Quote(animation.name)<<",\"start\":"<<animation.start<<",\"end\":"<<animation.end<<",\"flags\":"<<animation.flags<<",\"blendMs\":"<<animation.blendMs<<'}';
            }
            std::cout<<']';
        } catch (const std::exception& e) { ++failed; std::cout<<",\"ok\":false,\"error\":"<<Quote(e.what()); }
        std::cout<<'}';
    }
    std::cout<<"],\"total\":"<<files.size()<<",\"failed\":"<<failed<<"}\n";
    return failed?1:0;
}
