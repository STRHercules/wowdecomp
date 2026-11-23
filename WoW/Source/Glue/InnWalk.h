#pragma once
// Claude 2026-11-15: walking area of the inn hall for the character select (stage 3, step 3.2b = debug markers only; nothing moves yet).
// InnWalk.cfg (next to InnCast.cfg, re-read within half a second of a change) lists waypoints and links in WMO-local yards:
//   FLOOR_Z=-0.19          hall floor height (WMO-local)
//   WP=<id> <x> <y>        a point characters may stand on / walk to
//   LINK=<a> <b>           a straight walk between two points
// The coordinates go through the same placement as the WMO (WOW_INN_WMO_X/Y/Z/YAW/SCALE), so they follow the room when it is moved.
// WOW_INN_WALK_DEBUG=1 (InnCast.cfg) shows a marker model on every waypoint (big) and small beads along every link, so the layout can be
// judged by eye. WOW_INN_WALK_MARKER=<m2 path> overrides the marker model (default: the inn's own green bottle, scaled).
// Markers are ordinary scene M2 models and follow the draw rules learned for the furniture (re-link every frame, 0x8|0x10000 every frame).

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Tempest/C44Matrix.h"
#include "Engine/Source/Tempest/C3Vector.h"

namespace InnWalk {

struct Waypoint { int32_t id = 0; float x = 0.0f, y = 0.0f; float z = 0.0f; bool hasZ = false; };
struct Link { int32_t a = 0, b = 0; };
struct Spot { int32_t id = 0; std::string kind; float headingDeg = 0.0f; bool hasHeading = false; float shift = 0.0f; int32_t anim = 0; float lift = 0.0f; };   // SPOT=<wp> <drink|lean|away|sit|circle|transit|entrance> [heading deg [shift yd along the heading [animation id [lift yd]]]]
struct Marker { float x = 0.0f, y = 0.0f, z = 0.0f, scale = 1.0f; CM2Model* model = nullptr; bool failed = false; };

struct State {
    float floorZ = -0.19f;
    std::vector<Waypoint> waypoints;
    std::vector<Link> links;
    std::vector<Spot> spots;
    long long stamp = -2;
    uint32_t lastCheck = 0;
    uint32_t revision = 0;          // bumped when the file is re-read, so the markers are rebuilt
    std::vector<Marker> markers;
    uint32_t builtRevision = 0xFFFFFFFFu;
    std::string builtModel;
    uint32_t lastLog = 0;
};
inline State& S() { static State s; return s; }
inline void Forget() { S() = State(); }

// Reads InnWalk.cfg when its modification time/size changed (checked at most every 500 ms). now = OsGetAsyncTimeMs().
inline void Reload(uint32_t now) {
    State& s = S();
    if (s.lastCheck && now - s.lastCheck < 500) return;
    s.lastCheck = now;
    struct _stat64 info;
    const long long stamp = _stat64("InnWalk.cfg", &info) == 0 ? static_cast<long long>(info.st_mtime) * 1000003LL + static_cast<long long>(info.st_size) : -1;
    if (stamp == s.stamp) return;
    s.stamp = stamp;
    s.waypoints.clear();
    s.links.clear();
    s.spots.clear();
    s.revision++;
    if (stamp < 0) return;
    std::ifstream file("InnWalk.cfg");
    std::string line;
    while (std::getline(file, line)) {
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) key.erase(key.begin());
        std::istringstream value(line.substr(eq + 1));
        if (key == "FLOOR_Z") { float z; if (value >> z) s.floorZ = z; }
        else if (key == "WP") { Waypoint w; if (value >> w.id >> w.x >> w.y) { if (value >> w.z) w.hasZ = true; s.waypoints.push_back(w); } }
        else if (key == "LINK") { Link l; if (value >> l.a >> l.b) s.links.push_back(l); }
        else if (key == "SPOT") { Spot sp; if (value >> sp.id >> sp.kind) { if (value >> sp.headingDeg) { sp.hasHeading = true; if (value >> sp.shift) { if (value >> sp.anim) value >> sp.lift; } } s.spots.push_back(sp); } }
    }
    sExtensionMgr->Log("InnWalk.cfg read: %u waypoints, %u links, floor z %.2f", static_cast<uint32_t>(s.waypoints.size()), static_cast<uint32_t>(s.links.size()), s.floorZ);
}

inline const Waypoint* Find(const State& s, int32_t id) {
    for (const Waypoint& w : s.waypoints) if (w.id == id) return &w;
    return nullptr;
}

// Waypoint markers (big) plus beads every ~1.5 yd along each link (small).
inline void Rebuild(State& s, float waypointScale, float beadScale) {
    s.markers.clear();
    for (const Waypoint& w : s.waypoints) { Marker m; m.x = w.x; m.y = w.y; m.z = w.hasZ ? w.z : s.floorZ; m.scale = waypointScale; s.markers.push_back(m); }
    for (const Link& l : s.links) {
        const Waypoint* a = Find(s, l.a);
        const Waypoint* b = Find(s, l.b);
        if (!a || !b) continue;
        const float len = std::hypot(b->x - a->x, b->y - a->y);
        const int32_t beads = static_cast<int32_t>(len / 1.5f);
        for (int32_t i = 1; i <= beads; i++) {
            const float t = static_cast<float>(i) / static_cast<float>(beads + 1);
            const float za = a->hasZ ? a->z : s.floorZ, zb = b->hasZ ? b->z : s.floorZ;
            Marker m; m.x = a->x + (b->x - a->x) * t; m.y = a->y + (b->y - a->y) * t; m.z = za + (zb - za) * t; m.scale = beadScale; s.markers.push_back(m);
        }
    }
}

// Called every frame from the backdrop hook while the inn is up. placement = InnWmo::Placement(...). markerM2 = scene path of the model to use.
inline void Debug(bool enabled, bool showLinks, const std::string& markerM2, float waypointScale, float beadScale, const C44Matrix& placement, CM2Scene* scene,
    void* lightArg, void (*lightCallback)(CM2Model*, CM2Lighting*, void*), uint32_t now) {
    State& s = S();
    Reload(now);
    if (!enabled || !scene || markerM2.empty() || s.waypoints.empty()) return;

    if (s.builtRevision != s.revision || s.builtModel != markerM2) {
        s.builtRevision = s.revision;
        s.builtModel = markerM2;
        Rebuild(s, waypointScale, beadScale);
        sExtensionMgr->Log("InnWalk markers: %u (model %s)", static_cast<uint32_t>(s.markers.size()), markerM2.c_str());
    }

    int32_t made = 0;
    uint32_t shown = 0, failed = 0;
    const size_t limit = showLinks ? s.markers.size() : std::min(s.markers.size(), s.waypoints.size());
    for (size_t i = 0; i < limit; i++) {
        Marker& m = s.markers[i];
        if (m.failed) { failed++; continue; }
        if (!m.model) {
            if (made >= 8) break;
            m.model = scene->CreateModel(markerM2.c_str(), 0);
            made++;
            if (!m.model) { m.failed = true; failed++; continue; }
            m.model->m_fFlags |= 0x8000;
            m.model->m_lightingCallback = lightCallback;
            m.model->m_lightingArg = lightArg;
            m.model->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 1, 1);
        }
        m.model->SetAnimating(1);
        m.model->m_fFlags |= 0x8 | 0x10000;
        C44Matrix w;
        w.Translate(C3Vector(m.x, m.y, m.z));
        w.Scale(m.scale);
        w *= placement;
        m.model->m_worldTransform = w;
        shown++;
    }
    if (now - s.lastLog >= 5000) {
        s.lastLog = now;
        sExtensionMgr->Log("InnWalk debug: markers shown=%u failed=%u of %u", shown, failed, static_cast<uint32_t>(s.markers.size()));
    }
}

}
