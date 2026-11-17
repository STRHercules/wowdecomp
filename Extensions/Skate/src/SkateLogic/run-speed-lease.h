#pragma once
#include <cmath>
#include "ground-tuning.h"

// Engine-independent ownership checks. Never restore over a newer server value.
struct RunSpeedLease {
    bool held = false;
    float original = 0, applied = 0;
    float maximum=4,serverAllowance=0,restore=0;
    bool Capture(float current,float cap=4,float confirmedAllowance=0) {
        const bool confirmedTier=confirmedAllowance==groundtuning::Maximum() && cap==groundtuning::Maximum();
        const bool acknowledgedCurrent=confirmedTier && fabsf(current-confirmedAllowance)<=.01f;
        if (held || !std::isfinite(current) || current < 0.1f || (current > 30 && !acknowledgedCurrent)
            || !std::isfinite(cap) || cap<.1f || cap>groundtuning::Maximum()) return false;
        // The local server's run-only command confirms a rate without sending
        // a native speed packet. Accept only the enabled confirmed transport tier,
        // and retain the actual native original separately for restoration.
        if (!std::isfinite(confirmedAllowance) || (confirmedAllowance!=0 &&
            (!confirmedTier || (fabsf(current-7)>.01f && !acknowledgedCurrent)))) return false;
        original = applied = current; maximum=cap;
        // A fresh self-command acknowledgement may arrive with either native
        // baseline7 or the full server tier already applied. In the latter case
        // the owned tier must restore to normal, not leak70 into normal WoW.
        restore=acknowledgedCurrent?7.f:current;
        serverAllowance=confirmedAllowance?confirmedAllowance:current;
        held = true; return true;
    }
    bool Owns(float current) const {
        return held && std::isfinite(current) && std::fabs(current-applied) <= 0.0001f;
    }
    bool Allows(float requested) const {
        return held && std::isfinite(requested) && requested >= 0
            && requested <= maximum && requested <= serverAllowance;
    }
    void Clear() { held = false; original = applied = serverAllowance = restore = 0; maximum=4; }
};
