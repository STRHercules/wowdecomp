#include "SkateHelper.h"
#include <chrono>

namespace skate {
void Helper::Start(std::wstring binary, std::wstring assets) {
    Stop();
    logPath_.clear();
    if (binary.empty() || assets.empty() || binary.find(L'"') != binary.npos || assets.find(L'"') != assets.npos) return;
    std::vector<wchar_t> full(32768);
    DWORD length = GetFullPathNameW(binary.c_str(),static_cast<DWORD>(full.size()),full.data(),nullptr);
    if (!length || length >= full.size()) return;
    binary.assign(full.data(),length);
    length = GetFullPathNameW(assets.c_str(),static_cast<DWORD>(full.size()),full.data(),nullptr);
    if (!length || length >= full.size()) return;
    assets.assign(full.data(),length);
    // A final backslash escapes the closing quote in Windows command-line parsing.
    if (assets.back()==L'\\' || assets.back()==L'/') assets += L'.';
    { std::lock_guard<std::mutex> lock(mutex_); stop_ = false; failed_ = valid_ = false; }
    const LONG epoch = InterlockedIncrement(&epoch_);
    static volatile LONG serial=0;
    logPath_=L"Logs/SkateHelper-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(InterlockedIncrement(&serial))+L".log";
    worker_ = std::thread(&Helper::Run, this, std::move(binary), std::move(assets), logPath_, epoch);
}
void Helper::Stop() {
    InterlockedIncrement(&epoch_);
    { std::lock_guard<std::mutex> lock(mutex_); stop_ = true; active_ = false; valid_ = false; visual_ = {}; }
    wake_.notify_all();
    // Epoch cancellation makes the transport watchdog kill a blocked child before joining.
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    queued_ = false; next_ = {}; collision_ = {};
}
void Helper::Input(bool active, bool push, bool brake, int16_t steer, int16_t pop, int16_t flip) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = active; push_ = push; brake_ = brake; steer_ = steer; pop_ = pop; flip_ = flip;
}
void Helper::Cap(float metresPerSecond) {
    // The helper's accepted range (shadow_wire.rs speed_limit): 0.1 yd/s .. 14 yd/s.
    const float cap = metresPerSecond > 0 ? (std::min)((std::max)(metresPerSecond, .09144f), 12.8016f) : 0.f;
    std::lock_guard<std::mutex> lock(mutex_);
    cap_ = cap;
}
bool Helper::Visual(VisualSnapshot& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    out = visual_; return valid_ && out.frame && out.mesh;
}
bool Helper::Snapshot(skatetransport::Pose& pose, bool& failed, float* origin) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (origin) memcpy(origin, collision_.origin, sizeof(collision_.origin));
    pose = pose_; failed = failed_; return valid_;
}
void Helper::Submit(CollisionSnapshot snapshot) {
    { std::lock_guard<std::mutex> lock(mutex_); if (stop_) return; next_ = std::move(snapshot); queued_ = collision_.pending = true; }
    wake_.notify_all();
}
CollisionState Helper::Collision() {
    std::lock_guard<std::mutex> lock(mutex_);
    CollisionState state = collision_; collision_.retry = false; collision_.error.clear(); return state;
}
void Helper::Run(std::wstring binary, std::wstring assets, std::wstring log, LONG epoch) {
    skatetransport::Pipe pipe;
    const auto slash = binary.find_last_of(L"/\\");
    const std::wstring directory = slash == binary.npos ? L"." : binary.substr(0, slash);
    CreateDirectoryW(L"Logs",nullptr);
    std::vector<unsigned char> request, reply;
    bool okay = pipe.Start(binary.c_str(), assets.c_str(), log.c_str(), epoch, &epoch_, false, directory.c_str());
    skatetransport::Pose pose;
    skatecollision::Generations generations;
    uint32_t wire = 0; // helper session epoch: 0 until op 48 installs the client's world; each init needs a greater one
    bool wasActive = false, preview = false; // op 22 (ollie, flips) needs the op 7 preview, which every op 48 clears
    std::shared_ptr<const skatevisual::Mesh> mesh; // op 7's schema; frames decode against it
    VisualSnapshot visual;
    float sentCap = 0; // op 11 value the current helper session holds
    using Clock=std::chrono::steady_clock;
    auto nextStep=Clock::now();
    while (okay) {
        bool active, push, brake, send = false; int16_t steer, pop, flip; float cap;
        CollisionSnapshot world;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (stop_ || InterlockedCompareExchange(&epoch_,0,0) != epoch) break;
            if (wire) { pose_ = pose; valid_ = true; visual_ = visual; }
            collision_.installed = generations.installed;
            const auto deadline=wire && active_ ? nextStep : Clock::now()+std::chrono::milliseconds(16);
            wake_.wait_until(lock,deadline,[this] { return stop_ || queued_; });
            if (stop_) break;
            active = active_; push = push_; brake = brake_; steer = steer_; pop = pop_; flip = flip_; cap = cap_;
            if (queued_) { world = std::move(next_); next_ = {}; queued_ = false; send = true; }
        }
        if (send) {
            const uint32_t generation = world.init ? 1 : generations.Next();
            bool accepted = skatecollision::Packet(request, world.init, world.map, generation, world.origin, world.centre, world.heading, world.tris);
            if (accepted && world.init) {
                accepted = pipe.Request(48, wire + 1, request, reply);
                if (accepted && !skatetransport::Decode(reply, pose)) break;
                if (accepted) {
                    ++wire; generations.Initialized(); wasActive = false; visual = {}; sentCap = 0;
                    // A rejected preview only costs the ollie and the rig pose; ops 5/6 still drive the board.
                    preview = pipe.Request(7, wire, {}, reply);
                    if (!preview && !pipe.Running()) break;
                    mesh = preview ? skatevisual::Schema(reply) : nullptr;
                    // Viewers need private board geometry even with J off; op9 samples the existing pose without stepping.
                    if (mesh && pipe.Request(9,wire,{},reply)) {
                        visual.frame=skatevisual::Decode(reply,*mesh,true,nullptr,true);
                        visual.mesh=visual.frame?mesh:nullptr;
                        std::vector<unsigned char> state;
                        uint64_t tick=0;
                        if (visual.frame && pipe.Request(20,wire,{},state) && state.size()==12) {
                            memcpy(&tick,state.data(),8);
                            if (tick==visual.frame->pose.tick) visual.state=skatetransport::Get(state.data()+8);
                            else visual={};
                        } else visual={};
                    }
                    if (!pipe.Running()) break;
                    nextStep=Clock::now(); // Init/preview may take seconds: never replay that time as simulation steps.
                }
            } else if (accepted && (accepted = pipe.Request(49, wire, request, reply))) generations.Queued(generation);
            if (!accepted && !pipe.Running()) break;
            std::lock_guard<std::mutex> lock(mutex_);
            collision_.pending = queued_;
            if (!accepted) { collision_.retry = true; collision_.error = request.empty() ? "invalid snapshot" : pipe.Rejection(); }
            else if (world.init) {
                collision_.session = true; collision_.map = world.map;
                memcpy(collision_.origin, world.origin, sizeof(world.origin));
                pose_ = pose; valid_ = true; visual_ = visual; // preview/frame belongs to this new session, never its predecessor
            }
        }
        if (generations.awaiting) {
            if (!pipe.Request(50, wire, {}, reply)) break;
            const int status = generations.Status(reply);
            if (status == -2) break;
            if (status == -1) {
                std::lock_guard<std::mutex> lock(mutex_);
                collision_.retry = true; collision_.error = "asynchronous collision build failed";
            }
        }
        request.clear();
        if (wire && cap > 0 && cap != sentCap) {
            skatetransport::Float(request, cap);
            skatetransport::Pose capped;
            if (pipe.Request(11, wire, request, reply) && skatetransport::Decode(reply, capped)) pose = capped;
            else if (!pipe.Running()) break;
            sentCap = cap; // a refused cap is not retried every step
            request.clear();
        }
        if (!wire || (!active && !wasActive)) { nextStep=Clock::now(); continue; }
        if (active) {
            const auto now=Clock::now();
            if (now<nextStep) continue; // An early collision wake services its packet without advancing physics early.
            const auto period=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(pose.period));
            // Include IPC/decoding/status work in the fixed-step period. Drop a large overrun instead of an unbounded burst.
            if (now-nextStep>period) nextStep=now;
            nextStep+=period;
        } else nextStep=Clock::now();
        uint32_t op = active ? 5 : 6;
        if (active && preview) {
            // Op 22 = op 18 plus the full right stick: X during the pop picks the flip; one per-step sample follows.
            op = 22; skatetransport::TrickStep(request, 1, push, brake, false, false, 0, steer, flip, pop);
            skatetransport::Word(request, static_cast<uint32_t>(static_cast<int32_t>(flip)));
            skatetransport::Word(request, static_cast<uint32_t>(static_cast<int32_t>(pop)));
        }
        else if (active) {
            skatetransport::InputStep(request, 1, push, brake, false, false);
            const uint32_t axis = static_cast<uint32_t>(static_cast<int32_t>(steer));
            memcpy(request.data()+8,&axis,4);
        }
        okay = pipe.Request(op, wire, request, reply);
        if (okay && op == 22) {
            // The 60-byte pose leads; the skater frame follows. A detached (bailed) board far away is hidden, not refused.
            visual.frame = mesh ? skatevisual::Decode(reply, *mesh, true, nullptr, true) : nullptr;
            visual.mesh = visual.frame ? mesh : nullptr;
            if (reply.size() > 60) reply.resize(60);
        }
        okay = okay && skatetransport::Decode(reply, pose);
        if (okay && visual.frame) {
            // Op 20: tick + source state (300 wipeout = ragdoll). Same tick or the frame is not used.
            std::vector<unsigned char> state;
            uint64_t tick = 0;
            const bool known = pipe.Request(20, wire, {}, state) && state.size() == 12;
            if (known) memcpy(&tick, state.data(), 8);
            if (known && tick == pose.tick) visual.state = skatetransport::Get(state.data() + 8);
            else if (!pipe.Running()) break;
            else visual = {};
        }
        wasActive = active;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!stop_ && InterlockedCompareExchange(&epoch_,0,0) == epoch) { failed_ = true; valid_ = false; }
}
}
