#pragma once
#include "BackpackFit.h"
#include <set>
#include <sstream>
#include <string>
namespace Retail {
inline bool ValidBackpackFitExport(const std::string& text) {
    if (text.size()>1024*1024 || text.find('\0')!=std::string::npos) return false;
    std::istringstream input(text);
    std::string line;
    std::set<std::string> keys;
    while (std::getline(input,line)) {
        if (line.empty() || line[0]=='#') continue;
        if (line.size()>512 || keys.size()>=4096) return false;
        std::istringstream row(line);
        std::string field,body;
        unsigned display=0;
        BackpackFit fit;
        if (!std::getline(row,field,';')) return false;
        try { size_t end=0;const auto parsed=std::stoul(field,&end);if (end!=field.size() || !parsed || parsed>5000000) return false;display=unsigned(parsed); }
        catch (...) { return false; }
        if (!std::getline(row,body,';') || body.empty() || body.size()>240 || body.find_first_of("\r\n")!=std::string::npos) return false;
        float* values[]={&fit.scale,&fit.x,&fit.y,&fit.z};
        for (unsigned i=0;i<4;++i) {
            if (!std::getline(row,field,';')) return false;
            try { size_t end=0;*values[i]=std::stof(field,&end);if (end!=field.size()) return false; }
            catch (...) { return false; }
        }
        if (row.rdbuf()->in_avail() || !ValidBackpackFit(fit) || !keys.insert(std::to_string(display)+";"+body).second) return false;
    }
    return true;
}
}
