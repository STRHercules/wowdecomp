#pragma once
#include "SkateLogic/xbox-input.h"

namespace skate {
struct Controls { bool active = false, push = false, brake = false; int16_t steer = 0, pop = 0, flip = 0; };
// Right stick Y past ~9000 crouches (down) or arms a pop (up), and the engine then ignores push.
// Resting/drifting sticks must read 0: XInput's right-thumb deadzone.
inline int16_t Pop(int16_t ry) { return ry > 8689 || ry < -8689 ? ry : int16_t(0); }
class InputGate {
    xboxinput::ResumeGate resume_;
    xboxinput::Toggle toggle_;
    bool mode_ = true;
public:
    Controls Step(bool focused, bool held, bool connected, const xboxinput::Pad& pad) {
        if (toggle_.Step(connected, focused, pad.buttons)) mode_ = !mode_;
        const bool active = resume_.Step(focused && held && mode_, pad);
        return {active, active && (pad.buttons & 0x1000) != 0,
            active && (pad.buttons & 0x2000) != 0, active ? pad.lx : int16_t(0), active ? Pop(pad.ry) : int16_t(0),
            active ? Pop(pad.rx) : int16_t(0)}; // right stick X: flip direction (same deadzone)
    }
};
}
