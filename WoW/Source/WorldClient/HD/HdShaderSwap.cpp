// SPDX-License-Identifier: GPL-3.0-or-later
#include "WoW/Source/WorldClient/HD/HdShaderSwap.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "Engine/Source/Gx/CGxDevice/CGxShader.h"
#include "Engine/Source/Gx/GxEffects.h"

namespace {
    std::set<std::string> s_pending;  // names already written this run
}

bool HD::ShaderUsable(const uint8_t* code, size_t size, const char* name) {
    GxEffects::Device* device = GxEffects::Get();
    if (!device || !code || !size) return false;
    if (device->ShaderKnown(code, static_cast<uint32_t>(size))) return true;
    if (!name || !s_pending.insert(name).second) return false;
    char folder[MAX_PATH] = {};
    device->ShaderFolder(folder, sizeof folder);
    char path[MAX_PATH];
    std::snprintf(path, sizeof path, "%s\\hd-pending", folder);
    CreateDirectoryA(path, nullptr);
    std::snprintf(path, sizeof path, "%s\\hd-pending\\%s.bin", folder, name);
    if (FILE* file = std::fopen(path, "wb")) {
        std::fwrite(code, 1, size, file);
        std::fclose(file);
    }
    return false;
}

void HD::ShaderSetCode(CGxShader* shader, const uint8_t* code, uint32_t size) {
    // The backend's program for the old code is its own (a manifest record), so it is only let go.
    shader->m_apiSpecific = nullptr;
    shader->m_loaded = 0;
    shader->m_valid = 0;
    shader->m_code.SetCount(size);
    std::memcpy(shader->m_code.m_data, code, size);
}
