#pragma once
#include <atomic>
#include <memory>
#include <vector>
#include <cstdint>
namespace VirtualAssets {
using Bytes = std::shared_ptr<const std::vector<uint8_t>>;
using Reader = Bytes (*)(const char* name);
inline std::atomic<Reader> reader{nullptr};
inline Bytes Read(const char* name) {
    const auto read = reader.load(std::memory_order_acquire);
    return read ? read(name) : nullptr;
}
}
