#pragma once
#include <array>
#include <chrono>
#include <map>
#include <mutex>
#include <vector>

namespace Lumenveil {
struct WetExclusion { std::array<float,3> low,high; std::chrono::steady_clock::time_point seen; };
inline std::map<const void*,WetExclusion> wetExclusions;
inline std::mutex wetExclusionMutex;
inline void RegisterWetExclusion(const void* model,const std::array<float,3>& low,const std::array<float,3>& high) {
    // shortcut: eight recently drawn pup bounds, expand the shader table if crowded cosmetic scenes need more.
    std::lock_guard<std::mutex> lock(wetExclusionMutex);
    const auto now=std::chrono::steady_clock::now();
    for(auto i=wetExclusions.begin();i!=wetExclusions.end();) {
        if(now-i->second.seen>std::chrono::milliseconds(200))i=wetExclusions.erase(i);else ++i;
    }
    if(wetExclusions.size()<8 || wetExclusions.count(model))wetExclusions[model]={low,high,now};
}
inline std::vector<WetExclusion> CurrentWetExclusions() {
    std::lock_guard<std::mutex> lock(wetExclusionMutex);
    const auto now=std::chrono::steady_clock::now();
    std::vector<WetExclusion> result;
    for(const auto& entry:wetExclusions)if(now-entry.second.seen<=std::chrono::milliseconds(200))result.push_back(entry.second);
    return result;
}
}
