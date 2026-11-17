// Phase 5 camera follow: the donor's easing law, mouse-look yield and the stock fallback.
#include "SkateCamera.h"
#include <cassert>

static bool Near(float a, float b, float e = 1e-4f) { return fabsf(a - b) <= e; }

int main() {
    using skatecamera::Follow;
    Follow f;
    // Not applicable: the stock yaw is returned untouched and the state is clear.
    assert(f.Step(1.f, false, 2.f, 5.f, false, 0) == 1.f);
    // First step seeds from the stock view (no jump), time zero.
    assert(Near(f.Step(0.f, true, 1.f, 5.f, false, 1000), 0.f));
    // 100 ms at 5 yd/s moves 1 - exp(-0.25) of the way to the travel heading.
    float yaw = f.Step(0.f, true, 1.f, 5.f, false, 1100);
    assert(Near(yaw, 1.f * (1.f - expf(-.25f)), 1e-3f));
    // The stock yaw keeps turning with the character (it faces the board's heading); the held yaw does not follow it.
    const float second = f.Step(2.f, true, 1.f, 5.f, false, 1200);
    assert(second > yaw && second < 1.f);
    // Stopped (<= 0.5 yd/s): the yaw holds.
    const float held = f.Step(2.f, true, 1.f, .3f, false, 1300);
    assert(Near(held, second));
    // A mouse button adopts the actual view (no camera write), release resumes from it.
    assert(Near(f.Step(4.f, true, 1.f, 5.f, true, 1400), 4.f));
    const float resumed = f.Step(4.f, true, 1.f, 5.f, false, 1500);
    assert(resumed < 4.f && resumed > 1.f);
    // Shortest way around: stock 6.2 rad toward 0.1 rad crosses 2 pi, result stays in [0, 2 pi).
    Follow w;
    w.Step(6.2f, true, .1f, 5.f, false, 0);
    const float wrapped = w.Step(6.2f, true, .1f, 5.f, false, 100);
    assert(wrapped >= 0.f && wrapped < 6.2831855f && (wrapped > 6.2f || wrapped < .1f));
    // A stale clock (> 100 ms) and bad numbers never move the yaw by more than the law allows.
    Follow s;
    s.Step(1.f, true, 2.f, 5.f, false, 0);
    assert(Near(s.Step(1.f, true, 2.f, 5.f, false, 5000), 1.f));
    assert(Near(s.Step(1.f, true, NAN, 5.f, false, 5100), 1.f));
    assert(Near(s.Step(1.f, true, 2.f, 500.f, false, 5200), 1.f)); // speed out of range: stock
    // Leaving the skate clears the state: the next use seeds from the stock view again.
    s.Step(3.f, false, 0, 0, false, 5300);
    assert(Near(s.Step(3.f, true, 0.f, 5.f, false, 5400), 3.f));
    return 0;
}
