#pragma once

#include "DlssNr_DriverGate.h"

#include <optional>
#include <string>

// A game's own DLSS Neural Rendering option (NGX feature 18, e.g. Metro 2033 Redux) stays hidden when the driver
// reports NR unsupported. The fork's direct runtime (nvngx_dlssnr.dll) runs NR on GPUs NVIDIA does not support, so
// the option is unlocked on Windows and Linux alike, with one exception: on Windows a driver without NR support
// (FAIL_OutOfDate, DriverVersionUnsupported, or below the 610.88 gate) must never reach the direct runtime, which
// hung the GPU in DLSS Unlocked #60. Kept free of Windows headers so the unit test runs the same rule.
namespace DlssNr::GameNr
{
inline constexpr unsigned int FeatureId = 18;
inline constexpr unsigned int NgxResultSuccess = 0x1;
// NVSDK_NGX_FeatureSupportResult bits
inline constexpr unsigned int SupportDriverVersionUnsupported = 2;

enum class Decision
{
    PassThrough, // return the driver's answer unchanged
    Unlock       // report feature 18 supported; creation may use the direct runtime
};

// The support bits are only meaningful when the query itself succeeded.
constexpr bool DriverSupports(unsigned int result, unsigned int supportBits)
{
    return result == NgxResultSuccess && supportBits == 0;
}

constexpr bool DriverLacksNr(unsigned int result, unsigned int supportBits)
{
    return result == DriverGate::NgxResultOutOfDate ||
           (result == NgxResultSuccess && (supportBits & SupportDriverVersionUnsupported) != 0);
}

constexpr Decision Decide(bool onWine, bool enabled, bool hasCandidate, bool driverBlocked, unsigned int driverResult,
                          unsigned int supportBits)
{
    if (!enabled || !hasCandidate || DriverSupports(driverResult, supportBits))
        return Decision::PassThrough;

    // Wine: the Linux NGX has no NR feature at all, so every answer is "unsupported".
    if (!onWine && (driverBlocked || DriverLacksNr(driverResult, supportBits)))
        return Decision::PassThrough;

    return Decision::Unlock;
}

constexpr bool Failed(unsigned int result) { return (result & 0xFFF00000u) == 0xBAD00000u; }

// After the driver's CreateFeature(18): create through the direct runtime only when the driver produced no feature
// and the same rule unlocks it. A Windows FAIL_OutOfDate therefore always reaches the game unchanged.
constexpr bool ShouldCreateDirect(bool onWine, bool enabled, bool hasCandidate, bool driverBlocked,
                                  unsigned int createResult, bool gotHandle)
{
    return !gotHandle && Failed(createResult) &&
           Decide(onWine, enabled, hasCandidate, driverBlocked, createResult, 0) == Decision::Unlock;
}

// "Preload NR": load the direct runtime at the game's NGX init, never on a Windows driver below the 610.88 gate.
constexpr bool ShouldPreload(bool enabled, bool hasCandidate, bool driverBlocked)
{
    return enabled && hasCandidate && !driverBlocked;
}

// The weights nvngx_dlssnr.dll 310.9 picks from DLSSNR.Hint.Render.Preset (NR-LIGHTWEIGHT-MODEL-PRESET-SUPPORT.md):
// 0 or unset fall back to the lightweight model.
inline std::string ModelName(std::optional<unsigned int> preset)
{
    if (!preset || *preset == 0 || *preset == 2)
        return "Optimized (Small FP8)";
    if (*preset == 1)
        return "Original (Honest Tench)";
    return "unknown preset " + std::to_string(*preset);
}

inline const char* DecisionName(Decision decision) { return decision == Decision::Unlock ? "unlock" : "pass through"; }
} // namespace DlssNr::GameNr
