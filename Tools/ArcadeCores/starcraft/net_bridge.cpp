// Winsock inside StarCraft (32-bit, part of ddraw.dll): the Esteria arcade network.
//
// StarCraft's "Local Area Network (UDP)" provider opens two UDP sockets, binds them to 6111 (game discovery,
// broadcast) and 6112 (game traffic), and reads each with a blocking WSARecvFrom on a thread of its own (traced
// 2026-11-23). This file takes those sockets over: nothing reaches the real network. A send goes into the shared
// page's out ring for starcraft_core.cpp (which hands it to WoW and the server); datagrams from other cabinets come
// back through the in ring and are delivered as if from a LAN peer at their virtual address (10.77.x.y). Sockets of
// other kinds (Battle.net's TCP) are left alone.
//
// It also renames the provider in StarCraft's menu: the label lives only in the game's memory (not in any file we
// ship), so a sweep rewrites it there.
//
// Hooks go through the system DLLs' hot-patch slot (HotPatch below).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <vector>

#include "ScShared.h"

void BridgeLog(const char* fmt, ...);  // ddraw_bridge.cpp
#define Log BridgeLog

namespace net {

// Hooks an export through the system DLLs' hot-patch slot: mov edi,edi at the entry and 5 padding bytes before it.
// The original stays callable at entry + 2. Also used by ddraw_bridge.cpp (DirectSound).
bool HotPatch(HMODULE module, const char* name, void* hook, void** original) {
    auto* fn = module ? reinterpret_cast<BYTE*>(GetProcAddress(module, name)) : nullptr;
    if (!fn) { Log("hook: no %s", name); return false; }
    if (fn[0] == 0xFF && fn[1] == 0x25) {  // a forwarding stub (kernel32 -> kernelbase): jmp dword ptr [slot]; swap the slot
        void** slot = *reinterpret_cast<void***>(fn + 2);
        DWORD old;
        VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old);
        *original = *slot;
        *slot = hook;
        VirtualProtect(slot, sizeof(void*), old, &old);
        return true;
    }
    const bool slot = fn[0] == 0x8B && fn[1] == 0xFF;
    bool pad = true;
    for (int i = 1; i <= 5; i++) pad = pad && (fn[-i] == 0xCC || fn[-i] == 0x90);
    if (!slot || !pad) { Log("hook: %s has no hot-patch slot (%02X %02X)", name, fn[0], fn[1]); return false; }
    DWORD old;
    VirtualProtect(fn - 5, 7, PAGE_EXECUTE_READWRITE, &old);
    fn[-5] = 0xE9;  // jmp hook
    *reinterpret_cast<int32_t*>(fn - 4) = int32_t(reinterpret_cast<BYTE*>(hook) - fn);
    *reinterpret_cast<uint16_t*>(fn) = 0xF9EB;  // jmp short -7
    VirtualProtect(fn - 5, 7, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn - 5, 7);
    *original = fn + 2;
    return true;
}

namespace {

sc::Shared* g_sh;

// ---- virtual sockets ---------------------------------------------------------------------------------------------
struct Datagram {
    uint32_t ip;
    uint16_t port;
    std::vector<uint8_t> data;
};

struct VSock {
    uint16_t port = 0;  // bound port (host order); 0 until bind
    bool closed = false;
};

CRITICAL_SECTION g_lock;               // guards everything below
CONDITION_VARIABLE g_arrived;          // a datagram was queued or a socket closed
std::map<SOCKET, VSock> g_socks;       // the game's UDP sockets
std::map<uint16_t, std::deque<Datagram>> g_queues;  // per bound port
CRITICAL_SECTION g_sendLock;           // the out ring has one producer at a time
uint16_t g_nextPort = 50000;
// counters for the periodic "net: stats" line (diagnosing a game that stalls)
volatile LONG g_sent, g_sentBytesMax, g_tooBig, g_ringFull, g_received, g_unbound, g_queueFull, g_delivered;

using socket_t = SOCKET(WSAAPI*)(int, int, int);
using bind_t = int(WSAAPI*)(SOCKET, const sockaddr*, int);
using sendto_t = int(WSAAPI*)(SOCKET, const char*, int, int, const sockaddr*, int);
using recvfrom_t = int(WSAAPI*)(SOCKET, char*, int, int, sockaddr*, int*);
using closesocket_t = int(WSAAPI*)(SOCKET);
using getsockname_t = int(WSAAPI*)(SOCKET, sockaddr*, int*);
using WSARecvFrom_t = int(WSAAPI*)(SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD, sockaddr*, LPINT, LPWSAOVERLAPPED,
                                   LPWSAOVERLAPPED_COMPLETION_ROUTINE);
using WSASendTo_t = int(WSAAPI*)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, const sockaddr*, int, LPWSAOVERLAPPED,
                                 LPWSAOVERLAPPED_COMPLETION_ROUTINE);
socket_t o_socket;
bind_t o_bind;
sendto_t o_sendto;
recvfrom_t o_recvfrom;
closesocket_t o_closesocket;
getsockname_t o_getsockname;
WSARecvFrom_t o_WSARecvFrom;
WSASendTo_t o_WSASendTo;

VSock* Find(SOCKET s) {  // caller holds g_lock
    auto it = g_socks.find(s);
    return it == g_socks.end() ? nullptr : &it->second;
}

bool IsVirtual(SOCKET s) {
    EnterCriticalSection(&g_lock);
    const bool v = Find(s) != nullptr;
    LeaveCriticalSection(&g_lock);
    return v;
}

void FillAddr(sockaddr* a, int* len, uint32_t ip, uint16_t port) {
    if (!a || !len || *len < int(sizeof(sockaddr_in))) return;
    sockaddr_in in = {};
    in.sin_family = AF_INET;
    in.sin_addr.s_addr = htonl(ip);
    in.sin_port = htons(port);
    memcpy(a, &in, sizeof in);
    *len = sizeof in;
}

int Send(SOCKET s, const uint8_t* data, size_t len, const sockaddr* to) {
    uint16_t src = 0;
    EnterCriticalSection(&g_lock);
    if (VSock* v = Find(s)) {
        if (!v->port) v->port = g_nextPort++;  // an unbound socket gets a port on its first send, as Winsock does
        src = v->port;
    }
    LeaveCriticalSection(&g_lock);
    if (!to || to->sa_family != AF_INET) { WSASetLastError(WSAEAFNOSUPPORT); return SOCKET_ERROR; }
    if (len > sc::kNetMax) { InterlockedIncrement(&g_tooBig); WSASetLastError(WSAEMSGSIZE); return SOCKET_ERROR; }
    if (LONG(len) > g_sentBytesMax) g_sentBytesMax = LONG(len);
    const auto* in = reinterpret_cast<const sockaddr_in*>(to);
    uint32_t ip = ntohl(in->sin_addr.s_addr);
    if ((ip & 0xFF) == 0xFF) ip = 0xFFFFFFFF;  // any broadcast form (255.255.255.255, x.y.z.255) reaches every cabinet
    if (!g_sh->netAddr) return int(len);       // network off: the datagram is lost, like an unplugged cable
    EnterCriticalSection(&g_sendLock);
    sc::NetRing& r = g_sh->out;
    if (r.write - r.read < sc::kNetSlots) {
        sc::NetPacket& p = r.slot[r.write % sc::kNetSlots];
        p.len = uint32_t(len);
        p.ip = ip;
        p.srcPort = src;
        p.dstPort = ntohs(in->sin_port);
        memcpy(p.data, data, len);
        MemoryBarrier();
        r.write = r.write + 1;
        InterlockedIncrement(&g_sent);
    } else {
        InterlockedIncrement(&g_ringFull);  // a full ring drops the datagram: UDP may lose packets and the game resends
    }
    LeaveCriticalSection(&g_sendLock);
    return int(len);
}

// Blocks until a datagram arrives for the socket's port; copies it into the buffers.
int Receive(SOCKET s, LPWSABUF bufs, DWORD count, LPDWORD got, sockaddr* from, int* fromLen) {
    EnterCriticalSection(&g_lock);
    for (;;) {
        VSock* v = Find(s);
        if (!v || v->closed) { LeaveCriticalSection(&g_lock); WSASetLastError(WSAEINTR); return SOCKET_ERROR; }
        auto& q = g_queues[v->port];
        if (v->port && !q.empty()) {
            Datagram d = std::move(q.front());
            q.pop_front();
            LeaveCriticalSection(&g_lock);
            size_t at = 0;
            for (DWORD i = 0; i < count && at < d.data.size(); i++) {
                const size_t n = std::min<size_t>(bufs[i].len, d.data.size() - at);
                memcpy(bufs[i].buf, d.data.data() + at, n);
                at += n;
            }
            if (got) *got = DWORD(at);
            FillAddr(from, fromLen, d.ip, d.port);
            InterlockedIncrement(&g_delivered);
            if (at < d.data.size()) { WSASetLastError(WSAEMSGSIZE); return SOCKET_ERROR; }
            return 0;
        }
        SleepConditionVariableCS(&g_arrived, &g_lock, INFINITE);
    }
}

SOCKET WSAAPI h_socket(int af, int type, int proto) {
    const SOCKET s = o_socket(af, type, proto);
    if (s != INVALID_SOCKET && af == AF_INET && type == SOCK_DGRAM) {
        EnterCriticalSection(&g_lock);
        g_socks[s] = VSock();
        LeaveCriticalSection(&g_lock);
        Log("net: virtual UDP socket %u", unsigned(s));
    }
    return s;
}
int WSAAPI h_bind(SOCKET s, const sockaddr* a, int n) {
    if (!IsVirtual(s)) return o_bind(s, a, n);
    // never bound for real: two cabinets on one PC would both want 6111, and nothing should reach the real network
    const uint16_t port = a && a->sa_family == AF_INET ? ntohs(reinterpret_cast<const sockaddr_in*>(a)->sin_port) : 0;
    EnterCriticalSection(&g_lock);
    Find(s)->port = port ? port : g_nextPort++;
    LeaveCriticalSection(&g_lock);
    Log("net: bind %u to virtual port %u", unsigned(s), port);
    return 0;
}
int WSAAPI h_sendto(SOCKET s, const char* b, int len, int f, const sockaddr* a, int n) {
    return IsVirtual(s) ? Send(s, reinterpret_cast<const uint8_t*>(b), size_t(len), a) : o_sendto(s, b, len, f, a, n);
}
int WSAAPI h_WSASendTo(SOCKET s, LPWSABUF bufs, DWORD count, LPDWORD sent, DWORD f, const sockaddr* a, int n,
                       LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cr) {
    if (!IsVirtual(s) || ov) return o_WSASendTo(s, bufs, count, sent, f, a, n, ov, cr);
    std::vector<uint8_t> data;
    for (DWORD i = 0; i < count; i++) data.insert(data.end(), bufs[i].buf, bufs[i].buf + bufs[i].len);
    const int r = Send(s, data.data(), data.size(), a);
    if (r == SOCKET_ERROR) return r;
    if (sent) *sent = DWORD(r);
    return 0;
}
int WSAAPI h_recvfrom(SOCKET s, char* b, int len, int f, sockaddr* a, int* n) {
    if (!IsVirtual(s)) return o_recvfrom(s, b, len, f, a, n);
    WSABUF buf = { ULONG(len), b };
    DWORD got = 0;
    const int r = Receive(s, &buf, 1, &got, a, n);
    return r == SOCKET_ERROR ? r : int(got);
}
int WSAAPI h_WSARecvFrom(SOCKET s, LPWSABUF bufs, DWORD count, LPDWORD got, LPDWORD flags, sockaddr* a, LPINT n,
                         LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cr) {
    if (!IsVirtual(s)) return o_WSARecvFrom(s, bufs, count, got, flags, a, n, ov, cr);
    if (ov) { Log("net: overlapped WSARecvFrom is not virtualized"); WSASetLastError(WSAEOPNOTSUPP); return SOCKET_ERROR; }
    if (flags) *flags = 0;
    return Receive(s, bufs, count, got, a, n);
}
int WSAAPI h_closesocket(SOCKET s) {
    EnterCriticalSection(&g_lock);
    if (VSock* v = Find(s)) {
        g_queues.erase(v->port);
        g_socks.erase(s);  // a receiver blocked on it wakes, finds it gone and fails with WSAEINTR
        WakeAllConditionVariable(&g_arrived);
    }
    LeaveCriticalSection(&g_lock);
    return o_closesocket(s);
}
int WSAAPI h_getsockname(SOCKET s, sockaddr* a, int* n) {
    EnterCriticalSection(&g_lock);
    VSock* v = Find(s);
    const uint16_t port = v ? v->port : 0;
    LeaveCriticalSection(&g_lock);
    if (!v) return o_getsockname(s, a, n);
    FillAddr(a, n, g_sh->netAddr, port);
    return 0;
}

// ---- the menu label ----------------------------------------------------------------------------------------------
struct Rename { const char* from; const char* to; };
const Rename kRenames[] = {
    { "Local Area Network (UDP)", "Esteria Arcade Network" },
    { "All computers must be connected to a TCP/IP-compatible network.", "Play with everyone at an Esteria arcade cabinet." },
};

// One region; the game may free it while we look, so a fault just ends this region's scan (no C++ objects here,
// which __try requires).
int SweepRegion(BYTE* b, size_t size) {
    int changed = 0;
    __try {
        for (const Rename& r : kRenames) {
            const size_t n = strlen(r.from) + 1;  // with its NUL: a whole string, not a prefix of a longer one
            for (BYTE* p = b; (p = static_cast<BYTE*>(memchr(p, r.from[0], size - (p - b)))) && size_t(b + size - p) >= n; p++) {
                if (memcmp(p, r.from, n)) continue;
                memset(p, 0, n);
                memcpy(p, r.to, strlen(r.to));  // never longer than the original (checked in Install)
                changed++;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return changed;
}

int SweepLabels() {  // rewrites every copy in writable private memory; returns how many it changed
    int changed = 0;
    MEMORY_BASIC_INFORMATION m;
    for (BYTE* a = nullptr; VirtualQuery(a, &m, sizeof m); a = static_cast<BYTE*>(m.BaseAddress) + m.RegionSize) {
        if (m.State != MEM_COMMIT || m.Type != MEM_PRIVATE || !(m.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))
            || (m.Protect & PAGE_GUARD) || m.RegionSize > (64u << 20)) continue;
        changed += SweepRegion(static_cast<BYTE*>(m.BaseAddress), m.RegionSize);
    }
    return changed;
}

DWORD WINAPI LabelThread(LPVOID) {
    LONG lastSent = -1, lastReceived = -1;
    for (DWORD tick = 0;; tick++) {
        if (const int n = SweepLabels()) Log("net: renamed %d menu string(s)", n);
        if (tick % 10 == 0 && (g_sent != lastSent || g_received != lastReceived)) {
            lastSent = g_sent;
            lastReceived = g_received;
            Log("net: stats sent %ld (largest %ld, too big %ld, ring full %ld) received %ld (unbound port %ld, queue full %ld) delivered %ld",
                g_sent, g_sentBytesMax, g_tooBig, g_ringFull, g_received, g_unbound, g_queueFull, g_delivered);
        }
        Sleep(tick < 120 ? 500 : 10000);  // often while the menus first load, then a slow watch for rebuilt copies
    }
}

}  // namespace

// The in ring's datagrams, into the queue of the port they are for. Called often (the bridge's input thread).
void Pump() {
    if (!g_sh) return;
    sc::NetRing& r = g_sh->in;
    bool any = false;
    while (r.read != r.write) {
        const sc::NetPacket& p = r.slot[r.read % sc::kNetSlots];
        MemoryBarrier();
        Datagram d{ p.ip, p.srcPort, std::vector<uint8_t>(p.data, p.data + std::min(p.len, sc::kNetMax)) };
        const uint16_t port = p.dstPort;
        r.read = r.read + 1;
        EnterCriticalSection(&g_lock);
        bool bound = false;
        for (const auto& [s, v] : g_socks) bound = bound || v.port == port;
        InterlockedIncrement(&g_received);
        if (bound) {
            auto& q = g_queues[port];
            if (q.size() < 512) q.push_back(std::move(d));
            else InterlockedIncrement(&g_queueFull);
            any = true;
        } else {
            InterlockedIncrement(&g_unbound);
        }
        LeaveCriticalSection(&g_lock);
    }
    if (any) WakeAllConditionVariable(&g_arrived);
}

void Install(sc::Shared* shared) {
    g_sh = shared;
    InitializeCriticalSection(&g_lock);
    InitializeCriticalSection(&g_sendLock);
    InitializeConditionVariable(&g_arrived);
    for (const Rename& r : kRenames)
        if (strlen(r.to) > strlen(r.from)) Log("net: rename '%s' is longer than the original", r.to);
    CreateThread(nullptr, 0, LabelThread, nullptr, 0, nullptr);
    if (!LoadLibraryA("ws2_32.dll")) { Log("net: ws2_32.dll not loaded"); return; }
    HMODULE ws2 = GetModuleHandleA("ws2_32.dll");
#define HOOK(name) HotPatch(ws2, #name, reinterpret_cast<void*>(h_##name), reinterpret_cast<void**>(&o_##name))
    const bool ok = HOOK(socket) && HOOK(bind) && HOOK(sendto) && HOOK(recvfrom) && HOOK(closesocket) &&
                    HOOK(getsockname) && HOOK(WSARecvFrom) && HOOK(WSASendTo);
#undef HOOK
    Log(ok ? "net: arcade network ready" : "net: some hooks failed; the arcade network may not work");
}

}  // namespace net
