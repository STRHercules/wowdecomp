#pragma once
#include "character-retarget.h"
#include "board-placement.h"

// Human Male visual alignment, not full boot-mesh/two-foot IK. Fit deck stance
// to the support foot and lift/lower ONLY the visual skeleton's root to its deck.
// The kicking foot is never pinned; source animation and native limb lengths stay.
namespace characteralignment {
using namespace characterretarget;
struct Calibration { bool ready=false; float scale=1; };
struct Result {
    bool valid=false;
    boardplacement::Fit board{};
    float rootLift=0,deckHeight=0,unshiftedSoleHeight=0,rearHorizontalError=0;
    unsigned supportAnkle=20,sourceAnkle=22;
};
// A flip deliberately separates feet and deck. Keep the last grounded visual
// calibration and source-root datum, without replanting feet onto a rotating
// deck or pinning its lowest vertex to the character every airborne frame.
struct AirAnchor {
    Result ground{};
    float bottomFromRoot=0;
    void Clear() { *this={}; }
    void Capture(const Result& fit,const skatevisual::Frame& frame,float appliedLift) {
        if (!fit.valid || !frame.pose.ground || frame.pose.airborne || frame.vertices.empty()) return;
        float bottom=INFINITY;
        for (auto point:frame.vertices) bottom=(std::min)(bottom,point.y);
        const float offset=bottom-frame.pose.position[1];
        const float stanceLift=appliedLift-fit.board.z;
        if (!std::isfinite(offset) || fabsf(offset)>5 || !std::isfinite(appliedLift) || stanceLift<-.6f || stanceLift>.75f) return;
        ground=fit; ground.rootLift=appliedLift; bottomFromRoot=offset;
    }
    Result Solve(const skatevisual::Frame& frame) const {
        if (!ground.valid || frame.vertices.empty() || !std::isfinite(frame.pose.position[1])) return {};
        auto out=ground; out.board.rootRelative=true; out.board.sourceBottomFromRoot=bottomFromRoot;
        return boardplacement::Valid(out.board)?out:Result{};
    }
};
constexpr float SoleClearance=.025f; // visual boot clearance, not physics/ground movement
inline Point SoleSample(const characterrig::Snapshot& skeleton,const Pose& pose,unsigned ankle,float along) {
    const unsigned toe=Joint(pose,ankle==20?32:33);ankle=Joint(pose,ankle);
    const auto a=skeleton.bones[ankle].pivot,t=skeleton.bones[toe].pivot;
    return Add(pose.pivots[ankle],Rotate(pose.global[ankle],
        {(t.x-a.x)*along,(t.y-a.y)*along,-a.z}));
}
inline float LowestSole(const characterrig::Snapshot& skeleton,const Pose& pose,unsigned ankle) {
    return (std::min)(SoleSample(skeleton,pose,ankle,-.4f).z,
        (std::min)(SoleSample(skeleton,pose,ankle,0).z,SoleSample(skeleton,pose,ankle,1).z));
}
inline bool DeckSurface(const skatevisual::Mesh& mesh,const skatevisual::Frame& frame,
                        float bottom,const boardplacement::Fit& fit,Point foot,float& height) {
    height=-INFINITY;
    float nearest=INFINITY,edgeHeight=0;
    if (mesh.indices.empty() || mesh.indices.size()%3 || frame.vertices.size()!=mesh.colors.size()
        || !boardplacement::Valid(fit) || !characterrig::Finite(foot) || !std::isfinite(bottom)) return false;
    for (size_t i=0;i<mesh.indices.size();i+=3) {
        const auto ai=mesh.indices[i],bi=mesh.indices[i+1],ci=mesh.indices[i+2];
        if (ai>=frame.vertices.size() || bi>=frame.vertices.size() || ci>=frame.vertices.size()) return false;
        if (mesh.colors[ai]!=0xff32859d || mesh.colors[bi]!=0xff32859d || mesh.colors[ci]!=0xff32859d) continue;
        const auto a=boardplacement::WorldPoint(frame.vertices[ai],frame,{0,0,0},bottom,fit);
        const auto b=boardplacement::WorldPoint(frame.vertices[bi],frame,{0,0,0},bottom,fit);
        const auto c=boardplacement::WorldPoint(frame.vertices[ci],frame,{0,0,0},bottom,fit);
        const float determinant=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);
        if (fabsf(determinant)<1e-7f) continue;
        const float u=((b.y-c.y)*(foot.x-c.x)+(c.x-b.x)*(foot.y-c.y))/determinant;
        const float v=((c.y-a.y)*(foot.x-c.x)+(a.x-c.x)*(foot.y-c.y))/determinant;
        const float w=1-u-v;
        if (u>=-.0001f && v>=-.0001f && w>=-.0001f)
            height=(std::max)(height,u*a.z+v*b.z+w*c.z);
        // An ankle can sit just outside the deck while the sole overlaps its
        // rounded nose. Use the nearest REAL triangle edge within eight cm;
        // do not extend a plane arbitrarily or manufacture collision geometry.
        const skatevisual::Point corners[]={a,b,c,a};
        for (unsigned edge=0;edge<3;++edge) {
            const auto p=corners[edge],q=corners[edge+1];
            const float dx=q.x-p.x,dy=q.y-p.y,squared=dx*dx+dy*dy;
            if (squared<1e-8f) continue;
            const float t=(std::max)(0.f,(std::min)(1.f,((foot.x-p.x)*dx+(foot.y-p.y)*dy)/squared));
            const float distance=hypotf(p.x+t*dx-foot.x,p.y+t*dy-foot.y);
            const float z=p.z+t*(q.z-p.z);
            if (distance<nearest-1e-5f) { nearest=distance; edgeHeight=z; }
            else if (fabsf(distance-nearest)<=1e-5f) edgeHeight=(std::max)(edgeHeight,z);
        }
    }
    if (!std::isfinite(height) && nearest<=.08f) height=edgeHeight;
    return std::isfinite(height);
}
inline Point FootContact(const characterrig::Snapshot& skeleton,const Pose& pose,unsigned ankle) {
    // Human Male ankle is above its bind-ground plane. Approximate the sole at
    // that local offset, rotating it with the foot. Shoe geometry/IK comes later.
    ankle=Joint(pose,ankle);
    return Add(pose.pivots[ankle],Rotate(pose.global[ankle],{0,0,-skeleton.bones[ankle].pivot.z}));
}
inline Point WorldDirection(Point model,float facing) {
    const float c=cosf(facing),s=sinf(facing);
    return {c*model.x-s*model.y,s*model.x+c*model.y,model.z};
}
inline Result Solve(const characterrig::Snapshot& skeleton,const Pose& pose,
                    const skatevisual::Mesh& mesh,const skatevisual::Frame& frame,
                    float facing,Calibration& calibration,bool ownedOllie=false) {
    Result out{};
    if (!Supported(skeleton) || pose.count!=skeleton.count || !std::isfinite(facing)
        || ((!frame.pose.ground || frame.pose.airborne) && !ownedOllie) || frame.vertices.empty()
        || frame.vertices.size()!=mesh.colors.size()) return out;
    // Human20 / Skate22 normally supports pushing; braking can switch support.
    // Calibrate their resting spacing once
    // per J activation/epoch; never resize the board during a kick or turn.
    Point front=WorldDirection(FootContact(skeleton,pose,20),facing);
    const Point rear=WorldDirection(FootContact(skeleton,pose,22),facing);
    auto sourceFront=frame.anchors[22]; const auto sourceRear=frame.anchors[18];
    if (!characterrig::Finite(front) || !characterrig::Finite(rear)
        || !std::isfinite(sourceFront.x) || !std::isfinite(sourceFront.z)
        || !std::isfinite(sourceRear.x) || !std::isfinite(sourceRear.z)) return out;
    const float sourceDistance=hypotf(sourceFront.x-sourceRear.x,sourceFront.z-sourceRear.z)/boardplacement::MetresPerYard;
    const float humanDistance=hypotf(front.x-rear.x,front.y-rear.y);
    if (!std::isfinite(sourceDistance) || !std::isfinite(humanDistance)
        || sourceDistance<.1f || humanDistance<.1f) return out;
    Calibration next=calibration;
    if (!next.ready) {
        next.scale=(std::max)(.7f,(std::min)(1.4f,humanDistance/sourceDistance));
        next.ready=true;
    }
    float bottom=INFINITY,nearest=INFINITY;
    for (auto vertex:frame.vertices) {
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z)) return {};
        bottom=(std::min)(bottom,vertex.y);
    }
    const boardplacement::Fit raw{true,1,0,0};
    const auto sourceSupport=boardplacement::WorldPoint(sourceFront,frame,{0,0,0},bottom,raw);
    float sourceDeck=0;
    const bool frontSupported=DeckSurface(mesh,frame,bottom,raw,{sourceSupport.x,sourceSupport.y,sourceSupport.z},sourceDeck);
    const auto other=boardplacement::WorldPoint(sourceRear,frame,{0,0,0},bottom,raw);
    float otherDeck=0;
    const bool otherSupported=DeckSurface(mesh,frame,bottom,raw,{other.x,other.y,other.z},otherDeck);
    if (!frontSupported || (otherSupported && other.z-otherDeck+.05f<sourceSupport.z-sourceDeck)) {
        if (!otherSupported) return {};
        // Braking or opposite-foot pushing can leave Skate18 supporting the deck.
        // Follow the actual support foot; never pin the braking/kicking leg.
        front=rear; sourceFront=sourceRear; out.supportAnkle=22; out.sourceAnkle=18;
    }
    out.board={true,next.scale,
        front.x-next.scale*(sourceFront.x-frame.pose.position[0])/boardplacement::MetresPerYard,
        front.y+next.scale*(sourceFront.z-frame.pose.position[2])/boardplacement::MetresPerYard};
    if (!boardplacement::Valid(out.board)) return {};
    unsigned deckVertices=0;
    for (size_t i=0;i<frame.vertices.size();++i) if (mesh.colors[i]==0xff32859d) {
        const auto p=boardplacement::WorldPoint(frame.vertices[i],frame,{0,0,0},bottom,out.board);
        nearest=(std::min)(nearest,hypotf(p.x-front.x,p.y-front.y)); ++deckVertices;
    }
    if (deckVertices<3 || !std::isfinite(nearest) || nearest>.3f) return {};
    float surface=0;
    if (!DeckSurface(mesh,frame,bottom,out.board,front,surface)) return {};
    constexpr float solePoints[]={-.4f,0,1};
    for (float along:solePoints) {
        const auto sole=WorldDirection(SoleSample(skeleton,pose,out.supportAnkle,along),facing);
        float deck=0;
        if (DeckSurface(mesh,frame,bottom,out.board,sole,deck)) surface=(std::max)(surface,deck);
    }
    out.deckHeight=surface; out.unshiftedSoleHeight=LowestSole(skeleton,pose,out.supportAnkle);
    out.rootLift=surface-out.unshiftedSoleHeight+SoleClearance;
    if (!std::isfinite(out.rootLift) || out.rootLift<-.6f || out.rootLift>.75f
        || !std::isfinite(surface) || surface<0 || surface>.45f) return {};
    const auto rearBoard=boardplacement::WorldPoint(frame.anchors[out.sourceAnkle==22?18:22],frame,{0,0,0},bottom,out.board);
    const auto otherHuman=WorldDirection(FootContact(skeleton,pose,out.supportAnkle==20?22:20),facing);
    out.rearHorizontalError=hypotf(rearBoard.x-otherHuman.x,rearBoard.y-otherHuman.y);
    out.valid=true; calibration=next; return out;
}
}
