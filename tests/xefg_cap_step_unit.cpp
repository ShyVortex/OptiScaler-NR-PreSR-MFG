#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "../OptiScaler/framegen/xefg/XeFGPacingPolicy.h"

// XeMFG pacing step under an OptiScaler FPS cap (FG-FPS-LIMIT-XEMFG-PACING-AND-SYNC.md, section 4.1).
//
// The step between presents of one burst came only from the pure render time (19.8 ms at a 120 cap, 4X), so the
// four presents of a 33 ms base frame landed about 16 / 9 / 4 / 4 ms apart. Under a cap the step is floored to the
// cap interval (8.33 ms at 120), a value taken from the setting, never from a measurement.
//
// 1. CapStepNs: no cap, invalid caps and 120 fps.
// 2. Without a cap StepNs and IntervalNs give exactly the previous values.
// 3. Lost Soul Aside figures: 120 cap at 4X gives 8.33 ms; uncapped stays at about 2 ms.
// 4. A render step longer than the cap step wins (heavy scene).
// 5. Step bounds: a cap under 50 fps stops at 20 ms, a very high cap never goes under the render step or 1 ms.
// 6. Engine-thread model: under a cap the cycle converges and stays bounded, with even spacing while the render
//    time fits in the cap interval. No growth over iterations.
// 7. Engine-thread model without a cap: the step is the pure-render step, unchanged (resonant-lock fix kept).
// 8. XeFGPacing.h wires the policy: TsDetour uses StepNs with the cap, NoteFrame uses IntervalNs, PureRenderNs stays.

using namespace XeFGPacingPolicy;

constexpr int64_t Ms = 1000000LL;

static std::string ReadFile(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        std::fprintf(stderr, "cannot open %s\n", path);
        std::exit(1);
    }

    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static std::string Between(const std::string& text, const std::string& from, const std::string& to)
{
    const size_t start = text.find(from);
    assert(start != std::string::npos);
    const size_t end = text.find(to, start + from.size());
    assert(end != std::string::npos);
    return text.substr(start, end - start);
}

// Previous TsDetour step, before the cap floor.
static int64_t OldStepNs(int64_t refNs, int64_t countPlus1)
{
    return std::clamp<int64_t>(refNs / countPlus1, 1000000, 20000000);
}

struct CycleResult
{
    int64_t cycleNs;
    int64_t stepNs;
    int64_t lastGapNs; // base present -> next burst's first generated frame
};

// One base frame on a game whose present blocks the engine thread (Lost Soul Aside, Helldivers 2):
// the engine renders for renderNs, waits count steps inside the burst (the first generated frame presents at once,
// the base frame one step after the last generated one), and the limiter sleeps whatever is left of the cap period.
// The pure render time the pacing engine measures excludes the burst waits but includes the limiter's sleep.
static CycleResult RunModel(int64_t renderNs, int64_t multiplier, double capFps, int cycles)
{
    const int64_t count = multiplier - 1;
    const int64_t capStepNs = CapStepNs(capFps);
    const int64_t capPeriodNs = capStepNs * multiplier;

    int64_t pureNs = renderNs;
    CycleResult r {};

    for (int i = 0; i < cycles; i++)
    {
        const int64_t stepNs = StepNs(pureNs, multiplier, capStepNs);
        const int64_t blockNs = count * stepNs;
        const int64_t busyNs = renderNs + blockNs;
        const int64_t sleepNs = capPeriodNs > busyNs ? capPeriodNs - busyNs : 0;
        const int64_t cycleNs = busyNs + sleepNs;

        pureNs = PureRenderNs(cycleNs, blockNs);
        r = { cycleNs, stepNs, cycleNs - blockNs };
    }

    return r;
}

static bool Near(int64_t a, int64_t b, int64_t tolNs) { return std::llabs(a - b) <= tolNs; }

int main()
{
    std::printf("XeMFG cap-aware pacing step tests\n");

    // Test 1
    {
        assert(CapStepNs(0.0) == 0);
        assert(CapStepNs(-60.0) == 0);
        assert(CapStepNs(std::nan("")) == 0);
        assert(CapStepNs(120.0) == 8333333LL);
        assert(CapStepNs(60.0) == 16666666LL);
        std::printf("  [PASS] Test 1: cap interval is 0 without a valid cap, 8.33 ms at 120 fps.\n");
    }

    // Test 2
    {
        for (int64_t countPlus1 = 3; countPlus1 <= 6; countPlus1++)
        {
            for (int64_t refNs = 0; refNs <= 200 * Ms; refNs += Ms / 4)
            {
                assert(StepNs(refNs, countPlus1, 0) == OldStepNs(refNs, countPlus1));
                assert(IntervalNs(refNs, countPlus1, 0) == refNs / countPlus1);
            }
        }

        std::printf("  [PASS] Test 2: without a cap the step and interval are exactly the previous values.\n");
    }

    // Test 3
    {
        // Capped: ring 19.80 ms, 4X, 120 fps cap. Previously 4.95 ms.
        assert(OldStepNs(19800000LL, 4) == 4950000LL);
        assert(StepNs(19800000LL, 4, CapStepNs(120.0)) == 8333333LL);

        // Uncapped: ring 8.0 ms, 4X.
        assert(StepNs(8000000LL, 4, CapStepNs(0.0)) == 2000000LL);
        std::printf("  [PASS] Test 3: 120 fps cap at 4X steps 8.33 ms (was 4.95); uncapped stays 2.0 ms.\n");
    }

    // Test 4
    {
        // 40 ms of render at 4X is a 10 ms step; a 120 cap (8.33 ms) must not shorten it.
        assert(StepNs(40 * Ms, 4, CapStepNs(120.0)) == 10 * Ms);
        assert(IntervalNs(40 * Ms, 4, CapStepNs(120.0)) == 10 * Ms);
        std::printf("  [PASS] Test 4: a render step longer than the cap interval is kept.\n");
    }

    // Test 5
    {
        // 30 fps cap: 33.3 ms interval, bounded to 20 ms like any other step.
        assert(StepNs(8 * Ms, 4, CapStepNs(30.0)) == MaxStepNs);

        // 2000 fps cap: 0.5 ms interval; the render step (2 ms) wins, and the 1 ms floor still applies.
        assert(StepNs(8 * Ms, 4, CapStepNs(2000.0)) == 2 * Ms);
        assert(StepNs(2 * Ms, 4, CapStepNs(2000.0)) == MinStepNs);
        std::printf("  [PASS] Test 5: step bounds hold for very low and very high caps.\n");
    }

    // Test 6
    {
        // Light scene (8 ms render, as measured uncapped), 4X, 120 cap: one present every 8.33 ms, cycle 33.3 ms.
        const CycleResult light = RunModel(8 * Ms, 4, 120.0, 200);
        assert(light.stepNs == CapStepNs(120.0));
        assert(Near(light.cycleNs, 4 * CapStepNs(120.0), 2));
        assert(Near(light.lastGapNs, light.stepNs, Ms / 2));

        // The same after 1 and after 1000 cycles: no drift, no growth.
        const CycleResult first = RunModel(8 * Ms, 4, 120.0, 1);
        const CycleResult late = RunModel(8 * Ms, 4, 120.0, 1000);
        assert(first.stepNs == late.stepNs && first.cycleNs == late.cycleNs);

        // Heavy scene (18 ms render) over the same cap: the cycle is longer than the cap period but fixed
        // (render + 3 steps = 43 ms), never growing like the provider-median loop (84 ms in Helldivers 2).
        const CycleResult heavy = RunModel(18 * Ms, 4, 120.0, 1000);
        assert(heavy.stepNs == CapStepNs(120.0));
        assert(Near(heavy.cycleNs, 18 * Ms + 3 * CapStepNs(120.0), 2));
        assert(RunModel(18 * Ms, 4, 120.0, 2).cycleNs == heavy.cycleNs);

        // 6X, 28 ms render (Helldivers 2), 155 fps cap: bounded at render + 5 steps.
        const CycleResult hd2 = RunModel(28 * Ms, 6, 155.0, 1000);
        assert(Near(hd2.cycleNs, 28 * Ms + 5 * CapStepNs(155.0), 2));
        std::printf("  [PASS] Test 6: capped cycle converges, stays bounded, and is evenly spaced when render fits.\n");
    }

    // Test 7
    {
        // Helldivers 2 resonant-lock case, no cap: the step is 28 / 6 = 4.67 ms and the cycle stays at
        // render + 5 steps (51.3 ms), not the 84 ms lock.
        const CycleResult hd2 = RunModel(28 * Ms, 6, 0.0, 1000);
        assert(hd2.stepNs == 28 * Ms / 6);
        assert(Near(hd2.cycleNs, 28 * Ms + 5 * (28 * Ms / 6), 2));
        assert(hd2.cycleNs < 84 * Ms);
        std::printf("  [PASS] Test 7: without a cap the pure-render step is unchanged (no 71.4 fps lock).\n");
    }

    // Test 8
    {
        const std::string pacing = ReadFile("OptiScaler/framegen/xefg/XeFGPacing.h");

        const std::string ts = Between(pacing, "inline void* TsDetour(", "\n}\n");
        assert(ts.find("XeFGPacingPolicy::StepNs(refNs, static_cast<int64_t>(countPlus1),") != std::string::npos);
        assert(ts.find("g_capStepNs.load(") != std::string::npos);
        assert(ts.find("std::clamp(refNs") == std::string::npos);

        const std::string noteFrame = Between(pacing, "inline void NoteFrame(", "\n}\n");
        assert(noteFrame.find("XeFGPacingPolicy::PureRenderNs(") != std::string::npos);
        assert(noteFrame.find("XeFGPacingPolicy::IntervalNs(g_periodNs,") != std::string::npos);

        assert(pacing.find("inline void SetOutputCapFps(double fps)") != std::string::npos);
        std::printf("  [PASS] Test 8: XeFGPacing.h uses the cap-aware step in TsDetour and NoteFrame.\n");
    }

    std::printf("All XeMFG cap-aware pacing step tests passed.\n");
    return 0;
}
