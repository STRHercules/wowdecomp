// Exercise native integration without loading or running WoW.
#include <windows.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>
#include <type_traits>
#include "ShapeMath.h"
#include "StockProfiles.h"
#include <cassert>
#include <limits>
#include <memory>

// Redirect pinned API addresses to test doubles; native implementation stays unchanged.
static uintptr_t TestAddress(uintptr_t address);
template<class T, class U> T TestCast(U value) {
    if constexpr (std::is_integral_v<U>) return reinterpret_cast<T>(TestAddress(static_cast<uintptr_t>(value)));
    else return reinterpret_cast<T>(value);
}
#define reinterpret_cast TestCast
#include "BodySlider.cpp"
#undef reinterpret_cast

namespace {
struct LuaValue {
    int type = 0;
    std::string text;
    double number = 0;
    bool boolean = false;
    void* pointer = nullptr; // a table's [0] (frame) or a light userdata
};
struct Lua { std::vector<LuaValue> values; };
Guid testGuid = 0x1234;
void* testPlayer = nullptr;
unsigned destroyed = 0;

Guid __cdecl FakeActivePlayer() { return testPlayer ? testGuid : 0; }
void* __cdecl FakeObject(Guid guid, uint32_t mask, const char*, int) {
    return guid == testGuid && mask == 0x10 ? testPlayer : nullptr;
}
int __cdecl FakeTop(Lua* lua) { return static_cast<int>(lua->values.size()); }
int __cdecl FakeType(Lua* lua, int index) {
    if (index < 0) index = static_cast<int>(lua->values.size())+1+index;
    return index > 0 && static_cast<size_t>(index) <= lua->values.size() ? lua->values[index-1].type : -1;
}
const char* __cdecl FakeString(Lua* lua, int index, size_t* length) {
    if (FakeType(lua, index) != 4) return nullptr;
    auto& value = lua->values[index-1].text;
    if (length) *length = value.size();
    return value.c_str();
}
void __cdecl FakeRawGetI(Lua* lua, int index, int key) {
    LuaValue pushed;
    if (key == 0 && lua->values[index-1].pointer) { pushed.type = 2; pushed.pointer = lua->values[index-1].pointer; }
    lua->values.push_back(pushed);
}
void* __cdecl FakeUserdata(Lua* lua, int index) {
    assert(index == -1);
    return lua->values.back().pointer;
}
void __cdecl FakeSetTop(Lua* lua, int index) { assert(index == -2); lua->values.pop_back(); }
double __cdecl FakeNumber(Lua* lua, int index) { return lua->values[index-1].number; }
void __cdecl FakePushBool(Lua* lua, int value) {
    LuaValue result; result.type = 1; result.boolean = value != 0; lua->values.push_back(result);
}
Shape::Matrix* __cdecl FakeMultiply(Shape::Matrix* out, const Shape::Matrix* left, const Shape::Matrix* right) {
    *out = Shape::Multiply(*left, *right);
    return out;
}
void __fastcall FakePreAnimate(void*, void*, void*) {}
void __fastcall FakeDestroy(void*, void*) { ++destroyed; }

// Reproduce the original callsite's ESI and EBP context for the real naked thunk.
__declspec(naked) Shape::Matrix* __cdecl InvokeBoneThunk(Shape::Matrix*, const Shape::Matrix*,
        const Shape::Matrix*, void*, unsigned, void*) {
    __asm {
        push ebp
        mov ebp, esp
        push esi
        mov esi, dword ptr [ebp+14h]
        mov eax, dword ptr [ebp+1Ch]
        mov dword ptr [ebp+14h], eax
        push dword ptr [ebp+10h]
        push dword ptr [ebp+0Ch]
        push dword ptr [ebp+08h]
        call BoneMultiply
        add esp, 12
        pop esi
        pop ebp
        ret
    }
}

struct Fixture {
    std::vector<unsigned char> model, shared, data, bones, descriptors, unit, sourceBones;
    std::vector<uint16_t> keys;
    const Stock::Rig& rig;
    explicit Fixture(const Stock::Rig& profile) : model(0x300), shared(0x200), data(0x200),
            bones(profile.count*0xAC), descriptors(16), unit(0x100), sourceBones(profile.count*0x58),
            keys(profile.keys, profile.keys+27), rig(profile) {
        Field<uint32_t>(model.data(), 0x10) = 1;
        Field<void*>(model.data(), 0x2C) = shared.data();
        Field<void*>(model.data(), 0x94) = bones.data();
        Field<void*>(shared.data(), 0x150) = data.data();
        strcpy_s(reinterpret_cast<char*>(shared.data()+0x3C), 260, profile.path);
        Field<uint32_t>(data.data(), 0x2C) = profile.count;
        Field<void*>(data.data(), 0x30) = sourceBones.data();
        Field<uint32_t>(data.data(), 0x34) = 27;
        Field<void*>(data.data(), 0x38) = keys.data();
        for (uint32_t i = 0; i < profile.count; ++i) {
            Field<uint16_t>(sourceBones.data()+i*0x58, 8) = profile.parents[i];
            std::memcpy(sourceBones.data()+i*0x58+0x4C, profile.pivots[i], 3*sizeof(float));
            Field<uint32_t>(bones.data()+i*0xAC, 0x8C) = 0x200;
        }
        Field<Guid>(descriptors.data(), 0) = testGuid;
        Field<uint32_t>(descriptors.data(), 8) = 0x10;
        Field<void*>(unit.data(), 8) = descriptors.data();
        Field<void*>(unit.data(), 0xB4) = model.data();
    }
    uint32_t& Flags(int index) { return Field<uint32_t>(bones.data()+index*0xAC, 0x8C); }
};
Lua SetArgs(const std::string& guid = "0x0000000000001234", const std::string& name = "Height", double number = .5) {
    Lua lua;
    LuaValue first; first.type = 4; first.text = guid;
    LuaValue second; second.type = 4; second.text = name;
    LuaValue third; third.type = 3; third.number = number;
    lua.values = {first, second, third};
    return lua;
}
bool Result(Lua& lua) { assert(Set(&lua) == 1); return lua.values.back().boolean; }
}

static uintptr_t TestAddress(uintptr_t address) {
    static void* device = nullptr;
    static int shaders = 0;
    switch (address) {
        case 0xC5DF88: return reinterpret_cast<uintptr_t>(&device);
        case 0xD43020: return reinterpret_cast<uintptr_t>(&shaders);
        case 0x4C1F00: return reinterpret_cast<uintptr_t>(FakeMultiply);
        case 0x4D3790: return reinterpret_cast<uintptr_t>(FakeActivePlayer);
        case 0x4D4DB0: return reinterpret_cast<uintptr_t>(FakeObject);
        case 0x84DBD0: return reinterpret_cast<uintptr_t>(FakeTop);
        case 0x84DEB0: return reinterpret_cast<uintptr_t>(FakeType);
        case 0x84E030: return reinterpret_cast<uintptr_t>(FakeNumber);
        case 0x84E0E0: return reinterpret_cast<uintptr_t>(FakeString);
        case 0x84E4D0: return reinterpret_cast<uintptr_t>(FakePushBool);
        case 0x84E670: return reinterpret_cast<uintptr_t>(FakeRawGetI);
        case 0x84E1C0: return reinterpret_cast<uintptr_t>(FakeUserdata);
        case 0x84DBF0: return reinterpret_cast<uintptr_t>(FakeSetTop);
        default: return address;
    }
}

int main() {
    // Two bridges can share a page: successful install and rollback must both restore RX.
    auto* page = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE));
    assert(page);
    std::memset(page, 0xCC, 4096);
    DWORD oldProtection = 0;
    assert(VirtualProtect(page, 4096, PAGE_EXECUTE_READ, &oldProtection));
    assert(Prepare(g_hooks[0], reinterpret_cast<uintptr_t>(page), 5, false));
    assert(Prepare(g_hooks[1], reinterpret_cast<uintptr_t>(page+16), 5, false));
    Install(g_hooks[0], page+128);
    Install(g_hooks[1], page+144);
    assert(RestoreHookProtections());
    MEMORY_BASIC_INFORMATION protection{};
    assert(VirtualQuery(page, &protection, sizeof(protection)) && protection.Protect == PAGE_EXECUTE_READ);
    Rollback();
    assert(VirtualQuery(page, &protection, sizeof(protection)) && protection.Protect == PAGE_EXECUTE_READ);
    for (int i = 0; i < 5; ++i) assert(page[i] == 0xCC && page[16+i] == 0xCC);
    assert(VirtualFree(page, 0, MEM_RELEASE));
    assert(GetModuleHandleA(nullptr) == reinterpret_cast<HMODULE>(0x10000000));
    g_preAnimate = reinterpret_cast<PreAnimateFunction>(FakePreAnimate);
    g_destroy = reinterpret_cast<DestroyFunction>(FakeDestroy);
    g_ready = true;

    unsigned profiles = 0;
    for (const auto& rig : Stock::kRigs) {
        Fixture fixture(rig);
        assert(RigFor(fixture.model.data()) == &rig);
        fixture.keys[0] ^= 1;
        assert(!RigFor(fixture.model.data()));
        fixture.keys[0] ^= 1;
        Field<float>(fixture.sourceBones.data(), 0x4C) += .01f;
        assert(!RigFor(fixture.model.data()));
        Field<float>(fixture.sourceBones.data(), 0x4C) = rig.pivots[0][0];
        assert(RigFor(fixture.model.data()) == &rig);
        ModelState state;
        Build(state, rig, Values{});
        for (const auto& transform : state.transforms) assert(!transform.Active());
        for (size_t slider = 0; slider < 12; ++slider) {
            assert(HasSlider(rig, static_cast<int>(slider)));
            for (float value : {-1.f, 1.f}) {
                Values values{}; values[slider] = value; Build(state, rig, values);
                bool changed = false;
                for (const auto& transform : state.transforms) {
                    changed |= transform.Active();
                    for (int axis = 0; axis < 3; ++axis) {
                        assert(std::isfinite(transform.scale[axis]) && transform.scale[axis] > 0);
                        assert(std::isfinite(transform.move[axis]));
                    }
                }
                assert(changed);
            }
        }
        ++profiles;
    }
    assert(profiles == 20);

    Fixture fixture(Stock::kRigs[0]);
    testPlayer = fixture.unit.data();
    const int root = fixture.rig.roots[0], head = fixture.rig.head;
    fixture.Flags(head) |= 0x80; // Existing game-owned procedural flag must survive.
    Values values{}; values[0] = .5f; values[1] = .4f;
    g_values[testGuid] = values;
    PreAnimate(testPlayer, nullptr, nullptr);
    assert(g_models.count(fixture.model.data()) == 1);
    assert(fixture.Flags(root) == 0x280 && fixture.Flags(head) == 0x280);
    assert(g_models[fixture.model.data()].ownedFlags[root]);
    assert(!g_models[fixture.model.data()].ownedFlags[head]);

    Shape::Matrix identity{{{1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}}};
    Shape::Matrix result{};
    assert(ShapeMultiply(&result, &identity, &identity, fixture.model.data(), root,
        fixture.sourceBones.data()+root*0x58) == &result);
    assert(std::fabs(result.m[0][0]-1.125f) < 1e-5f);
    Shape::Matrix thunkResult{};
    assert(InvokeBoneThunk(&thunkResult, &identity, &identity, fixture.model.data(), root,
        fixture.sourceBones.data()+root*0x58) == &thunkResult);
    assert(std::memcmp(&thunkResult, &result, sizeof(result)) == 0);
    Shape::Matrix untouched{};
    ShapeMultiply(&untouched, &identity, &identity, fixture.model.data(), fixture.rig.count, nullptr);
    assert(std::memcmp(&untouched, &identity, sizeof(identity)) == 0);

    g_values[testGuid] = Values{};
    PreAnimate(testPlayer, nullptr, nullptr);
    assert(fixture.Flags(root) == 0x200 && fixture.Flags(head) == 0x280);
    ShapeMultiply(&untouched, &identity, &identity, fixture.model.data(), root,
        fixture.sourceBones.data()+root*0x58);
    assert(std::memcmp(&untouched, &identity, sizeof(identity)) == 0);

    g_values.clear();
    PreAnimate(testPlayer, nullptr, nullptr);
    assert(!g_models.count(fixture.model.data()));
    assert(fixture.Flags(root) == 0x200 && fixture.Flags(head) == 0x280);
    auto valid = SetArgs(); assert(Result(valid));
    assert(g_values[testGuid][0] == .5f);
    for (const auto& badGuid : {"", "0x0000000000000000", "0x0000000000009999", "0x0000000000001234junk"}) {
        auto lua = SetArgs(badGuid); assert(!Result(lua));
    }
    for (double value : {-1.01, 1.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto lua = SetArgs("0x0000000000001234", "Height", value); assert(!Result(lua));
    }
    auto unknown = SetArgs("0x0000000000001234", "Unknown"); assert(!Result(unknown));
    for (const char* name : {"BreastSize", "ButtSize"}) {
        auto negative = SetArgs("0x0000000000001234", name, -.01); assert(!Result(negative));
        auto positive = SetArgs("0x0000000000001234", name, .5); assert(Result(positive));
    }
    auto face = SetArgs("0x0000000000001234", "NoseLength", -.5); assert(Result(face));
    assert(Shape::SliderCount == 37);
    auto embedded = SetArgs("0x0000000000001234", std::string("Height\0suffix", 13)); assert(!Result(embedded));
    auto wrongType = SetArgs(); wrongType.values[2].type = 4; assert(!Result(wrongType));
    auto extra = SetArgs(); extra.values.push_back({}); assert(!Result(extra));
    assert(g_values[testGuid][0] == .5f);
    Lua clear; Clear(&clear); assert(g_values.empty());
    testPlayer = nullptr;
    auto outsideWorld = SetArgs(); assert(!Result(outsideWorld));
    testPlayer = fixture.unit.data();
    g_values[testGuid] = values;
    PreAnimate(testPlayer, nullptr, nullptr);
    Destroy(fixture.model.data(), nullptr);
    assert(destroyed == 1 && g_models.empty());

    // A PlayerModel preview frame mirrors the local player's values onto its own model.
    Fixture preview(Stock::kRigs[0]);
    std::vector<unsigned char> frameObject(0x300);
    Field<uintptr_t>(frameObject.data(), 0) = PlayerModelVtable;
    Field<void*>(frameObject.data(), 0x2A0) = preview.model.data();
    auto previewCall = [&](void* pointer) {
        Lua lua; LuaValue table; table.type = 5; table.pointer = pointer; lua.values = {table};
        assert(Set(&lua) == 1 && lua.values.size() == 2);
        return lua.values.back().boolean;
    };
    assert(previewCall(frameObject.data()));
    assert(g_models.count(preview.model.data()) == 1 && preview.Flags(root) == 0x280);
    assert(previewCall(frameObject.data()) && preview.Flags(root) == 0x280); // per-frame repeat is stable
    g_values[testGuid] = Values{};
    assert(!previewCall(frameObject.data()) && !g_models.count(preview.model.data()) && preview.Flags(root) == 0x200);
    g_values[testGuid] = values;
    Field<uintptr_t>(frameObject.data(), 0) = PlayerModelVtable+4; // any other frame type is refused
    assert(!previewCall(frameObject.data()) && !g_models.count(preview.model.data()));
    assert(!previewCall(nullptr));
    Field<uintptr_t>(frameObject.data(), 0) = PlayerModelVtable;
    testPlayer = nullptr;
    assert(!previewCall(frameObject.data()));
    testPlayer = fixture.unit.data();
    g_values.clear();
    std::puts("native integration: 20 rigs, flags, matrix path, reset, Lua validation, preview frame passed");
    return 0;
}
