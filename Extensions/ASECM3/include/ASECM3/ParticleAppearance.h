#pragma once
#include "MaterialEvaluation.h"
namespace ASECM3 {
struct ParticleVisual {
    float size=0,rotation=0;
    KeyValue color{1,1,1,1};
    float u0=0,v0=0,u1=1,v1=1;
};
// Birth-sampled source controls, independent of particle motion random state.
class ParticleAppearance final {
    KeyValue m_size{},m_rotation{},m_colors[3]{};
    float m_sizeMid=.5f,m_rotationMid=.5f,m_colorMid=.5f,m_alphaMid=.5f,m_uvMid=1;
    float m_sizeHold=0,m_rotationHold=0,m_colorHold=0,m_alphaHold=0;
    uint32_t m_sizeMode=0,m_rotationMode=0,m_colorMode=0;
    uint32_t m_columns=1,m_rows=1,m_frames[4]={};
    bool m_alpha=true;
public:
    static ParticleAppearance Capture(const MaterialRecord& source,const MaterialEvaluator& values,uint32_t seed);
    ParticleVisual At(double normalizedAge)const;
};
}
