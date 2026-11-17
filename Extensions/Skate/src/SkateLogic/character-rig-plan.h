#pragma once
#include "character-rig-schema.h"
// Semantic joint labels keep the existing Human Male solver's role names, not
// its numeric layout. Actual bone numbers come from this model's M2 keys/tree.
namespace characterrig {
struct Plan {
    unsigned joint[39]{};
    Plan(){for(unsigned i=0;i<39;++i)joint[i]=i;}
    unsigned operator[](unsigned role)const{return joint[role];}
};
inline float Distance(Point a,Point b){return sqrtf((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));}
inline int Key(const Snapshot& s,unsigned key){
    if(key>=s.keyCount || s.keys[key]>=s.count)return -1;
    const unsigned bone=s.keys[key];return s.bones[bone].key==static_cast<int>(key)?static_cast<int>(bone):-1;
}
inline bool HasChild(const Snapshot& s,unsigned parent){
    for(unsigned i=0;i<s.count;++i)if(s.bones[i].parent==static_cast<int>(parent) && (s.bones[i].flags&0x287)==0x200)return true;
    return false;
}
inline int Next(const Snapshot& s,unsigned parent){
    int continuing=-1,leaf=-1;float farChain=.01f,farLeaf=.01f;
    for(unsigned i=0;i<s.count;++i)if(s.bones[i].parent==static_cast<int>(parent) && (s.bones[i].flags&0x287)==0x200){
        const float distance=Distance(s.bones[i].pivot,s.bones[parent].pivot);
        if(distance>farLeaf){farLeaf=distance;leaf=static_cast<int>(i);}
        if(HasChild(s,i) && distance>farChain){farChain=distance;continuing=static_cast<int>(i);}
    }
    return continuing>=0?continuing:leaf;
}
inline bool Discover(const Snapshot& s,Plan& out){
    if(!Valid(s))return false;
    for(unsigned i=0;i<s.count;++i)if(s.bones[i].parent>=static_cast<int>(i))return false;
    const int waist=Key(s,5),spine=Key(s,4),head=Key(s,6),root=Key(s,26);
    if(waist<0 || spine<0 || head<0 || root<0)return false;
    const int neck=s.bones[head].parent;
    if(neck<0)return false;
    const int chest=s.bones[neck].parent;
    if(chest<0 || chest==spine)return false;
    out={};out.joint[3]=waist;out.joint[2]=spine;out.joint[16]=head;
    out.joint[9]=neck;out.joint[4]=chest;out.joint[1]=root;
    unsigned actor=static_cast<unsigned>(root);
    while(s.bones[actor].parent>=0)actor=static_cast<unsigned>(s.bones[actor].parent);
    out.joint[0]=actor;
    const unsigned arms[][4]={{10,17,30,36},{11,18,31,38}};
    for(unsigned side=0;side<2;++side){
        int shoulder=Key(s,2+side),upper=Key(s,side);
        // Some Wrath rigs omit ARM_L/R while tagging the upper arm as SHOULDER.
        // The adjacent parent is the clavicle; discover it, never use race IDs.
        if(upper<0 && shoulder>=0){upper=shoulder;shoulder=s.bones[upper].parent;}
        if(shoulder<0 || upper<0)return false;
        const int elbow=Next(s,upper);if(elbow<0)return false;
        const int hand=Next(s,elbow);if(hand<0)return false;
        out.joint[arms[side][0]]=shoulder;out.joint[arms[side][1]]=upper;
        out.joint[arms[side][2]]=elbow;out.joint[arms[side][3]]=hand;
        if(s.bones[upper].parent!=shoulder || s.bones[elbow].parent!=upper || s.bones[hand].parent!=elbow)return false;
    }
    const unsigned legs[][4]={{7,14,20,32},{8,15,22,33}};
    for(unsigned side=0;side<2;++side){
        int thigh=-1;float farSide=.03f;
        for(unsigned i=0;i<s.count;++i)if(s.bones[i].parent==waist && HasChild(s,i)){
            // Native M2 is +X forward, +Y left, +Z up (Vanilla converts axes).
            const float delta=s.bones[i].pivot.y-s.bones[waist].pivot.y;
            if((delta>0)==(side==0) && fabsf(delta)>farSide){farSide=fabsf(delta);thigh=static_cast<int>(i);}
        }
        if(thigh<0)return false;
        const int knee=Next(s,thigh);if(knee<0)return false;
        const int ankle=Next(s,knee);if(ankle<0)return false;
        const int toe=Next(s,ankle);if(toe<0)return false;
        out.joint[legs[side][0]]=thigh;out.joint[legs[side][1]]=knee;
        out.joint[legs[side][2]]=ankle;out.joint[legs[side][3]]=toe;
    }
    constexpr unsigned roles[]={0,1,2,3,4,7,8,9,10,11,14,15,16,17,18,20,22,30,31,32,33,36,38};
    for(unsigned i=0;i<23;++i){
        const unsigned a=out[roles[i]];if(a>=s.count)return false;
        for(unsigned j=0;j<i;++j)if(a==out[roles[j]] && !(i==1 && j==0))return false;
    }
    const unsigned links[][2]={{3,2},{2,4},{4,9},{9,16},{10,17},{17,30},{30,36},
        {11,18},{18,31},{31,38},{7,14},{14,20},{20,32},{8,15},{15,22},{22,33}};
    for(const auto& link:links){
        const float span=Distance(s.bones[out[link[0]]].pivot,s.bones[out[link[1]]].pivot);
        // Troll Female's keyed Waist/Spine share a pivot. Its chest supplies the
        // axial bind direction; this is not a missing or zero-length limb.
        if(!std::isfinite(span) || (span<.01f && !(link[0]==3 && link[1]==2 && span<.00001f)) || span>2.5f)return false;
    }
    return true;
}
}
