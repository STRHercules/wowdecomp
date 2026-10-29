#ifndef WOW_SOURCE_UI_VISUALS_H
#define WOW_SOURCE_UI_VISUALS_H

#include "WoW/Source/Ui/OptionCVars.h"

// Not in the original binary. Which visual pack draws the world: the Effects page's "Visuals" dropdown sets the
// saved `visuals` cvar, and each pack checks Visuals::Current() before it does anything.
namespace Visuals {

enum Mode : int32_t {
    Stock = 0,              // the Vulkan backend as it is, no extra passes
    Lumenveil = 1,
    AzerothRemastered = 2,
};

inline CVar*& Cvar() {
    static CVar* cvar = nullptr;
    return cvar;
}

inline void Register() {
    if (!Cvar()) {
        Cvar() = OptionCVars::Register("visuals", "Visual pack: 0 stock, 1 Lumenveil, 2 Azeroth Remastered", "1");
    }
}

inline Mode Current() {
    return static_cast<Mode>(OptionCVars::Int(Cvar(), Stock, AzerothRemastered, Lumenveil));
}

}  // namespace Visuals

#endif
