#pragma once
#include "SkateCollision.h"
#include "SkateLogic/skate-visual.h"
#include <condition_variable>
#include <mutex>
#include <thread>

namespace skate {
// A complete main-thread gather in absolute world yards, handed to the worker.
struct CollisionSnapshot {
    bool init = false;
    uint32_t map = 0;
    float origin[3]{}, centre[3]{}, heading = 0;
    std::vector<float> tris;
};
// The installed client-owned session as the worker last saw it.
struct CollisionState {
    bool session = false, pending = false, retry = false;
    std::string error;
    uint32_t map = 0, installed = 0;
    float origin[3]{};
};
// Phase 4: the skater frame (24 body anchors + board) from the last step, its source state (op 20) and the
// preview mesh (op 7). Immutable once published; the main thread copies the pointers.
struct VisualSnapshot {
    std::shared_ptr<const skatevisual::Frame> frame;
    std::shared_ptr<const skatevisual::Mesh> mesh;
    uint32_t state = 0;
};
class Helper {
public:
    ~Helper() { Stop(); }
    void Start(std::wstring binary, std::wstring assets);
    const std::wstring& LogPath() const { return logPath_; }
    void Stop();
    // pop: right stick Y (pull down, flick up to ollie), sent with op 18 once the preview is loaded.
    // flip: right stick X during a pop (kickflip/heelflip), sent with op 22.
    void Input(bool active, bool push, bool brake, int16_t steer, int16_t pop = 0, int16_t flip = 0);
    // Main thread: the board's ground speed cap (m/s), the character's run allowance, so the board never outruns it
    // (op 11, as the donor sent it; re-sent after every op 48, which clears it). 0 = none.
    void Cap(float metresPerSecond);
    // Main thread: the latest skater frame; false until a step after the preview loaded returned one.
    bool Visual(VisualSnapshot& out);
    // origin (optional): the session origin that belongs to this pose.
    bool Snapshot(skatetransport::Pose& pose, bool& failed, float* origin = nullptr);
    // Main thread: replaces a snapshot the worker has not sent yet (superseded, never sent).
    void Submit(CollisionSnapshot snapshot);
    // Main thread: retry is reported once after a rejected/failed init or refresh.
    CollisionState Collision();
private:
    void Run(std::wstring binary, std::wstring assets, std::wstring log, LONG epoch);
    std::wstring logPath_;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable wake_;
    volatile LONG epoch_ = 0;
    bool stop_ = true, active_ = false, push_ = false, brake_ = false;
    int16_t steer_ = 0, pop_ = 0, flip_ = 0;
    float cap_ = 0;
    bool valid_ = false, failed_ = false;
    skatetransport::Pose pose_{};
    VisualSnapshot visual_;
    bool queued_ = false;
    CollisionSnapshot next_;
    CollisionState collision_;
};
}
