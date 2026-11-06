#pragma once

// Pure decisions behind XeFGPacing.h, kept free of Windows headers so unit tests can include them directly.

#include <cstdint>

namespace XeFGPacingPolicy
{
// Time between two bursts outside this window is not a frame (startup, pause, loading) and is not sampled.
constexpr int64_t MinBurstDeltaNs = 1000000LL;   // 1 ms
constexpr int64_t MaxBurstDeltaNs = 500000000LL; // 500 ms

// Lower bound of a sampled render time, and the margin the burst delta must exceed the recorded wait by.
constexpr int64_t MinPureRenderNs = 2000000LL; // 2 ms

// Render time of one real frame: the time since the previous burst minus the time spent waiting inside the
// provider's scheduler and OptiScaler's own pacing. Feeding the wait back as render time is the feedback loop of
// FG-XEMFG-RESONANT-LOCK-AND-TIMING-DECOUPLING.md. Returns 0 when the delta must not be sampled.
constexpr int64_t PureRenderNs(int64_t totalDeltaNs, int64_t blockNs)
{
    if (totalDeltaNs < MinBurstDeltaNs || totalDeltaNs > MaxBurstDeltaNs)
        return 0;

    return (totalDeltaNs > blockNs + MinPureRenderNs) ? (totalDeltaNs - blockNs) : MinPureRenderNs;
}

// Bounds of the step TsDetour gives the provider's scheduler between two presents of one burst.
constexpr int64_t MinStepNs = 1000000LL;  // 1 ms
constexpr int64_t MaxStepNs = 20000000LL; // 20 ms

// Present interval of an OptiScaler FPS cap, or 0 without one. The cap counts every presented frame, generated ones
// included (docs/DLSS-FRAME-GENERATION.md).
constexpr int64_t CapStepNs(double capFps)
{
    if (!(capFps > 0.0))
        return 0;

    return static_cast<int64_t>(1000000000.0 / capFps);
}

// Time between two presents of one burst, before the step bounds.
//
// periodNs / multiplier comes from the pure render time. That never contains our own waits, so it cannot feed back
// (FG-XEMFG-RESONANT-LOCK-AND-TIMING-DECOUPLING.md), but it is shorter than the real cadence whenever something
// else sets the cadence: under a cap all frames of a burst land in the first part of the base frame.
//
// Under an OptiScaler cap the cadence is known from the setting: one present per cap interval. That value does not
// depend on any measurement either, so using it as a floor cannot feed back (FG-FPS-LIMIT-XEMFG-PACING-AND-SYNC.md).
// The spacing is even while the pure render time stays at or under the cap interval; above that it stays bounded.
// Never use the provider's ring median here.
constexpr int64_t IntervalNs(int64_t periodNs, int64_t multiplier, int64_t capStepNs)
{
    const int64_t renderStepNs = multiplier > 0 ? periodNs / multiplier : 0;
    return capStepNs > renderStepNs ? capStepNs : renderStepNs;
}

// Step TsDetour hands to the provider's scheduler: IntervalNs within MinStepNs..MaxStepNs.
constexpr int64_t StepNs(int64_t refNs, int64_t multiplier, int64_t capStepNs)
{
    const int64_t stepNs = IntervalNs(refNs, multiplier, capStepNs);

    if (stepNs < MinStepNs)
        return MinStepNs;

    return stepNs > MaxStepNs ? MaxStepNs : stepNs;
}

// Whether a generated frame must only be measured (NoteFrame), not paced.
//
// When the provider's scheduler is usable it already schedules the last generated frame of a burst, so OptiScaler
// must not wait on it a second time (9e593639). At 2X that last frame is also the only generated frame, index 1,
// and therefore the only place the burst can be measured: without this, the render time stays 0 and XeFG is fed the
// swapchain delta including its own wait (ShyVortex/DLSS-Unlocked#64). Above 2X the last frame has index > 1 and
// index 1 is measured by ScheduleFrame, so this never matches there.
constexpr bool MeasureOnlyLastFrame(bool schedulerUsable, bool isLast, uint64_t index)
{
    return schedulerUsable && isLast && index <= 1;
}
} // namespace XeFGPacingPolicy
