#include <cstdint>
#include <new>
#include "Engine/Source/BlizzardCore/Source/Packages/iounit/IOChecksumUnit.h"
#include "Engine/Source/BlizzardCore/Source/Packages/iounit/IOUnitContainer.h"
#include "Engine/Source/BlizzardCore/Source/Packages/Mopaq/MPQGlobals.h"
#include "Engine/Source/BlizzardCore/Source/System/File/File.h"
#include "Engine/Source/BlizzardCore/Source/System/Lock/Lock.h"
#include <zlib.h>

// VERIFY: 759a2ec7 manual
// OFFSET: 0x00426820
void IOChecksumUnit::ChecksumJob(void* param) {
    Job* job = static_cast<Job*>(param);
    job->m_unit->Checksum(job->m_data, job->m_count);
    job->m_unit->m_position += job->m_count;
}

// INLINED
IOChecksumUnit::IOChecksumUnit(int64_t size, bool readToEnd, IOChecksumUnit** self, bool ownsMemory, IOUnit* next) : IOUnit(next, ownsMemory) {
    this->m_position = 0;
    this->m_size = size;
    this->m_context = nullptr;
    this->m_readToEnd = readToEnd;

    if (self) {
        *self = this;
    }
}

// VERIFY: b072b424 auto
// OFFSET: 0x00429620
uint32_t IOChecksumUnit::GetType() {
    return 'crc ';
}

// VERIFY: 875dcaaa manual
// OFFSET: 0x00457ED0
bool IOChecksumUnit::Read(void* buffer, int64_t offset, uint32_t count, uint32_t* bytesRead) {
    if (!this->Clamp(offset, &count, bytesRead) || !this->m_next->Read(buffer, offset, count, bytesRead)) {
        return false;
    }

    return this->Update(static_cast<const uint8_t*>(buffer), offset, bytesRead ? *bytesRead : count);
}

// VERIFY: 750c7f6b manual
// OFFSET: 0x00457F40
bool IOChecksumUnit::Write(const void* buffer, int64_t offset, uint32_t count) {
    if (!this->Clamp(offset, &count, nullptr)) {
        return false;
    }

    bool result;
    Job job;
    job.m_unit = this;

    if (this->GetRange(static_cast<const uint8_t*>(buffer), offset, count, &job.m_data, &job.m_count)) {
        System_MPQGlobals::Worker* worker;
        System_MPQGlobals::GetWorkerPool()->Acquire(1, &worker);

        if (!worker->m_proc) {
            worker->m_proc = ChecksumJob;
            worker->m_param = &job;
            Blizzard::Lock::EventSet(worker->m_start.m_event);
        }

        result = this->m_next->Write(buffer, offset, count);
        Blizzard::Lock::EventWaitForever(worker->m_done.m_event);
        System_MPQGlobals::GetWorkerPool()->Release(1, &worker);
    } else {
        result = this->m_next->Write(buffer, offset, count);
    }

    if (!result) {
        return false;
    }

    if (this->m_position == this->m_size) {
        return this->Finish();
    }

    return true;
}

// VERIFY: d6c9da29 manual
// OFFSET: 0x00426850
bool IOChecksumUnit::SetFileSize(int64_t size) {
    if (size <= this->m_size) {
        return this->m_next->SetFileSize(size);
    }

    return false;
}

// INLINED
IOChecksumUnit::~IOChecksumUnit() {
}

// VERIFY: 6d8f8a39 manual
// OFFSET: 0x00430E00
bool IOChecksumUnit::Close() {
    if (!this->m_readToEnd) {
        return true;
    }

    if (this->m_position >= this->m_size) {
        return true;
    }

    uint32_t count = static_cast<uint32_t>(this->m_size - this->m_position);
    uint8_t* buffer = count ? new uint8_t[count] : nullptr;
    bool result = this->Read(buffer, this->m_position, count, nullptr);

    if (buffer) {
        delete[] buffer;
    }

    return result;
}

void IOChecksumUnit::SetContext(void* context) {
    this->m_context = context;
}

// VERIFY: 80e5336f manual
// OFFSET: 0x00457E50
bool IOChecksumUnit::Clamp(int64_t offset, uint32_t* count, uint32_t* bytesRead) {
    if (offset + *count > this->m_size) {
        if (offset >= this->m_size) {
            Blizzard::File::SetLastError(10);
            Blizzard::File::AddToLastErrorStack(10, "IOChecksumUnit::Clamp - Range Error", 1);
            return false;
        }

        if (!bytesRead) {
            return false;
        }

        *count = static_cast<uint32_t>(this->m_size - offset);
    }

    return true;
}

// VERIFY: 7f568e1b manual
// OFFSET: 0x00426740
bool IOChecksumUnit::GetRange(const uint8_t* data, int64_t offset, uint32_t count, const uint8_t** rangeData, uint32_t* rangeCount) {
    if (offset > this->m_position) {
        return false;
    }

    if (offset + count <= this->m_position) {
        return false;
    }

    if (offset < this->m_position) {
        uint32_t skip = static_cast<uint32_t>(this->m_position - offset);
        data += skip;
        count -= skip;
    }

    *rangeData = data;
    *rangeCount = count;

    return true;
}

// VERIFY: 1fd2bcd5 manual
// OFFSET: 0x004267B0
bool IOChecksumUnit::Update(const uint8_t* data, int64_t offset, uint32_t count) {
    if (this->GetRange(data, offset, count, &data, &count)) {
        this->Checksum(data, count);
        this->m_position += count;

        if (this->m_position == this->m_size) {
            return this->Finish();
        }
    }

    return true;
}

// VERIFY: d67d5826 manual
// OFFSET: 0x004350C0
IOUnit* IOCrcUnit::Create(IOUnitContainer* container, int64_t size, Callback callback, bool readToEnd, IOChecksumUnit** self, IOUnit* next) {
    if (!next) {
        return nullptr;
    }

    if (!callback) {
        return next;
    }

    bool ownsMemory;
    void* memory = IOUnitContainer::Alloc(container, sizeof(IOCrcUnit), &ownsMemory);

    if (!memory) {
        return nullptr;
    }

    return new (memory) IOCrcUnit(size, callback, readToEnd, self, ownsMemory, next);
}

// VERIFY: 543472ca manual
// OFFSET: 0x00430C90
IOCrcUnit::IOCrcUnit(int64_t size, Callback callback, bool readToEnd, IOChecksumUnit** self, bool ownsMemory, IOUnit* next) : IOChecksumUnit(size, readToEnd, self, ownsMemory, next) {
    this->m_callback = callback;
    this->m_crc = 0;
}

// VERIFY: 81fee3cc auto
// OFFSET: 0x00430CF0
void IOCrcUnit::Checksum(const uint8_t* data, uint32_t count) {
    this->m_crc = crc32(this->m_crc, data, count);
}

// VERIFY: 83438dc2 auto
// OFFSET: 0x00430D20
bool IOCrcUnit::Finish() {
    return this->m_callback(this->m_context, this->m_crc);
}

// VERIFY: 72d6d727 manual
// OFFSET: 0x00435120
IOUnit* IOMd5Unit::Create(int64_t size, Callback callback, bool readToEnd, IOChecksumUnit** self, IOUnit* next) {
    if (!next) {
        return nullptr;
    }

    if (!callback) {
        return next;
    }

    bool ownsMemory;
    void* memory = IOUnitContainer::Alloc(nullptr, sizeof(IOMd5Unit), &ownsMemory);

    if (!memory) {
        return nullptr;
    }

    return new (memory) IOMd5Unit(size, callback, readToEnd, self, ownsMemory, next);
}

// VERIFY: 16498305 manual
// OFFSET: 0x00435180
IOUnit* IOMd5Unit::Create(IOUnitContainer* container, int64_t size, Callback callback, bool readToEnd, IOChecksumUnit** self, IOUnit* next) {
    if (!next) {
        return nullptr;
    }

    if (!callback) {
        return next;
    }

    bool ownsMemory;
    void* memory = IOUnitContainer::Alloc(container, sizeof(IOMd5Unit), &ownsMemory);

    if (!memory) {
        return nullptr;
    }

    return new (memory) IOMd5Unit(size, callback, readToEnd, self, ownsMemory, next);
}

// VERIFY: 71e51316 manual
// OFFSET: 0x00430D40
IOMd5Unit::IOMd5Unit(int64_t size, Callback callback, bool readToEnd, IOChecksumUnit** self, bool ownsMemory, IOUnit* next) : IOChecksumUnit(size, readToEnd, self, ownsMemory, next) {
    this->m_callback = callback;
}

// VERIFY: 0e82453d auto
// OFFSET: 0x00430DC0
void IOMd5Unit::Checksum(const uint8_t* data, uint32_t count) {
    this->m_md5.Process(data, count);
}

// VERIFY: 7776d404 auto
// OFFSET: 0x00430DD0
bool IOMd5Unit::Finish() {
    uint8_t digest[16];
    this->m_md5.Finish(digest);

    return this->m_callback(this->m_context, digest);
}
