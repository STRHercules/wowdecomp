#pragma once
#include <cstdint>
namespace nativefloorprobe {
// Stock step-down simulates a temporary fall over a distance-derived time,
// then restores its fall-clock/flags/velocity through its own save/restore.
// It is not elapsed Source time. All Source overrides must pass through for
// this entire native operation, including nested contact/inverse callbacks.
struct Gate {
    void* movement=nullptr;
    uint32_t thread=0;
    bool Blocks(void* p,uint32_t currentThread) const {
        return movement && movement==p && thread==currentThread;
    }
};
struct Scope {
    Gate& gate;
    Gate previous;
    Scope(Gate& owner,void* p,uint32_t thread):gate(owner),previous(owner) { gate={p,thread}; }
    ~Scope() { gate=previous; }
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
};
}
