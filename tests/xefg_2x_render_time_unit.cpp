#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../OptiScaler/framegen/xefg/XeFGPacingPolicy.h"

// XeMFG 2X render-time measurement (ShyVortex/DLSS-Unlocked#64).
//
// At 2X the only generated frame is the last one of the burst. When the provider's scheduler is usable, TryPace
// returned before NoteFrame for that frame (the double-wait fix of 9e593639), so the render time was never sampled
// at 2X: XeFG was fed the swapchain delta including its own wait. Switching to 3X and back left a frozen value.
//
// Uses the production policy header and checks how XeFGPacing.h wires it:
// 1. 2X, scheduler usable, last frame: measured without pacing.
// 2. 3X..6X: the last frame is never measure-only.
// 3. Scheduler not usable: not measure-only (PaceFrame keeps handling the frame, and measures it itself).
// 4. Render time excludes the recorded wait; the 2 ms floor applies when the wait covers the delta.
// 5. Deltas under 1 ms or over 500 ms are not sampled.
// 6. With the scheduler usable, every multiplier samples exactly once per burst (never 0, never twice).
// 7. A 3X -> 2X switch keeps tracking the render time instead of freezing it.
// 8. XeFGPacing.h wires the policy: measure-only call in the scheduler branch, PureRenderNs in NoteFrame,
//    and no fall-through of the last frame to PaceFrame while the scheduler is usable.

using namespace XeFGPacingPolicy;

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

// Mirrors which generated frames reach NoteFrame inside TryPace when the scheduler is usable:
// intermediate frames go through ScheduleFrame (NoteFrame samples index <= 1 only), the last frame
// is measure-only when the policy says so.
static int SamplesPerBurst(uint64_t count)
{
    int samples = 0;

    for (uint64_t index = 1; index <= count; index++)
    {
        const bool isLast = index == count;

        if (!isLast)
        {
            if (index <= 1)
                samples++;
        }
        else if (MeasureOnlyLastFrame(true, isLast, index))
        {
            samples++;
        }
    }

    return samples;
}

static int64_t Median(std::vector<int64_t> values)
{
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

int main()
{
    std::printf("XeMFG 2X render-time measurement tests\n");

    // Test 1
    {
        assert(MeasureOnlyLastFrame(true, true, 1));
        std::printf("  [PASS] Test 1: 2X last frame is measured when the provider's scheduler handles it.\n");
    }

    // Test 2
    {
        for (uint64_t count = 2; count <= 5; count++)
            assert(!MeasureOnlyLastFrame(true, true, count));

        std::printf("  [PASS] Test 2: 3X..6X last frames are never measure-only.\n");
    }

    // Test 3
    {
        assert(!MeasureOnlyLastFrame(false, true, 1));
        assert(!MeasureOnlyLastFrame(true, false, 1));
        std::printf("  [PASS] Test 3: no measure-only without the scheduler or for intermediate frames.\n");
    }

    // Test 4
    {
        // 10 ms of real work, 4 ms inside the provider's scheduler, 1 ms base-frame spacing.
        assert(PureRenderNs(15000000LL, 5000000LL) == 10000000LL);

        // The wait covers the whole delta: floor instead of 0 or a negative value.
        assert(PureRenderNs(15000000LL, 14000000LL) == MinPureRenderNs);
        assert(PureRenderNs(15000000LL, 20000000LL) == MinPureRenderNs);

        // No wait recorded: the full delta is the render time.
        assert(PureRenderNs(16000000LL, 0) == 16000000LL);
        std::printf("  [PASS] Test 4: render time excludes the recorded wait, with a 2 ms floor.\n");
    }

    // Test 5
    {
        assert(PureRenderNs(MinBurstDeltaNs - 1, 0) == 0);
        assert(PureRenderNs(MaxBurstDeltaNs + 1, 0) == 0);
        assert(PureRenderNs(MinBurstDeltaNs, 0) == MinPureRenderNs);
        assert(PureRenderNs(MaxBurstDeltaNs, 0) == MaxBurstDeltaNs);
        std::printf("  [PASS] Test 5: deltas outside [1 ms, 500 ms] are not sampled.\n");
    }

    // Test 6
    {
        for (uint64_t count = 1; count <= 5; count++)
            assert(SamplesPerBurst(count) == 1);

        std::printf("  [PASS] Test 6: every multiplier (2X..6X) samples exactly once per burst.\n");
    }

    // Test 7
    {
        std::vector<int64_t> window;
        auto push = [&window](int64_t ns)
        {
            if (ns <= 0)
                return;

            window.push_back(ns);
            if (window.size() > 15)
                window.erase(window.begin());
        };

        // 3X: light scene, 4 ms of work.
        for (int i = 0; i < 20; i++)
            for (int s = 0; s < SamplesPerBurst(2); s++)
                push(PureRenderNs(4000000LL + 3000000LL, 3000000LL));

        assert(Median(window) == 4000000LL);

        // Back to 2X, heavier scene: 12 ms of work, 3 ms of waiting.
        for (int i = 0; i < 20; i++)
            for (int s = 0; s < SamplesPerBurst(1); s++)
                push(PureRenderNs(12000000LL + 3000000LL, 3000000LL));

        assert(Median(window) == 12000000LL);
        std::printf("  [PASS] Test 7: a 3X -> 2X switch keeps tracking the render time (not frozen).\n");
    }

    // Test 8
    {
        const std::string pacing = ReadFile("OptiScaler/framegen/xefg/XeFGPacing.h");
        assert(pacing.find("#include \"XeFGPacingPolicy.h\"") != std::string::npos);

        const std::string noteFrame = Between(pacing, "inline void NoteFrame(", "\n}\n");
        assert(noteFrame.find("XeFGPacingPolicy::PureRenderNs(") != std::string::npos);

        const std::string tryPace = Between(pacing, "inline void TryPace(", "\n}\n");
        const std::string schedBranch =
            Between(tryPace, "if (g_schedNative != nullptr && SchedulerUsable(ctx))", "        return;\n    }");

        assert(schedBranch.find("ScheduleFrame(ctx, burst, index);") != std::string::npos);
        assert(schedBranch.find("XeFGPacingPolicy::MeasureOnlyLastFrame(true, isLast, index)") != std::string::npos);
        assert(schedBranch.find("NoteFrame(index, count,") != std::string::npos);

        // The scheduler branch must still return for the last frame: no PaceFrame / WaitUntil (double wait).
        assert(schedBranch.find("PaceFrame(") == std::string::npos);
        assert(schedBranch.find("WaitUntil(") == std::string::npos);
        assert(tryPace.find("&& !isLast)") == std::string::npos);
        std::printf("  [PASS] Test 8: XeFGPacing.h measures the 2X last frame without pacing it.\n");
    }

    std::printf("All 8 tests passed.\n");
    return 0;
}
