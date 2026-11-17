#pragma once
#include <cstdint>
#include <cmath>
namespace trickinput {
struct Axes { int16_t x=0,y=0; uint8_t lt=0,rt=0; };
struct TriggerGate {
    bool neutralRequired=true;
    void Clear() { neutralRequired=true; }
    Axes Step(bool allowed,uint8_t lt,uint8_t rt) {
        if (!allowed) { Clear(); return {}; }
        if (neutralRequired) {
            if (lt<=30 && rt<=30) neutralRequired=false;
            return {};
        }
        return {0,0,lt,rt}; // Preserve analog bytes; native engine owns grab thresholds.
    }
};
struct Gate {
    bool neutralRequired=true;
    void Clear() { neutralRequired=true; }
    Axes Step(bool allowed,Axes raw) {
        if (!allowed) { Clear(); return {}; }
        if (neutralRequired) {
            if (std::abs(static_cast<int>(raw.x))<8000 && std::abs(static_cast<int>(raw.y))<8000) neutralRequired=false;
            return {};
        }
        return raw; // Engine owns dead zones, gesture recognition and trick choice.
    }
};
// Render-rate samples are resampled at simulation timestamps, not reduced to
// the final forward Y value of a flick. Access under the existing shadow lock.
struct History {
    struct Sample { uint32_t epoch=0,at=0; Axes axes{}; };
    Sample samples[128]{}; unsigned cursor=0;
    void Push(uint32_t epoch,uint32_t at,Axes axes) { samples[cursor++%128]={epoch,at,axes}; }
    Axes At(uint32_t epoch,uint32_t at,uint32_t now) const {
        for (unsigned n=0;n<128 && n<cursor;++n) {
            const auto& sample=samples[(cursor-1-n)%128];
            if (sample.epoch==epoch && static_cast<int32_t>(at-sample.at)>=0 && now-sample.at<150) return sample.axes;
        }
        return {};
    }
};
}
