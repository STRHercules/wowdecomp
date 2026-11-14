#pragma once
#include "BackpackFit.h"
#include <cmath>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>

namespace Retail {
struct BackpackAppearance {
    uint32_t display = 0;
    std::string model;
    float width = 0, height = 0, depth = 0;
    BackpackFit fit;
};
inline bool ParseBackpacks(const std::string& text, std::map<uint32_t, BackpackAppearance>& result) {
    std::map<uint32_t, BackpackAppearance> parsed;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (parsed.size() >= 4096 || line.size() > 512) return false;
        for (char& c : line) if (c == ';') c = ' ';
        std::istringstream row(line);
        BackpackAppearance item;
        std::string extra;
        if (!(row >> item.display >> item.model >> item.width >> item.height >> item.depth)) return false;
        row >> std::ws;
        if (!row.eof() && (!(row >> item.fit.scale >> item.fit.x >> item.fit.y >> item.fit.z) || row >> extra))
            return false;
        if (!ValidBackpackFit(item.fit) ||
            !item.display || item.display > 5000000 || item.model.size() > 240 ||
            item.model.compare(0, 16, "custom/fortnite/") || item.model.find("..") != std::string::npos ||
            item.model.size() < 3 || item.model.substr(item.model.size() - 3) != ".m2" ||
            !std::isfinite(item.width) || !std::isfinite(item.height) || !std::isfinite(item.depth) ||
            item.width <= 0 || item.width > 20 || item.height <= 0 || item.height > 20 || item.depth < 0 || item.depth > 20 ||
            !parsed.emplace(item.display, item).second) return false;
    }
    result = std::move(parsed);
    return true;
}
}
