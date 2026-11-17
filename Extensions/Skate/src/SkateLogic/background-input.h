#pragma once
#include <cstdint>
namespace backgroundinput {
// Input ownership, not simulation ownership. Never carry a held keyboard
// control across focus loss; each control must publish its release first.
struct Keyboard {
    uint32_t blocked=0;
    uint32_t Step(bool focused,uint32_t pressed) {
        pressed&=15u;
        if(!focused){blocked=15u;return 0;}
        blocked&=pressed;
        return pressed&~blocked;
    }
};
}
