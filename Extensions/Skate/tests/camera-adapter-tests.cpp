// Exercise the real camera adapter against persistent native yaw and an independently consumed view basis.
#include "SkateCamera.h"
#include <cassert>

static float Wrap(float a) {
    a=fmodf(a,camerafollow::Turn); return a<0 ? a+camerafollow::Turn : a;
}
static bool Near(float a,float b) { return fabsf(camerafollow::Delta(a,b))<1e-5f; }
struct Camera {
    float yaw=.25f, smoothTarget=.25f, smoothStart=.25f;
    float basisYaw=0,pitch=0,roll=0,forwardX=0,forwardY=0;
    unsigned yawWrites=0;
    void UpdateYaw(float delta) {
        yaw=Wrap(yaw+delta); smoothTarget=Wrap(smoothTarget+delta); smoothStart=Wrap(smoothStart+delta); ++yawWrites;
    }
    void SetFacing(float y,float p,float r) {
        basisYaw=y; pitch=p; roll=r; forwardX=cosf(y)*cosf(p); forwardY=sinf(y)*cosf(p);
    }
    float Compose(float body,float offset) const { return Wrap(body+offset+yaw); }
};
static float Invoke(skatecamera::Follow& follow,Camera& camera,float body,float offset,uint32_t now,
                    bool valid=true,bool looking=false,float speed=5) {
    float yaw=camera.Compose(body,offset);
    // All three native callers go through this stock composition/basis build before the extension hook.
    camera.SetFacing(yaw,.3f,.1f);
    follow.Apply(camera,yaw,.3f,.1f,valid,2.f,speed,looking,now);
    assert(Near(camera.basisYaw,yaw)); // MakeRelativeTo consumes only this basis; no returned locals.
    assert(fabsf(camera.forwardX-cosf(yaw)*cosf(.3f))<1e-6f);
    assert(fabsf(camera.forwardY-sinf(yaw)*cosf(.3f))<1e-6f); // The collision boom and returned yaw agree.
    assert(camera.pitch==.3f && camera.roll==.1f);
    assert(Near(camera.Compose(body,offset),yaw)); // Recomposition retains the actual displayed view.
    return yaw;
}
int main() {
    skatecamera::Follow follow; Camera camera;
    const float initial=Invoke(follow,camera,0,.1f,1000);
    assert(Near(initial,.35f));
    // A turn of the character must not turn the held world view by the same amount.
    const float desired=Invoke(follow,camera,1,.1f,1016);
    assert(Near(desired,initial+camerafollow::Delta(2,initial)*(1-expf(-2.5f*.016f))));
    assert(camera.yawWrites==1);
    assert(Near(camera.smoothTarget,camera.yaw) && Near(camera.smoothStart,camera.yaw));
    const unsigned writes=camera.yawWrites;
    assert(Near(Invoke(follow,camera,1,.1f,1016),desired) && camera.yawWrites==writes);
    // Another composition at the same timestamp can compensate a changed facing, but cannot ease twice.
    assert(Near(Invoke(follow,camera,1.4f,.1f,1016),desired));
    assert(camera.yawWrites==writes+1);
    const float held=Invoke(follow,camera,2,.1f,1032,true,false,.3f);
    assert(Near(held,desired));
    // Both mouse buttons use the same looking=true gate: their native update remains authoritative.
    for (unsigned button=0;button<2;++button) {
        camera.UpdateYaw(.4f);
        const unsigned before=camera.yawWrites;
        const float raw=camera.Compose(2,.1f);
        assert(Near(Invoke(follow,camera,2,.1f,1048+button*16,true,true),raw));
        assert(camera.yawWrites==before);
    }
    const float mouseYaw=camera.Compose(2,.1f);
    const float resumed=Invoke(follow,camera,2,.1f,1080);
    assert(Near(resumed,mouseYaw+camerafollow::Delta(2,mouseYaw)*(1-expf(-2.5f*.016f))));
    // J release/focus loss/stale pose returns to stock with the persisted view, not an old relative offset.
    const unsigned before=camera.yawWrites;
    assert(Near(Invoke(follow,camera,2,.1f,1096,false),resumed));
    assert(camera.yawWrites==before && !follow.view.active);
    assert(Near(Invoke(follow,camera,2,.1f,5000),resumed)); // fresh entry seeds, even after a long pause
    // Wraparound and the native clock wrapping do not cause a second turn or a nonfinite delta.
    skatecamera::Follow wrap; Camera c; c.yaw=c.smoothTarget=c.smoothStart=6.2f;
    float yaw=c.Compose(0,0); c.SetFacing(yaw,0,0);
    wrap.Apply(c,yaw,0,0,true,.1f,5,false,UINT32_MAX-10);
    yaw=c.Compose(0,0); c.SetFacing(yaw,0,0);
    wrap.Apply(c,yaw,0,0,true,.1f,5,false,5);
    assert(std::isfinite(yaw) && yaw>=0 && yaw<camerafollow::Turn && Near(c.basisYaw,yaw));
    assert(fabsf(camerafollow::Delta(yaw,6.2f))<.02f);
    const auto invalidWrites=c.yawWrites;
    wrap.Apply(c,yaw,NAN,0,true,.1f,5,false,21);
    assert(c.yawWrites==invalidWrites && !wrap.view.active);
}
