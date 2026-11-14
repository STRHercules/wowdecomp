#pragma once
namespace Retail {
// A cached fit is usable only while the model still owns its explicit attachment frame.
inline bool ReuseBackpackFit(bool loaded, bool refit, bool bound) { return loaded && !refit && bound; }
inline bool BackpackFitPending(bool selected, bool loaded, bool fitted, bool bound) {
    return selected && (!loaded || !fitted || !bound);
}
}
