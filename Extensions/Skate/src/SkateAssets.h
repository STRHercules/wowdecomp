// Phase 5: the in-memory files the board's M2 loads (VirtualAssets reader backing store). Engine-independent, tested.
// Names are matched the way the client asks for them: any case, either slash. Every request for a Skate\ file is
// recorded (bounded, once per name and outcome) so the log can say whether the loaders ever asked for it.
#pragma once
#include <cctype>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace skateassets {
using Bytes = std::shared_ptr<const std::vector<uint8_t>>;
inline std::string Key(const char* name) {
    std::string key(name);
    for (char& c : key) c = c == '/' ? '\\' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return key;
}
// Cheap prefix test: the reader runs for every file open on any thread.
inline bool Ours(const char* name) {
    return name && (name[0] | 32) == 's' && (name[1] | 32) == 'k' && (name[2] | 32) == 'a' && (name[3] | 32) == 't'
        && (name[4] | 32) == 'e' && (name[5] == '\\' || name[5] == '/');
}
struct Request { std::string name; size_t size = 0; bool hit = false; };
class Registry {
    std::mutex lock_;
    std::unordered_map<std::string, Bytes> files_;
    std::set<std::string> seen_;
    std::vector<Request> events_;
public:
    void Publish(const std::string& name, std::vector<uint8_t> bytes) {
        std::lock_guard<std::mutex> lock(lock_);
        files_[Key(name.c_str())] = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
    }
    void Clear() { std::lock_guard<std::mutex> lock(lock_); files_.clear(); seen_.clear(); events_.clear(); }
    // nullptr when the name is not a published file.
    Bytes Find(const char* name) {
        if (!Ours(name)) return nullptr;
        const std::string key = Key(name);
        std::lock_guard<std::mutex> lock(lock_);
        const auto found = files_.find(key);
        const Bytes bytes = found == files_.end() ? nullptr : found->second;
        if (events_.size() < 256 && seen_.insert(std::string(bytes ? "+" : "-") + key).second)
            events_.push_back({name, bytes ? bytes->size() : 0, bytes != nullptr});
        return bytes;
    }
    std::vector<Request> TakeRequests() { std::lock_guard<std::mutex> lock(lock_); std::vector<Request> out; out.swap(events_); return out; }
};
}
