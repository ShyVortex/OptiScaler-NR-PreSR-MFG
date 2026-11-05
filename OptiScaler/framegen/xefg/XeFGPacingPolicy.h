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
