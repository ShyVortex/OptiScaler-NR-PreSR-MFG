#pragma once

// Streamline log capture shared by the full slInit hooks and the External-FG log-only hooks.
// In External FG mode OptiScaler must leave the game's Streamline untouched (architecture rule 4,
// FG-WRC-GENERATIONS-EXTERNAL-MFG.md), but without Streamline's own log there is no way to see why sl.dlss_g refuses
// to create frame generation (FG-LSA-EXTERNAL-MFG-NO-FEATURE-CREATE.md). The log-only hook rewrites exactly the two
// logging fields below and nothing else. Kept free of Streamline and Windows headers so the unit test runs the same
// code against SL1- and SL2-shaped preference structs.
namespace StreamlineLogPrefs
{
// Redirects Streamline's log into OptiScaler. The game's own callback is saved for chaining, unless the prefs
// already carry ours (a repeated slInit must not make our callback call itself). Only logLevel and
// logMessageCallback are written.
template <typename Prefs, typename Callback, typename Level>
inline void ApplyLogCapture(Prefs& prefs, Callback ours, Level level, Callback& savedGameCallback)
{
    if (prefs.logMessageCallback != ours)
        savedGameCallback = prefs.logMessageCallback;

    prefs.logLevel = level;
    prefs.logMessageCallback = ours;
}

enum class InterposerHookSet
{
    Full,   // OptiScaler owns or feeds FG: slInit, tags, constants, capability hooks as configured
    LogOnly // External FG: only the slInit log capture; no DLSS-G, tag, option or capability hooks
};

constexpr InterposerHookSet SelectHookSet(bool externalFrameGeneration)
{
    return externalFrameGeneration ? InterposerHookSet::LogOnly : InterposerHookSet::Full;
}
} // namespace StreamlineLogPrefs
