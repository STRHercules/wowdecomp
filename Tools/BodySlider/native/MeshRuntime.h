// Included inside the native implementation's namespace after its stock ABI helpers.
// Every geometry buffer belongs to one model; no engine-owned vertex array is changed.
struct MeshRecord {
    void* shared = nullptr;
    void* data = nullptr;
    void* skin = nullptr;
    const Deform::Vertex* source = nullptr;
    uint32_t count = 0;
    const Deform::Profile* profile = nullptr;
    Values values{};
    bool valuesReady = false;
    std::shared_ptr<const std::vector<Deform::Vertex>> vertices;
    void* device = nullptr;
    void* pool = nullptr;
    void* buffer = nullptr;
};
std::unordered_map<void*, MeshRecord> g_meshes;
const auto MeshPoolCreate = reinterpret_cast<void* (__thiscall*)(void*, int, int, int, int, const char*)>(0x6876D0);
const auto MeshBufCreate = reinterpret_cast<void* (__thiscall*)(void*, void*, uint32_t, uint32_t, uint32_t)>(0x687660);
const auto MeshBufDestroy = reinterpret_cast<void (__thiscall*)(void*, void*&)>(0x688290);
const auto MeshBufLock = reinterpret_cast<void* (__cdecl*)(void*)>(0x4829D0);
const auto MeshBufUnlock = reinterpret_cast<void (__cdecl*)(void*, uint32_t)>(0x4829F0);
const auto MeshVertexPtr = reinterpret_cast<void (__cdecl*)(void*, int)>(0x681B00);
const auto MeshSharedVertices = reinterpret_cast<int (__thiscall*)(void*, uint32_t)>(0x8362B0);
const auto MeshSharedIndices = reinterpret_cast<int (__thiscall*)(void*)>(0x8360A0);

void* MeshDevice() { return *reinterpret_cast<void**>(0xC5DF88); }
bool MeshShaders() { return *reinterpret_cast<int*>(0xD43020) != 0; }

void MeshReleaseGpu(MeshRecord& record) {
    void* device = MeshDevice();
    if (device && device == record.device) {
        if (record.buffer) MeshBufDestroy(device, record.buffer);
        if (record.pool) {
            auto destroy = reinterpret_cast<void (__thiscall*)(void*, void*)>(Field<void**>(device, 0)[0xD4/4]);
            destroy(device, record.pool);
        }
    }
    record.pool = record.buffer = record.device = nullptr;
}

MeshRecord* MeshEnsure(void* model) {
    if (!model || !(Field<uint32_t>(model, 0x10)&1)) return nullptr;
    void* shared = Field<void*>(model, 0x2C);
    void* data = shared ? Field<void*>(shared, 0x150) : nullptr;
    void* skin = shared ? Field<void*>(shared, 0x170) : nullptr;
    if (!data || !skin) return nullptr;
    uint32_t count = Field<uint32_t>(data, 0x3C);
    const auto* source = Field<const Deform::Vertex*>(data, 0x40);
    if (!source || !count || count > 65535) return nullptr;
    auto& record = g_meshes[model];
    if (record.shared == shared && record.data == data && record.skin == skin
            && record.source == source && record.count == count) return &record;
    MeshReleaseGpu(record);
    record = {};
    record.shared = shared;
    record.data = data;
    record.skin = skin;
    record.source = source;
    record.count = count;
    const char* name = static_cast<char*>(shared)+0x3C;
    size_t length = strnlen_s(name, 260);
    if (!length || length == 260) return &record;
    std::string path(name, length);
    for (char& c : path) c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (path.size() >= 4 && path.compare(path.size()-4, 4, ".mdx") == 0) path.replace(path.size()-4, 4, ".m2");
    record.profile = Deform::Find(path.c_str(), source, count);
    return &record;
}

void MeshUpdate(void* model, const Values& values) {
    MeshRecord* record = MeshEnsure(model);
    if (!record || !record->profile) return;
    if (record->valuesReady && std::equal(record->values.begin()+12, record->values.end(), values.begin()+12)) return;
    record->values = values;
    record->valuesReady = true;
    MeshReleaseGpu(*record);
    bool active = false;
    for (size_t i = 12; i < Shape::SliderCount; ++i)
        if (values[i] != 0 && Deform::HasSlider(*record->profile, static_cast<int>(i))) active = true;
    if (!active) { record->vertices.reset(); return; }
    auto vertices = std::make_shared<std::vector<Deform::Vertex>>(record->count);
    bool moved = false;
    for (uint32_t i = 0; i < record->count; ++i)
        moved = Deform::Apply((*vertices)[i], record->source[i], *record->profile, values.data(), i) || moved;
    record->vertices = moved ? vertices : nullptr;
}

void MeshClear(void* model) {
    auto record = g_meshes.find(model);
    if (record == g_meshes.end()) return;
    MeshReleaseGpu(record->second);
    g_meshes.erase(record);
}

bool MeshHasSlider(void* model, int slider) {
    MeshRecord* record = MeshEnsure(model);
    return record && record->profile && Deform::HasSlider(*record->profile, slider);
}

bool MeshBind(void* model) {
    auto found = g_meshes.find(model);
    if (found == g_meshes.end() || !found->second.vertices) return false;
    auto& record = found->second;
    void* shared = Field<void*>(model, 0x2C);
    if (shared != record.shared || Field<void*>(shared, 0x150) != record.data
            || Field<void*>(shared, 0x170) != record.skin) return false;
    if (Field<const Deform::Vertex*>(record.data, 0x40) != record.source
            || Field<uint32_t>(record.data, 0x3C) != record.count) return false;
    void* device = MeshDevice();
    if (!device) return false;
    if (record.device && record.device != device) MeshReleaseGpu(record);
    void* skin = record.skin;
    uint32_t count = Field<uint32_t>(skin, 4);
    auto* remap = Field<uint16_t*>(skin, 8);
    uint32_t boneCount = Field<uint32_t>(skin, 0x14);
    auto* bones = Field<uint32_t*>(skin, 0x18);
    if (!count || count > 65535 || !remap || !bones || boneCount < count) return false;
    if (!record.pool) {
        record.device = device;
        record.pool = MeshPoolCreate(device, 0, 0, static_cast<int>(count*sizeof(Deform::Vertex)), 1, "BodySlider");
        if (!record.pool) return false;
        record.buffer = MeshBufCreate(device, record.pool, sizeof(Deform::Vertex), count, 0);
        if (!record.buffer) { MeshReleaseGpu(record); return false; }
    }
    if (Field<uint32_t>(record.buffer, 0x10) != count) { MeshReleaseGpu(record); return false; }
    if (!Field<uint8_t>(record.buffer, 0x1C) || !Field<uint8_t>(record.buffer, 0x1D)) {
        // Validate every remap before locking a device buffer, so failures leave no lock outstanding.
        for (uint32_t i = 0; i < count; ++i) if (remap[i] >= record.vertices->size()) return false;
        auto* destination = static_cast<Deform::Vertex*>(MeshBufLock(record.buffer));
        if (!destination) return false;
        for (uint32_t i = 0; i < count; ++i) {
            destination[i] = (*record.vertices)[remap[i]];
            std::memcpy(destination[i].bones, &bones[i], 4);
        }
        MeshBufUnlock(record.buffer, 0);
    }
    MeshVertexPtr(record.buffer, 12);
    return true;
}

using MeshBatchFunction = void (__thiscall*)(void*, int);
MeshBatchFunction g_batchVertices = nullptr;
void __fastcall MeshBatchVertices(void* render, void*, int sharedVertices) {
    g_batchVertices(render, sharedVertices);
    if (!MeshShaders()) return;
    AcquireSRWLockExclusive(&g_lock);
    void* model = Field<void*>(render, 0x60);
    bool bound = model && MeshBind(model);
    // A reset may erase the previous shape record while its buffer is still the engine's current stream.
    if (!bound) {
        void* shared = Field<void*>(render, 0x68);
        if (shared) MeshSharedVertices(shared, 0);
    }
    ReleaseSRWLockExclusive(&g_lock);
}

using MeshCpuFunction = void (__cdecl*)(void*, void*, void*, int);
using MeshNoUVFunction = void (__cdecl*)(void*, void*, void*);
MeshCpuFunction g_cpu[4]{};
MeshNoUVFunction g_noUV = nullptr;

// These five stock transform routines read only these prefixes and write the caller's output.
struct MeshCpuView {
    std::array<unsigned char, 0xA0> model;
    std::array<unsigned char, 0x154> shared;
    std::array<unsigned char, 0x44> data;
    std::shared_ptr<const std::vector<Deform::Vertex>> vertices;
    bool Prepare(void* original, void* section) {
        AcquireSRWLockShared(&g_lock);
        auto it = g_meshes.find(original);
        if (it != g_meshes.end()) vertices = it->second.vertices;
        if (!vertices) { ReleaseSRWLockShared(&g_lock); return false; }
        void* originalShared = Field<void*>(original, 0x2C);
        void* originalData = originalShared ? Field<void*>(originalShared, 0x150) : nullptr;
        uint32_t start = Field<uint16_t>(section, 4);
        uint32_t count = Field<uint16_t>(section, 6);
        if (originalShared != it->second.shared || originalData != it->second.data
                || Field<const Deform::Vertex*>(originalData, 0x40) != it->second.source
                || Field<uint32_t>(originalData, 0x3C) != it->second.count
                || start > vertices->size() || count > vertices->size()-start) {
            ReleaseSRWLockShared(&g_lock); vertices.reset(); return false;
        }
        std::memcpy(model.data(), original, model.size());
        std::memcpy(shared.data(), originalShared, shared.size());
        std::memcpy(data.data(), originalData, data.size());
        ReleaseSRWLockShared(&g_lock);
        Field<void*>(model.data(), 0x2C) = shared.data();
        Field<void*>(shared.data(), 0x150) = data.data();
        Field<const Deform::Vertex*>(data.data(), 0x40) = vertices->data();
        return true;
    }
};
void MeshCpu(unsigned routine, void* model, void* section, void* output, int texCoord) {
    MeshCpuView view;
    g_cpu[routine](view.Prepare(model, section) ? view.model.data() : model, section, output, texCoord);
}
void __cdecl MeshCpu0(void* model, void* section, void* output, int texCoord) { MeshCpu(0, model, section, output, texCoord); }
void __cdecl MeshCpu1(void* model, void* section, void* output, int texCoord) { MeshCpu(1, model, section, output, texCoord); }
void __cdecl MeshCpu2(void* model, void* section, void* output, int texCoord) { MeshCpu(2, model, section, output, texCoord); }
void __cdecl MeshCpu3(void* model, void* section, void* output, int texCoord) { MeshCpu(3, model, section, output, texCoord); }
void __cdecl MeshNoUV(void* model, void* section, void* output) {
    MeshCpuView view;
    g_noUV(view.Prepare(model, section) ? view.model.data() : model, section, output);
}

struct MeshBatchEntry { void* model; uint32_t batchIndex; uint32_t count; };
struct MeshBatchList { MeshBatchEntry* batches; uint32_t count; uint32_t maxCount; };
using MeshShadowFunction = void (__thiscall*)(void*, int, void*, const MeshBatchList*, uint32_t, void*, void*);
MeshShadowFunction g_shadow = nullptr;
void __fastcall MeshShadow(void* model, void*, int untextured, void* batch, const MeshBatchList* list,
        uint32_t index, void* section, void* previousSection) {
    if (!MeshShaders() || !list || index >= list->count) {
        g_shadow(model, untextured, batch, list, index, section, previousSection);
        return;
    }
    uint32_t total = list->batches[index].count;
    if (!total || total > list->count-index) return;
    bool shaped = false;
    AcquireSRWLockShared(&g_lock);
    for (uint32_t i = 0; i < total; ++i) {
        auto record = g_meshes.find(list->batches[index+i].model);
        if (record != g_meshes.end() && record->second.vertices) { shaped = true; break; }
    }
    ReleaseSRWLockShared(&g_lock);
    if (!shaped) { g_shadow(model, untextured, batch, list, index, section, previousSection); return; }
    if (total > 1 && !MeshSharedIndices(Field<void*>(model, 0x2C))) return;
    // Stock grouping only combines equal shared geometry/batch IDs. Split without editing the shared list.
    for (uint32_t i = 0; i < total; ++i) {
        MeshBatchEntry entry = list->batches[index+i];
        entry.count = 1;
        MeshBatchList single{&entry, 1, 1};
        AcquireSRWLockExclusive(&g_lock);
        if (!MeshBind(entry.model)) MeshSharedVertices(Field<void*>(entry.model, 0x2C), 0);
        ReleaseSRWLockExclusive(&g_lock);
        g_shadow(entry.model, untextured, batch, &single, 0, section, nullptr);
    }
}

struct MeshSignature { uintptr_t address; size_t size; unsigned char bytes[12]; };
const MeshSignature MeshSignatures[] = {
    {0x81F700,10,{0x55,0x8B,0xEC,0x83,0x3D,0x20,0x30,0xD4,0,0}},
    {0x82A210,6,{0x55,0x8B,0xEC,0x83,0xEC,0x68}},
    {0x82A4E0,6,{0x55,0x8B,0xEC,0x8B,0x45,0x08}},
    {0x82A600,6,{0x53,0x8B,0xDC,0x83,0xEC,0x08}},
    {0x82AC10,6,{0x53,0x8B,0xDC,0x83,0xEC,0x08}},
    {0x829F40,6,{0x55,0x8B,0xEC,0x83,0xEC,0x64}},
    {0x829BA0,6,{0x55,0x8B,0xEC,0x83,0xEC,0x24}}
};
