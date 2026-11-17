#pragma once
#include <algorithm>
#include <cmath>

// Engine-independent controller; the local adapter applies this through the
// audited native run-speed setter. Live integration is checked separately.
struct RollingController {
    bool enabled = false;
    float speed = 0;
    float maximum = 4.0f;
    void Disable() { enabled = false; speed = 0; }
    void Step(float elapsed, bool push, bool brake) {
        if (!enabled) { speed = 0; return; }
        if (!std::isfinite(elapsed) || elapsed <= 0) return;
        elapsed = (std::min)(elapsed, 0.05f);
        const float acceleration = brake ? -8.0f : push ? 3.0f : -0.8f;
        speed = (std::max)(0.0f, (std::min)(maximum, speed + acceleration*elapsed));
        if (speed < 0.01f) speed = 0;
    }
};
