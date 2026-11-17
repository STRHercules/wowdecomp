// Phase 4: Skate skater pose -> the local player's bone locals (docs/SKATE_INTEGRATION.md "Phase 4").
// Pure. The main thread fills a Pose before the scene animates; ModelScript::OnModelBoneLocal only calls Apply.
#pragma once
#include "SkateLogic/character-ragdoll.h"
#include "SkateLogic/rider-air-alignment.h"
#include "SkateLogic/rider-air-legs.h"
#include "SkateLogic/character-height.h"
#include <algorithm>
#include <vector>

namespace skaterig {
using characterrig::Point;
// The 23 body roles the donor posed (character-pose-scope.h BodyBones); every other bone keeps its stock animation.
constexpr unsigned BodyRoles[] = {0,1,2,3,4,7,8,9,10,11,14,15,16,17,18,20,22,30,31,32,33,36,38};
constexpr unsigned BodyCount = sizeof(BodyRoles) / sizeof(BodyRoles[0]);

// One M2 bone as the model's data has it (M2CompBone).
struct SourceBone { int32_t key = -1, parent = -1; uint32_t flags = 0; Point pivot{}; uint32_t rotationSequences = 1; };

// Builds the donor's skeleton snapshot. Rigs over 256 bones (Retail Dracthyr) keep the first 256 bones whose
// parent is kept; map[] gives each kept bone's model index. A body bone past that cut fails Discover (stock pose).
inline bool Skeleton(const SourceBone* bones, uint32_t count, const uint16_t* keys, uint32_t keyCount,
                     characterrig::Snapshot& out, std::vector<uint32_t>& map) {
    out = {}; map.clear();
    out.version = 264;
    std::vector<int32_t> compact(count, -1);
    for (uint32_t i = 0; i < count && map.size() < characterrig::MaxBones; ++i) {
        const int32_t parent = bones[i].parent;
        if (parent >= static_cast<int32_t>(i)) return false;
        if (parent >= 0 && compact[parent] < 0) continue;
        auto& bone = out.bones[map.size()];
        bone.key = bones[i].key >= 0 && bones[i].key < static_cast<int32_t>(characterrig::MaxKeys) ? bones[i].key : -1;
        bone.parent = parent < 0 ? -1 : compact[parent];
        // Discover's "plain animated bone" test is (flags & 0x287) == 0x200. Retail rigs also carry 0x80 and other
        // bits on ordinary limbs, so it sees what the engine animates (0x280) plus the ignore-parent bits; the file
        // flags stay in runtimeFlags for Bind.
        bone.flags = (bones[i].flags & 0x7) | ((bones[i].flags & 0x280) ? 0x200u : 0u);
        bone.runtimeFlags = bones[i].flags; bone.pivot = bones[i].pivot;
        bone.palette[0] = bone.palette[5] = bone.palette[10] = bone.palette[15] = 1;
        compact[i] = static_cast<int32_t>(map.size()); map.push_back(i);
    }
    out.count = static_cast<uint32_t>(map.size());
    out.keyCount = (std::min)(keyCount, characterrig::MaxKeys);
    for (uint32_t k = 0; k < out.keyCount; ++k) {
        const int32_t bone = keys[k] < count ? compact[keys[k]] : -1;
        out.keys[k] = bone >= 0 && out.bones[bone].key == static_cast<int32_t>(k) ? static_cast<uint16_t>(bone) : 0xffff;
    }
    return out.count > 0;
}

// ---- Role planning on the model's own bone tree -------------------------------------------------------------------
// Roles use the donor's numbering (character-rig-plan.h): 0 actor, 1 root, 2 spine, 3 waist, 4 chest, 9 neck, 16 head,
// 10/11 shoulder (clavicle) L/R, 17/18 upper arm, 30/31 elbow, 36/38 hand, 7/8 thigh, 14/15 knee, 20/22 ankle,
// 32/33 toe. Model space: +X forward, +Y left, +Z up.
struct Roles { int32_t bone[39]; Roles() { for (auto& b : bone) b = -1; } };
namespace detail {
inline float Dist(Point a, Point b) { return sqrtf((a.x-b.x)*(a.x-b.x) + (a.y-b.y)*(a.y-b.y) + (a.z-b.z)*(a.z-b.z)); }
inline bool Animated(const SourceBone& b) { return (b.flags & 0x280) != 0; }
inline bool AtOrigin(Point p) { return fabsf(p.x) < .02f && fabsf(p.y) < .02f && fabsf(p.z) < .02f; }
struct Tree {
    const SourceBone* bones; uint32_t count;
    std::vector<std::vector<uint32_t>> children;
    std::vector<uint32_t> size; // subtree bone count
    Tree(const SourceBone* b, uint32_t n) : bones(b), count(n), children(n), size(n, 1) {
        for (uint32_t i = 0; i < n; ++i) if (b[i].parent >= 0) children[b[i].parent].push_back(i);
        for (uint32_t i = n; i-- > 0;) if (b[i].parent >= 0) size[b[i].parent] += size[i];
    }
    bool Ancestor(int32_t a, int32_t b) const { // a is b or above it
        for (; b >= 0; b = bones[b].parent) if (b == a) return true;
        return false;
    }
    // The next joint of a limb: an animated descendant at least 3 cm away, looking through zero-length helpers
    // (Retail ankles carry one before the toe). Twist bones and attachment helpers are pruned by taking the
    // candidate with the largest subtree; a helper parked at the model origin is never a joint.
    void Candidates(uint32_t from, uint32_t at, int depth, std::vector<uint32_t>& out) const {
        for (uint32_t c : children[at]) {
            const float d = Dist(bones[c].pivot, bones[from].pivot);
            if (d < .03f) { if (depth < 4) Candidates(from, c, depth + 1, out); continue; }
            if (d <= 2.5f && Animated(bones[c]) && !(AtOrigin(bones[c].pivot) && !AtOrigin(bones[from].pivot))) out.push_back(c);
        }
    }
    int32_t Next(uint32_t from) const {
        std::vector<uint32_t> c; Candidates(from, from, 0, c);
        int32_t best = -1;
        for (uint32_t i : c) if (best < 0 || size[i] > size[best]
            || (size[i] == size[best] && Dist(bones[i].pivot, bones[from].pivot) > Dist(bones[best].pivot, bones[from].pivot))) best = static_cast<int32_t>(i);
        return best;
    }
    int32_t RotationJoint(int32_t joint) const {
        // Some Retail ankles are passive skinning wrappers; the foot's rotation lives on a co-located child.
        if (joint<0) return joint;
        for (uint32_t child:children[joint])
            if (Animated(bones[child]) && bones[child].rotationSequences>bones[joint].rotationSequences
                && Dist(bones[child].pivot,bones[joint].pivot)<.0001f)
                return static_cast<int32_t>(child);
        return joint;
    }
};
inline int32_t Key(const SourceBone* bones, uint32_t count, const uint16_t* keys, uint32_t keyCount, uint32_t k) {
    return k < keyCount && keys[k] < count && bones[keys[k]].key == static_cast<int32_t>(k) ? keys[k] : -1;
}
}
// Sanity every plan must pass (also the donor's): limbs on their side, legs going down, no joint at the origin.
inline const char* Check(const SourceBone* bones, uint32_t count, const Roles& r) {
    for (unsigned role : BodyRoles)
        if (r.bone[role] < 0 || static_cast<uint32_t>(r.bone[role]) >= count) return "a body role is missing";
    for (unsigned i = 1; i < BodyCount; ++i) for (unsigned j = 0; j < i; ++j)
        if (r.bone[BodyRoles[i]] == r.bone[BodyRoles[j]] && !(BodyRoles[i] == 1 && BodyRoles[j] == 0)) return "two body roles share a bone";
    const auto p = [&](unsigned role) { return bones[r.bone[role]].pivot; };
    for (unsigned role : {7u,17u,36u}) if (!(p(role).y > .01f)) return "a left limb is not on the left (+Y)";
    for (unsigned role : {8u,18u,38u}) if (!(p(role).y < -.01f)) return "a right limb is not on the right (-Y)";
    for (unsigned side = 0; side < 2; ++side)
        if (!(p(20 + 2*side).z < p(14 + side).z && p(14 + side).z < p(7 + side).z)) return "a leg does not go down (thigh > knee > ankle)";
    for (unsigned role : {36u,38u,32u,33u,16u}) if (detail::AtOrigin(p(role))) return "a hand, toe or head is at the model origin";
    return nullptr;
}
// Structural planner for rigs the donor's Discover refuses (Retail bodies, custom races). Key bones are hints only
// where every M2 agrees on them (6 head, 26 root, 0..3 arm/shoulder, 4 spine, 5 waist); limbs, chest, neck, pelvis
// and the leg fork come from the tree and pivot geometry.
inline const char* Structural(const SourceBone* bones, uint32_t count, const uint16_t* keys, uint32_t keyCount, Roles& r) {
    using namespace detail;
    r = {};
    const Tree t(bones, count);
    const auto key = [&](uint32_t k) { return Key(bones, count, keys, keyCount, k); };
    const int32_t head = key(6);
    if (head < 0) return "no head key bone (6)";
    int32_t actor = head; while (bones[actor].parent >= 0) actor = bones[actor].parent;
    const int32_t root = key(26) >= 0 && t.Ancestor(key(26), head) ? key(26) : actor;
    // Arms: shoulder (clavicle) and upper arm, with the donor's rule for rigs that tag the upper arm as Shoulder.
    int32_t shoulder[2], upper[2];
    for (uint32_t side = 0; side < 2; ++side) {
        shoulder[side] = key(2 + side); upper[side] = key(side);
        if (upper[side] < 0 && shoulder[side] >= 0) { upper[side] = shoulder[side]; shoulder[side] = bones[upper[side]].parent; }
        if (shoulder[side] < 0 || upper[side] < 0) return "arm/shoulder key bones (0..3) missing";
    }
    // Chest: where the two clavicles meet; neck: the chest's child on the way to the head.
    int32_t chest = shoulder[0];
    while (chest >= 0 && !t.Ancestor(chest, shoulder[1])) chest = bones[chest].parent;
    if (chest < 0 || !t.Ancestor(chest, head) || chest == head) return "clavicles do not meet below the head";
    int32_t neck = head;
    while (bones[neck].parent != chest) neck = bones[neck].parent;
    if (neck == head) neck = bones[head].parent, chest = bones[neck].parent; // head sits on the chest: donor layout
    if (chest < 0 || neck < 0) return "no neck between chest and head";
    // Pelvis: Key 5 when it forks the legs, else the bone whose children split left/right going down to the feet.
    const float height = bones[head].pivot.z - bones[actor].pivot.z;
    int32_t thigh[2] = {-1, -1}, waist = -1;
    const auto fork = [&](uint32_t b) {
        if (t.Ancestor(chest, b) || !Animated(bones[b])) return false;
        int32_t best[2] = {-1, -1};
        for (uint32_t i : t.children[b]) { // thighs are direct children of the pelvis (the donor requires it too)
            if (!Animated(bones[i])) continue;
            const float dy = bones[i].pivot.y - bones[b].pivot.y;
            if (fabsf(dy) < .03f || bones[i].pivot.z > bones[b].pivot.z + .05f) continue; // beside or below the pelvis
            float low = bones[i].pivot.z; // lowest point the chain reaches
            for (int32_t j = static_cast<int32_t>(i), n = 0; j >= 0 && n < 8; j = t.Next(j), ++n) low = (std::min)(low, bones[j].pivot.z);
            if (low - bones[actor].pivot.z > .25f * height) continue;
            const int side = dy > 0 ? 0 : 1;
            if (best[side] < 0 || t.size[i] > t.size[best[side]]) best[side] = static_cast<int32_t>(i);
        }
        if (best[0] < 0 || best[1] < 0) return false;
        thigh[0] = best[0]; thigh[1] = best[1]; return true;
    };
    if (key(5) >= 0 && fork(key(5))) waist = key(5);
    for (uint32_t b = 0; waist < 0 && b < count; ++b) if (fork(b)) waist = static_cast<int32_t>(b);
    if (waist < 0) return "no pelvis forks into two legs";
    // Spine: Key 4 when it lies between the root and the chest, else the chest's parent.
    int32_t spine = key(4);
    if (spine < 0 || spine == chest || !t.Ancestor(spine, chest)) spine = bones[chest].parent;
    if (spine < 0 || spine == waist) return "no spine between pelvis and chest";
    r.bone[0] = actor; r.bone[1] = root; r.bone[2] = spine; r.bone[3] = waist; r.bone[4] = chest; r.bone[9] = neck; r.bone[16] = head;
    const unsigned arms[2][4] = {{10,17,30,36},{11,18,31,38}}, legs[2][4] = {{7,14,20,32},{8,15,22,33}};
    for (uint32_t side = 0; side < 2; ++side) {
        r.bone[arms[side][0]] = shoulder[side]; r.bone[arms[side][1]] = upper[side];
        r.bone[arms[side][2]] = t.Next(upper[side]);
        r.bone[arms[side][3]] = r.bone[arms[side][2]] >= 0 ? t.Next(r.bone[arms[side][2]]) : -1;
        r.bone[legs[side][0]] = thigh[side];
        for (unsigned j = 1; j < 4; ++j) {
            int32_t joint = r.bone[legs[side][j-1]] >= 0 ? t.Next(r.bone[legs[side][j-1]]) : -1;
            r.bone[legs[side][j]] = j==2 ? t.RotationJoint(joint) : joint;
        }
    }
    return Check(bones, count, r);
}

// The donor solvers run on this: just the role bones, each parented to its nearest role ancestor, with the key
// bones Discover expects. model[] maps each of its bones back to the model; its Discover plan equals the roles.
struct Rig {
    characterrig::Snapshot skeleton;
    characterrig::Plan plan;
    std::vector<uint32_t> model;
};
inline const char* Reduce(const SourceBone* bones, uint32_t count, const Roles& r, Rig& out) {
    out.skeleton = {}; out.model.clear();
    std::vector<int32_t> index(count, -1);
    std::vector<uint32_t> list;
    for (unsigned role : BodyRoles) if (index[r.bone[role]] < 0) { index[r.bone[role]] = 0; list.push_back(r.bone[role]); }
    std::sort(list.begin(), list.end());
    auto& s = out.skeleton;
    s.version = 264; s.count = static_cast<uint32_t>(list.size()); s.keyCount = 27;
    for (auto& k : s.keys) k = 0xffff;
    for (uint32_t i = 0; i < list.size(); ++i) index[list[i]] = static_cast<int32_t>(i);
    for (uint32_t i = 0; i < list.size(); ++i) {
        auto& b = s.bones[i];
        int32_t p = bones[list[i]].parent;
        while (p >= 0 && index[p] < 0) p = bones[p].parent;
        b.parent = p < 0 ? -1 : index[p];
        b.flags = 0x200; b.pivot = bones[list[i]].pivot;
        b.palette[0] = b.palette[5] = b.palette[10] = b.palette[15] = 1;
    }
    const std::pair<uint32_t, unsigned> keyed[] = {{26,1},{5,3},{4,2},{6,16},{2,10},{3,11},{0,17},{1,18}};
    for (auto [k, role] : keyed) { const int32_t b = index[r.bone[role]]; s.keys[k] = static_cast<uint16_t>(b); s.bones[b].key = static_cast<int32_t>(k); }
    out.model = list;
    if (!characterrig::Discover(s, out.plan)) return "the role skeleton was refused by the donor's Discover";
    for (unsigned role : BodyRoles) if (out.model[out.plan[role]] != static_cast<uint32_t>(r.bone[role])) return "the donor's Discover re-mapped a role";
    return nullptr;
}
// Plans a model: the donor's Discover first (Wrath rigs, as accepted live), the structural planner when it refuses
// or its plan fails the sanity check. Returns nullptr on success, else why (for the log).
inline const char* Plan(const SourceBone* bones, uint32_t count, const uint16_t* keys, uint32_t keyCount, Rig& out, bool* donor = nullptr) {
    Roles r;
    static characterrig::Snapshot full; std::vector<uint32_t> map;
    characterrig::Plan plan;
    if (donor) *donor = false;
    for (uint32_t i = 0; i < count; ++i) if (bones[i].parent >= static_cast<int32_t>(i)) return "a bone is listed before its parent";
    if (Skeleton(bones, count, keys, keyCount, full, map) && characterrig::Discover(full, plan)) {
        for (unsigned role : BodyRoles) r.bone[role] = static_cast<int32_t>(map[plan[role]]);
        if (!Check(bones, count, r) && !Reduce(bones, count, r, out)) { if (donor) *donor = true; return nullptr; }
    }
    if (const char* why = Structural(bones, count, keys, keyCount, r)) return why;
    return Reduce(bones, count, r, out);
}

// What the bone hook reads: per model bone, the replacement local (3x3 rows + about-pivot translation).
struct Pose {
    // bone: model bone; parent: entries index of the nearest posed animated ancestor (-1 = model space);
    // source: rig bone whose solved rotation this bone takes (its nearest role at or above it, -1 = none).
    struct Entry { uint32_t bone = 0; int32_t parent = -1, source = -1; float local[12]{}; };
    bool ready = false;
    std::vector<int16_t> slot;  // model bone index -> entries index, -1 = stock
    std::vector<Entry> entries; // local: a0..a2, b0..b2, c0..c2, d0..d2
};
// Poses the role bones and every bone between them and the root (Retail rigs put animated helpers there; left stock
// they would add the run cycle back; posed, they inherit their role ancestor's rotation, i.e. identity locals).
// Bones the engine does not animate (no 0x280, e.g. a Retail root) never reach the hook; they keep their bind pose,
// so their children are posed relative to the nearest animated ancestor. Refuses (stock animation) when a posed bone
// billboards or ignores its parent (flags 0x7F), as the donor refused "unsupported_bone_flags".
inline bool Bind(const SourceBone* bones, uint32_t count, const Rig& rig, Pose& out, const char** why = nullptr) {
    out = {};
    const auto refuse = [&](const char* reason) { if (why) *why = reason; out = {}; return false; };
    std::vector<int32_t> role(count, -1);
    for (uint32_t i = 0; i < rig.model.size(); ++i) { if (rig.model[i] >= count) return refuse("a role is outside the model"); role[rig.model[i]] = static_cast<int32_t>(i); }
    // path: the roles and every bone between them and the root. These must be posed.
    std::vector<bool> path(count, false), posed(count, false);
    for (uint32_t b : rig.model) for (int32_t a = static_cast<int32_t>(b); a >= 0 && !path[a]; a = bones[a].parent) path[a] = true;
    // Also the limb helper branches (Retail legs carry twin knee/ankle and twist bones beside
    // the joints; left on the stock run cycle they bent the shins). They take their role ancestor's rotation, i.e.
    // their bind pose relative to it. The face (below the head) and the fingers (below the hands) keep their stock
    // animation, and so does any helper that billboards or ignores its parent, with everything below it.
    std::vector<bool> stop(count, false);
    std::vector<int32_t> entry(count, -1), source(count, -1);
    const auto limb = [&](int32_t source) {
        for (unsigned r : {7u,8u,10u,11u,14u,15u,17u,18u,20u,22u,30u,31u})
            if (source == static_cast<int32_t>(rig.plan[r])) return true;
        return false;
    };
    for (uint32_t b = 0; b < count; ++b) {
        const int32_t parent = bones[b].parent;
        posed[b] = path[b] || (parent >= 0 && posed[parent] && !stop[parent] && limb(source[parent]) && !(bones[b].flags & 0x7f)
            && !(detail::AtOrigin(bones[b].pivot) && !detail::AtOrigin(bones[parent].pivot)));
        if (!posed[b]) continue;
        source[b] = role[b] >= 0 ? role[b] : parent >= 0 ? source[parent] : -1;
        // Retail parallel joints share the main joint's pivot and parent role (e.g. twin shins).
        // Inheriting only the thigh rotation would leave their weighted vertices above the bent shin.
        if (role[b] < 0) for (uint32_t i = 0; i < rig.model.size(); ++i) {
            const uint32_t joint = rig.model[i];
            if (detail::Dist(bones[b].pivot,bones[joint].pivot) > .0001f) continue;
            bool descendant=false;
            for (int32_t p=bones[joint].parent;p>=0;p=bones[p].parent) if (p==static_cast<int32_t>(b)) { descendant=true; break; }
            if (descendant) continue; // A passive ankle wrapper must not inherit its co-located foot child's rotation.
            int32_t p = bones[joint].parent;
            while (p >= 0 && role[p] < 0) p = bones[p].parent;
            if (p >= 0 && role[p] == source[b]) { source[b] = static_cast<int32_t>(i); break; }
        }
        for (unsigned r : {16u,36u,38u}) if (source[b] == static_cast<int32_t>(rig.plan[r])) stop[b] = true;
    }
    out.slot.assign(count, -1);
    for (uint32_t b = 0; b < count; ++b) {
        if (!posed[b]) continue;
        const int32_t parent = bones[b].parent;
        if (path[b] && (bones[b].flags & 0x7f)) return refuse("a posed bone billboards or ignores its parent (flags 0x7F)");
        if (!detail::Animated(bones[b])) continue;
        int32_t p = parent;
        while (p >= 0 && entry[p] < 0) p = bones[p].parent; // ancestors come first
        entry[b] = static_cast<int32_t>(out.entries.size());
        out.slot[b] = static_cast<int16_t>(out.entries.size());
        out.entries.push_back({b, p < 0 ? -1 : entry[p], source[b]});
    }
    if (out.entries.empty()) return refuse("no posed bone is animated");
    if (why) *why = "";
    return true;
}
// Writes this frame's pose: each posed bone's rotation relative to its effective parent, its file pivot (drops the
// animated translation, as the donor's RemoveAnimatedTranslation did) and the root offset on bones whose effective
// parent is the model itself.
inline bool Fill(const SourceBone* bones, const Rig& rig, const characterretarget::Pose& target, Point offset, Pose& out) {
    out.ready = false;
    if (target.count != rig.skeleton.count || out.entries.empty() || !characterrig::Finite(offset)) return false;
    const auto global = [&](int32_t source) { return source < 0 ? characterretarget::Quaternion{} : target.global[source]; };
    for (auto& e : out.entries) {
        const auto q = characterretarget::Multiply(characterretarget::Inverse(global(e.parent < 0 ? -1 : out.entries[e.parent].source)),
            global(e.source));
        float m[16];
        characterretarget::Matrix(q, {0,0,0}, m);
        const Point p = bones[e.bone].pivot;
        const Point t = e.parent < 0 ? Point{p.x + offset.x, p.y + offset.y, p.z + offset.z} : p;
        const float values[12] {m[0],m[1],m[2], m[4],m[5],m[6], m[8],m[9],m[10], t.x,t.y,t.z};
        for (float v : values) if (!std::isfinite(v)) return false;
        memcpy(e.local, values, sizeof(values));
    }
    out.ready = true;
    return true;
}
// The bone hook: replace `local` (C44Matrix, 16 floats row major) for a posed bone. False leaves it stock.
inline bool Apply(const Pose& pose, uint32_t bone, float* local) {
    if (!pose.ready || bone >= pose.slot.size() || pose.slot[bone] < 0) return false;
    const float* v = pose.entries[pose.slot[bone]].local;
    for (int row = 0; row < 3; ++row) memcpy(local + row * 4, v + row * 3, 12);
    memcpy(local + 12, v + 9, 12);
    return true;
}

// Per-activation rider state (the donor's calibration/anchors), cleared when skating stops.
struct Rider {
    characteralignment::Calibration calibration{};
    characteralignment::AirAnchor air;
    riderair::Anchor hips;
    characterheight::Filter height{};
    characterragdoll::Anchor ragdoll;
    void Clear() { *this = {}; }
};
// Source states the donor drew as ragdoll (WipeoutGround, Teleporting checkpoint reset).
inline bool Ragdoll(uint32_t state) { return state == 300 || state == 702; }
// Solves the skeleton's pose and root offset (model yards) for one frame, as the donor's RigPoseFrame did minus the
// foot IK and displayed-ground fit. origin: helper session origin, native: player position (world yards).
// The ragdoll root: the donor's hip anchor when the Source body is within 4.5 yd of the character. A tumbling bail
// often leaves it farther (the character does not follow the body), which the donor refused, so the whole bail
// stayed on stock (live test 2026-11-15): keep the ragdoll on the character then, at the Source hip height.
inline bool RagdollRoot(Rider& rider, const skatevisual::Frame& frame, const characterretarget::Pose& target,
                        float facing, Point origin, Point native, Point& offset) {
    if (rider.ragdoll.Shift(frame, target, origin, native, facing, offset)) return true;
    const Point hip = characterragdoll::SourceHip(frame, origin, native, facing);
    const float z = hip.z - target.pivots[characterretarget::Joint(target, 3)].z;
    offset = {0, 0, (std::max)(-2.f, (std::min)(2.f, z))};
    return std::isfinite(z);
}
// What the foot solvers did this frame (log/tests): grounded support-leg planting, airborne leg reach.
struct Contact { bool planted = false, legs = false; float soleError = 0, legError = 0; };
// why (optional) names a rejection for the log.
inline bool Solve(Rider& rider, const characterrig::Snapshot& skeleton, const skatevisual::Mesh* mesh,
                  const skatevisual::Frame& frame, uint32_t state, float facing, Point origin, Point native,
                  uint32_t now, uint32_t epoch, characterretarget::Pose& target, Point& offset, const char** why = nullptr,
                  Contact* contact = nullptr) {
    offset = {};
    if (contact) *contact = {};
    const auto refuse = [&](const char* reason) { if (why) *why = reason; return false; };
    if (Ragdoll(state)) {
        if (!characterragdoll::Solve(skeleton, frame, facing, target)) return refuse("bail frame did not solve");
        return RagdollRoot(rider, frame, target, facing, origin, native, offset) || refuse("bail root not finite");
    }
    const bool air = !frame.pose.ground || frame.pose.airborne;
    if (!characterretarget::Solve(skeleton, frame, facing, target)) {
        // The riding solver needs an upright torso (chest-facing yaw); a flip or a fall can tip it past that.
        // The ragdoll solver takes any 3D torso, so the body keeps following the Source instead of going stock.
        if (!characterragdoll::Solve(skeleton, frame, facing, target)) return refuse("air frame did not solve");
        if (!air || !mesh) return RagdollRoot(rider, frame, target, facing, origin, native, offset) || refuse("air root not finite");
    }
    if (!mesh) return true;
    if (air) {
        // Flips: keep the last grounded calibration, follow the Source hips (rider-air-alignment.h) and reach the
        // feet for the Source feet on the board as it is drawn (rider-air-legs.h); the WoW legs are not the
        // Source's length, so rotations alone put the feet through the deck (Phase 5 live test).
        const auto fit = rider.air.Solve(frame);
        const auto hips = fit.valid ? rider.hips.Solve(fit, frame, target, facing) : riderair::Result{};
        if (hips.valid) {
            offset = hips.shift;
            const auto legs = riderlegs::Solve(skeleton, target, frame, fit.board, facing, hips.shift);
            if (contact) { contact->legs = legs.valid; contact->legError = legs.after; }
        }
        rider.height.Clear();
        return true;
    }
    const auto fit = characteralignment::Solve(skeleton, target, *mesh, frame, facing, rider.calibration);
    float lift = 0;
    if (!fit.valid || !rider.height.Step(fit.rootLift, now, epoch, frame.pose.tick, lift)) { rider.height.Clear(); return true; }
    // Support-leg IK onto the deck (character-foot-contact.h), as the donor ran it after the height filter: the
    // feet stand on the board instead of sinking into it or hovering.
    const auto planted = charactercontact::Solve(skeleton, target, *mesh, frame, facing, fit, lift);
    lift = planted.valid ? planted.rootLift : fit.rootLift;
    rider.height.height = lift;
    if (contact) { contact->planted = planted.valid; contact->soleError = planted.maxSoleError; }
    offset = {0, 0, lift};
    rider.air.Capture(fit, frame, lift);
    rider.hips.Capture(fit, frame, target, facing, lift);
    rider.ragdoll.Capture(frame, target, origin, native, facing, offset, fit.board.scale);
    return true;
}
}
