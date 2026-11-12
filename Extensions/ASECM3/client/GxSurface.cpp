#include "GxSurface.h"
#include "GxGeometryTransform.h"
#include "SceneEnvironment.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDevice/CGxShader.h"
#include "Engine/Source/Gx/CGxBuf.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/Tempest/C4Vector.h"
#include "Engine/Source/Tempest/C2Vector.h"
#include "Engine/Source/Tempest/CImVector.h"
#include "Engine/Source/Tempest/CRect.h"
#include "Engine/Source/Tempest/C2iVector.h"
#include "Engine/Source/Gx/CGxTex.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <windows.h>
#include <vector>
namespace ASECM3::Client {
namespace {
// Draw-phase timing, appended to Logs\ASECM3-materials.log every 400 layered draws (milliseconds, summed).
struct Prof{
    LARGE_INTEGER t[8];int n=0;static inline double sum[8]={};static inline int draws=0;
    static double Now(){LARGE_INTEGER c,f;QueryPerformanceCounter(&c);QueryPerformanceFrequency(&f);return double(c.QuadPart)*1000.0/double(f.QuadPart);}
    double at[8]={};int count=0;
    void Mark(){at[count++]=Now();}
    ~Prof(){if(count<2)return;for(int i=1;i<count;++i)sum[i-1]+=at[i]-at[i-1];if(++draws%400==0){
        if(FILE* log=std::fopen("Logs\\ASECM3-materials.log","a")){std::fprintf(log,"prof draws=%d layers=%.1f setup=%.1f stream=%.1f draw=%.1f ms\n",draws,sum[0],sum[1],sum[2],sum[3]);std::fclose(log);}
        for(auto& v:sum)v=0;}}
};
#include "SurfaceShaders.generated.inc"
#include "DistortionShader.generated.inc"
constexpr unsigned PixelVectors=93,LayerCount=13;
// The module's pixel constants start at c16: rows c14/c15 are the Vulkan device's fog state, and c20 and up reach the
// device through the extended block of the translated record.
constexpr unsigned PixelBase=16,PixelShadowRows=PixelBase+PixelVectors;
const char* roles[]={"layer_diff","layer_decal","layer_emis1","layer_emis2","layer_alpha1","layer_alpha2","layer_light","layer_ao","layer_norm","layer_spec","layer_gloss","layer_envi","layer_envi_mask"};
double Value(const MaterialRecord& r,const std::string& n,double f){const auto* p=r.Find(n);return p?std::visit([](auto v){return double(v);},p->value):f;}
int UV(uint32_t n){switch(n){case 0:return 0;case 1:return 1;case 9:return 2;case 10:return 3;default:return -1;}}
// Planar (4,5,11..14), screen (15) and triplanar (16..18) sources are computed per vertex into a spare UV channel.
bool GeneratedUV(uint32_t n){return n==4||n==5||(n>=11&&n<=18);}
// shortcut: world planar/triplanar use the model's local axes and screen space is per-vertex NDC; exact SC2 projections are unverified.
C2Vector GeneratedUVValue(uint32_t source,const C3Vector& local,const C3Vector& view,const C44Matrix& viewProjection){
    if(source==15){C4Vector p;p.x=view.x;p.y=view.y;p.z=view.z;p.w=1;p=p*viewProjection;if(p.w<=1e-6f)return C2Vector(0,0);return C2Vector(.5f+.5f*p.x/p.w,.5f-.5f*p.y/p.w);}
    if(source==11||source==13)return C2Vector(local.y,local.z);
    if(source==12||source==14)return C2Vector(local.x,local.z);
    return C2Vector(local.x,local.y);
}
struct Vertex{C3Vector p,n;CImVector c;C2Vector uv[4];C3Vector tangent;float sign;};
static_assert(sizeof(Vertex)==76,"M3 shader declaration stride changed");
bool ClippedTriangleDepth(const C3Vector& a,const C3Vector& b,const C3Vector& c,const C44Matrix& viewProjection,float& depth,bool& visible){
    std::array<C4Vector,12> first{},second{};
    auto project=[&](const C3Vector& p){C4Vector v;v.x=p.x;v.y=p.y;v.z=p.z;v.w=1;return v*viewProjection;};
    first[0]=project(a);first[1]=project(b);first[2]=project(c);
    size_t count=3;
    auto distance=[](const C4Vector& p,unsigned plane){switch(plane){case 0:return p.z;case 1:return p.w-p.z;case 2:return p.x+p.w;case 3:return p.w-p.x;case 4:return p.y+p.w;default:return p.w-p.y;}};
    auto interpolate=[](const C4Vector& a,const C4Vector& b,float t){C4Vector p;p.x=a.x+(b.x-a.x)*t;p.y=a.y+(b.y-a.y)*t;p.z=a.z+(b.z-a.z)*t;p.w=a.w+(b.w-a.w)*t;return p;};
    for(const auto& p:first)if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||!std::isfinite(p.w))return false;
    for(unsigned plane=0;plane<6;++plane){
        if(!count){visible=false;return true;}
        size_t output=0;C4Vector previous=first[count-1];float previousDistance=distance(previous,plane);
        if(!std::isfinite(previousDistance))return false;bool previousInside=previousDistance>=0;
        for(size_t i=0;i<count;++i){const C4Vector current=first[i];const float currentDistance=distance(current,plane);if(!std::isfinite(currentDistance))return false;const bool currentInside=currentDistance>=0;
            if(previousInside!=currentInside){const float denominator=previousDistance-currentDistance;if(!std::isfinite(denominator)||std::abs(denominator)<1e-20f)return false;if(output>=second.size())return false;second[output++]=interpolate(previous,current,previousDistance/denominator);}
            if(currentInside){if(output>=second.size())return false;second[output++]=current;}
            previous=current;previousDistance=currentDistance;previousInside=currentInside;
        }
        count=output;first.swap(second);
    }
    if(count<3){visible=false;return true;}
    // Sort by the depth the rasterizer actually interpolates, weighted by
    // projected screen area. Averaging clip-space w gives a large triangle's
    // near and far corners equal influence even when nearly all visible pixels
    // lie near one corner; this becomes unstable as the camera closes in.
    double areaSum=0,weightedDepth=0;
    const C4Vector& origin=first[0];
    if(origin.w<=1e-20f)return false;
    const double ox=origin.x/origin.w,oy=origin.y/origin.w,oz=origin.z/origin.w;
    for(size_t i=1;i+1<count;++i){
        const C4Vector& b=first[i];const C4Vector& c=first[i+1];
        if(b.w<=1e-20f||c.w<=1e-20f)return false;
        const double bx=b.x/b.w,by=b.y/b.w,bz=b.z/b.w;
        const double cx=c.x/c.w,cy=c.y/c.w,cz=c.z/c.w;
        const double area=std::abs((bx-ox)*(cy-oy)-(by-oy)*(cx-ox));
        if(!std::isfinite(area))return false;
        if(area<=1e-20)continue;
        areaSum+=area;
        weightedDepth+=area*(oz+bz+cz)/3.0;
    }
    if(areaSum<=1e-20){
        double sum=0;for(size_t i=0;i<count;++i){if(first[i].w<=1e-20f)return false;sum+=first[i].z/first[i].w;}
        depth=float(sum/count);
    }else depth=float(weightedDepth/areaSum);
    if(!std::isfinite(depth))return false;visible=true;return true;
}
float ApproximateTriangleDepth(const C3Vector& a,const C3Vector& b,const C3Vector& c,const C44Matrix& viewProjection){
    double sum=0;unsigned count=0;
    const C3Vector vertices[]={a,b,c};
    for(const auto& vertex:vertices){
        C4Vector p;p.x=vertex.x;p.y=vertex.y;p.z=vertex.z;p.w=1;p=p*viewProjection;
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||!std::isfinite(p.w)||p.w<=1e-20f)continue;
        const double z=double(p.z)/p.w;if(!std::isfinite(z))continue;
        sum+=std::clamp(z,0.0,1.0);++count;
    }
    return count?float(sum/count):.5f;
}
CGxShader* Create(CGxDevice* device,EGxShTarget target,const char* name,const unsigned char* code,size_t size){
    auto* shader=device->m_shaderList[target].Ptr(name);
    if(!shader){shader=device->m_shaderList[target].New(name,0,0);shader->m_target=target;shader->m_code.SetCount(uint32_t(size));std::memcpy(shader->m_code.m_data,code,size);}
    ++shader->m_refCount;return shader;
}
}
bool GxSurface::Ready(){
    if(m_attempted){const bool ready=m_vertex&&m_pixel&&m_pixelCube&&m_device==g_theGxDevicePtr&&m_vertex->Valid()&&m_pixel->Valid()&&m_pixelCube->Valid();if(ready)m_error.clear();return ready;}
    m_attempted=true;m_device=g_theGxDevicePtr;
    if(!m_device||(m_device->DeviceApi()!=GxApi_D3d9&&m_device->DeviceApi()!=GxApi_D3d9Ex&&m_device->DeviceApi()!=GxApi_Vulkan)){m_error="M3 standard-layer shaders require a supported Gx backend";return false;}
    m_vertex=Create(m_device,GxSh_Vertex,"ASECM3_StandardSurface_VS_v7",surfaceVS,sizeof(surfaceVS));
    m_pixel=Create(m_device,GxSh_Pixel,"ASECM3_StandardSurface_PS_v7",surfacePS,sizeof(surfacePS));
    m_pixelCube=Create(m_device,GxSh_Pixel,"ASECM3_StandardSurface_PS_Cube_v7",surfacePSCube,sizeof(surfacePSCube));
    m_distortionVertex=Create(m_device,GxSh_Vertex,"ASECM3_Distortion_VS_v2",distortionVS,sizeof(distortionVS));
    m_distortionPixel=Create(m_device,GxSh_Pixel,"ASECM3_Distortion_PS_v2",distortionPS,sizeof(distortionPS));
    if(!m_vertex->Valid()||!m_pixel->Valid()||!m_pixelCube->Valid()){m_error="Native M3 standard-layer shader creation failed";return false;}
    return true;
}
GxSurface::~GxSurface(){
    if(m_device!=g_theGxDevicePtr)return;
    if(m_vertex)m_device->ShaderDestroy(m_vertex);
    if(m_pixel)m_device->ShaderDestroy(m_pixel);
    if(m_pixelCube)m_device->ShaderDestroy(m_pixelCube);
    if(m_distortionVertex)m_device->ShaderDestroy(m_distortionVertex);
    if(m_distortionPixel)m_device->ShaderDestroy(m_distortionPixel);
    if(m_screen)m_device->TexDestroy(m_screen);
}
bool GxSurface::Draw(const SceneRegion& region,const Material& material,const MaterialPass& pass,
                     const MaterialEvaluator& values,const C3Vector& eye,const C3Vector& center,
                     float scale,bool lit,const std::function<GxDDS*(const std::string&)>& texture,const GxGeometryTransform* geometry,const SceneEnvironment* environment){
    if(material.type!=1||!Ready())return false;
    Prof prof;prof.Mark();
    float constants[PixelVectors*4]={};
    const auto flags=uint32_t(Value(material.record,"flags",0));
    // The native camera fade makes an otherwise opaque player material use
    // alpha blending. Keep its depth writes enabled so overlapping M3 regions
    // still occlude one another when transparent packet sorting is imperfect.
    // Materials authored as transparent remain depth-write disabled.
    const bool fadeFromOpaque=!pass.blend&&environment&&environment->opacity<.99999f;
    const uint32_t blend=fadeFromOpaque?1:pass.blend;
    constants[0]=pass.opacity*(environment?environment->opacity:1);constants[1]=lit&&!(flags&16)?1.0f:0.0f;
    if(environment){
        const auto put=[&](unsigned reg,const C3Vector& value,float w){auto* p=constants+reg*4;p[0]=value.x;p[1]=value.y;p[2]=value.z;p[3]=w;};
        put(74,environment->ambient,1);put(75,environment->diffuse,0);put(76,environment->specular,float(environment->pointCount));put(77,environment->towardSun,environment->clipEnabled?1.0f:0.0f);
        std::copy(environment->clipPlane.begin(),environment->clipPlane.end(),constants+92*4);
        auto* fog=constants+78*4;fog[0]=environment->fogStart;fog[1]=environment->fogEnabled?1/(environment->fogEnd-environment->fogStart):0;
        fog[2]=std::clamp(environment->fogDensity,0.0f,1.0f);fog[3]=environment->fogDistanceScale;
        C3Vector color=environment->fogColor;
        if(blend==2||blend==3)color=C3Vector(0,0,0);else if(blend==4)color=C3Vector(1,1,1);else if(blend==5)color=C3Vector(.5f,.5f,.5f);
        put(79,color,environment->fogEnabled&&!(flags&4)?1.0f:0.0f);
        for(unsigned i=0;i<environment->pointCount&&i<4;++i){const auto& p=environment->points[i];put(80+i*3,p.position,0);put(81+i*3,p.color,0);put(82+i*3,p.attenuation,0);}
    }
    constants[2]=float(std::min(255.0,Value(material.record,"alpha_test_threshold",0))/255.0);
    constants[3]=float(Value(material.record,"hdr_emis",1));
    constants[4]=float(Value(material.record,"blend_mode_emis1",0));constants[5]=float(Value(material.record,"blend_mode_emis2",0));
    constants[8]=0.15f;constants[9]=0.45f;constants[10]=0.95f;constants[11]=1; // explicit preview team tint
    constants[12]=(flags&1)?1.0f:0.0f;constants[13]=(flags&2)?1.0f:0.0f;
    constants[14]=float(Value(material.record,"specularity",20));constants[15]=float(Value(material.record,"hdr_spec",1));constants[7]=float(Value(material.record,"spec_mode",0));
    CGxTex* native[LayerCount]={};GxDDS* owners[LayerCount]={};uint32_t wraps[LayerCount]={};
    bool cube=false;
    uint32_t channelSource[4]={}; // nonzero: generated source written into that UV channel
    unsigned spareChannel=region.vertices.empty()?4u:unsigned(std::min<size_t>(4,region.vertices.front().uv.size()/2));
    const auto channelOf=[&](uint32_t source)->int{
        const int direct=UV(source);if(direct>=0)return direct;
        if(source==6)return 0; // animated particle UVs outside a particle system read UV0
        if(!GeneratedUV(source))return -1;
        for(unsigned i=0;i<4;++i)if(channelSource[i]==source)return int(i);
        if(spareChannel>=4)return -1;
        channelSource[spareChannel]=source;return int(spareChannel++);
    };
    for(unsigned slot=0;slot<LayerCount;++slot){
        if((slot==9||slot==10)&&constants[1]<.5f)continue; // unlit passes do not consume specular inputs
        if(slot==12&&constants[16+11*20]<.5f)continue;
        const MaterialLayer* layer=nullptr;for(const auto& candidate:material.layers)if(candidate.role==roles[slot]){layer=&candidate;break;}
        if(!layer)continue;
        const auto layerFlags=uint32_t(Value(layer->record,"flags",0));const bool color=(layerFlags&0x400)!=0;
        if(!color&&layer->image.empty())continue;
        auto source=uint32_t(Value(layer->record,"uv_source",0));
        if(slot==11&&source==8)source=3; // sphere environment uses the existing sphere projection
        const int channel=slot==11&&(source==2||source==3)?-1:channelOf(source);
        const bool spare=channel>=0&&channelSource[channel]!=0;
        const bool generated=slot==11&&(source==2||source==3);
        if(slot==11&&!color&&source==2)cube=true;
        if(slot==8&&(color||channel!=0)){m_error="Normal layer requires a BC3N bitmap on the source tangent UV0";return false;}
        if(channel<0&&!color&&!generated){m_error="Standard layer uses an unsupported generated UV source";return false;}
        if(!color&&!generated&&!spare&&!region.vertices.empty()&&size_t(channel*2+1)>=region.vertices.front().uv.size()){m_error="Standard layer references an absent source UV channel";return false;}
        const unsigned base=16+slot*20;float* p=constants+base;
        p[0]=1;p[1]=color?0.0f:1.0f;p[2]=float(Value(layer->record,"color_channels",0));const float codes[]={0,1,9,10};p[3]=slot==11?(generated?float(source):codes[std::max(0,channel)]):float(std::max(0,channel));
        if(p[2]<0||p[2]>6){m_error="Standard layer has an unknown color-channel operation";return false;}
        const auto rgba=values.Sample(layer->record,"color_value",4,{255,255,255,255});for(unsigned i=0;i<4;++i)p[4+i]=rgba[i]/255.0f;
        p[8]=values.Scalar(layer->record,"color_multiply",1);p[9]=values.Scalar(layer->record,"color_add",0);p[10]=values.Scalar(layer->record,"color_brightness",1);
        p[11]=LayerClampsColor(layer->record)?1.0f:0.0f;
        if(slot==8&&(p[8]!=1||p[9]!=0||p[10]!=1)){m_error="Normal layer color operations are not implemented";return false;}
        const auto tiling=values.Sample(layer->record,"uv_tiling",1,{1,1,0,0}),offset=values.Sample(layer->record,"uv_offset",1,{}),angle=values.Sample(layer->record,"uv_angle",2,{});
        p[12]=tiling[0];p[13]=tiling[1];p[14]=offset[0];p[15]=offset[1];p[16]=std::cos(angle[2]);p[17]=std::sin(angle[2]);
        if(slot==11){
            constants[69*4]=float(Value(material.record,"blend_mode_layer",0));constants[69*4+1]=float(Value(material.record,"hdr_envi_const",1));
            constants[69*4+2]=float(Value(layer->record,"fresnel_min",0));constants[69*4+3]=float(Value(layer->record,"fresnel_max_offset",1));
            p[18]=float(Value(layer->record,"fresnel_type",0));p[19]=float(Value(layer->record,"fresnel_exponent",4));
            if(constants[69*4]>3||constants[69*4+1]<0||p[18]>2||(p[18]>0&&(p[19]<0||(layerFlags&0x3c000)))||Value(material.record,"hdr_envi_diff",0)!=0||Value(material.record,"hdr_envi_spec",0)!=0){m_error="Unsupported environment blend/HDR/Fresnel controls";return false;}
            if(cube&&(tiling[0]!=1||tiling[1]!=1||offset[0]!=0||offset[1]!=0||angle[0]!=0||angle[1]!=0||angle[2]!=0)){m_error="Cube environment UV transforms are not implemented";return false;}
        }
        if(!color){
            owners[slot]=texture(layer->image);
            if(!owners[slot])return false; // progressive texture loading
            if(owners[slot]->Image().faces!=(slot==11&&cube?6u:1u)){m_error="Environment/2D sampler disagrees with source texture face count";return false;}
            if(slot==8&&owners[slot]->Image().format!=DDSFormat::BC3){m_error="Normal texture is not BC3/DXT5N";return false;}
            if(slot==8&&!std::any_of(region.vertices.begin(),region.vertices.end(),[](const auto& v){return std::abs(v.tangentSign)>.5f;})){m_error="Normal layer has no valid source tangent basis";return false;}
            wraps[slot]=layerFlags&12;
            for(unsigned i=0;i<slot;++i)if(owners[i]==owners[slot]&&wraps[i]!=wraps[slot]){m_error="Shared texture requires distinct sampler wrap states";return false;}
            owners[slot]->SetWrap((layerFlags&4)!=0,(layerFlags&8)!=0);native[slot]=owners[slot]->Get();
            if(!native[slot])return false;
        }
    }
    if(constants[16+9*20]>.5f&&(constants[14]<0||constants[15]<0||constants[7]>1)){m_error="Invalid or unsupported specular controls";return false;}
    constants[6]=constants[16+2]==1?1.0f:0.0f;
    // Unknown layer modes stay on the diagnostic/initial surface path.
    if(constants[4]>4||constants[5]>4){m_error="Unimplemented emissive layer blend mode";return false;}
    prof.Mark();
    float savedVS[16],savedPS[PixelShadowRows*4];
    std::memcpy(savedVS,m_device->ShaderConstantsLock(GxSh_Vertex),sizeof(savedVS));
    std::memcpy(savedPS,m_device->ShaderConstantsLock(GxSh_Pixel),sizeof(savedPS));
    EGxRenderState states[LayerCount+5]={GxRs_VertexShader,GxRs_PixelShader};
    for(unsigned i=0;i<LayerCount;++i)states[i+2]=static_cast<EGxRenderState>(GxRs_Texture0+i);
    states[LayerCount+2]=GxRs_BlendingMode;states[LayerCount+3]=GxRs_DepthWrite;states[LayerCount+4]=GxRs_Culling;
    uintptr_t savedStates[LayerCount+5];for(unsigned i=0;i<LayerCount+5;++i)savedStates[i]=reinterpret_cast<uintptr_t>(m_device->m_appRenderStates[states[i]].m_value.m_data.p);
    m_device->RsPush();
    struct Restore{CGxDevice* d;const float* vs;const float* ps;const EGxRenderState* states;const uintptr_t* saved;uint64_t& checks;uint64_t& failures;
        ~Restore(){d->RsPop();d->ShaderConstantsSet(GxSh_Vertex,0,vs,4);d->ShaderConstantsSet(GxSh_Pixel,0,ps,PixelShadowRows);
            bool good=std::memcmp(vs,d->ShaderConstantsLock(GxSh_Vertex),16*sizeof(float))==0&&std::memcmp(ps,d->ShaderConstantsLock(GxSh_Pixel),PixelShadowRows*4*sizeof(float))==0;
            for(unsigned i=0;i<LayerCount+5;++i)good&=saved[i]==reinterpret_cast<uintptr_t>(d->m_appRenderStates[states[i]].m_value.m_data.p);
            ++checks;if(!good)++failures;
        }} restore{m_device,savedVS,savedPS,states,savedStates,m_stateRestores,m_stateFailures};
    const auto& view=m_device->m_xforms[GxXform_View];const auto matrix=view.m_mtx[view.m_level]*m_device->m_projNative;
    std::memcpy(constants+70*4,&view.m_mtx[view.m_level],16*sizeof(float));
    m_device->ShaderConstantsSet(GxSh_Vertex,0,reinterpret_cast<const float*>(&matrix),4);
    m_device->ShaderConstantsSet(GxSh_Pixel,PixelBase,constants,PixelVectors);
    m_device->RsSet(GxRs_VertexShader,m_vertex);m_device->RsSet(GxRs_PixelShader,cube?m_pixelCube:m_pixel);
    for(unsigned i=0;i<LayerCount;++i)m_device->RsSet(static_cast<EGxRenderState>(GxRs_Texture0+i),native[i]);
    const int blends[]={GxBlend_Opaque,GxBlend_Alpha,GxBlend_NoAlphaAdd,GxBlend_Add,GxBlend_Mod,GxBlend_Mod2x};
    if(blend>=6)return false;
    m_device->RsSet(GxRs_BlendingMode,blends[blend]);m_device->RsSet(GxRs_DepthWrite,blend==0||fadeFromOpaque?1:0);m_device->RsSet(GxRs_Culling,flags&8?0:(geometry&&geometry->CullMirrored()?2:1));
    const CGxVertexAttrib attribs[]={{GxVA_Position,4,0,76},{GxVA_Normal,4,12,76},{GxVA_Color0,0,24,76},{GxVA_TexCoord0,3,28,76},{GxVA_TexCoord1,3,36,76},{GxVA_TexCoord2,3,44,76},{GxVA_TexCoord3,3,52,76},{GxVA_TexCoord4,4,60,76},{GxVA_TexCoord5,6,72,76}};
    if(region.indices.empty())return true;
    const uint32_t vertexCount=uint32_t(*std::max_element(region.indices.begin(),region.indices.end()))+1;
    if(vertexCount>region.vertices.size())throw Error("M3 surface index exceeds its region vertices");
    prof.Mark();
    auto* vb=m_device->BufStream(GxPoolTarget_Vertex,sizeof(Vertex),vertexCount);
    // Build in cached memory: the stream buffer is write-combined host memory, where the read-modify-write steps below
    // (GxFormatColor, the geometry transforms) cost milliseconds per draw.
    std::vector<Vertex> staged(vertexCount);Vertex* vertices=staged.data();
    for(uint32_t i=0;i<vertexCount;++i){const auto& source=region.vertices[i];auto& v=vertices[i];v.p=(C3Vector(source.position.x,source.position.y,source.position.z)-center)*scale-eye;v.n={source.normal.x,source.normal.y,source.normal.z};v.c.r=source.color[0];v.c.g=source.color[1];v.c.b=source.color[2];v.c.a=source.color[3];GxFormatColor(v.c);v.tangent={source.tangent.x,source.tangent.y,source.tangent.z};v.sign=source.tangentSign;if(geometry){v.p=geometry->Position(C3Vector(source.position.x,source.position.y,source.position.z));v.n=geometry->Normal(v.n);v.tangent=geometry->Tangent(v.tangent,v.n);if(geometry->Mirrored())v.sign=-v.sign;}
        for(unsigned u=0;u<4;++u)v.uv[u]=u*2+1<source.uv.size()?C2Vector(source.uv[u*2],source.uv[u*2+1]):C2Vector(0,0);
        for(unsigned u=0;u<4;++u)if(channelSource[u])v.uv[u]=GeneratedUVValue(channelSource[u],C3Vector(source.position.x,source.position.y,source.position.z),v.p,matrix);}
    std::memcpy(m_device->BufLock(vb),staged.data(),size_t(vertexCount)*sizeof(Vertex));
    GxBufUnlock(vb,0);m_verticesPacked+=vertexCount;
    const uint16_t* drawIndices=region.indices.data();std::vector<uint16_t> sortedIndices;size_t drawIndexCount=region.indices.size();
    if(blend==1&&material.name=="Void_Cape"&&region.indices.size()>3&&region.indices.size()%3==0){
        // The authored cloak alpha is soft. Sort clipped portions to improve
        // blend order when large triangles cross the near plane. A triangle
        // that degenerates at the camera/near-plane boundary gets its own
        // conservative estimate; it must not disable sorting for the whole
        // cape. This is ordering only: the device remains the sole frustum
        // clipper, so CPU estimates can never delete model geometry.
        struct Triangle{float depth;std::array<uint16_t,3> indices;};
        const auto& view=m_device->m_xforms[GxXform_View];
        const C44Matrix viewProjection=view.m_mtx[view.m_level]*m_device->m_projNative;
        std::vector<Triangle> triangles;triangles.reserve(region.indices.size()/3);
        for(size_t i=0;i+2<region.indices.size();i+=3){
            const auto a=region.indices[i],b=region.indices[i+1],c=region.indices[i+2];
            float depth=0;bool visible=false;
            if(!ClippedTriangleDepth(vertices[a].p,vertices[b].p,vertices[c].p,viewProjection,depth,visible)||!visible)
                depth=ApproximateTriangleDepth(vertices[a].p,vertices[b].p,vertices[c].p,viewProjection);
            triangles.push_back({depth,{a,b,c}});
        }
        std::stable_sort(triangles.begin(),triangles.end(),[](const Triangle& a,const Triangle& b){return a.depth>b.depth;});
        sortedIndices.reserve(region.indices.size());
        for(const auto& triangle:triangles)sortedIndices.insert(sortedIndices.end(),triangle.indices.begin(),triangle.indices.end());
        drawIndices=sortedIndices.data();drawIndexCount=sortedIndices.size();
    }
    prof.Mark();
    for(size_t start=0;start<drawIndexCount;start+=60000){
        const uint32_t count=uint32_t(std::min(size_t(60000),drawIndexCount-start));
        auto* ib=m_device->BufStream(GxPoolTarget_Index,sizeof(uint16_t),count);
        auto* indices=reinterpret_cast<uint16_t*>(m_device->BufLock(ib));std::copy_n(drawIndices+start,count,indices);
        GxBufUnlock(ib,0);m_device->PrimVertexFormat(vb,attribs,9);m_device->PrimVertexMask((1<<GxVA_Position)|(1<<GxVA_Normal)|(1<<GxVA_Color0)|(1<<GxVA_TexCoord0)|(1<<GxVA_TexCoord1)|(1<<GxVA_TexCoord2)|(1<<GxVA_TexCoord3)|(1<<GxVA_TexCoord4)|(1<<GxVA_TexCoord5));m_device->PrimIndexPtr(ib);
        CGxBatch draw;draw.m_primType=GxPrim_Triangles;draw.m_start=0;draw.m_count=count;draw.m_minIndex=0;draw.m_maxIndex=vertexCount-1;m_device->Draw(&draw,1);++m_draws;m_indicesSubmitted+=count;
        if(environment){++m_worldDraws;if(constants[79*4+3]>.5f)++m_fogDraws;if(environment->pointCount&&constants[1]>.5f)++m_pointDraws;}
        for(const auto* t:native)if(t){++m_texturedDraws;break;}
        if(native[8])++m_normalDraws;
        if(constants[1]>.5f&&constants[16+9*20]>.5f&&constants[15]>0)++m_specularDraws;
        if(constants[16+11*20]>.5f){++m_environmentDraws;if(cube)++m_cubeDraws;else if(constants[16+11*20+1]>.5f&&constants[16+11*20+3]==3)++m_sphereDraws;}
    }
    prof.Mark();
    m_error.clear();return true;
}
}

namespace ASECM3::Client {
namespace {
void NoTexels(EGxTexCommand,uint32_t,uint32_t,uint32_t,uint32_t,void*,uint32_t&,const void*&){}
}
// A render-target texture that holds a copy of the backbuffer, rounded up to powers of two like the engine's FFX targets.
bool GxSurface::EnsureScreen(){
    CRect rect;m_device->CapsWindowSize(rect);
    const uint32_t width=uint32_t(std::max(1.0f,rect.maxX-rect.minX)),height=uint32_t(std::max(1.0f,rect.maxY-rect.minY));
    if(m_screen&&width==m_screenWidth&&height==m_screenHeight)return true;
    if(m_screen){m_device->TexDestroy(m_screen);m_screen=nullptr;}
    uint32_t texWidth=8,texHeight=8;while(texWidth<width)texWidth*=2;while(texHeight<height)texHeight*=2;
    EGxTexFormat format=GxTex_Unknown;
    if(m_device->Caps().m_rttFormat[GxTex_Argb8888])format=GxTex_Argb8888;
    else if(m_device->Caps().m_rttFormat[GxTex_Abgr8888])format=GxTex_Abgr8888;
    if(format==GxTex_Unknown){m_error="No render-target format for distortion";return false;}
    CGxTexFlags flags(GxTex_Linear,0,0,0,0,1,1,1,0,0);
    if(!m_device->TexCreate(GxTex_2d,texWidth,texHeight,1,format,format,flags,nullptr,&NoTexels,"ASECM3_Screen",m_screen)||!m_screen){m_screen=nullptr;m_error="Cannot create the distortion scene texture";return false;}
    m_screenWidth=width;m_screenHeight=height;m_screenTexWidth=texWidth;m_screenTexHeight=texHeight;m_copyFrame=-1;return true;
}
bool GxSurface::DrawDistortion(const SceneRegion& region,const Material& material,const MaterialPass& pass,
                     const MaterialEvaluator& values,const C3Vector& eye,const C3Vector& center,
                     float scale,const std::function<GxDDS*(const std::string&)>& texture,const GxGeometryTransform* geometry,const SceneEnvironment* environment){
    if(material.type!=2||!Ready()||!m_distortionVertex||!m_distortionPixel||!m_distortionVertex->Valid()||!m_distortionPixel->Valid())return false;
    const MaterialLayer *normalLayer=nullptr,*strengthLayer=nullptr;
    for(const auto& layer:material.layers){if(layer.role=="layer_norm")normalLayer=&layer;else if(layer.role=="layer_strength")strengthLayer=&layer;}
    if(!normalLayer||!strengthLayer||normalLayer->image.empty()||strengthLayer->image.empty()){m_error="Distortion material lacks its normal or strength bitmap";return false;}
    GxDDS* normalTexture=texture(normalLayer->image);GxDDS* strengthTexture=texture(strengthLayer->image);
    if(!normalTexture||!strengthTexture)return false; // progressive texture loading
    if(normalTexture->Image().faces!=1||strengthTexture->Image().faces!=1){m_error="Distortion layers must be 2D bitmaps";return false;}
    if(region.indices.empty())return true;
    if(!EnsureScreen())return false;
    const float strengthFactor=float(values.Scalar(material.record,"strength_factor",5));
    constexpr float displacementScale=0.02f; // screen fraction per unit of strength*normal; tuned by eye, SC2's units are not known
    // Project the mesh to find the pixels it covers, plus what the bending can pull in. A mesh off screen draws nothing.
    int32_t left=0,top=0,right=0,bottom=0;
    {
        const auto& view=m_device->m_xforms[GxXform_View];const C44Matrix viewProjection=view.m_mtx[view.m_level]*m_device->m_projNative;
        float lowX=1e30f,lowY=1e30f,highX=-1e30f,highY=-1e30f;bool behind=false;
        for(const auto index:region.indices){
            const auto& source=region.vertices[index];
            C3Vector p=geometry?geometry->Position(C3Vector(source.position.x,source.position.y,source.position.z)):(C3Vector(source.position.x,source.position.y,source.position.z)-center)*scale-eye;
            C4Vector clip;clip.x=p.x;clip.y=p.y;clip.z=p.z;clip.w=1;clip=clip*viewProjection;
            if(!(clip.w>1e-6f)){behind=true;break;}
            const float x=(clip.x/clip.w*.5f+.5f)*float(m_screenWidth),y=(.5f-clip.y/clip.w*.5f)*float(m_screenHeight);
            lowX=std::min(lowX,x);highX=std::max(highX,x);lowY=std::min(lowY,y);highY=std::max(highY,y);
        }
        if(behind){lowX=0;lowY=0;highX=float(m_screenWidth);highY=float(m_screenHeight);} // crossing the camera plane: be safe
        const float reach=std::fabs(strengthFactor)*displacementScale*float(std::max(m_screenWidth,m_screenHeight))+4.0f;
        left=int32_t(std::max(0.0f,std::floor(lowX-reach)));top=int32_t(std::max(0.0f,std::floor(lowY-reach)));
        right=int32_t(std::min(float(m_screenWidth),std::ceil(highX+reach)));bottom=int32_t(std::min(float(m_screenHeight),std::ceil(highY+reach)));
        if(right<=left||bottom<=top)return true; // nothing of it is on screen
    }
    // One copy serves every distortion of a frame that falls inside it; a larger need replaces it with the union.
    if(m_device->m_frameCount!=m_copyFrame){m_copyFrame=m_device->m_frameCount;m_copyLeft=m_copyTop=m_copyRight=m_copyBottom=0;}
    if(!(left>=m_copyLeft&&top>=m_copyTop&&right<=m_copyRight&&bottom<=m_copyBottom)){
        if(m_copyRight>m_copyLeft){left=std::min(left,m_copyLeft);top=std::min(top,m_copyTop);right=std::max(right,m_copyRight);bottom=std::max(bottom,m_copyBottom);}
        const C2iVector origin{left,top},extent{right-left,bottom-top};
        if(!GxTexCopy(m_screen,origin,origin,extent,0,0)){m_error="Cannot copy the scene behind a distortion";return false;}
        m_copyLeft=left;m_copyTop=top;m_copyRight=right;m_copyBottom=bottom;++m_distortionCopies;
    }
    float constants[9*4]={};
    constants[0]=pass.opacity*(environment?environment->opacity:1);
    constants[1]=strengthFactor;
    constants[2]=1.0f/m_screenTexWidth;constants[3]=1.0f/m_screenTexHeight;
    constants[4]=displacementScale;
    constants[5]=normalTexture->Image().format==DDSFormat::BC3?1.0f:0.0f;
    constants[24]=float(m_screenWidth);constants[25]=float(m_screenHeight);
    const auto fill=[&](const MaterialLayer& layer,float* out,float* rotation,float& source){
        const int channel=UV(uint32_t(Value(layer.record,"uv_source",0)));source=float(std::max(0,channel));
        const auto tiling=values.Sample(layer.record,"uv_tiling",1,{1,1,0,0}),offset=values.Sample(layer.record,"uv_offset",1,{}),angle=values.Sample(layer.record,"uv_angle",2,{});
        out[0]=tiling[0];out[1]=tiling[1];out[2]=offset[0];out[3]=offset[1];rotation[0]=std::cos(angle[2]);rotation[1]=std::sin(angle[2]);
    };
    fill(*normalLayer,constants+8,constants+12,constants[6]);
    fill(*strengthLayer,constants+16,constants+20,constants[7]);
    constants[28]=values.Scalar(strengthLayer->record,"color_multiply",1);constants[29]=values.Scalar(strengthLayer->record,"color_add",0);
    constants[30]=values.Scalar(strengthLayer->record,"color_brightness",1);constants[31]=float(Value(strengthLayer->record,"color_channels",0));
    constants[32]=float(Value(strengthLayer->record,"fresnel_type",0));constants[33]=float(Value(strengthLayer->record,"fresnel_exponent",4));
    constants[34]=float(Value(strengthLayer->record,"fresnel_min",0));constants[35]=float(Value(strengthLayer->record,"fresnel_max_offset",1));
    const auto normalFlags=uint32_t(Value(normalLayer->record,"flags",0)),strengthFlags=uint32_t(Value(strengthLayer->record,"flags",0));
    normalTexture->SetWrap((normalFlags&4)!=0,(normalFlags&8)!=0);strengthTexture->SetWrap((strengthFlags&4)!=0,(strengthFlags&8)!=0);
    CGxTex* const normalNative=normalTexture->Get();CGxTex* const strengthNative=strengthTexture->Get();
    if(!normalNative||!strengthNative)return false;
    float savedVS[16],savedPS[PixelShadowRows*4];
    std::memcpy(savedVS,m_device->ShaderConstantsLock(GxSh_Vertex),sizeof(savedVS));
    std::memcpy(savedPS,m_device->ShaderConstantsLock(GxSh_Pixel),sizeof(savedPS));
    EGxRenderState states[LayerCount+5]={GxRs_VertexShader,GxRs_PixelShader};
    for(unsigned i=0;i<LayerCount;++i)states[i+2]=static_cast<EGxRenderState>(GxRs_Texture0+i);
    states[LayerCount+2]=GxRs_BlendingMode;states[LayerCount+3]=GxRs_DepthWrite;states[LayerCount+4]=GxRs_Culling;
    uintptr_t savedStates[LayerCount+5];for(unsigned i=0;i<LayerCount+5;++i)savedStates[i]=reinterpret_cast<uintptr_t>(m_device->m_appRenderStates[states[i]].m_value.m_data.p);
    m_device->RsPush();
    struct Restore{CGxDevice* d;const float* vs;const float* ps;const EGxRenderState* states;const uintptr_t* saved;uint64_t& checks;uint64_t& failures;
        ~Restore(){d->RsPop();d->ShaderConstantsSet(GxSh_Vertex,0,vs,4);d->ShaderConstantsSet(GxSh_Pixel,0,ps,PixelShadowRows);
            bool good=std::memcmp(vs,d->ShaderConstantsLock(GxSh_Vertex),16*sizeof(float))==0&&std::memcmp(ps,d->ShaderConstantsLock(GxSh_Pixel),PixelShadowRows*4*sizeof(float))==0;
            for(unsigned i=0;i<LayerCount+5;++i)good&=saved[i]==reinterpret_cast<uintptr_t>(d->m_appRenderStates[states[i]].m_value.m_data.p);
            ++checks;if(!good)++failures;
        }} restore{m_device,savedVS,savedPS,states,savedStates,m_stateRestores,m_stateFailures};
    const auto& view=m_device->m_xforms[GxXform_View];const auto matrix=view.m_mtx[view.m_level]*m_device->m_projNative;
    m_device->ShaderConstantsSet(GxSh_Vertex,0,reinterpret_cast<const float*>(&matrix),4);
    m_device->ShaderConstantsSet(GxSh_Pixel,PixelBase,constants,9);
    m_device->RsSet(GxRs_VertexShader,m_distortionVertex);m_device->RsSet(GxRs_PixelShader,m_distortionPixel);
    for(unsigned i=0;i<LayerCount;++i)m_device->RsSet(static_cast<EGxRenderState>(GxRs_Texture0+i),i==0?m_screen:i==1?normalNative:i==2?strengthNative:nullptr);
    const uint32_t flags=uint32_t(Value(material.record,"flags",0));
    m_device->RsSet(GxRs_BlendingMode,GxBlend_Alpha);m_device->RsSet(GxRs_DepthWrite,0);m_device->RsSet(GxRs_Culling,flags&8?0:(geometry&&geometry->CullMirrored()?2:1));
    const CGxVertexAttrib attribs[]={{GxVA_Position,4,0,76},{GxVA_Normal,4,12,76},{GxVA_Color0,0,24,76},{GxVA_TexCoord0,3,28,76},{GxVA_TexCoord1,3,36,76},{GxVA_TexCoord2,3,44,76},{GxVA_TexCoord3,3,52,76},{GxVA_TexCoord4,4,60,76},{GxVA_TexCoord5,6,72,76}};
    const uint32_t vertexCount=uint32_t(*std::max_element(region.indices.begin(),region.indices.end()))+1;
    if(vertexCount>region.vertices.size())throw Error("M3 distortion index exceeds its region vertices");
    auto* vb=m_device->BufStream(GxPoolTarget_Vertex,sizeof(Vertex),vertexCount);
    // Build in cached memory: the stream buffer is write-combined host memory, where the read-modify-write steps below
    // (GxFormatColor, the geometry transforms) cost milliseconds per draw.
    std::vector<Vertex> staged(vertexCount);Vertex* vertices=staged.data();
    for(uint32_t i=0;i<vertexCount;++i){const auto& source=region.vertices[i];auto& v=vertices[i];v.p=(C3Vector(source.position.x,source.position.y,source.position.z)-center)*scale-eye;v.n={source.normal.x,source.normal.y,source.normal.z};v.c.r=source.color[0];v.c.g=source.color[1];v.c.b=source.color[2];v.c.a=source.color[3];GxFormatColor(v.c);v.tangent={source.tangent.x,source.tangent.y,source.tangent.z};v.sign=source.tangentSign;if(geometry){v.p=geometry->Position(C3Vector(source.position.x,source.position.y,source.position.z));v.n=geometry->Normal(v.n);v.tangent=geometry->Tangent(v.tangent,v.n);if(geometry->Mirrored())v.sign=-v.sign;}
        for(unsigned u=0;u<4;++u)v.uv[u]=u*2+1<source.uv.size()?C2Vector(source.uv[u*2],source.uv[u*2+1]):C2Vector(0,0);}
    std::memcpy(m_device->BufLock(vb),staged.data(),size_t(vertexCount)*sizeof(Vertex));
    GxBufUnlock(vb,0);m_verticesPacked+=vertexCount;
    for(size_t start=0;start<region.indices.size();start+=60000){
        const uint32_t count=uint32_t(std::min(size_t(60000),region.indices.size()-start));
        auto* ib=m_device->BufStream(GxPoolTarget_Index,sizeof(uint16_t),count);
        auto* indices=reinterpret_cast<uint16_t*>(m_device->BufLock(ib));std::copy_n(region.indices.data()+start,count,indices);
        GxBufUnlock(ib,0);m_device->PrimVertexFormat(vb,attribs,9);m_device->PrimVertexMask((1<<GxVA_Position)|(1<<GxVA_Normal)|(1<<GxVA_Color0)|(1<<GxVA_TexCoord0)|(1<<GxVA_TexCoord1)|(1<<GxVA_TexCoord2)|(1<<GxVA_TexCoord3)|(1<<GxVA_TexCoord4)|(1<<GxVA_TexCoord5));m_device->PrimIndexPtr(ib);
        CGxBatch draw;draw.m_primType=GxPrim_Triangles;draw.m_start=0;draw.m_count=count;draw.m_minIndex=0;draw.m_maxIndex=vertexCount-1;m_device->Draw(&draw,1);++m_draws;++m_distortionDraws;m_indicesSubmitted+=count;
    }
    m_error.clear();return true;
}
}
