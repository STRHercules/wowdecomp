// SPDX-License-Identifier: GPL-3.0-or-later
#include "WoW/Source/WorldClient/HD/HdFrame.h"

#include "Engine/Source/Gx/GxEffects.h"

namespace {
#include "WoW/Source/WorldClient/HD/Shaders/Spirv.inc"

    struct State {
        GxEffects::Device* device = nullptr;
        const GxEffects::Program* copy = nullptr;
        GxEffects::Texture* frame = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        bool open = false;
    };

    State s_state;
}

GxEffects::Texture* HD::Frame::Open(uint32_t width, uint32_t height) {
    State& st = s_state;
    GxEffects::Device* device = GxEffects::Get();
    if (!device) return nullptr;
    if (device != st.device) {
        // A new device: what the old one made is gone with it.
        st = State();
        st.device = device;
    }
    if (st.open && st.width == width && st.height == height) return st.frame;
    st.open = false;
    if (!st.copy) st.copy = device->CreateProgram(kCopyFrag, sizeof kCopyFrag, "HD frame copy");
    if (st.width != width || st.height != height) {
        device->Destroy(st.frame);
        st.frame = device->CreateStorage(width, height, GxEffects::Format::RGBA16F);
        st.width = width;
        st.height = height;
    }
    if (!st.copy || !st.frame || !device->CopyColor(st.frame)) return nullptr;
    st.open = true;
    return st.frame;
}

bool HD::Frame::Close() {
    State& st = s_state;
    if (!st.open) return true;
    st.open = false;
    GxEffects::Pass back;
    back.program = st.copy;
    back.textures[0].texture = st.frame;
    return st.device == GxEffects::Get() && st.device->Draw(back);
}
