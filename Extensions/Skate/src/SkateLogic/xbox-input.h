#pragma once
#include <windows.h>
#include <cstdint>

namespace xboxinput {
struct Pad { uint16_t buttons=0; uint8_t leftTrigger=0,rightTrigger=0; int16_t lx=0,ly=0,rx=0,ry=0; };
struct State { uint32_t packet=0; Pad pad{}; };
static_assert(sizeof(Pad)==12 && sizeof(State)==16,"Raw Xbox XInput packet");
using GetStateFn=DWORD (WINAPI*)(DWORD,State*);
class Reader {
    HMODULE module=nullptr; GetStateFn get=nullptr;
public:
    bool Poll(int& slot,State& state) {
        if (!get) {
            module=LoadLibraryExW(L"xinput1_4.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!module) module=LoadLibraryExW(L"xinput9_1_0.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!module) return false;
            get=reinterpret_cast<GetStateFn>(GetProcAddress(module,"XInputGetState"));
            if (!get) return false;
        }
        if (slot>=0 && slot<4) return get(static_cast<DWORD>(slot),&state)==ERROR_SUCCESS;
        for (unsigned i=0;i<4;++i) if (get(i,&state)==ERROR_SUCCESS) { slot=static_cast<int>(i); return true; }
        return false;
    }
};
struct Toggle {
    bool armed=false; uint16_t previous=0;
    bool Step(bool connected,bool focused,uint16_t buttons) {
        const uint16_t mask=0x8000; // Y: original board action; J alone owns mode.
        if (!connected || !focused) { armed=false; previous=buttons; return false; }
        if (!(buttons&mask)) armed=true;
        const bool pressed=armed && (buttons&mask) && !(previous&mask);
        previous=buttons; return pressed;
    }
};
// No carried push, board action, stick or trigger input after a suspension.
struct ResumeGate {
    bool armed=false;
    bool Step(bool available,const Pad& pad) {
        if(!available){armed=false;return false;}
        const auto centred=[](int16_t v){return v>=-7849 && v<=7849;};
        if(!pad.buttons && pad.leftTrigger<=30 && pad.rightTrigger<=30
            && centred(pad.lx) && centred(pad.ly) && centred(pad.rx) && centred(pad.ry))armed=true;
        return armed;
    }
};
}
