#pragma once
// Claude 2026-11-15: the cast of the inn character select walks around (stage 3, step 3.3). PURE LOGIC: no engine calls here. The caller
// (CharacterSelection.cpp) tells it which characters are ready and which is selected, and applies the returned position/heading/animation.
// Positions are WMO-local yards (the same space as InnWalk.cfg); the caller converts to the backdrop's attachment space.
//
// Behaviour (organic, not everyone at once): each character, when free, picks an activity by weight:
//   wander   walk to a free waypoint and stand a few seconds
//   stand    just stand a few more seconds
//   drink    walk to a bar spot (SPOT=<wp> drink <deg>), face the counter, "eat" emote on and off, stay a long time
//   lean     walk to a wall spot (SPOT=<wp> lean <deg>), stand with the back to the wall, stay a long time
//   away     walk to a far spot (SPOT=<wp> away), stay out of the way for a while ("stepped out back")
//   chat     walk up to another idle character, face each other and take turns with talk/laugh/cheer emotes
// At most maxWalking characters walk at the same time (default about 3 of 8). The selected character stops where it is, turns to the camera
// and stays until another one is selected. Nothing here ever teleports a character; the graph is InnWalk.cfg (WP/LINK/SPOT).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
#include "WoW/Source/Glue/InnWalk.h"

namespace InnWalkers {

constexpr float kPi = 3.14159265f;

// Candidate emotes (checked with HasSequence by the caller). Bit k of CharIn::emoteMask = kEmoteIds[k] exists on the model.
constexpr int32_t kEmoteCount = 12;
constexpr int32_t kEmoteIds[kEmoteCount] = { 60 /*talk*/, 61 /*eat*/, 64 /*talk exclamation*/, 65 /*talk question*/, 68 /*cheer*/, 70 /*laugh*/,
    1260 /*retail lean (WALEAN01, STATE_LEAN)*/, 1262 /*retail lean 02*/, 102 /*SitChairLow*/, 103 /*SitChairMed*/, 104 /*SitChairHigh*/, 97 /*SitGround: sit on the floor*/ };
// bit of an animation id in CharIn::emoteMask (-1 when it is not a candidate)
inline int32_t EmoteBit(int32_t animId) { for (int32_t k = 0; k < kEmoteCount; k++) if (kEmoteIds[k] == animId) return k; return -1; }
constexpr int32_t kAnimStand = 0;
constexpr int32_t kAnimWalk = 4;
constexpr int32_t kAnimRun = 5;

struct Config {
    float speed = 2.2f;         // yards per second at WOW_INN_WALK_SPEED = 1
    float pauseMin = 2.0f, pauseMax = 8.0f;
    int32_t maxWalking = 0;     // 0 = automatic: about three eighths of the cast
    float longMin = 35.0f, longMax = 90.0f;     // drink / lean stays (seconds)
    float awayMin = 25.0f, awayMax = 70.0f;
    // v33: more spread out, less walking
    float wanderWeight = 26.0f;     // chance weight of a short walk to another place (was 40)
    float standWeight = 26.0f;      // chance weight of just standing where it is (was 14)
    float spread = 3.6f;            // preferred distance (yards) from every other character and every reserved destination
    float range = 9.0f;             // walks prefer places within this many yards of where the character stands
    float comeWalkSec = 3.0f;       // v45: a clicked character that is not in the hall walks in from a door for this long, then runs
    float runSpeedMult = 2.0f;      // v45: running speed = walking speed x this
};

struct CharIn { bool ready = false; bool hasWalk = false; uint32_t emoteMask = 0; bool arrive = false; bool hasRun = false; bool coming = false; bool leave = false; };   // arrive: just built, enter through an entrance spot; coming: clicked while away, take the shortest way in and run if it takes long; leave: walk out through a door (v45)
struct CharOut { bool valid = false; float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f; int32_t anim = kAnimStand; bool animChanged = false; bool gone = false; };   // gone: it reached a door and left the hall (the caller frees the model)
struct Stats { int32_t walking = 0, paused = 0, longStay = 0, chatting = 0, selected = 0; };

enum Mode : int32_t { M_Idle, M_Walk, M_Pause, M_Long, M_ChatWait, M_Chat, M_Selected };
enum Kind : int32_t { K_None, K_Drink, K_Lean, K_Away, K_Transit, K_Entrance, K_Sit, K_Circle };   // sit = a chair/stool (animation per spot), circle = one seat of a standing talking circle   // transit = stairs/corridor nodes (passable, never a destination), entrance = where new characters appear
enum Arrive : int32_t { A_Pause, A_Long, A_Chat };

struct Node { float x = 0.0f, y = 0.0f, z = 0.0f; int32_t id = 0; Kind kind = K_None; float heading = 0.0f; bool hasHeading = false; float shift = 0.0f; int32_t anim = 0; float lift = 0.0f; int32_t reservedBy = -1; std::vector<int32_t> nbr; };

struct Walker {
    bool placed = false;
    Mode mode = M_Idle;
    Arrive arrive = A_Pause;
    Kind spotKind = K_None;
    float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f, wantHeading = 0.0f;
    float timer = 0.0f;                 // seconds left in the current mode
    float emoteTimer = 0.0f;            // chat / drink emote scheduling
    int32_t emote = -1;                 // emote id playing, -1 none
    int32_t anim = -1;                  // sequence last requested (-1 = nothing yet)
    int32_t node = -1;                  // waypoint it stands at or last left
    int32_t reserved = -1;              // waypoint reserved for it (destination or the one it stands on)
    int32_t partner = -1;
    std::vector<int32_t> path;          // waypoint indices still to visit
    bool hasFinal = false;              // after the path: a free point (chat spot on a link)
    float fx = 0.0f, fy = 0.0f, fz = 0.0f;
    float sx = 0.0f, sy = 0.0f, sz = 0.0f, tx = 0.0f, ty = 0.0f, tz = 0.0f, segLen = 0.0f, segDone = 0.0f;
    bool haveSeg = false;
    float blocked = 0.0f;
    float chatTimer = 0.0f;
    bool selected = false;
    float crowd = 0.0f;                 // seconds spent standing close to another idle character
    bool forceMove = false;             // leave this place: the next activity must be a walk (crowding)
    bool leaving = false, leaveDone = false;   // v45: walking out through a door
    bool coming = false;                // v45: walking in as a clicked character (no greeting until it arrived)
    float comeT = 0.0f;                 // seconds spent walking in
};

struct State {
    std::vector<Node> nodes;
    std::vector<Walker> walkers;
    uint32_t revision = 0xFFFFFFFFu;
    uint32_t rng = 0x2545F491u;
    Stats stats;
};
inline State& S() { static State s; return s; }
inline void Reset() { State& s = S(); s.nodes.clear(); s.walkers.clear(); s.revision = 0xFFFFFFFFu; s.stats = Stats(); }

// v46: two list entries swapped places (the owner dragged a name in the character list): the walkers, their reservations and chat partners follow.
inline void SwapIndex(int32_t a, int32_t b) {
    State& s = S();
    const int32_t n = static_cast<int32_t>(s.walkers.size());
    if (a < 0 || b < 0 || a >= n || b >= n || a == b) return;
    std::swap(s.walkers[a], s.walkers[b]);
    auto map = [&](int32_t v) { return v == a ? b : (v == b ? a : v); };
    for (Node& node : s.nodes) if (node.reservedBy >= 0) node.reservedBy = map(node.reservedBy);
    for (Walker& w : s.walkers) if (w.partner >= 0) w.partner = map(w.partner);
}

inline uint32_t Rand(State& s) { s.rng ^= s.rng << 13; s.rng ^= s.rng >> 17; s.rng ^= s.rng << 5; return s.rng; }
inline float Rand01(State& s) { return static_cast<float>(Rand(s) & 0xFFFFFF) / 16777216.0f; }
inline float RandRange(State& s, float a, float b) { return a + (b - a) * Rand01(s); }

inline float NormAngle(float a) { if (!std::isfinite(a)) return 0.0f; a = std::fmod(a, 2.0f * kPi); if (a > kPi) a -= 2.0f * kPi; else if (a < -kPi) a += 2.0f * kPi; return a; }     // v49: the old loops never ended for an infinite or huge angle (a silent freeze)
inline float Dist(float ax, float ay, float bx, float by) { return std::hypot(bx - ax, by - ay); }

inline void BuildGraph(State& s, const InnWalk::State& g) {
    s.nodes.clear();
    for (const InnWalk::Waypoint& w : g.waypoints) {
        Node n;
        n.x = w.x; n.y = w.y; n.z = w.hasZ ? w.z : g.floorZ; n.id = w.id;
        s.nodes.push_back(n);
    }
    auto indexOf = [&](int32_t id) -> int32_t {
        for (size_t i = 0; i < s.nodes.size(); i++) if (s.nodes[i].id == id) return static_cast<int32_t>(i);
        return -1;
    };
    for (const InnWalk::Link& l : g.links) {
        const int32_t a = indexOf(l.a), b = indexOf(l.b);
        if (a < 0 || b < 0 || a == b) continue;
        s.nodes[a].nbr.push_back(b);
        s.nodes[b].nbr.push_back(a);
    }
    for (const InnWalk::Spot& sp : g.spots) {
        const int32_t i = indexOf(sp.id);
        if (i < 0) continue;
        Node& n = s.nodes[i];
        n.kind = sp.kind == "drink" ? K_Drink : (sp.kind == "lean" ? K_Lean : (sp.kind == "away" ? K_Away : (sp.kind == "transit" ? K_Transit : (sp.kind == "entrance" ? K_Entrance : (sp.kind == "sit" ? K_Sit : (sp.kind == "circle" ? K_Circle : K_None))))));
        if (sp.hasHeading) { n.hasHeading = true; n.heading = sp.headingDeg * kPi / 180.0f; n.shift = sp.shift; }
        n.anim = sp.anim; n.lift = sp.lift;
    }
}

// Shortest path (by length) over the links from node a to node b; empty when unreachable. The result excludes a, includes b.
inline std::vector<int32_t> FindPath(const State& s, int32_t a, int32_t b) {
    std::vector<int32_t> out;
    const int32_t n = static_cast<int32_t>(s.nodes.size());
    if (a < 0 || b < 0 || a >= n || b >= n) return out;
    if (a == b) return out;
    std::vector<float> dist(n, 1.0e30f);
    std::vector<int32_t> prev(n, -1);
    std::vector<bool> done(n, false);
    dist[a] = 0.0f;
    for (int32_t iter = 0; iter < n; iter++) {
        int32_t u = -1;
        for (int32_t i = 0; i < n; i++) if (!done[i] && (u < 0 || dist[i] < dist[u])) u = i;
        if (u < 0 || dist[u] >= 1.0e29f) break;
        done[u] = true;
        if (u == b) break;
        for (int32_t v : s.nodes[u].nbr) {
            const float d = dist[u] + Dist(s.nodes[u].x, s.nodes[u].y, s.nodes[v].x, s.nodes[v].y);
            if (d < dist[v]) { dist[v] = d; prev[v] = u; }
        }
    }
    if (prev[b] < 0) return out;
    for (int32_t v = b; v != a && v >= 0; v = prev[v]) out.push_back(v);
    std::reverse(out.begin(), out.end());
    return out;
}

inline int32_t NearestNode(const State& s, float x, float y) {
    int32_t best = -1;
    float bestD = 1.0e30f;
    for (size_t i = 0; i < s.nodes.size(); i++) {
        const float d = Dist(x, y, s.nodes[i].x, s.nodes[i].y);
        if (d < bestD) { bestD = d; best = static_cast<int32_t>(i); }
    }
    return best;
}

inline void Release(State& s, int32_t who) {
    Walker& w = s.walkers[who];
    if (w.reserved >= 0 && w.reserved < static_cast<int32_t>(s.nodes.size()) && s.nodes[w.reserved].reservedBy == who) s.nodes[w.reserved].reservedBy = -1;
    w.reserved = -1;
}

inline void StopWalking(Walker& w) { w.path.clear(); w.hasFinal = false; w.haveSeg = false; w.blocked = 0.0f; }

inline void GoIdle(State& s, int32_t who, float delay) {
    Walker& w = s.walkers[who];
    w.mode = M_Idle; w.timer = delay; w.emote = -1; w.partner = -1; w.spotKind = K_None; w.arrive = A_Pause;
    StopWalking(w);
}

inline int32_t CountMode(const State& s, Mode m) {
    int32_t c = 0;
    for (const Walker& w : s.walkers) if (w.placed && w.mode == m) c++;
    return c;
}

// Starts walking walker `who` along the path of node indices (from its current node), then optionally to a free point.
inline bool StartWalk(State& s, int32_t who, const std::vector<int32_t>& path, bool hasFinal, float fx, float fy, float fz, Arrive arrive) {
    Walker& w = s.walkers[who];
    if (path.empty() && !hasFinal) return false;
    w.path = path; w.hasFinal = hasFinal; w.fx = fx; w.fy = fy; w.fz = fz; w.arrive = arrive;
    w.mode = M_Walk; w.haveSeg = false; w.blocked = 0.0f; w.emote = -1;
    return true;
}

inline bool NodeFree(const State& s, int32_t i, int32_t who) {
    const Node& n = s.nodes[i];
    if (n.reservedBy >= 0 && n.reservedBy != who) return false;
    for (size_t k = 0; k < s.walkers.size(); k++) {
        if (static_cast<int32_t>(k) == who || !s.walkers[k].placed) continue;
        // somebody sitting on the floor (animation 97, e.g. by the fireplace) gets a wider keep-clear circle than a standing person
        const Walker& ow = s.walkers[k];
        const bool floorSitter = ow.mode == M_Long && ow.spotKind == K_Sit && ow.node >= 0 && ow.node < static_cast<int32_t>(s.nodes.size()) && s.nodes[ow.node].anim == 97;
        if (Dist(ow.x, ow.y, n.x, n.y) < (floorSitter ? 1.9f : 1.2f)) return false;
    }
    return true;
}

inline void StartPause(State& s, int32_t who, const Config& cfg) {
    Walker& w = s.walkers[who];
    w.mode = M_Pause; w.timer = RandRange(s, cfg.pauseMin, std::max(cfg.pauseMin, cfg.pauseMax)); w.emote = -1;
    if (w.node >= 0 && s.nodes[w.node].hasHeading && Rand01(s) < 0.5f) w.wantHeading = s.nodes[w.node].heading;
    else w.wantHeading = NormAngle(w.heading + RandRange(s, -1.0f, 1.0f));
}

inline int32_t PickChatEmote(State& s, uint32_t mask) {
    // mostly talk, sometimes exclamation/question/laugh/cheer, whichever the model has
    const float r = Rand01(s);
    const int32_t order[5] = { 0, 2, 3, 5, 4 };
    int32_t choice = (r < 0.6f) ? 0 : order[1 + Rand(s) % 4];
    if (!(mask & (1u << choice))) choice = 0;
    return (mask & (1u << choice)) ? kEmoteIds[choice] : -1;
}

// Distance from a waypoint to the nearest other character (where it stands) or other character's reserved destination.
inline float Clearance(const State& s, int32_t node, int32_t who) {
    float best = 1000.0f;
    const int32_t nodeCount = static_cast<int32_t>(s.nodes.size());
    for (size_t k = 0; k < s.walkers.size(); k++) {
        if (static_cast<int32_t>(k) == who) continue;
        const Walker& o = s.walkers[k];
        if (!o.placed) continue;
        best = std::min(best, Dist(o.x, o.y, s.nodes[node].x, s.nodes[node].y));
        if (o.reserved >= 0 && o.reserved < nodeCount) best = std::min(best, Dist(s.nodes[o.reserved].x, s.nodes[o.reserved].y, s.nodes[node].x, s.nodes[node].y));
    }
    return best;
}

// From a list of candidate waypoints keep the roomy ones (clearance >= spread); when none is roomy keep those close to the best clearance.
inline void KeepRoomy(const State& s, int32_t who, float spread, std::vector<int32_t>& v) {
    if (v.size() < 2) return;
    std::vector<int32_t> roomy;
    float bestClear = 0.0f;
    std::vector<float> clear(v.size());
    for (size_t k = 0; k < v.size(); k++) { clear[k] = Clearance(s, v[k], who); bestClear = std::max(bestClear, clear[k]); }
    const float limit = bestClear >= spread ? spread : bestClear * 0.8f;
    for (size_t k = 0; k < v.size(); k++) if (clear[k] >= limit) roomy.push_back(v[k]);
    if (!roomy.empty()) v = roomy;
}

inline void ChooseActivity(State& s, int32_t who, const Config& cfg, const std::vector<CharIn>& in, int32_t maxWalking) {
    Walker& w = s.walkers[who];
    const int32_t nodeCount = static_cast<int32_t>(s.nodes.size());
    const int32_t walking = CountMode(s, M_Walk);
    const bool force = w.forceMove;
    w.forceMove = false;
    const bool canWalk = (walking < maxWalking || force) && in[who].hasWalk && w.node >= 0;

    // available targets
    std::vector<int32_t> wander, drink, lean, away, sit, circle;
    bool someoneInCircle = false;
    if (canWalk) {
        for (int32_t i = 0; i < nodeCount; i++) {
            if (i == w.node || !NodeFree(s, i, who)) continue;
            const float d = Dist(w.x, w.y, s.nodes[i].x, s.nodes[i].y);
            switch (s.nodes[i].kind) {
                case K_Drink: drink.push_back(i); break;
                case K_Lean: lean.push_back(i); break;
                case K_Away: away.push_back(i); break;
                case K_Sit: {
                    const int32_t bit = EmoteBit(s.nodes[i].anim > 0 ? s.nodes[i].anim : 103);
                    if (bit >= 0 && (in[who].emoteMask & (1u << bit))) sit.push_back(i);
                    break;
                }
                case K_Circle: if (in[who].emoteMask & 1u) circle.push_back(i); break;
                case K_Transit: case K_Entrance: break;     // passable only: never a place to stand or walk to
                default: if (d > 2.0f) wander.push_back(i); break;
            }
        }
    }
    // prefer nearby destinations (less walking), then destinations with nobody else around (nobody lingers on top of someone else unless they are talking)
    if (!wander.empty()) {
        std::vector<int32_t> nearby;
        for (int32_t i : wander) if (Dist(w.x, w.y, s.nodes[i].x, s.nodes[i].y) <= cfg.range) nearby.push_back(i);
        if (!nearby.empty()) wander = nearby;
        KeepRoomy(s, who, cfg.spread, wander);
    }
    KeepRoomy(s, who, cfg.spread * 0.6f, drink);
    KeepRoomy(s, who, cfg.spread * 0.6f, lean);
    // chat partners
    std::vector<int32_t> partners;
    if (canWalk && !force && (in[who].emoteMask & 1u)) {
        for (int32_t j = 0; j < static_cast<int32_t>(s.walkers.size()); j++) {
            if (j == who) continue;
            const Walker& o = s.walkers[j];
            if (!o.placed || o.partner >= 0 || o.selected || !(in[j].emoteMask & 1u) || !in[j].ready) continue;
            if (o.mode != M_Pause && o.mode != M_Idle) continue;
            if (o.node < 0 || Dist(o.x, o.y, s.nodes[o.node].x, s.nodes[o.node].y) > 0.6f) continue;
            partners.push_back(j);
        }
    }

    const float wWander = wander.empty() ? 0.0f : cfg.wanderWeight;
    const float wStand = force ? 0.0f : cfg.standWeight;
    const float wDrink = drink.empty() ? 0.0f : 12.0f;
    const float wLean = lean.empty() ? 0.0f : 12.0f;
    const float wChat = partners.empty() ? 0.0f : 18.0f;
    const float wAway = away.empty() ? 0.0f : 6.0f;
    for (int32_t i = 0; i < nodeCount; i++) if (s.nodes[i].kind == K_Circle && s.nodes[i].reservedBy >= 0 && s.nodes[i].reservedBy != who) someoneInCircle = true;
    const float wSit = sit.empty() ? 0.0f : 20.0f;
    const float wCircle = circle.empty() ? 0.0f : (someoneInCircle ? 50.0f : 24.0f);     // a started circle draws more people in
    const float total = wWander + wStand + wDrink + wLean + wChat + wAway + wSit + wCircle;
    float r = Rand01(s) * total;

    auto pick = [&](const std::vector<int32_t>& v) { return v[Rand(s) % v.size()]; };

    auto walkToNode = [&](int32_t dest, Arrive arrive, Kind kind) -> bool {
        const std::vector<int32_t> path = FindPath(s, w.node, dest);
        if (path.empty()) return false;
        const Node& dn = s.nodes[dest];
        // a spot can ask to be stood on slightly off its waypoint (closer to the counter or the wall): a final free point along its heading
        const bool shifted = dn.hasHeading && std::fabs(dn.shift) > 0.01f;
        if (!StartWalk(s, who, path, shifted, dn.x + std::cos(dn.heading) * dn.shift, dn.y + std::sin(dn.heading) * dn.shift, dn.z + dn.lift, arrive)) return false;
        Release(s, who);
        w.reserved = dest; s.nodes[dest].reservedBy = who; w.spotKind = kind;
        return true;
    };

    if ((r -= wWander) < 0.0f) { if (walkToNode(pick(wander), A_Pause, K_None)) return; }
    else if ((r -= wDrink) < 0.0f) { if (walkToNode(pick(drink), A_Long, K_Drink)) return; }
    else if ((r -= wLean) < 0.0f) { if (walkToNode(pick(lean), A_Long, K_Lean)) return; }
    else if ((r -= wAway) < 0.0f) { if (walkToNode(pick(away), A_Long, K_Away)) return; }
    else if ((r -= wSit) < 0.0f) { if (walkToNode(pick(sit), A_Long, K_Sit)) return; }
    else if ((r -= wCircle) < 0.0f) { if (walkToNode(pick(circle), A_Long, K_Circle)) return; }
    else if ((r -= wChat) < 0.0f) {
        const int32_t j = pick(partners);
        Walker& o = s.walkers[j];
        // a free neighbour node of the partner's node to approach from, then stop 1.6 yd from the partner on that link
        std::vector<int32_t> options;
        for (int32_t nb : s.nodes[o.node].nbr) if (NodeFree(s, nb, who)) options.push_back(nb);
        if (!options.empty()) {
            const int32_t nb = pick(options);
            const Node& a = s.nodes[o.node];
            const Node& b = s.nodes[nb];
            const float len = std::max(0.01f, Dist(a.x, a.y, b.x, b.y));
            const float k = std::min(1.0f, 1.6f / len);
            const float fx = a.x + (b.x - a.x) * k, fy = a.y + (b.y - a.y) * k, fz = a.z + (b.z - a.z) * k;
            std::vector<int32_t> path = FindPath(s, w.node, nb);
            if (w.node == nb || !path.empty()) {
                if (StartWalk(s, who, path, true, fx, fy, fz, A_Chat)) {
                    Release(s, who);
                    w.reserved = nb; s.nodes[nb].reservedBy = who;
                    w.partner = j; o.partner = who; o.mode = M_ChatWait; o.timer = 60.0f; o.emote = -1;
                    o.wantHeading = std::atan2(fy - o.y, fx - o.x);
                    return;
                }
            }
        }
    }
    // stand a while longer (also the fallback when a chosen walk could not start)
    StartPause(s, who, cfg);
}

inline void EnterLong(State& s, int32_t who, const Config& cfg) {
    Walker& w = s.walkers[who];
    w.mode = M_Long; w.emote = -1; w.emoteTimer = RandRange(s, 2.0f, 6.0f);
    if (w.spotKind == K_Away) w.timer = RandRange(s, cfg.awayMin, std::max(cfg.awayMin, cfg.awayMax));
    else w.timer = RandRange(s, cfg.longMin, std::max(cfg.longMin, cfg.longMax));
    if (w.node >= 0) {
        const Node& n = s.nodes[w.node];
        if (n.hasHeading) w.wantHeading = (w.spotKind == K_Lean) ? n.heading : n.heading;
    }
}

inline void BeginChat(State& s, int32_t a, int32_t b) {
    Walker& wa = s.walkers[a];
    Walker& wb = s.walkers[b];
    wa.mode = M_Chat; wb.mode = M_Chat;
    wa.chatTimer = wb.chatTimer = RandRange(s, 12.0f, 28.0f);
    wa.wantHeading = std::atan2(wb.y - wa.y, wb.x - wa.x);
    wb.wantHeading = std::atan2(wa.y - wb.y, wa.x - wb.x);
    wa.emoteTimer = 0.2f;
    wb.emoteTimer = RandRange(s, 2.0f, 3.5f);
    wa.emote = wb.emote = -1;
    wa.timer = wb.timer = 0.0f;
}

// One frame. dt in seconds. in[i] describes list entry i (only the first in.size() entries are driven). selected = list index of the selected character
// (or -1). camX/camY = the camera in WMO-local yards (the selected character turns to it). out is resized to in.size().
inline void Update(float dt, const Config& cfg, const InnWalk::State& graph, const std::vector<CharIn>& in, int32_t selected, float camX, float camY, std::vector<CharOut>& out) {
    State& s = S();
    out.assign(in.size(), CharOut());
    if (graph.waypoints.size() < 2) return;
    if (s.revision != graph.revision || s.nodes.empty()) {
        s.revision = graph.revision;
        BuildGraph(s, graph);
        s.walkers.clear();
    }
    if (s.walkers.size() != in.size()) s.walkers.resize(in.size());
    dt = std::min(std::max(dt, 0.0f), 0.1f);

    int32_t readyCount = 0;
    for (const CharIn& c : in) if (c.ready) readyCount++;
    const int32_t maxWalking = cfg.maxWalking > 0 ? cfg.maxWalking : std::max(1, (readyCount * 3 + 7) / 8);
    const int32_t nodeCount = static_cast<int32_t>(s.nodes.size());

    // place newly ready characters on random free nodes, staggered
    for (int32_t i = 0; i < static_cast<int32_t>(in.size()); i++) {
        Walker& w = s.walkers[i];
        if (!in[i].ready || w.placed) continue;

        // A character that was just built enters through an entrance spot (hidden from the camera) and walks into the room.
        if (in[i].arrive) {
            std::vector<int32_t> entrances, destinations;
            for (int32_t c = 0; c < nodeCount; c++) {
                if (s.nodes[c].kind == K_Entrance && NodeFree(s, c, i)) entrances.push_back(c);
                if (s.nodes[c].kind == K_None && s.nodes[c].reservedBy < 0 && NodeFree(s, c, i)) destinations.push_back(c);
            }
            KeepRoomy(s, i, cfg.spread, destinations);
            if (!entrances.empty() && !destinations.empty()) {
                const int32_t entrance = entrances[Rand(s) % static_cast<uint32_t>(entrances.size())];
                std::vector<int32_t> path;
                int32_t destination = -1;
                for (int32_t tries = 0; tries < (in[i].coming ? 14 : 12) && (in[i].coming || path.empty()); tries++) {
                    const int32_t candidate = destinations[Rand(s) % static_cast<uint32_t>(destinations.size())];
                    const std::vector<int32_t> candidatePath = FindPath(s, entrance, candidate);
                    if (!candidatePath.empty() && (path.empty() || (in[i].coming && candidatePath.size() < path.size()))) { path = candidatePath; destination = candidate; }
                }
                if (!path.empty()) {
                    w.placed = true; w.node = entrance;
                    w.x = s.nodes[entrance].x; w.y = s.nodes[entrance].y; w.z = s.nodes[entrance].z;
                    const Node& first = s.nodes[path.front()];
                    w.heading = w.wantHeading = std::atan2(first.y - w.y, first.x - w.x);
                    GoIdle(s, i, 0.0f);
                    StartWalk(s, i, path, false, 0.0f, 0.0f, 0.0f, A_Pause);
                    w.reserved = destination; s.nodes[destination].reservedBy = i;
                    w.coming = in[i].coming; w.comeT = 0.0f;
                    continue;
                }
            }
        }

        int32_t start = -1;
        for (int32_t tries = 0; tries < 64 && start < 0; tries++) {
            const int32_t c = static_cast<int32_t>(Rand(s) % static_cast<uint32_t>(nodeCount));
            if (s.nodes[c].kind == K_None && NodeFree(s, c, i)) start = c;
        }
        if (start < 0) for (int32_t c = 0; c < nodeCount && start < 0; c++) if (NodeFree(s, c, i)) start = c;
        if (start < 0) continue;
        w.placed = true; w.node = start; w.x = s.nodes[start].x; w.y = s.nodes[start].y; w.z = s.nodes[start].z;
        w.heading = w.wantHeading = RandRange(s, -kPi, kPi);
        w.reserved = start; s.nodes[start].reservedBy = i;
        GoIdle(s, i, RandRange(s, 0.0f, 14.0f));
    }

    // selection changes
    for (int32_t i = 0; i < static_cast<int32_t>(in.size()); i++) {
        Walker& w = s.walkers[i];
        if (!w.placed) continue;
        const bool nowSel = (i == selected);
        if (nowSel && !w.selected && !w.coming) {
            const int32_t p = w.partner;
            if (p >= 0 && p < static_cast<int32_t>(s.walkers.size())) GoIdle(s, p, RandRange(s, 1.0f, 3.0f));
            if (w.node >= 0 && w.mode == M_Walk) {
                // it keeps its place: reserve the nearest node it stands at
                Release(s, i);
                const int32_t nn = NearestNode(s, w.x, w.y);
                if (nn >= 0 && NodeFree(s, nn, i)) { w.reserved = nn; s.nodes[nn].reservedBy = i; w.node = nn; }
            }
            StopWalking(w);
            w.leaving = false; w.leaveDone = false;
            w.mode = M_Selected; w.partner = -1; w.emote = -1; w.selected = true;
            w.anim = -1;                 // forces an animation (stand) to be sent once
        } else if (!nowSel && w.selected) {
            w.selected = false;
            GoIdle(s, i, RandRange(s, 1.5f, 3.5f));
            w.anim = -1;
        }
    }

    // validate chat pairs
    for (int32_t i = 0; i < static_cast<int32_t>(s.walkers.size()); i++) {
        Walker& w = s.walkers[i];
        if (w.partner < 0) continue;
        const int32_t p = w.partner;
        const bool ok = p < static_cast<int32_t>(s.walkers.size()) && s.walkers[p].partner == i && s.walkers[p].placed &&
            (w.mode == M_Walk || w.mode == M_ChatWait || w.mode == M_Chat);
        if (!ok) { w.partner = -1; if (w.mode == M_ChatWait || w.mode == M_Chat) GoIdle(s, i, RandRange(s, 1.0f, 3.0f)); }
    }

    // per character
    for (int32_t i = 0; i < static_cast<int32_t>(in.size()); i++) {
        Walker& w = s.walkers[i];
        if (!w.placed) continue;
        int32_t wantAnim = kAnimStand;
        const float turnRate = 4.0f;     // rad/s

        // Nobody lingers close to another standing character unless they are talking: after a few seconds the one with the higher list
        // index (or anyone next to the selected character) moves on.
        if (w.mode == M_Idle || w.mode == M_Pause || w.mode == M_Long) {
            bool crowded = false;
            for (int32_t j = 0; j < static_cast<int32_t>(s.walkers.size()) && !crowded; j++) {
                if (j == i || !s.walkers[j].placed) continue;
                const Walker& o = s.walkers[j];
                if (o.mode == M_Chat || o.mode == M_ChatWait || o.mode == M_Walk) continue;
                if (Dist(w.x, w.y, o.x, o.y) < 2.4f && (o.mode == M_Selected || i > j)) crowded = true;
            }
            if (crowded) w.crowd += dt; else w.crowd = std::max(0.0f, w.crowd - dt * 2.0f);
            if (w.crowd > 6.0f) {
                w.crowd = 0.0f;
                GoIdle(s, i, 0.0f);
                w.forceMove = true;
            }
        } else {
            w.crowd = 0.0f;
        }

        // v45: walk out through the nearest door (the caller frees the model when `gone` comes back); cancelled when the caller stops asking
        if (w.leaving && !in[i].leave) { w.leaving = false; w.leaveDone = false; GoIdle(s, i, 1.0f); }
        if (in[i].leave && !w.leaving && i != selected && w.mode != M_Selected && w.node >= 0 && w.node < nodeCount) {
            if (s.nodes[w.node].kind == K_Entrance) {
                GoIdle(s, i, 0.0f); Release(s, i); w.leaving = true; w.leaveDone = true;
            } else {
                std::vector<int32_t> best;
                for (int32_t c = 0; c < nodeCount; c++) {
                    if (s.nodes[c].kind != K_Entrance) continue;
                    std::vector<int32_t> p = FindPath(s, w.node, c);
                    if (!p.empty() && (best.empty() || p.size() < best.size())) best = p;
                }
                if (!best.empty()) {
                    const int32_t p0 = w.partner;
                    if (p0 >= 0 && p0 < static_cast<int32_t>(s.walkers.size())) GoIdle(s, p0, RandRange(s, 1.0f, 3.0f));
                    GoIdle(s, i, 0.0f); Release(s, i);
                    StartWalk(s, i, best, false, 0.0f, 0.0f, 0.0f, A_Pause);
                    w.leaving = true; w.leaveDone = false; w.wantHeading = w.heading;
                }
            }
        }

        switch (w.mode) {
        case M_Selected: {
            w.wantHeading = std::atan2(camY - w.y, camX - w.x);
            wantAnim = -2;               // the caller's greeting emote owns the animation
            if (w.anim == -1) wantAnim = kAnimStand;
            break;
        }
        case M_Idle:
            w.timer -= dt;
            if (w.timer <= 0.0f) ChooseActivity(s, i, cfg, in, maxWalking);
            break;
        case M_Pause:
            w.timer -= dt;
            if (w.timer <= 0.0f) GoIdle(s, i, 0.0f);
            break;
        case M_Long: {
            w.timer -= dt;
            // Lean spots: the retail lean (animation 1260, sometimes 1262) for the whole stay, on models that have it; others just stand.
            if (w.spotKind == K_Lean && w.emote < 0 && (in[i].emoteMask & 64u)) w.emote = ((in[i].emoteMask & 128u) && (Rand(s) & 1u)) ? kEmoteIds[7] : kEmoteIds[6];
            // Seats: the sit animation of the spot (default SitChairMed 103) for the whole stay.
            if (w.spotKind == K_Sit && w.emote < 0 && w.node >= 0) {
                const int32_t bit = EmoteBit(s.nodes[w.node].anim > 0 ? s.nodes[w.node].anim : 103);
                if (bit >= 0 && (in[i].emoteMask & (1u << bit))) w.emote = kEmoteIds[bit];
            }
            // Talking circle: facing the middle, talk / gesture emotes with pauses (like a chat, without a partner).
            if (w.spotKind == K_Circle) {
                w.emoteTimer -= dt;
                if (w.emoteTimer <= 0.0f) {
                    if (w.emote < 0) { w.emote = PickChatEmote(s, in[i].emoteMask); w.emoteTimer = RandRange(s, 2.5f, 4.5f); if (w.emote < 0) w.emoteTimer = 2.0f; }
                    else { w.emote = -1; w.emoteTimer = RandRange(s, 2.0f, 6.0f); }
                }
            }
            if (w.spotKind == K_Drink && (in[i].emoteMask & 2u)) {
                w.emoteTimer -= dt;
                if (w.emoteTimer <= 0.0f) {
                    if (w.emote < 0) { w.emote = kEmoteIds[1]; w.emoteTimer = RandRange(s, 4.0f, 8.0f); }
                    else { w.emote = -1; w.emoteTimer = RandRange(s, 3.0f, 7.0f); }
                }
            }
            if (w.timer <= 0.0f) GoIdle(s, i, 0.0f);
            break;
        }
        case M_ChatWait:
            w.timer -= dt;
            if (w.timer <= 0.0f) GoIdle(s, i, 1.0f);
            break;
        case M_Chat: {
            w.chatTimer -= dt;
            w.emoteTimer -= dt;
            if (w.emoteTimer <= 0.0f) {
                if (w.emote < 0) { w.emote = PickChatEmote(s, in[i].emoteMask); w.emoteTimer = RandRange(s, 2.5f, 4.5f); if (w.emote < 0) w.emoteTimer = 2.0f; }
                else { w.emote = -1; w.emoteTimer = RandRange(s, 1.0f, 2.5f); }
            }
            if (w.chatTimer <= 0.0f) {
                const int32_t p = w.partner;
                StartPause(s, i, cfg);
                w.partner = -1;
                if (p >= 0) { Walker& o = s.walkers[p]; StartPause(s, p, cfg); o.partner = -1; }
            }
            break;
        }
        case M_Walk: {
            if (w.coming) { w.comeT += dt; if (w.comeT > 40.0f) w.coming = false; }
            const bool running = w.coming && w.comeT > cfg.comeWalkSec;
            wantAnim = (running && in[i].hasRun) ? kAnimRun : kAnimWalk;
            if (!w.haveSeg) {
                // next target: next node on the path, else the free final point, else we have arrived
                float ttx, tty, ttz;
                bool have = false;
                if (!w.path.empty()) { const Node& n = s.nodes[w.path.front()]; ttx = n.x; tty = n.y; ttz = n.z; have = true; }
                else if (w.hasFinal) { ttx = w.fx; tty = w.fy; ttz = w.fz; have = true; w.hasFinal = false; }
                else { ttx = tty = ttz = 0.0f; }
                if (!have) {
                    // arrived
                    if (w.leaving) { w.leaveDone = true; wantAnim = kAnimStand; break; }
                    w.coming = false;
                    w.node = (w.reserved >= 0) ? w.reserved : NearestNode(s, w.x, w.y);
                    wantAnim = kAnimStand;
                    if (w.arrive == A_Chat && w.partner >= 0) {
                        BeginChat(s, i, w.partner);
                        w.node = NearestNode(s, w.x, w.y);
                    } else if (w.arrive == A_Long) EnterLong(s, i, cfg);
                    else StartPause(s, i, cfg);
                    break;
                }
                w.sx = w.x; w.sy = w.y; w.sz = w.z; w.tx = ttx; w.ty = tty; w.tz = ttz;
                w.segLen = std::max(0.001f, Dist(w.sx, w.sy, w.tx, w.ty)); w.segDone = 0.0f; w.haveSeg = true;
            }
            w.wantHeading = std::atan2(w.ty - w.y, w.tx - w.x);
            const float err = std::fabs(NormAngle(w.wantHeading - w.heading));
            if (err < 0.6f) {
                // keep clear of others
                bool blockedNow = false;
                const float speed = std::max(0.2f, cfg.speed) * (running ? std::max(1.0f, cfg.runSpeedMult) : 1.0f);
                const float step = speed * dt;
                const float nx = w.x + std::cos(w.heading) * step, ny = w.y + std::sin(w.heading) * step;
                for (int32_t j = 0; j < static_cast<int32_t>(s.walkers.size()); j++) {
                    if (j == i || !s.walkers[j].placed) continue;
                    const Walker& o = s.walkers[j];
                    if (w.partner == j && w.path.empty() && !w.hasFinal) continue;
                    if (Dist(nx, ny, o.x, o.y) < 0.85f && Dist(nx, ny, o.x, o.y) < Dist(w.x, w.y, o.x, o.y) + 0.001f) {
                        if (w.partner == j) continue;                      // walking up to the chat partner is fine
                        if (o.mode != M_Walk || i > j) blockedNow = true;  // the lower index of two walkers keeps going
                    }
                }
                if (blockedNow && w.blocked < 6.0f) { w.blocked += dt; wantAnim = kAnimStand; }
                else {
                    w.blocked = 0.0f;
                    w.segDone = std::min(w.segLen, w.segDone + step);
                    const float t = w.segDone / w.segLen;
                    w.x = w.sx + (w.tx - w.sx) * t; w.y = w.sy + (w.ty - w.sy) * t; w.z = w.sz + (w.tz - w.sz) * t;
                    if (w.segDone >= w.segLen - 0.0001f) {
                        w.haveSeg = false;
                        if (!w.path.empty()) { w.node = w.path.front(); w.path.erase(w.path.begin()); }
                    }
                }
                if (w.blocked > 3.0f && w.partner < 0 && !w.leaving) { GoIdle(s, i, RandRange(s, 0.5f, 2.0f)); wantAnim = kAnimStand; }
            } else {
                wantAnim = kAnimStand;       // turning on the spot first
            }
            break;
        }
        }

        // turn toward the wanted heading
        {
            const float diff = NormAngle(w.wantHeading - w.heading);
            const float maxTurn = turnRate * dt;
            w.heading = NormAngle(w.heading + std::max(-maxTurn, std::min(maxTurn, diff)));
        }

        // animation request: an emote while one is set, else stand/walk
        int32_t anim = wantAnim;
        if (wantAnim == kAnimStand && w.emote >= 0 && (w.mode == M_Long || w.mode == M_Chat)) anim = w.emote;
        CharOut& o = out[i];
        if (w.leaving && w.leaveDone) { Release(s, i); o.gone = true; w = Walker(); continue; }
        o.valid = true; o.x = w.x; o.y = w.y; o.z = w.z; o.heading = w.heading;
        if (anim == -2) { o.anim = w.anim; o.animChanged = false; }
        else if (anim != w.anim) { w.anim = anim; o.anim = anim; o.animChanged = true; }
        else { o.anim = anim; o.animChanged = false; }
    }

    // stats
    s.stats = Stats();
    for (const Walker& w : s.walkers) {
        if (!w.placed) continue;
        switch (w.mode) {
            case M_Walk: s.stats.walking++; break;
            case M_Pause: case M_Idle: s.stats.paused++; break;
            case M_Long: s.stats.longStay++; break;
            case M_Chat: case M_ChatWait: s.stats.chatting++; break;
            case M_Selected: s.stats.selected++; break;
        }
    }
}

}
