#pragma once
#include "Material.h"
#include "Pose.h"
#include "BankBinding.h"
#include "Physics.h"
#include "Collision.h"
#include "Attachments.h"
#include "Effects.h"
#include "EffectBankBinding.h"
#include "EffectMaterialBinding.h"
#include <memory>
namespace ASECM3 {
class MaterialEvaluator;
struct SceneVertex {
    Vec3 position, normal;
    Vec3 tangent;
    float tangentSign=0;
    uint8_t color[4]={255,255,255,255};
    std::vector<float> uv; // REGN decompression applied, original UV channel order
};
struct SceneRegion {
    uint32_t division=0, region=0;
    std::vector<SceneVertex> vertices;
    std::vector<uint16_t> indices; // region-local, never a global palette/index guess
};
struct SceneFrame {
    Pose pose;
    // Canonical animation skeleton (external bank when bound), before rigid or
    // cloth feedback. Retained for interruptible source-local transitions.
    std::vector<LocalTransform> animationLocals;
    std::vector<SceneRegion> regions; int32_t materialGroup=-1,sourceTime=0;
};
// One owning object for original data, skeletal animation, materials and batches.
// No engine pointers, file handles, Python dependencies or on-disk conversion.
class Scene final {
    Document m_document;
    ModelData m_model;
    MaterialCatalog m_materials;
    AnimationBank m_animation;
    PhysicsData m_physics;
    CollisionData m_collision;
    AttachmentData m_attachments;
    EffectsData m_effects;
    std::vector<std::string> m_textures;
    std::vector<int32_t> m_sequenceGroups;
    std::vector<ASECM3::Animation> m_localSequences;
    std::vector<int32_t> m_localSequenceGroups;
    std::shared_ptr<const Scene> m_externalBank;
    BankBinding m_binding;
    std::string m_animationSource;
    std::shared_ptr<const Scene> m_effectBank;
    EffectBankBinding m_effectBinding;
    std::string m_effectAnimationSource;
    EffectMaterialBinding m_effectMaterials;
    std::string m_effectMaterialError;
    bool m_embeddedEffects=false;
    std::string m_effectSetupError;
    explicit Scene(Document document,bool embeddedEffects);
public:
    static std::shared_ptr<const Scene> Parse(std::vector<uint8_t> bytes,bool embeddedEffects=false);
    // Completes deferred reference validation before decoding any scene data.
    static std::shared_ptr<const Scene> FromDocument(Document document,bool embeddedEffects=false);
    std::shared_ptr<const Scene> WithAnimationBank(std::vector<uint8_t> bytes,const std::string& source,bool meshLocalTracks=false) const;
    // Explicit actor/model dependency lane. It does not select clips, merge
    // material IDs, remap bones or alter the character's skeletal animation.
    std::shared_ptr<const Scene> WithEffectAnimationBank(std::vector<uint8_t> bytes,const std::string& source) const;
    const EffectBankBinding& EffectBinding()const{return m_effectBinding;}
    const std::string& EffectAnimationSource()const{return m_effectAnimationSource;}
    const std::string& EffectSetupError()const{return m_effectSetupError;}
    const AnimationBank* EffectAnimation()const{return m_effectBank?&m_effectBank->m_animation:m_embeddedEffects?&m_animation:nullptr;}
    const std::vector<ASECM3::Animation>& EffectSequences()const{static const std::vector<ASECM3::Animation> empty;return m_effectBank?m_effectBank->m_model.animations:m_embeddedEffects?m_localSequences:empty;}
    int32_t EffectSequenceGroup(uint32_t sequence)const;
    float SampleEffectScalar(uint32_t meshEmitter,const std::string& field,int32_t group,int32_t sourceTime)const;
    const EffectMaterialBinding& EffectMaterials()const{return m_effectMaterials;}
    const std::string& EffectMaterialError()const{return m_effectMaterialError;}
    // Optional adapted material retains source ID/role ownership while allowing
    // private particle flags/UV routing. Original Scene records are never changed.
    void ApplyEffectMaterialValues(MaterialEvaluator& values,uint32_t effectSequence,uint64_t elapsedMs,uint32_t sourceMaterial=UINT32_MAX,const Material* adapted=nullptr)const;
    const TransformTrack* EffectScalarTrack(uint32_t meshEmitter,const std::string& field,int32_t group)const;
    // Borrowed exact keys, valid for this Scene's lifetime. Null means the
    // selected group has no count channel; caller retains the mesh default.
    // This resolves ownership/priority, not burst timing or actor activation.
    const Signed16Track* EffectCountTrack(uint32_t meshEmitter,const std::string& field,int32_t group)const;
    const std::string& AnimationSource() const { return m_animationSource; }
    const BankBinding& Binding() const { return m_binding; }
    const ModelData& Model() const { return m_model; }
    const MaterialCatalog& Materials() const { return m_materials; }
    const PhysicsData& Physics() const { return m_physics; }
    const CollisionData& Collision() const { return m_collision; }
    const AttachmentData& Attachments() const { return m_attachments; }
    const EffectsData& Effects() const { return m_effects; }
    const AnimationBank& Animation() const { return m_externalBank?m_externalBank->Animation():m_animation; }
    const AnimationBank& MaterialAnimation() const { return m_animation; }
    const std::vector<std::string>& Textures() const { return m_textures; }
    int32_t SequenceGroup(uint32_t sequence) const;
    // sourceTime is explicitly the M3 timeline, not a WoW action number.
    SceneFrame EvaluateGroup(int32_t group=-1,int32_t sourceTime=0,uint32_t division=0,
        const std::vector<LocalTransform>* blendFrom=nullptr,double blend=1) const;
    SceneFrame EvaluateSequence(uint32_t sequence,uint64_t elapsedMs,uint32_t division=0,
        const std::vector<LocalTransform>* blendFrom=nullptr,double blend=1) const;
};
}
