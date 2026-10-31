// Everlook radio: internet radio through the engine's FMOD. Lua gets PlayRadioStream(url), StopRadioStream(),
// IsRadioPlaying(), GetRadioTitle(), SetRadioVolume(0..1) and SetRadioAutoPause(0|1), which the
// EverlookBroadcasting add-on calls. WinINet fetches the stream (http and https, redirects, ICY titles) on a
// worker thread and FMOD reads it through user file callbacks, so the codec only ever sees the audio bytes.
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/SoundEngine/SoundEngine.h"
#include "WoW/Source/Extension/ExtensionMgr.h"
#include <windows.h>
#include <wininet.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "wininet.lib")

namespace {
constexpr size_t kLead = 1u << 20;      // how far the worker reads ahead of FMOD
constexpr size_t kHistory = 1u << 18;   // bytes kept behind the read position so a codec can seek back to the header

class Stream {
public:
    explicit Stream(std::string url) : m_url(std::move(url)) {}
    ~Stream() { Cancel(); }

    void Start() { m_thread = std::thread([this] { Run(); }); }

    void Cancel() {
        m_cancel = true;
        m_cv.notify_all();
        if (HINTERNET session = m_session.exchange(nullptr)) InternetCloseHandle(session); // unblocks a read in flight
        if (m_thread.joinable()) m_thread.join();
    }

    // FMOD side: blocks until `size` bytes are there or the stream ended.
    FMOD_RESULT Read(void* out, unsigned size, unsigned* got) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [&] { return m_cancel || m_done || m_base + m_data.size() - m_pos >= size; });
        size_t have = m_base + m_data.size() - m_pos;
        size_t n = std::min<size_t>(size, have);
        if (n) std::copy_n(m_data.begin() + (m_pos - m_base), n, static_cast<uint8_t*>(out));
        m_pos += n;
        *got = static_cast<unsigned>(n);
        if (m_pos - m_base > 2 * kHistory) {
            size_t drop = m_pos - m_base - kHistory;
            m_data.erase(m_data.begin(), m_data.begin() + drop);
            m_base += drop;
        }
        m_cv.notify_all();
        return n < size ? FMOD_ERR_FILE_EOF : FMOD_OK;
    }

    FMOD_RESULT Seek(size_t pos) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (pos < m_base || pos > m_base + m_data.size()) return FMOD_ERR_FILE_COULDNOTSEEK;
        m_pos = pos;
        return FMOD_OK;
    }

    bool Failed() const { return m_failed; }
    std::string Title() { std::lock_guard<std::mutex> lock(m_mutex); return m_title; }

private:
    void Push(const uint8_t* bytes, size_t n) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [&] { return m_cancel || m_base + m_data.size() - m_pos < kLead; });
        if (m_cancel) return;
        m_data.insert(m_data.end(), bytes, bytes + n);
        m_cv.notify_all();
    }

    // Splits the ICY metadata blocks out of the audio: after every m_metaint audio bytes comes a length byte (x16) and that much text.
    void Feed(const uint8_t* p, size_t n) {
        if (!m_metaint) { Push(p, n); return; }
        while (n && !m_cancel) {
            if (m_untilMeta) {
                size_t take = std::min(n, m_untilMeta);
                Push(p, take);
                p += take; n -= take; m_untilMeta -= take;
            } else if (m_metaLeft < 0) {
                m_metaLeft = *p++ * 16; --n;
                m_meta.clear();
                if (!m_metaLeft) { m_untilMeta = m_metaint; m_metaLeft = -1; }
            } else {
                size_t take = std::min<size_t>(n, m_metaLeft);
                m_meta.append(reinterpret_cast<const char*>(p), take);
                p += take; n -= take; m_metaLeft -= static_cast<int>(take);
                if (!m_metaLeft) { ParseMeta(); m_untilMeta = m_metaint; m_metaLeft = -1; }
            }
        }
    }

    void ParseMeta() {
        size_t a = m_meta.find("StreamTitle='");
        if (a == std::string::npos) return;
        a += 13;
        size_t b = m_meta.find("';", a);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_title = m_meta.substr(a, b == std::string::npos ? std::string::npos : b - a);
    }

    void Run() {
        HINTERNET net = InternetOpenA("Mozilla/5.0 (EverlookRadio)", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
        if (!net) { Finish(true); return; }
        m_session = net;
        static const char kHeaders[] = "Icy-MetaData: 1\r\n";
        HINTERNET req = InternetOpenUrlA(net, m_url.c_str(), kHeaders, static_cast<DWORD>(sizeof(kHeaders) - 1),
            INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_COOKIES, 0);
        if (!req) { Finish(true); return; }
        char meta[16] = "icy-metaint";
        DWORD len = sizeof(meta);
        if (HttpQueryInfoA(req, HTTP_QUERY_CUSTOM, meta, &len, nullptr)) m_metaint = static_cast<size_t>(atoi(meta));
        m_untilMeta = m_metaint;
        uint8_t buf[8192];
        DWORD n = 0;
        while (!m_cancel && InternetReadFile(req, buf, sizeof(buf), &n) && n) Feed(buf, n);
        InternetCloseHandle(req);
        Finish(false);
    }

    void Finish(bool failed) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_failed = failed || m_data.empty();
        m_done = true;
        m_cv.notify_all();
    }

    std::string m_url, m_title, m_meta;
    std::vector<uint8_t> m_data;
    size_t m_base = 0, m_pos = 0, m_metaint = 0, m_untilMeta = 0;
    int m_metaLeft = -1;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_thread;
    std::atomic<bool> m_cancel{false}, m_failed{false};
    std::atomic<HINTERNET> m_session{nullptr};
    bool m_done = false;
};

std::unique_ptr<Stream> s_stream;
FMOD::Sound* s_sound;
FMOD::Channel* s_channel;
float s_volume = 0.5f;
bool s_autoPause, s_pausedByUs;

FMOD_RESULT F_CALLBACK OnOpen(const char*, int, unsigned* size, void** handle, void** userdata) {
    if (!s_stream) return FMOD_ERR_FILE_NOTFOUND;
    *size = 0x7FFFFFFF; // a live stream has no length
    *handle = s_stream.get();
    *userdata = nullptr;
    return FMOD_OK;
}
FMOD_RESULT F_CALLBACK OnClose(void*, void*) { return FMOD_OK; }
FMOD_RESULT F_CALLBACK OnRead(void* handle, void* buffer, unsigned size, unsigned* got, void*) {
    return static_cast<Stream*>(handle)->Read(buffer, size, got);
}
FMOD_RESULT F_CALLBACK OnSeek(void* handle, unsigned pos, void*) { return static_cast<Stream*>(handle)->Seek(pos); }

void Stop() {
    s_channel = nullptr;
    if (s_stream) s_stream->Cancel(); // wakes a blocked FMOD read so release() can return
    if (s_sound) { s_sound->release(); s_sound = nullptr; }
    s_stream.reset();
    s_pausedByUs = false;
}

int32_t Script_PlayRadioStream(lua_State* L) {
    const char* url = lua_tostring(L, 1);
    FMOD::System* system = SE2::GetSystem();
    if (!url || !system) return 0;
    Stop();
    s_stream = std::make_unique<Stream>(url);
    s_stream->Start();
    static FMOD_CREATESOUNDEXINFO info;
    info = {};
    info.cbsize = sizeof(info);
    info.useropen = OnOpen;
    info.userclose = OnClose;
    info.userread = OnRead;
    info.userseek = OnSeek;
    if (system->createSound("radio", FMOD_SOFTWARE | FMOD_2D | FMOD_CREATESTREAM | FMOD_NONBLOCKING | FMOD_LOOP_OFF, &info, &s_sound) != FMOD_OK) Stop();
    return 0;
}

int32_t Script_StopRadioStream(lua_State*) { Stop(); return 0; }

int32_t Script_IsRadioPlaying(lua_State* L) {
    bool playing = false;
    if (s_channel) s_channel->isPlaying(&playing);
    lua_pushboolean(L, playing);
    return 1;
}

int32_t Script_GetRadioTitle(lua_State* L) {
    lua_pushstring(L, s_stream ? s_stream->Title().c_str() : "");
    return 1;
}

int32_t Script_SetRadioVolume(lua_State* L) {
    s_volume = static_cast<float>(std::clamp(lua_tonumber(L, 1), 0.0, 1.0));
    if (s_channel) s_channel->setVolume(s_volume);
    return 0;
}

int32_t Script_SetRadioAutoPause(lua_State* L) { s_autoPause = lua_tonumber(L, 1) != 0; return 0; }

bool GameInBackground() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid != GetCurrentProcessId();
}

class RadioUpdate : public ClientScript {
public:
    RadioUpdate() : ClientScript("EverlookRadioUpdate") {}

    void OnUpdate(float) override {
        if (!s_sound) return;
        if (!s_channel) {
            FMOD_OPENSTATE state;
            if (s_sound->getOpenState(&state, nullptr, nullptr) != FMOD_OK || state == FMOD_OPENSTATE_ERROR || s_stream->Failed()) { Stop(); return; }
            if (state != FMOD_OPENSTATE_READY) return;
            FMOD::System* system = SE2::GetSystem();
            if (!system || system->playSound(FMOD_CHANNEL_FREE, s_sound, false, &s_channel) != FMOD_OK) { Stop(); return; }
            s_channel->setVolume(s_volume);
            return;
        }
        bool hide = s_autoPause && GameInBackground();
        if (hide != s_pausedByUs) { s_channel->setPaused(hide); s_pausedByUs = hide; }
    }

    void OnClientDestroy() override { Stop(); }
};
}

void AddEverlookRadioExtension() {
    sExtensionMgr->RegisterLuaFunction("PlayRadioStream", Script_PlayRadioStream);
    sExtensionMgr->RegisterLuaFunction("StopRadioStream", Script_StopRadioStream);
    sExtensionMgr->RegisterLuaFunction("IsRadioPlaying", Script_IsRadioPlaying);
    sExtensionMgr->RegisterLuaFunction("GetRadioTitle", Script_GetRadioTitle);
    sExtensionMgr->RegisterLuaFunction("SetRadioVolume", Script_SetRadioVolume);
    sExtensionMgr->RegisterLuaFunction("SetRadioAutoPause", Script_SetRadioAutoPause);
    new RadioUpdate();
}
