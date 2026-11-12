#include <windows.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include "ShapeMath.h"
#include "NativeBridge.h"
#include "StockProfiles.h"
#include "DeformMath.h"
#include "DeformProfiles.h"

// Addresses and register contracts are pinned to the supplied build; see evidence-native.txt.
namespace {
using Guid = uint64_t;
using Values = std::array<float, Shape::SliderCount>;
struct Lua;
using LuaFunction = int (__cdecl*)(Lua*);
template<class T> T& Field(void* base, size_t offset) { return *reinterpret_cast<T*>(static_cast<char*>(base)+offset); }
const auto ActivePlayer = reinterpret_cast<Guid (__cdecl*)()>(0x4D3790);
const auto Object = reinterpret_cast<void* (__cdecl*)(Guid, uint32_t, const char*, int)>(0x4D4DB0);
const auto EnumVisible = reinterpret_cast<int (__cdecl*)(int (__cdecl*)(Guid, void*), void*)>(0x4D4B30);
const auto UnitName = reinterpret_cast<const char* (__thiscall*)(void*, const char**, int)>(0x72A000);
const auto Register = reinterpret_cast<void (__cdecl*)(const char*, LuaFunction)>(0x817F90);
const auto LuaTop = reinterpret_cast<int (__cdecl*)(Lua*)>(0x84DBD0);
const auto LuaType = reinterpret_cast<int (__cdecl*)(Lua*, int)>(0x84DEB0);
const auto LuaNumber = reinterpret_cast<double (__cdecl*)(Lua*, int)>(0x84E030);
const auto LuaString = reinterpret_cast<const char* (__cdecl*)(Lua*, int, size_t*)>(0x84E0E0);
const auto PushString = reinterpret_cast<void (__cdecl*)(Lua*, const char*)>(0x84E350);
const auto PushBool = reinterpret_cast<void (__cdecl*)(Lua*, int)>(0x84E4D0);
const auto NewTable = reinterpret_cast<void (__cdecl*)(Lua*, int, int)>(0x84E6E0);
const auto SetField = reinterpret_cast<void (__cdecl*)(Lua*, int, const char*)>(0x84E900);
const auto RawSet = reinterpret_cast<void (__cdecl*)(Lua*, int, int)>(0x84EA00);
const auto LuaSetTop = reinterpret_cast<void (__cdecl*)(Lua*, int)>(0x84DBF0);
const auto LuaUserdata = reinterpret_cast<void* (__cdecl*)(Lua*, int)>(0x84E1C0);
const auto RawGetI = reinterpret_cast<void (__cdecl*)(Lua*, int, int)>(0x84E670);
// CPlayerModel vtable (GetObjectTypeName 0x5970D0 returns "PlayerModel"); its CM2Model sits at CSimpleModel+0x2A0.
constexpr uintptr_t PlayerModelVtable = 0xA144B0;
using MultiplyFunction = Shape::Matrix* (__cdecl*)(Shape::Matrix*, const Shape::Matrix*, const Shape::Matrix*);
const auto Multiply = reinterpret_cast<MultiplyFunction>(0x4C1F00);

SRWLOCK g_lock = SRWLOCK_INIT;
bool g_ready = false;
std::unordered_map<Guid, Values> g_values;
struct ModelState {
    void* bones = nullptr;
    void* data = nullptr;
    std::vector<Shape::Transform> transforms;
    std::vector<bool> ownedFlags;
};
std::unordered_map<void*, ModelState> g_models;

#include "MeshRuntime.h"

void Log(const char* text) {
    char path[MAX_PATH]{};
    DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    char* slash = std::strrchr(path, '\\');
    if (!slash || static_cast<size_t>(slash-path)+sizeof("\\BodySlider.log") > MAX_PATH) return;
    strcpy_s(slash+1, MAX_PATH-static_cast<size_t>(slash-path)-1, "BodySlider.log");
    FILE* file = nullptr;
    if (fopen_s(&file, path, "ab") == 0) {
        std::fprintf(file, "%s\r\n", text);
        std::fclose(file);
    }
}
void* Player(Guid guid) { return Object(guid, 0x10, "BodySlider", 1); }
bool InWorld() { return g_ready && ActivePlayer() && Player(ActivePlayer()); }

const Stock::Rig* RigFor(void* model) {
    if (!model || !(Field<uint32_t>(model, 0x10)&1)) return nullptr;
    void* shared = Field<void*>(model, 0x2C);
    void* data = shared ? Field<void*>(shared, 0x150) : nullptr;
    if (!data || !Field<void*>(model, 0x94)) return nullptr;
    const char* name = static_cast<char*>(shared)+0x3C;
    size_t length = strnlen_s(name, 260);
    if (!length || length == 260) return nullptr;
    std::string path(name, length);
    for (char& c : path) c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (path.size() >= 4 && path.compare(path.size()-4, 4, ".mdx") == 0) path.replace(path.size()-4, 4, ".m2");
    for (const auto& rig : Stock::kRigs) {
        if (path != rig.path || Field<uint32_t>(data, 0x2C) != rig.count) continue;
        // Reject changed skeletons even when a model pack reuses a stock filename.
        if (Field<uint32_t>(data, 0x34) != 27) return nullptr;
        auto* keys = Field<uint16_t*>(data, 0x38);
        auto* bones = Field<unsigned char*>(data, 0x30);
        if (!keys || !bones) return nullptr;
        for (int k = 0; k < 27; ++k) if (keys[k] != rig.keys[k]) return nullptr;
        for (uint32_t b = 0; b < rig.count; ++b) {
            if (Field<uint16_t>(bones+b*0x58, 8) != rig.parents[b]) return nullptr;
            const float* pivot = reinterpret_cast<float*>(bones+b*0x58+0x4C);
            for (int a = 0; a < 3; ++a)
                if (!std::isfinite(pivot[a]) || std::fabs(pivot[a]-rig.pivots[b][a]) > 0.00001f) return nullptr;
        }
        return &rig;
    }
    return nullptr;
}

void RestoreFlags(void* model, ModelState& state) {
    if (!(Field<uint32_t>(model, 0x10)&1) || Field<void*>(model, 0x94) != state.bones) return;
    void* shared = Field<void*>(model, 0x2C);
    if (!shared || Field<void*>(shared, 0x150) != state.data) return;
    for (size_t i = 0; i < state.ownedFlags.size(); ++i) {
        if (state.ownedFlags[i]) Field<uint32_t>(static_cast<char*>(state.bones)+i*0xAC, 0x8C) &= ~0x80u;
        state.ownedFlags[i] = false;
    }
}

void Scale(std::vector<Shape::Transform>& transforms, int bone, int axes, float value) {
    if (bone < 0 || static_cast<size_t>(bone) >= transforms.size()) return;
    for (int a = 0; a < 3; ++a) if (axes&(1<<a)) transforms[bone].scale[a] *= 1+value;
}
void Move(std::vector<Shape::Transform>& transforms, int bone, int axis, float value) {
    if (bone >= 0 && static_cast<size_t>(bone) < transforms.size()) transforms[bone].move[axis] += value;
}
bool HasSlider(const Stock::Rig& r, int slider) {
    switch (slider) {
        case 0: return r.roots[0] >= 0;
        case 1: return r.head >= 0;
        case 2: return r.neck >= 0;
        case 3: return r.shoulders[0] >= 0 && r.shoulders[1] >= 0;
        case 4: case 5: return r.arms[0] >= 0 && r.arms[1] >= 0;
        case 6: return r.chest >= 0;
        case 7: case 8: return r.waist >= 0;
        case 9: return r.hips >= 0;
        case 10: case 11: return r.upperlegs[0] >= 0 && r.upperlegs[1] >= 0;
    }
    return false;
}
void Build(ModelState& s, const Stock::Rig& r, const Values& v) {
    s.transforms.assign(r.count, Shape::Transform{});
    s.ownedFlags.assign(r.count, false);
    for (int root : r.roots) {
        Scale(s.transforms, root, 7, .25f*v[0]);
        // Some stock roots pivot at the pelvis; whole-body height scales about the floor.
        if (root >= 0) for (int a = 0; a < 3; ++a) Move(s.transforms, root, a, r.pivots[root][a]*.25f*v[0]);
    }
    Scale(s.transforms, r.head, 7, .35f*v[1]);
    float size = r.head >= 0 ? r.pivots[r.head][2]/1.85f : 1;
    Move(s.transforms, r.neck, 2, .06f*size*v[2]);
    for (int side = 0; side < 2; ++side) {
        int shoulder = r.shoulders[side];
        if (shoulder >= 0) Move(s.transforms, shoulder, 1, .05f*size*v[3]*(r.pivots[shoulder][1] < 0 ? -1.f : 1.f));
        Scale(s.transforms, r.arms[side], 1<<r.armAxis[side], .25f*v[4]);
        Scale(s.transforms, r.arms[side], 7^(1<<r.armAxis[side]), .3f*v[5]);
        Scale(s.transforms, r.upperlegs[side], 1<<r.legAxis[side], .3f*v[10]);
        Scale(s.transforms, r.upperlegs[side], 7^(1<<r.legAxis[side]), .3f*v[11]);
    }
    Scale(s.transforms, r.chest, 3, .25f*v[6]);
    Scale(s.transforms, r.waist, 3, .25f*v[7]);
    Scale(s.transforms, r.waist, 4, .2f*v[8]);
    Scale(s.transforms, r.hips, 3, .25f*v[9]);
    if (r.hips >= 0 && r.upperlegs[0] >= 0 && r.lowerlegs[0] >= 0) {
        // Lift the pelvis by the extra hip-to-ankle height; descendants inherit it.
        float ankleZ = r.pivots[r.lowerlegs[0]][2];
        for (uint32_t b = 0; b < r.count; ++b)
            if (r.parents[b] == r.lowerlegs[0]) ankleZ = (std::min)(ankleZ, r.pivots[b][2]);
        Move(s.transforms, r.hips, 2, (r.pivots[r.upperlegs[0]][2]-ankleZ)*.3f*v[10]);
    }
}

void ApplyLocked(void* model, Guid guid);
using PreAnimateFunction = void (__thiscall*)(void*, void*);
PreAnimateFunction g_preAnimate = nullptr;
void __fastcall PreAnimate(void* unit, void*, void* frame) {
    void* previous = Field<void*>(unit, 0xB4);
    AcquireSRWLockExclusive(&g_lock);
    auto existing = g_models.find(previous);
    if (existing != g_models.end()) RestoreFlags(previous, existing->second);
    ReleaseSRWLockExclusive(&g_lock);
    g_preAnimate(unit, frame);
    void* descriptor = Field<void*>(unit, 8);
    void* model = Field<void*>(unit, 0xB4);
    if (!descriptor || !model || !(Field<uint32_t>(descriptor, 8)&0x10)) return;
    Guid guid = Field<Guid>(descriptor, 0);
    AcquireSRWLockExclusive(&g_lock);
    ApplyLocked(model, guid);
    ReleaseSRWLockExclusive(&g_lock);
}

// Shapes `model` with `guid`'s values, or unshapes it when they are all zero. Caller holds g_lock exclusively.
void ApplyLocked(void* model, Guid guid) {
    auto values = g_values.find(guid);
    bool changed = values != g_values.end() && std::any_of(values->second.begin(), values->second.end(), [](float v) { return v != 0; });
    const Stock::Rig* rig = changed ? RigFor(model) : nullptr;
    if (!rig) {
        MeshClear(model);
        auto old = g_models.find(model);
        if (old != g_models.end()) { RestoreFlags(model, old->second); g_models.erase(old); }
        return;
    }
    auto& state = g_models[model];
    state.bones = Field<void*>(model, 0x94);
    state.data = Field<void*>(Field<void*>(model, 0x2C), 0x150);
    Build(state, *rig, values->second);
    MeshUpdate(model, values->second);
    for (uint32_t b = 0; b < rig->count; ++b) {
        if (!state.transforms[b].Active()) continue;
        uint32_t& flags = Field<uint32_t>(static_cast<char*>(state.bones)+b*0xAC, 0x8C);
        state.ownedFlags[b] = !(flags&0x80);
        flags |= 0x80;
    }
}

using DestroyFunction = void (__thiscall*)(void*);
DestroyFunction g_destroy = nullptr;
void __fastcall Destroy(void* model, void*) {
    AcquireSRWLockExclusive(&g_lock);
    MeshClear(model);
    g_models.erase(model);
    ReleaseSRWLockExclusive(&g_lock);
    g_destroy(model);
}

Shape::Matrix* __cdecl ShapeMultiply(Shape::Matrix* out, const Shape::Matrix* local,
        const Shape::Matrix* parent, void* model, unsigned index, void* bone) {
    Shape::Transform transform;
    AcquireSRWLockShared(&g_lock);
    auto it = g_models.find(model);
    if (it != g_models.end() && index < it->second.transforms.size()) transform = it->second.transforms[index];
    ReleaseSRWLockShared(&g_lock);
    if (!transform.Active()) return Multiply(out, local, parent);
    Shape::Matrix shaped = Shape::Apply(*local, reinterpret_cast<float*>(static_cast<char*>(bone)+0x4C), transform);
    return Multiply(out, &shaped, parent);
}

__declspec(naked) void BoneMultiply() {
    __asm {
        push dword ptr [ebp+14h]
        push dword ptr [ebp+18h]
        push esi
        push dword ptr [esp+24]
        push dword ptr [esp+24]
        push dword ptr [esp+24]
        call ShapeMultiply
        add esp, 24
        ret
    }
}

bool ReadGuid(Lua* lua, int index, Guid& guid) {
    if (LuaType(lua, index) != 4) return false;
    size_t length = 0;
    const char* text = LuaString(lua, index, &length);
    return text && Shape::Guid(text, length, guid);
}
// BodySlider_Set(playerModelFrame): shape a PlayerModel preview with the local player's values. Called per frame
// from the frame's OnUpdate, like PreAnimate for units; the frame's model loads asynchronously and can be replaced.
int __cdecl Preview(Lua* lua) {
    void* frame = nullptr;
    if (InWorld()) {
        RawGetI(lua, 1, 0);
        if (LuaType(lua, -1) == 2) frame = LuaUserdata(lua, -1);
        LuaSetTop(lua, -2);
    }
    void* model = frame && Field<uintptr_t>(frame, 0) == PlayerModelVtable ? Field<void*>(frame, 0x2A0) : nullptr;
    bool shaped = false;
    if (model) {
        AcquireSRWLockExclusive(&g_lock);
        auto existing = g_models.find(model);
        if (existing != g_models.end()) RestoreFlags(model, existing->second);
        ApplyLocked(model, ActivePlayer());
        shaped = g_models.count(model) != 0;
        ReleaseSRWLockExclusive(&g_lock);
    }
    PushBool(lua, shaped);
    return 1;
}
int __cdecl Set(Lua* lua) {
    if (LuaTop(lua) == 1 && LuaType(lua, 1) == 5) return Preview(lua);
    Guid guid = 0;
    bool ok = InWorld() && LuaTop(lua) == 3 && ReadGuid(lua, 1, guid)
        && LuaType(lua, 2) == 4 && LuaType(lua, 3) == 3;
    int slider = -1;
    double value = 0;
    if (ok) {
        size_t length = 0;
        const char* name = LuaString(lua, 2, &length);
        slider = name && std::strlen(name) == length ? Shape::Find(name) : -1;
        value = LuaNumber(lua, 3);
        ok = slider >= 0 && Shape::ValidValue(value, slider) && Player(guid);
    }
    if (ok) {
        AcquireSRWLockExclusive(&g_lock);
        ok = g_values.size() < 128 || g_values.count(guid);
        if (ok) g_values[guid][slider] = static_cast<float>(value);
        ReleaseSRWLockExclusive(&g_lock);
    }
    PushBool(lua, ok);
    return 1;
}
int __cdecl Clear(Lua* lua) {
    if (!InWorld()) return 0;
    Guid guid = 0;
    bool all = LuaTop(lua) == 0 || LuaType(lua, 1) == 0;
    if (!all && !ReadGuid(lua, 1, guid)) return 0;
    AcquireSRWLockExclusive(&g_lock);
    if (all) g_values.clear(); else g_values.erase(guid);
    ReleaseSRWLockExclusive(&g_lock);
    return 0;
}
int __cdecl Supported(Lua* lua) { PushBool(lua, InWorld()); return 1; }
int __cdecl Names(Lua* lua) {
    NewTable(lua, InWorld() ? static_cast<int>(Shape::SliderCount) : 0, 0);
    if (InWorld()) for (size_t i = 0; i < Shape::SliderCount; ++i) {
        PushString(lua, Shape::Names[i]); RawSet(lua, -2, static_cast<int>(i+1));
    }
    return 1;
}
int __cdecl Has(Lua* lua) {
    bool ok = false;
    if (InWorld() && LuaType(lua, 1) == 4) {
        int slider = Shape::Find(LuaString(lua, 1, nullptr));
        void* player = Player(ActivePlayer());
        const Stock::Rig* rig = player ? RigFor(Field<void*>(player, 0xB4)) : nullptr;
        if (rig) {
            AcquireSRWLockExclusive(&g_lock);
            ok = slider < 12 ? HasSlider(*rig, slider) : MeshHasSlider(Field<void*>(player, 0xB4), slider);
            ReleaseSRWLockExclusive(&g_lock);
        }
    }
    PushBool(lua, ok);
    return 1;
}
struct Enumeration { Lua* lua; int count = 0; };
int __cdecl Enumerate(Guid guid, void* context) {
    auto& enumeration = *static_cast<Enumeration*>(context);
    void* player = Player(guid);
    if (!player) return 1;
    const char* realm = nullptr;
    const char* name = UnitName(player, &realm, 0);
    if (!name || !*name || strnlen_s(name, 128) == 128) return 1;
    char hex[19]{};
    std::snprintf(hex, sizeof(hex), "0x%016llX", static_cast<unsigned long long>(guid));
    NewTable(enumeration.lua, 0, 2);
    PushString(enumeration.lua, name); SetField(enumeration.lua, -2, "name");
    PushString(enumeration.lua, hex); SetField(enumeration.lua, -2, "guid");
    RawSet(enumeration.lua, -2, ++enumeration.count);
    return enumeration.count < 128;
}
int __cdecl Units(Lua* lua) {
    NewTable(lua, 0, 0);
    if (InWorld()) { Enumeration enumeration{lua}; EnumVisible(Enumerate, &enumeration); }
    return 1;
}
using InitializeLuaFunction = int (__cdecl*)(int);
InitializeLuaFunction g_initializeLua = nullptr;
int __cdecl InitializeLua(int profile) {
    int result = g_initializeLua(profile);
    if (result) {
        // The API gates every operation on a live player; there are no GlueXML hooks or panels.
        AcquireSRWLockExclusive(&g_lock);
        g_values.clear();
        ReleaseSRWLockExclusive(&g_lock);
        for (size_t i = 0; i < 6; ++i)
            Register(Bridge::Names[i], reinterpret_cast<LuaFunction>(Bridge::Slots[i]));
        // Exercise the real Lua C-call validation path even before entering a world.
        const auto execute = reinterpret_cast<void (__cdecl*)(const char*, const char*, const char*)>(0x819210);
        execute("assert(type(BodySlider_Supported()) == 'boolean')", "BodySlider bridge check", nullptr);
        Log("Lua API registered (operations require an in-world player)");
    }
    return result;
}

struct Signature { uintptr_t address; size_t size; unsigned char bytes[9]; };
const Signature Signatures[] = {
    {0x819BB0,6,{0x55,0x8B,0xEC,0x83,0xEC,0x08}},
    {0x817F90,6,{0x55,0x8B,0xEC,0x8B,0x45,0x0C}},
    {0x73DAB0,6,{0x55,0x8B,0xEC,0x83,0xEC,0x60}},
    {0x832640,5,{0x53,0x56,0x8B,0xF1,0x57}},
    {0x82FE4F,5,{0xE8,0xAC,0x20,0xC9,0xFF}},
    {0x4C1F00,6,{0x55,0x8B,0xEC,0x8B,0x4D,0x10}},
    {0x4D3790,7,{0x64,0x8B,0x0D,0x2C,0,0,0}},
    {0x4D4DB0,8,{0x55,0x8B,0xEC,0x64,0x8B,0x0D,0x2C,0}},
    {0x4D4B30,8,{0x55,0x8B,0xEC,0xA1,0xBC,0x39,0xD4,0}},
    {0x72A000,8,{0x55,0x8B,0xEC,0x81,0xEC,0xB0,0x02,0}},
    {0x84DBD0,6,{0x55,0x8B,0xEC,0x8B,0x4D,0x08}},
    {0x84DEB0,6,{0x55,0x8B,0xEC,0x8B,0x45,0x0C}},
    {0x84E030,6,{0x55,0x8B,0xEC,0x8B,0x45,0x0C}},
    {0x84E0E0,7,{0x55,0x8B,0xEC,0x56,0x8B,0x75,0x08}},
    {0x84E350,6,{0x55,0x8B,0xEC,0x8B,0x55,0x0C}},
    {0x84E4D0,9,{0x55,0x8B,0xEC,0x8B,0x15,0x9C,0x13,0xD4,0}},
    {0x84E6E0,7,{0x55,0x8B,0xEC,0x56,0x8B,0x75,0x08}},
    {0x84E900,6,{0x55,0x8B,0xEC,0x83,0xEC,0x10}},
    {0x84EA00,6,{0x55,0x8B,0xEC,0x8B,0x45,0x0C}},
    {0x84DBF0,6,{0x55,0x8B,0xEC,0x8B,0x4D,0x0C}},
    {0x84E1C0,6,{0x55,0x8B,0xEC,0x8B,0x45,0x0C}},
    {0x84E670,7,{0x55,0x8B,0xEC,0x8B,0x45,0x0C,0x56}},
    {PlayerModelVtable+0x1C,4,{0xD0,0x70,0x59,0x00}}
};

struct Hook {
    unsigned char* address = nullptr;
    size_t length = 0;
    unsigned char saved[16]{};
    void* trampoline = nullptr;
    DWORD protection = 0;
    bool installed = false;
};
Hook g_hooks[17];
void Jump(unsigned char* at, const void* to, unsigned char opcode) {
    at[0] = opcode;
    int32_t relative = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to)-reinterpret_cast<uintptr_t>(at)-5);
    std::memcpy(at+1, &relative, 4);
}
bool Prepare(Hook& hook, uintptr_t address, size_t length, bool trampoline) {
    hook.address = reinterpret_cast<unsigned char*>(address);
    hook.length = length;
    std::memcpy(hook.saved, hook.address, length);
    if (!VirtualProtect(hook.address, length, PAGE_EXECUTE_READWRITE, &hook.protection)) return false;
    if (!trampoline) return true;
    hook.trampoline = VirtualAlloc(nullptr, length+5, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    if (!hook.trampoline) return false;
    std::memcpy(hook.trampoline, hook.saved, length);
    Jump(static_cast<unsigned char*>(hook.trampoline)+length, hook.address+length, 0xE9);
    DWORD old = 0;
    if (!VirtualProtect(hook.trampoline, length+5, PAGE_EXECUTE_READ, &old)) return false;
    return FlushInstructionCache(GetCurrentProcess(), hook.trampoline, length+5) != 0;
}
void Install(Hook& hook, const void* replacement, bool call = false) {
    Jump(hook.address, replacement, call ? 0xE8 : 0xE9);
    for (size_t i = 5; i < hook.length; ++i) hook.address[i] = 0x90;
    hook.installed = true;
}
bool RestoreProtection(Hook& hook) {
    DWORD unused = 0;
    return VirtualProtect(hook.address, hook.length, hook.protection, &unused)
        && FlushInstructionCache(GetCurrentProcess(), hook.address, hook.length);
}
bool RestoreHookProtections() {
    // Later hooks on the same page observed the temporary writable protection.
    for (size_t i = std::size(g_hooks); i-- > 0;)
        if (g_hooks[i].address && !RestoreProtection(g_hooks[i])) return false;
    return true;
}
void Rollback() {
    for (size_t i = std::size(g_hooks); i-- > 0;) {
        auto& hook = g_hooks[i];
        if (hook.address && hook.protection) {
            DWORD unused = 0;
            if (VirtualProtect(hook.address, hook.length, PAGE_EXECUTE_READWRITE, &unused)) {
                if (hook.installed) std::memcpy(hook.address, hook.saved, hook.length);
                RestoreProtection(hook);
            } else if (hook.installed) {
                // A surviving detour still needs its original trampoline to remain executable.
                Log("Rollback protection failed; retaining trampoline, BodySlider disabled");
                continue;
            }
        }
        if (hook.trampoline) VirtualFree(hook.trampoline, 0, MEM_RELEASE);
        hook = {};
    }
}
}

extern "C" __declspec(dllexport) BOOL __cdecl BodySlider_Initialize() {
    if (g_ready) return TRUE;
    if (GetModuleHandleA(nullptr) != reinterpret_cast<HMODULE>(0x400000)) return FALSE;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(0x400000);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 4096) return FALSE;
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(0x400000+dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
            || pe->OptionalHeader.SizeOfImage < 0x9FD000) return FALSE;
    for (const auto& signature : Signatures) {
        if (std::memcmp(reinterpret_cast<void*>(signature.address), signature.bytes, signature.size) != 0) {
            Log("Unsupported client or conflicting hook: BodySlider disabled");
            return FALSE;
        }
    }
    for (const auto& signature : MeshSignatures) {
        if (std::memcmp(reinterpret_cast<void*>(signature.address), signature.bytes, signature.size) != 0) {
            Log("Unsupported rendering code or conflicting mesh hook: BodySlider disabled");
            return FALSE;
        }
    }
    for (uintptr_t slot : Bridge::Slots) {
        for (size_t byte = 0; byte < 5; ++byte) {
            if (*reinterpret_cast<const unsigned char*>(slot+byte) != 0xCC) {
                Log("Conflicting Lua bridge slot: BodySlider disabled");
                return FALSE;
            }
        }
    }
    if (!Prepare(g_hooks[0], 0x819BB0, 6, true) || !Prepare(g_hooks[1], 0x73DAB0, 6, true)
            || !Prepare(g_hooks[2], 0x832640, 5, true) || !Prepare(g_hooks[3], 0x82FE4F, 5, false)) {
        Rollback(); Log("Hook allocation failed: BodySlider disabled"); return FALSE;
    }
    g_initializeLua = reinterpret_cast<InitializeLuaFunction>(g_hooks[0].trampoline);
    g_preAnimate = reinterpret_cast<PreAnimateFunction>(g_hooks[1].trampoline);
    g_destroy = reinterpret_cast<DestroyFunction>(g_hooks[2].trampoline);
    for (size_t i = 0; i < 7; ++i) {
        if (!Prepare(g_hooks[4+i], MeshSignatures[i].address, MeshSignatures[i].size, true)) {
            Rollback(); Log("Mesh hook allocation failed: BodySlider disabled"); return FALSE;
        }
    }
    g_batchVertices = reinterpret_cast<decltype(g_batchVertices)>(g_hooks[4].trampoline);
    for (size_t i = 0; i < 4; ++i) g_cpu[i] = reinterpret_cast<std::remove_reference_t<decltype(g_cpu[0])>>(g_hooks[5+i].trampoline);
    g_noUV = reinterpret_cast<decltype(g_noUV)>(g_hooks[9].trampoline);
    g_shadow = reinterpret_cast<decltype(g_shadow)>(g_hooks[10].trampoline);
    const LuaFunction callbacks[6] = {Set, Clear, Supported, Names, Has, Units};
    for (size_t i = 0; i < 6; ++i) {
        if (!Prepare(g_hooks[11+i], Bridge::Slots[i], 5, false)) {
            Rollback(); Log("Lua bridge preparation failed: BodySlider disabled"); return FALSE;
        }
    }
    Install(g_hooks[0], InitializeLua);
    Install(g_hooks[1], PreAnimate);
    Install(g_hooks[2], Destroy);
    Install(g_hooks[3], BoneMultiply, true);
    Install(g_hooks[4], MeshBatchVertices);
    Install(g_hooks[5], MeshCpu0);
    Install(g_hooks[6], MeshCpu1);
    Install(g_hooks[7], MeshCpu2);
    Install(g_hooks[8], MeshCpu3);
    Install(g_hooks[9], MeshNoUV);
    Install(g_hooks[10], MeshShadow);
    for (size_t i = 0; i < 6; ++i) Install(g_hooks[11+i], callbacks[i]);
    if (!RestoreHookProtections()) {
        Rollback(); Log("Hook protection failed: BodySlider disabled"); return FALSE;
    }
    g_ready = true;
    Log("BodySlider 0.3.0: eleven rendering/Lua hooks and six validated Lua bridges installed; server protocol unchanged");
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
