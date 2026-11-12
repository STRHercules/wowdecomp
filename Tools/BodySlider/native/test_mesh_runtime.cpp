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
#include <cassert>
#include "ShapeMath.h"
#include "StockProfiles.h"
#include "DeformMath.h"
#include "DeformProfiles.h"
static uintptr_t TestAddress(uintptr_t address);
template<class T, class U> T TestCast(U value) {
    if constexpr (std::is_integral_v<U>) return reinterpret_cast<T>(TestAddress(static_cast<uintptr_t>(value)));
    else return reinterpret_cast<T>(value);
}
#define reinterpret_cast TestCast
#include "BodySlider.cpp"
#undef reinterpret_cast

namespace {
std::array<void*, 64> deviceTable{};
void** deviceTablePtr = deviceTable.data();
void* fakeDevice = &deviceTablePtr;
int shaders = 1;
int defaultBinds = 0, privateBinds = 0, uploads = 0, poolsFreed = 0, indicesBound = 0;
bool failLock = false;
bool failBuffer = false;
std::unordered_map<void*, std::vector<Deform::Vertex>> gpuData;
void* lastBound = nullptr;
void* expectedModel = nullptr;
const Deform::Vertex* expectedVertices = nullptr;
int cpuCalls = 0, shadowCalls = 0;

void* __fastcall FakePoolCreate(void*, void*, int target, int usage, int size, int hint, const char*) {
    assert(target == 0 && usage == 0 && size > 0 && hint == 1);
    return new unsigned char[8]{};
}
void* __fastcall FakeBufCreate(void*, void*, void* pool, uint32_t size, uint32_t count, uint32_t index) {
    assert(pool && size == 48 && count == 3 && index == 0);
    if (failBuffer) return nullptr;
    auto* buffer = new unsigned char[0x20]{};
    Field<uint32_t>(buffer, 0x10) = count;
    Field<uint8_t>(buffer, 0x1D) = 1;
    gpuData[buffer].resize(count);
    return buffer;
}
void __fastcall FakeBufDestroy(void*, void*, void*& buffer) {
    assert(gpuData.erase(buffer) == 1);
    delete[] static_cast<unsigned char*>(buffer);
    buffer = nullptr;
}
void __fastcall FakePoolDestroy(void*, void*, void* pool) { ++poolsFreed; delete[] static_cast<unsigned char*>(pool); }
void* __cdecl FakeLock(void* buffer) { return failLock ? nullptr : gpuData.at(buffer).data(); }
void __cdecl FakeUnlock(void* buffer, uint32_t size) {
    assert(size == 0); ++uploads;
    Field<uint8_t>(buffer, 0x1C) = Field<uint8_t>(buffer, 0x1D) = 1;
}
void __cdecl FakeVertexPtr(void* buffer, int format) { assert(format == 12); lastBound = buffer; ++privateBinds; }
int __fastcall FakeSharedVertices(void*, void*, uint32_t coord) { assert(coord == 0); ++defaultBinds; return 1; }
int __fastcall FakeSharedIndices(void*, void*) { ++indicesBound; return 1; }
void __fastcall FakeBatch(void*, void*, int) {}
void __cdecl FakeCpu(void* model, void*, void*, int) {
    assert(model != expectedModel);
    void* shared = Field<void*>(model, 0x2C);
    void* data = Field<void*>(shared, 0x150);
    assert(Field<const Deform::Vertex*>(data, 0x40) == expectedVertices);
    assert(Field<void*>(model, 0x98) == Field<void*>(expectedModel, 0x98));
    ++cpuCalls;
}
void __cdecl FakeNoUV(void* model, void* section, void* output) { FakeCpu(model, section, output, 0); }
void __fastcall FakeShadow(void*, void*, int, void*, const MeshBatchList* list, uint32_t index, void*, void* previous) {
    assert(list->count == 1 && list->batches[0].count == 1 && index == 0 && !previous);
    ++shadowCalls;
}
struct Fixture {
    std::array<unsigned char, 0xA0> model{};
    std::array<unsigned char, 0x174> shared{};
    std::array<unsigned char, 0x44> data{};
    std::array<unsigned char, 0x30> skin{};
    std::array<unsigned char, 0x30> section{};
    std::array<uint16_t, 3> remap{{2, 0, 1}};
    std::array<uint32_t, 3> bones{{0x01020304,0x05060708,0x090A0B0C}};
    std::array<Deform::Vertex, 3> source{};
    explicit Fixture(float offset) {
        Field<uint32_t>(model.data(), 0x10) = 1;
        Field<void*>(model.data(), 0x2C) = shared.data();
        Field<void*>(model.data(), 0x98) = source.data();
        Field<void*>(shared.data(), 0x150) = data.data();
        Field<void*>(shared.data(), 0x170) = skin.data();
        Field<uint32_t>(data.data(), 0x3C) = 3;
        Field<void*>(data.data(), 0x40) = source.data();
        Field<uint32_t>(skin.data(), 4) = 3;
        Field<void*>(skin.data(), 8) = remap.data();
        Field<uint32_t>(skin.data(), 0x14) = 3;
        Field<void*>(skin.data(), 0x18) = bones.data();
        Field<uint16_t>(section.data(), 6) = 3;
        auto vertices = std::make_shared<std::vector<Deform::Vertex>>(3);
        for (int i = 0; i < 3; ++i) {
            source[i].position[0] = static_cast<float>(i);
            (*vertices)[i] = source[i];
            (*vertices)[i].position[0] += offset;
        }
        MeshRecord& record = g_meshes[model.data()];
        record.shared = shared.data(); record.data = data.data(); record.skin = skin.data();
        record.source = source.data(); record.count = 3; record.vertices = vertices;
    }
};
}
static uintptr_t TestAddress(uintptr_t address) {
    switch (address) {
        case 0xC5DF88: return reinterpret_cast<uintptr_t>(&fakeDevice);
        case 0xD43020: return reinterpret_cast<uintptr_t>(&shaders);
        case 0x6876D0: return reinterpret_cast<uintptr_t>(FakePoolCreate);
        case 0x687660: return reinterpret_cast<uintptr_t>(FakeBufCreate);
        case 0x688290: return reinterpret_cast<uintptr_t>(FakeBufDestroy);
        case 0x4829D0: return reinterpret_cast<uintptr_t>(FakeLock);
        case 0x4829F0: return reinterpret_cast<uintptr_t>(FakeUnlock);
        case 0x681B00: return reinterpret_cast<uintptr_t>(FakeVertexPtr);
        case 0x8362B0: return reinterpret_cast<uintptr_t>(FakeSharedVertices);
        case 0x8360A0: return reinterpret_cast<uintptr_t>(FakeSharedIndices);
        default: return address;
    }
}

int main() {
    deviceTable[0xD4/4] = reinterpret_cast<void*>(FakePoolDestroy);
    g_batchVertices = reinterpret_cast<MeshBatchFunction>(FakeBatch);
    g_shadow = reinterpret_cast<MeshShadowFunction>(FakeShadow);
    for (auto& function : g_cpu) function = FakeCpu;
    g_noUV = FakeNoUV;
    Fixture first(10), second(20);
    auto originalModel = first.model;
    auto originalShared = first.shared;
    auto originalData = first.data;
    expectedModel = first.model.data();
    expectedVertices = g_meshes[first.model.data()].vertices->data();
    for (unsigned i = 0; i < 4; ++i) MeshCpu(i, first.model.data(), first.section.data(), nullptr, 0);
    MeshNoUV(first.model.data(), first.section.data(), nullptr);
    assert(cpuCalls == 5 && first.model == originalModel && first.shared == originalShared && first.data == originalData);
    // A held snapshot survives reset; no shared/global source buffer was modified.
    MeshCpuView held;
    assert(held.Prepare(first.model.data(), first.section.data()));
    assert(MeshBind(first.model.data()));
    assert(uploads == 1 && privateBinds == 1);
    auto* buffer = g_meshes[first.model.data()].buffer;
    const auto& uploaded = gpuData.at(buffer);
    for (int i = 0; i < 3; ++i) {
        assert(uploaded[i].position[0] == first.source[first.remap[i]].position[0]+10);
        assert(std::memcmp(uploaded[i].bones, &first.bones[i], 4) == 0);
    }
    assert(MeshBind(first.model.data()) && uploads == 1);
    Field<uint8_t>(buffer, 0x1D) = 0;
    assert(MeshBind(first.model.data()) && uploads == 2);
    std::array<unsigned char, 0x94> render{};
    Field<void*>(render.data(), 0x60) = second.model.data();
    Field<void*>(render.data(), 0x68) = second.shared.data();
    failBuffer = true;
    MeshBatchVertices(render.data(), nullptr, 0);
    assert(defaultBinds == 1 && poolsFreed == 1 && !g_meshes[second.model.data()].pool);
    failBuffer = false;
    failLock = true;
    MeshBatchVertices(render.data(), nullptr, 0);
    assert(defaultBinds == 2);
    failLock = false;
    MeshBatchVertices(render.data(), nullptr, 0);
    assert(privateBinds == 4);
    // Reset must force a default bind, even after erasing previous-model state.
    MeshClear(second.model.data());
    MeshBatchVertices(render.data(), nullptr, 0);
    assert(defaultBinds == 3);
    // Changed remap is bounded before any buffer lock.
    first.remap[0] = 100;
    Field<uint8_t>(buffer, 0x1D) = 0;
    assert(!MeshBind(first.model.data()));
    first.remap[0] = 2;
    assert(MeshBind(first.model.data()));
    // Mix shaped and default instances; only caller-owned one-entry lists are changed.
    MeshBatchEntry entries[2]{{first.model.data(), 0, 2}, {second.model.data(), 0, 1}};
    MeshBatchList list{entries,2,2};
    MeshShadow(first.model.data(), nullptr, 1, nullptr, &list, 0, first.section.data(), nullptr);
    assert(shadowCalls == 2 && indicesBound == 1 && entries[0].count == 2);
    MeshClear(first.model.data());
    assert(held.vertices && (*held.vertices)[0].position[0] == 10);
    assert(g_meshes.empty() && gpuData.empty() && poolsFreed == 3);
    assert(first.source[0].position[0] == 0);
    std::puts("Mesh runtime: CPU snapshots, GPU isolation/reset/failure, mixed shadows and cleanup passed");
}
