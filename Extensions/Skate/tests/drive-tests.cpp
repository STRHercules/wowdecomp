#include "SkateDrive.h"
#include <cassert>

using namespace skatedrive;
static bool Near(float a, float b, float e = 1e-3f) { return fabsf(a - b) <= e; }

int main() {
    // World mapping inverts the helper's (dx, dz, -dy) * 0.9144.
    const float origin[3]{100, 200, 50}, engine[3]{.9144f, 1.8288f, -2.7432f};
    float world[3];
    World(origin, engine, world);
    assert(Near(world[0], 101) && Near(world[1], 203) && Near(world[2], 52));

    Frame f; f.valid = true;
    for (int i = 0; i < 3; ++i) f.origin[i] = origin[i];
    const float at[3]{100, 200, 50};
    Driver d;
    // Board pushing along engine +x (WoW north) at 4 m/s: forward, facing 0, speed in yards.
    f.velocity[0] = 4;
    auto c = d.Step(f, at, 14, false, 0);
    assert(c.drive && c.forward && !c.jump && !c.resync && Near(c.speed, 4 / Yards) && Near(c.facing, 0));
    // Engine -z is WoW +y (west): facing pi/2.
    f.velocity[0] = 0; f.velocity[2] = -4;
    c = d.Step(f, at, 14, false, 0);
    assert(c.forward && Near(c.facing, 1.5707963f));
    // Engine +z is WoW -y: facing wraps to 3pi/2, never negative.
    f.velocity[2] = 4;
    c = d.Step(f, at, 14, false, 0);
    assert(Near(c.facing, 4.712389f));
    // Downhill momentum is capped by the server allowance (7 yd/s without the GM speed tier).
    f.velocity[2] = 0; f.velocity[0] = 12;
    c = d.Step(f, at, 7, false, 0);
    assert(c.forward && Near(c.speed, 7));
    // Braked to a stop: still driven (mode kept), forward released.
    f.velocity[0] = .05f;
    c = d.Step(f, at, 14, false, 0);
    assert(c.drive && !c.forward && c.speed == 0);
    // Regression (Phase 3 live test): the character capped at 7 yd/s fell a few yards behind the board; that
    // must keep driving on the board's velocity, not re-init (about 9 s, and it looped).
    f.velocity[0] = 4;
    const float behind[3]{95, 200, 50};
    c = d.Step(f, behind, 14, false, 0);
    assert(c.drive && c.forward && !c.resync && Near(c.speed, 4 / Yards));
    // Steering (heading change) then pushing (speed rise) while behind still reaches the mover.
    f.velocity[0] = 3; f.velocity[2] = -6;
    c = d.Step(f, behind, 14, false, 0);
    assert(c.forward && !c.resync && Near(c.speed, hypotf(3, 6) / Yards) && c.facing > 1.f && c.facing < 1.2f);
    // Only a large split resyncs.
    const float far[3]{125, 200, 50};
    c = d.Step(f, far, 14, false, 0);
    assert(c.resync && !c.drive && !c.forward);
    const float low[3]{100, 200, 38};
    assert(d.Step(f, low, 14, false, 0).resync);
    // A frozen pose (helper busy re-initializing) stops the character instead of running on stale velocity.
    Driver frozen;
    f.tick = 7;
    assert(frozen.Step(f, at, 14, false, 1000).forward);
    assert(frozen.Step(f, at, 14, false, 1000 + StallMs).forward);
    assert(!frozen.Step(f, at, 14, false, 1001 + StallMs).drive);
    f.tick = 8;
    assert(frozen.Step(f, at, 14, false, 1100 + StallMs).forward);
    assert(!d.Step(Frame{}, at, 14, false, 0).drive);

    // Ollie: pop, then the board leaves the ground inside the window -> one jump with its velocity.
    Driver o;
    f.airborne = false;
    ++f.tick; o.Step(f, at, 14, true, 1000);
    f.airborne = true; f.velocity[1] = 4.5f;
    ++f.tick; c = o.Step(f, at, 14, false, 1200);
    assert(c.jump && Near(c.jumpVelocity, -4.5f / Yards));
    ++f.tick; assert(!o.Step(f, at, 14, false, 1216).jump);  // only on the takeoff edge
    // Rolling off a ledge without a pop: no jump, the native fall takes over.
    Driver ledge;
    f.airborne = false; ++f.tick; ledge.Step(f, at, 14, false, 0);
    f.airborne = true;
    ++f.tick; assert(!ledge.Step(f, at, 14, false, 100).jump);
    // A stale pop does not fire.
    Driver stale;
    f.airborne = false; ++f.tick; stale.Step(f, at, 14, true, 0);
    f.airborne = true;
    ++f.tick; assert(!stale.Step(f, at, 14, false, PopWindowMs).jump);
    // A weak lift falls back to the stock jump, a huge one is clamped.
    assert(TakeoffVelocity(.2f) == StockJump && TakeoffVelocity(NAN) == StockJump);
    assert(Near(TakeoffVelocity(100), -20));
    // The native jump is queued, not synchronous. Only its later accepted callback can start an arc.
    {
        PendingJump pending; JumpRequest request; Arc arc;
        pending.Arm({-5,1,4,99},1000);
        assert(!arc.Active()); // Returning from TryJumpOrAscend must not cancel this request.
        assert(pending.Take(1032,request)); // Simulated movement-queue dispatch on a later tick.
        arc.Start(request.rootY,request.vy);
        assert(arc.Active() && request.velocity==-5 && request.tick==99);
        assert(!pending.Take(1048,request)); // The event cannot consume it twice.
        arc.Stop(); pending.Arm({-5,1,4,100},2000);
        assert(!pending.Take(2500,request) && !arc.Active()); // Refused/delayed past the ownership window.
        pending.Arm({-5,1,4,101},3000); pending.Clear();
        assert(!pending.Take(3016,request)); // J release, focus loss, bail or map change before dispatch.
        pending.Arm({-5,1,4,102},UINT32_MAX-10);
        assert(pending.Take(5,request)); // Native millisecond clock wrap.
    }
    // Sampled ollie: movement consumes only the remaining step, not the accumulated takeoff displacement again.
    {
        Arc arc; arc.Start(0,3 * Yards);
        arc.Add(.1f,.3f*Yards,3*Yards);
        arc.Add(.2f,.6f*Yards,3*Yards);
        arc.Add(.3f,.8f*Yards,0);
        arc.Add(.4f,.6f*Yards,-3*Yards);
        float z=50, distance=0;
        for (unsigned ms=10;ms<=400;ms+=10) {
            assert(arc.Step(ms*.001f,z,50,distance)); z-=distance;
            assert(Near(z,50-arc.Distance(ms*.001f)));
            assert(fabsf(distance)<.031f); // No replay of the whole ascent on every movement step.
        }
        assert(Near(z,50.6f));
        float contact=0;
        assert(arc.Time(-.7f,true,contact) && Near(contact,.25f));
        assert(arc.Time(-.7f,false,contact) && Near(contact,.35f));
        assert(arc.Time(0,false,contact) && contact>.4f && contact<1.f);
        assert(Near(arc.Distance(contact),0));
        // Missing/delayed helper samples continue falling under stock gravity; they never hold a pose in mid-air.
        assert(arc.Distance(.6f)>arc.Distance(.5f));
        const float accepted=arc.Distance(.4f);
        arc.Add(.5f,NAN,0); arc.Add(.5f,0,INFINITY); arc.Add(.1f,100,0);
        assert(arc.Distance(.4f)==accepted);
        assert(!arc.Step(NAN,z,50,distance) && !arc.Step(9,z,50,distance));
        arc.Stop(); assert(!arc.Active() && !arc.Step(.1f,z,50,distance));
        arc.Start(NAN,0); assert(!arc.Active());
        arc.Start(0,0);
        for (unsigned i=1;i<=400;++i) arc.Add(i*.01f,sinf(i*.01f)*Yards,cosf(i*.01f)*Yards);
        assert(Near(arc.Distance(4),-sinf(4))); // Buffer rollover continues using new samples.
    }
    // Regression (Phase 4 live test): the board pulled 20 yd ahead downhill and the split was found mid-ollie; the
    // resync froze the character in the air for 9.7 s. Airborne: keep driving every step, resync only on landing.
    {
        Driver air; Frame a = f; a.airborne = true; a.velocity[0] = 6; a.velocity[1] = -1; a.velocity[2] = 0;
        const float behindFar[3]{75, 200, 50};
        for (uint32_t t = 0; t < 10; ++t) {
            ++a.tick; const auto step = air.Step(a, behindFar, 7, false, 2000 + t * 16);
            assert(step.drive && step.forward && !step.resync);
        }
        a.airborne = false; ++a.tick;
        assert(air.Step(a, behindFar, 7, false, 2200).resync); // landed: now the board re-inits at the player
    }

    assert(Turn(1, 0) && !Turn(6.28f, 0) && !Turn(.01f, 0));

    // Lease: captures the server speed, ignores our own writes, follows a newer server value.
    Lease l;
    assert(l.Observe(7) == 7);
    l.Applied(3.5f);
    assert(l.Observe(3.5f) == 7);   // our write
    assert(l.Observe(14) == 14);    // server raised it (.modify speed ack)
    l.Applied(9);
    assert(l.Observe(9) == 14 && l.Release() == 14 && !l.held);
    return 0;
}
