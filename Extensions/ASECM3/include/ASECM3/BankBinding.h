#pragma once
#include "Pose.h"
namespace ASECM3 {
struct BankBinding {
    std::vector<int32_t> meshToBank;
    uint32_t matchedBones=0,animatedBones=0;
    double influencedWeightCoverage=0;
    bool meshLocalTracks=false;
};
// Names and typed channel ownership are mandatory. Mesh-local mode evaluates
// catalog-declared tracks on the mesh hierarchy; default deformation transfer also validates topology.
// Bone indices and filename similarity alone never establish a binding.
BankBinding BindAnimationBank(const ModelData& mesh,const ModelData& bank,const AnimationBank& animation,bool meshLocalTracks=false);
// Transfer source deformation (animatedWorld * inverseBind) onto the mesh's
// own bind armature. Unanimated extra face/helper bones inherit the nearest
// mapped ancestor's deformation; source and mesh rest proportions stay distinct.
Pose EvaluateBoundPose(const ModelData& mesh,const ModelData& bank,const AnimationBank& animation,
    const BankBinding& binding,int32_t group,int32_t sourceTime);
Pose BindEvaluatedPose(const ModelData& mesh,const BankBinding& binding,const Pose& source);
std::vector<std::string> AnimationBankCandidates(const std::string& modelPath);
}
