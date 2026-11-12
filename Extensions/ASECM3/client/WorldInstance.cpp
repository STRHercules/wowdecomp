#include "WorldInstance.h"
#include "GxGeometryTransform.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include <cmath>
#include <algorithm>
namespace ASECM3::Client {
void WorldInstance::SetWorldTransform(const C44Matrix& matrix){
    GxGeometryTransform validate(matrix);Matrix source;
    const auto* elements=reinterpret_cast<const float*>(&matrix);
    // Engine uses row vectors; source physics uses column vectors.
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)source.At(row,col)=elements[col*4+row];
    m_renderer.SetClothWorldTransform(source);
}
WorldInstance::WorldInstance(std::shared_ptr<const Scene> source,const std::string& path):m_renderer(std::move(source),path){
    // Use the same evidence-based action mapping as the viewer. Absence of
    // Stand leaves the source bind pose; it never guesses a clip by ordinal.
    const auto stand=SelectAction(m_renderer.Actions(),0);
    if(stand>=0){m_renderer.SetSequence(stand,true);m_action=0;}
    m_hasBounds=m_renderer.FrameBounds(m_lower,m_upper);
    m_maxLower=m_lower;m_maxUpper=m_upper;
}
bool WorldInstance::Advance(double seconds,const std::function<std::vector<uint8_t>(const std::string&)>& provider){
    try{if(!std::isfinite(seconds)||seconds<0)throw Error("Invalid M3 world frame interval");
        if(provider)m_renderer.LoadNextTexture(provider);
        m_renderer.Advance(seconds);const bool hadBounds=m_hasBounds;m_hasBounds=m_renderer.FrameBounds(m_lower,m_upper);m_hasBounds=m_renderer.IncludeWorldParticleBounds(m_lower,m_upper,m_hasBounds);
        if(m_hasBounds){if(!hadBounds){m_maxLower=m_lower;m_maxUpper=m_upper;}else{
            m_maxLower.x=std::min(m_maxLower.x,m_lower.x);m_maxLower.y=std::min(m_maxLower.y,m_lower.y);m_maxLower.z=std::min(m_maxLower.z,m_lower.z);
            m_maxUpper.x=std::max(m_maxUpper.x,m_upper.x);m_maxUpper.y=std::max(m_maxUpper.y,m_upper.y);m_maxUpper.z=std::max(m_maxUpper.z,m_upper.z);}}
        m_error.clear();return true;
    }catch(const std::exception& e){m_error=e.what();return false;}
}
bool WorldInstance::SetWoWAction(uint32_t action,bool restart){
    if(action>505){m_error="WoW action must be in the range 0..505";return false;}
    if(m_action==int32_t(action)&&!restart)return true;
    try{
        if(!m_renderer.ActionError().empty())throw Error(m_renderer.ActionError());
        const auto clip=SelectBodyAction(m_renderer.Actions(),action);
        if(clip<0)throw Error("No eligible M3 source clip maps to WoW action "+std::to_string(action));
        // Persistent /dance repeats its source take, even when the HotS actor
        // originally controlled looping outside the sequence's one-shot flag.
        m_renderer.SetSequence(clip,action==69||action==0||action==4||action==5);m_action=int32_t(action);m_error.clear();return true;
    }
    catch(const std::exception& e){m_error=e.what();return false;}
}
bool WorldInstance::Bounds(const C44Matrix& matrix,WorldBounds& out,bool maximum)const{
    if(!m_hasBounds)return false;
    const auto& lower=maximum?m_maxLower:m_lower;const auto& upper=maximum?m_maxUpper:m_upper;
    const auto box=GxGeometryTransform(matrix).Bounds(CAaBox(C3Vector(lower.x,lower.y,lower.z),C3Vector(upper.x,upper.y,upper.z)));
    out.lower=box.b;out.upper=box.t;
    return true;
}
bool WorldInstance::Draw(WorldRenderContext& context,const C44Matrix& modelToCamera,const C44Matrix& projection,bool lit,const SceneEnvironment* environment,int32_t packet,const C44Matrix* worldToCamera,bool modelMirrored){
    m_drawDeferred=false;
    if(context.m_failures){m_error="M3 world pass stopped after render-state restoration failure";return false;}
    auto* device=g_theGxDevicePtr;if(!device||!device->m_context){m_drawDeferred=true;m_error.clear();return true;}
    try{
        // Normals and tangents follow the full camera-space transform. Face
        // culling parity is relative to the model transform; the camera view
        // is shared with the native M2 renderer and is already in its baseline.
        const GxGeometryTransform transform(modelToCamera,modelMirrored);
        const auto* elements=reinterpret_cast<const float*>(&projection);for(unsigned i=0;i<16;++i)if(!std::isfinite(elements[i]))throw Error("Nonfinite M3 world projection");
        {
            GxRenderGuard isolation(device,context.m_buffers,true,context.m_checks,context.m_failures);
            if(!isolation.Ready())throw Error("Could not isolate M3 world draw state");
            C44Matrix identity;identity.Identity();GxXformSet(GxXform_World,identity);GxXformSetView(identity);GxXformSetProjection(projection);
            GxRsSet(GxRs_ClipPlaneMask,0); // camera-space pixel clipping is owned by the M3 surface
            // Existing target/depth/viewport remain in place. No scene clear,
            // grid, UI camera, fit scale or preview offset participates here.
            Matrix particleCamera;
            if(worldToCamera){const auto* values=reinterpret_cast<const float*>(worldToCamera);for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)particleCamera.At(r,c)=values[c*4+r];}
            m_renderer.DrawCameraSpace(transform,lit,environment,packet,worldToCamera?&particleCamera:nullptr);
        }
        if(context.m_failures)throw Error("M3 world draw did not restore render state");
        m_error.clear();return true;
    }catch(const std::exception& e){m_error=e.what();return false;}
}
}
