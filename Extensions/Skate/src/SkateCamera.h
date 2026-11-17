// Phase 5 camera: the donor's follow law (camera-follow.h ViewYaw) behind one stateful step. Pure, tested.
// The camera yaw eases toward the board's travel heading at 2.5/s while the board moves, holds when it
// stops, and yields to either mouse button (WoW-style mouse look), exactly as the donor's CameraAnglesHook.
#pragma once
#include "SkateLogic/camera-follow.h"
#include <cmath>
#include <cstdint>

namespace skatecamera {
struct Follow {
    camerafollow::ViewYaw view;
    camerafollow::Clock clock;
    void Clear() { view.Clear(); clock.Clear(); }
    // composed: the stock composed world yaw; target: travel heading (facing convention); speed in yd/s.
    // Returns the yaw to use this frame (composed itself whenever the follow does not apply).
    float Step(float composed, bool valid, float target, float speed, bool looking, uint32_t now) {
        if (!valid || !std::isfinite(composed)) { Clear(); return composed; }
        float desired = composed, relative = 0;
        if (!view.Step(composed, target, clock.Step(now) , speed, looking, desired, relative)) return composed;
        desired = std::fmod(desired, camerafollow::Turn);
        return desired < 0 ? desired + camerafollow::Turn : desired;
    }
    // UpdateTargetFacing already built the stock basis. Keep it and the persistent relative yaw in sync
    // with the returned angle before native camera collision/boom code consumes either of them.
    template<class Camera>
    void Apply(Camera& camera, float& yaw, float pitch, float roll, bool valid,
               float target, float speed, bool looking, uint32_t now) {
        if (!std::isfinite(pitch) || !std::isfinite(roll)) { Clear(); return; }
        const float desired = Step(yaw,valid,target,speed,looking,now);
        const float delta = camerafollow::Delta(desired,yaw);
        if (valid && !looking && std::isfinite(delta) && fabsf(delta)>.000001f) {
            camera.UpdateYaw(delta);
            yaw = desired;
            camera.SetFacing(yaw,pitch,roll);
        }
    }
};
}
