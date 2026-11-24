// The page shared by starcraft_libretro.dll (64-bit, inside the WoW client) and the ddraw.dll bridge (32-bit, inside
// StarCraft.exe). Same layout on both sides: fixed-size fields only. The core names the mapping and passes the name to
// StarCraft in the ESTERIA_SC_SHM environment variable.
#pragma once
#include <cstdint>

namespace sc {

constexpr uint32_t kMagic = 0x31435345;  // "ESC1"
constexpr uint32_t kMaxW = 640, kMaxH = 480;
constexpr uint32_t kKeyRing = 256;
constexpr uint32_t kNetSlots = 256, kNetMax = 2048;  // above any datagram that crosses Ethernet (~1472 bytes)

// One UDP datagram of the arcade network. Addresses are virtual (10.77.x.y, host byte order) and assigned by the
// server; 0xFFFFFFFF is a broadcast to every cabinet in the network.
struct NetPacket {
    uint32_t len;
    uint32_t ip;               // outgoing: destination; incoming: sender
    uint16_t srcPort, dstPort;
    uint8_t data[kNetMax];
};

// Single-producer single-consumer ring: the producer fills slot[write % kNetSlots], then advances write.
struct NetRing {
    volatile uint32_t write, read;
    NetPacket slot[kNetSlots];
};

struct Shared {
    uint32_t magic;
    // video: the bridge writes pixels + palette, then bumps frame
    volatile uint32_t frame;
    uint32_t width, height;
    uint32_t palette[256];  // 0x00RRGGBB
    uint8_t pixels[kMaxW * kMaxH];
    // input: the core writes these
    volatile int32_t mouseX, mouseY;  // game pixels
    volatile uint32_t buttons;        // 1 left, 2 right, 4 middle
    volatile uint32_t keyWrite;       // ring of (virtual key | down << 16), written by the core
    uint32_t keys[kKeyRing];
    volatile uint32_t keyRead;        // advanced by the bridge
    // status
    volatile uint32_t bridgeAlive;    // the bridge's heartbeat (ms tick)
    volatile uint32_t quit;           // the core asks the game to close
    // arcade network (net_bridge.cpp <-> starcraft_core.cpp): the game's UDP sockets, virtualized
    volatile uint32_t netAddr;        // this cabinet's virtual address, set by the core (0 = network off)
    NetRing out;                      // the game's datagrams, for the core to send (bridge produces)
    NetRing in;                       // datagrams from other cabinets (core produces)
};

}  // namespace sc
