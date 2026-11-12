#include "ASECM3/BufferSurface.h"
#include "ASECM3/Material.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace ASECM3 {
namespace {
struct BufferGroupName {uint32_t id;const char* name;};
struct BufferInputName {uint32_t group,id,kind;const char* name;};
#include "BufferInputs.generated.inc"
void U(MaterialRecord& r,const char* name,uint32_t value){r.properties.push_back({name,"uint32",uint64_t(value)});}
void SetU(MaterialRecord& r,const char* name,uint32_t value){
    for(auto& property:r.properties)if(property.name==name){property.kind="uint32";property.value=uint64_t(value);return;}
    U(r,name,value);
}
void F(MaterialRecord& r,const std::string& name,float value){r.properties.push_back({name,"float",value});}
void SetF(MaterialRecord& r,const char* name,float value){
    for(auto& property:r.properties)if(property.name==name){property.kind="float";property.value=value;return;}
    F(r,name,value);
}
std::string NormalizeTexturePath(std::string value){
    for(auto& c:value)c=c=='/'?'\\':char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
const std::string* FindTexturePath(const Material& material,const char* expected){
    const auto key=NormalizeTexturePath(expected);const std::string* result=nullptr;
    for(const auto& path:material.bufferTextures)if(NormalizeTexturePath(path)==key){
        if(result&&NormalizeTexturePath(*result)!=key)return nullptr;
        result=&path;
    }
    return result;
}
bool BuildVoidCapeFallback(Material& material){
    if(material.name!="Void_Cape")return false;
    const auto* mask=FindTexturePath(material,"Assets/Textures/t_fx_xalatath_cloakvin.dds");
    const auto* gradient=FindTexturePath(material,"Assets/Textures/t_fx_bluepurple_gradient_base_color.dds");
    if(!mask||!gradient)return false;
    auto surface=std::make_shared<Material>();surface->type=1;surface->name=material.name;surface->record.tag="ASEC_MADD_SURFACE";
    F(surface->record,"hdr_spec",1);F(surface->record,"hdr_emis",1);U(surface->record,"blend_mode",1);

    MaterialLayer diffuse;diffuse.role="layer_diff";diffuse.image=*gradient;diffuse.record.tag="ASEC_MADD_AUTHORED_DIFFUSE_FALLBACK";
    U(diffuse.record,"flags",0);U(diffuse.record,"color_channels",0);U(diffuse.record,"uv_source",1);
    F(diffuse.record,"color_multiply.default",0.8f);F(diffuse.record,"color_add.default",-0.1f);F(diffuse.record,"color_brightness.default",1.0f);U(diffuse.record,"asec_color_clamp",1);
    MaterialLayer alpha;alpha.role="layer_alpha1";alpha.image=*mask;alpha.record.tag="ASEC_MADD_AUTHORED_ALPHA_FALLBACK";
    U(alpha.record,"flags",0);U(alpha.record,"color_channels",2);U(alpha.record,"uv_source",1);
    surface->layers.push_back(std::move(diffuse));surface->layers.push_back(std::move(alpha));
    material.bufferSurfaceError="MADD partial surface; known Void_Cape gradient/opacity textures reconstructed; source shader blocks remain approximate";
    if(!material.bufferDecodeError.empty())material.bufferSurfaceError+="; decode error="+material.bufferDecodeError;
    material.bufferSurface=std::move(surface);return true;
}
bool BuildMountSurface(Material& material){
    // shortcut: named HotS texture sets approximate unsupported shader graphs; replace with decoded shader inputs for parity.
    std::map<std::string,std::string> selected;
    for(const auto& path:material.bufferTextures){
        const auto key=NormalizeTexturePath(path);
        if(key.find("storm_mount_")==std::string::npos&&key.find("storm_fx_")==std::string::npos&&key.find("storm_hero_")==std::string::npos&&key.find("storm_morph_")==std::string::npos)continue;
        for(const auto& entry:{std::pair<const char*,const char*>{"_diff.dds","layer_diff"},{"_norm.dds","layer_norm"},{"_spec.dds","layer_spec"},{"_emis.dds","layer_emis1"}}){
            const std::string suffix=entry.first;
            if(key.size()<suffix.size()||key.compare(key.size()-suffix.size(),suffix.size(),suffix)!=0)continue;
            const auto found=selected.find(entry.second);if(found!=selected.end()&&NormalizeTexturePath(found->second)!=key)return false;
            selected[entry.second]=path;
        }
    }
    if(!selected.count("layer_diff")){
        // Celestial mounts draw a starfield in screen space through an otherwise undecoded shell shader.
        for(const auto& path:material.bufferTextures)if(NormalizeTexturePath(path).find("starfield")!=std::string::npos){
            auto surface=std::make_shared<Material>();surface->type=1;surface->name=material.name;surface->record.tag="ASEC_HOTS_MOUNT_SURFACE";
            F(surface->record,"hdr_emis",1);U(surface->record,"blend_mode_emis1",2);
            MaterialLayer layer;layer.role="layer_emis1";layer.image=path;layer.record.tag="ASEC_HOTS_MOUNT_LAYER";
            U(layer.record,"flags",12);U(layer.record,"uv_source",15);U(layer.record,"color_channels",0);surface->layers.push_back(std::move(layer));
            material.bufferSurface=std::move(surface);
            material.bufferSurfaceError="HotS celestial starfield approximation; original shader graph retained but not fully implemented";
            return true;
        }
        return false;
    }
    auto surface=std::make_shared<Material>();surface->type=1;surface->name=material.name;surface->record.tag="ASEC_HOTS_MOUNT_SURFACE";
    F(surface->record,"hdr_spec",1);F(surface->record,"hdr_emis",1);
    U(surface->record,"blend_mode_emis1",2); // _Emis maps are glow added on top; the default 0 multiplies the body to black
    for(const auto& entry:selected){MaterialLayer layer;layer.role=entry.first;layer.image=entry.second;layer.record.tag="ASEC_HOTS_MOUNT_LAYER";
        U(layer.record,"flags",12);U(layer.record,"uv_source",0);U(layer.record,"color_channels",0);surface->layers.push_back(std::move(layer));}
    material.bufferSurface=std::move(surface);
    material.bufferSurfaceError="HotS mount named-texture surface approximation; original shader graph retained but not fully implemented";
    return true;
}
uint32_t UInt(const BufferParameter& p,size_t at=0){
    if(at>p.bytes.size()||p.bytes.size()-at<4)throw Error("Truncated MADD surface parameter");
    uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(p.bytes[at+i])<<(i*8);return v;
}
float Float(const BufferParameter& p,size_t at=0){uint32_t bits=UInt(p,at);float value;std::memcpy(&value,&bits,4);if(!std::isfinite(value))throw Error("Non-finite MADD surface parameter");return value;}
class Inputs {
    std::unordered_map<std::string,const BufferParameter*> fields;
    std::unordered_set<std::string> used;
public:
    explicit Inputs(const BufferParameterGroup& group){
        for(const auto& field:group.parameters){
            const BufferInputName* name=nullptr;
            // Xal'atath's authored Diffuse block has a distinct CRC32 block
            // ID but reuses the four typed texture inputs from
            // DiffuseTeamColor. Its payload does not contain team-color or
            // material-alpha parameters, so alias only those shared inputs.
            const uint32_t inputGroup=group.id==1629286381u?1543313946u:group.id;
            for(const auto& candidate:bufferInputNames)if(candidate.group==inputGroup&&candidate.id==field.id){name=&candidate;break;}
            if(!name||(name->kind&0x10000)==0||field.flags!=1)throw Error("MADD surface has an unknown or non-material parameter");
            if(!fields.emplace(name->name,&field).second)throw Error("MADD surface has duplicate named inputs");
        }
    }
    const BufferParameter& Get(const std::string& name,size_t width){
        const auto found=fields.find(name);
        if(found==fields.end())throw Error("MADD surface input is absent: "+name);
        if(found->second->byteWidth!=width)throw Error("MADD surface input has an unsupported width: "+name);
        used.insert(name);return *found->second;
    }
    void Complete() const {if(used.size()!=fields.size())throw Error("MADD surface has unimplemented named inputs");}
};
void IdentityTransform(Inputs& inputs,const std::string& name){
    const auto& p=inputs.Get(name,32);const float identity[]={0,0,1,1,0,0,0,0};
    for(unsigned i=0;i<8;++i)if(Float(p,4*i)!=identity[i])throw Error("MADD non-identity UV transform requires further decoding");
}
void Channels(Inputs& inputs,const std::string& name,MaterialRecord& record){
    const auto& p=inputs.Get(name,20);const auto channel=UInt(p);
    // MADD RGBA/RGB/R/G/B/A selection compared with the source MAT_/LAYR
    // equivalents. Byte 16 is the invert flag and byte 17 is the clamp flag;
    // the final two bytes are reserved. Inversion is still unsupported.
    const uint32_t sourceChannels[]={1,0,3,4,5,2};
    if(channel>=6||p.bytes[16]>1||p.bytes[17]>1)throw Error("Unsupported MADD channel selection/invert/clamp control");
    if(p.bytes[16])throw Error("MADD channel inversion requires further decoding");
    U(record,"color_channels",sourceChannels[channel]);
    U(record,"asec_color_clamp",p.bytes[17]);
    F(record,"color_multiply.default",Float(p,4));F(record,"color_add.default",Float(p,8));F(record,"color_brightness.default",Float(p,12));
}
MaterialLayer Layer(const Material& source,Inputs& inputs,const char* texture,const char* prefix,const char* role,bool normal=false,bool environment=false,uint32_t expectedSampler=3,uint32_t layerFlags=12){
    MaterialLayer layer;layer.role=role;layer.record.tag="ASEC_MADD_LAYER";
    const auto& binding=inputs.Get(texture,8);const uint32_t index=UInt(binding),sampler=UInt(binding,4);
    if(index>=source.bufferTextures.size()||source.bufferTextures[index].empty())throw Error("MADD surface texture index is invalid");
    if(sampler!=expectedSampler)throw Error("MADD sampler flags require further decoding");
    layer.image=source.bufferTextures[index];U(layer.record,"flags",layerFlags);
    if(environment){
        IdentityTransform(inputs,"EnvironmentMapUVWTransform");U(layer.record,"uv_source",2);
        Channels(inputs,"EnvironmentMapChannelSelection",layer.record);
    }else{
        const auto channel=UInt(inputs.Get(std::string(prefix)+"UVSelection",4));
        if(channel>3)throw Error("MADD generated UV selection requires further decoding");
        if(normal&&channel!=0)throw Error("MADD normal maps on UV1..UV3 require a matching tangent basis");
        const uint32_t uv[]={0,1,9,10};U(layer.record,"uv_source",uv[channel]);
        IdentityTransform(inputs,std::string(prefix)+"UVTransform");
        if(!normal)Channels(inputs,std::string(prefix)+"ChannelSelection",layer.record);
    }
    return layer;
}
}
void BuildBufferSurface(Material& material){
    if(material.type!=12)return;
    material.bufferSurface.reset();material.bufferSurfaceError.clear();
    // This named cape's private MADD shader is not implemented. Reconstruct it
    // from its exact known texture dependencies so it does not fall back to an
    // opaque neutral sheet. The regular graph decoder is retained for every
    // other material.
    if(BuildVoidCapeFallback(material))return;
    if(!material.bufferDecodeError.empty()){if(!BuildMountSurface(material))material.bufferSurfaceError=material.bufferDecodeError;return;}
    try{
        for(const auto& payload:material.bufferPayloads)if(payload.role=="unknown3"&&!payload.bytes.empty())throw Error("MADD dynamic parameter payload requires further decoding");
        auto surface=std::make_shared<Material>();surface->type=1;surface->name=material.name;surface->record.tag="ASEC_MADD_SURFACE";
        uint32_t flags=0;bool diffuse=false;std::vector<std::pair<uint32_t,std::string>> unsupportedBlocks;
        F(surface->record,"hdr_spec",1);F(surface->record,"hdr_emis",1);
        for(const auto& group:material.bufferParameters.groups){
            std::string name;
            for(const auto& entry:bufferGroupNames)if(entry.id==group.id){name=entry.name;break;}
            if(group.id==1629286381u)name="Diffuse"; // CRC32("Diffuse")
            if(name.empty()){
                // Unknown DDBS blocks are independent graph contributions.
                // Preserve all validated named layers that follow them, while
                // retaining an explicit partial-surface diagnostic.
                unsupportedBlocks.emplace_back(group.id,"unknown shader block");continue;
            }
            std::vector<MaterialLayer> blockLayers;MaterialRecord blockRecord;uint32_t blockFlags=0;bool blockDiffuse=false;
            try{
                Inputs inputs(group);
                if(name=="Normal")blockLayers.push_back(Layer(material,inputs,"TexNormal","Normal","layer_norm",true));
                else if(name=="Gloss")blockLayers.push_back(Layer(material,inputs,"TexGloss","Gloss","layer_gloss"));
                else if(name=="Specular")blockLayers.push_back(Layer(material,inputs,"TexSpecular","Specular","layer_spec"));
                else if(name=="DiffuseTeamColor"){blockLayers.push_back(Layer(material,inputs,"TexDiffuse","Diffuse","layer_diff"));blockDiffuse=true;}
                else if(name=="Diffuse"){blockLayers.push_back(Layer(material,inputs,"TexDiffuse","Diffuse","layer_diff"));blockDiffuse=true;}
                else if(name=="EmissiveFast"){
                    blockLayers.push_back(Layer(material,inputs,"TexEmissive","Emissive","layer_emis1"));
                    const auto control=UInt(inputs.Get("EmissiveControl",4));if(control>3)throw Error("Unsupported MADD emissive control");U(blockRecord,"blend_mode_emis1",control);
                }else if(name=="EnvioMask")blockLayers.push_back(Layer(material,inputs,"TexEnvioMask","EnvioMask","layer_envi_mask"));
                else if(name=="EnvioBlurred"){
                    blockLayers.push_back(Layer(material,inputs,"TexEnvironmentMap","","layer_envi",false,true));
                    const auto control=UInt(inputs.Get("EnvioControl",4));if(control>3)throw Error("Unsupported MADD environment control");U(blockRecord,"blend_mode_layer",control);
                }else if(name=="LightingDeferred"){
                    const float exponent=Float(inputs.Get("Specularity",4));if(exponent<0)throw Error("Invalid MADD specular exponent");F(blockRecord,"specularity",exponent);
                }else if(name=="TwoSided")blockFlags|=8;
                else if(name!="ECSpec"&&name!="MaterialFinalize"&&name!="Fog"&&name!="FOW")throw Error("Unimplemented MADD surface block: "+name);
                inputs.Complete();
            }catch(const Error& error){
                // A known block can still use an unsupported UV transform or
                // control. Skip only that contribution so a valid diffuse
                // surface remains visible instead of replacing the whole M3
                // material with an untextured neutral sheet.
                unsupportedBlocks.emplace_back(group.id,error.what());continue;
            }
            surface->layers.insert(surface->layers.end(),blockLayers.begin(),blockLayers.end());
            surface->record.properties.insert(surface->record.properties.end(),blockRecord.properties.begin(),blockRecord.properties.end());
            flags|=blockFlags;diffuse=diffuse||blockDiffuse;
        }
        if(!diffuse)throw Error("MADD preview surface requires a named diffuse input");
        U(surface->record,"flags",flags);
        if(!unsupportedBlocks.empty()){
            material.bufferSurfaceError="MADD partial surface; skipped shader blocks";
            for(const auto& skipped:unsupportedBlocks)material.bufferSurfaceError+=" "+std::to_string(skipped.first)+" ("+skipped.second+")";
        }
        material.bufferSurface=std::move(surface);
    }catch(const Error& e){
        if(!BuildVoidCapeFallback(material)&&!BuildMountSurface(material))material.bufferSurfaceError=e.what();
    }
}
}
