#pragma once

// RTX 40 (Ada) MFG unlock: the most generated frames advertised to the game ([DLSSG] AdaMfgMaxFrames).
//
// The patch always unlocks kUnlockedFrames. The ceiling only lowers what MfgUnlock::EffectiveMax reports (NGX
// parameters, Streamline state, DLSS-G capability): it never forces a frame count (the game, or Override DLSSG Ratio,
// still chooses one; see docs/issues/FG/FG-FF7-REBIRTH-MULTIPLIER-LOCK.md) and never drops below the runtime's own
// maximum. Pure: no Config or State dependencies.

#include <algorithm>

namespace AdaMfgCeiling
{

inline constexpr unsigned int kUnlockedFrames = 5; // 6X

// AdaMfgMaxFrames: 0 (or outside 1..5) = the unlocked maximum, 1 = 2X ... 5 = 6X.
constexpr unsigned int Ceiling(int configured)
{
    if (configured < 1 || configured > static_cast<int>(kUnlockedFrames))
        return kUnlockedFrames;
    return static_cast<unsigned int>(configured);
}

// What EffectiveMax reports. A failed patch reports 1 whatever the runtime said (its report may be altered);
// a complete patch reports the ceiling; otherwise the runtime's own maximum stands.
constexpr unsigned int Effective(unsigned int nativeMaximum, bool patchFailed, bool patchVerified, int configured)
{
    if (patchFailed)
        return 1;

    const unsigned int unlocked = patchVerified ? Ceiling(configured) : 0;
    return std::max(nativeMaximum, unlocked);
}

} // namespace AdaMfgCeiling
