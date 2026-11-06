#pragma once

// Pure decisions behind FrameLimit (the fallback FPS limiter), kept free of Windows headers so unit tests can include
// them directly. See docs/issues/FG/FG-FPS-LIMIT-XEMFG-PACING-AND-SYNC.md, section 4.3.

#include <cstdint>

namespace FrameLimitPolicy
{
constexpr uint64_t MaxIntervalUs = 100'000'000ULL; // 100 s

// Presented frames per real frame at a call site that sleeps once per real frame.
constexpr uint32_t OutputMultiplier(bool fgActive, uint32_t interpolatedFrames)
{
    return fgActive ? interpolatedFrames + 1 : 1;
}

// Minimum time between two calls. The limit counts presented frames, generated ones included, so a call site that
// runs once per real frame waits multiplier times one output interval (previously always 2, whatever the
// multiplier). 0 = no limit.
constexpr uint64_t MinIntervalUs(float limitFps, uint32_t outputMultiplier)
{
    if (!(limitFps > 0.0f))
        return 0;

    uint64_t intervalUs = static_cast<uint64_t>(1'000'000.0 / static_cast<double>(limitFps));
    if (intervalUs > MaxIntervalUs)
        intervalUs = MaxIntervalUs;

    return intervalUs * (outputMultiplier > 0 ? outputMultiplier : 1);
}

// External FG mode (dlssg_sm86 and other game-owned frame generation): the game's DLSS-G presents and paces the
// generated frames. A sleep in Present lands on that path and fights its pacing, and Reflex limiting is off in that
// mode (a60f3c93), so OptiScaler applies no FPS limit there (FG-FPS-LIMIT-XEMFG-PACING-AND-SYNC.md, section 4.4).
constexpr bool AppliesInMode(bool externalFrameGeneration) { return !externalFrameGeneration; }

inline constexpr const char* ExternalModeMethod = "None (External FG mode)";
inline constexpr const char* ExternalModeMessage =
    "OptiScaler's FPS limit is not available in External FG mode.\nUse RTSS or the game's own frame limit.";
} // namespace FrameLimitPolicy
