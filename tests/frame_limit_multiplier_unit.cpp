#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "../OptiScaler/misc/FrameLimitPolicy.h"

// Fallback FPS limiter multiplier (FG-FPS-LIMIT-XEMFG-PACING-AND-SYNC.md, section 4.3).
//
// FrameLimit::sleep(fgActive) doubled the interval whenever frame generation was active and runs once per real
// frame in FGHooks::FGPresent, so the output was limit x multiplier / 2: right at 2X, 1.5x the limit at 3X, 2x at 4X.
// It now takes the real output multiplier.
//
// 1. OutputMultiplier: 1 without FG, interpolated + 1 with it.
// 2. MinIntervalUs: no limit, invalid limits, 1X, bounds.
// 3. 2X keeps the previous interval; 4X is twice the previous one.
// 4. Presented frame rate equals the limit for every multiplier 1X..6X.
// 5. Production wiring: FrameLimit.cpp uses the policy, FGPresent passes the live multiplier, other call sites 1.

using namespace FrameLimitPolicy;

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

// Previous FrameLimit::sleep interval.
static uint64_t OldIntervalUs(float fps, bool fgActive)
{
    uint64_t us = static_cast<uint64_t>(1'000'000 / fps);
    return fgActive ? us * 2 : us;
}

int main()
{
    std::printf("Fallback FPS limiter multiplier tests\n");

    // Test 1
    {
        static_assert(OutputMultiplier(false, 3) == 1);
        static_assert(OutputMultiplier(true, 1) == 2);
        static_assert(OutputMultiplier(true, 3) == 4);
        static_assert(OutputMultiplier(true, 5) == 6);
        static_assert(OutputMultiplier(true, 0) == 1); // passthrough: no generated frames
        std::printf("  [PASS] Test 1: output multiplier is 1 without FG, interpolated frames + 1 with it.\n");
    }

    // Test 2
    {
        assert(MinIntervalUs(0.0f, 1) == 0);
        assert(MinIntervalUs(-60.0f, 4) == 0);
        assert(MinIntervalUs(std::nanf(""), 4) == 0);
        assert(MinIntervalUs(120.0f, 1) == 8333);
        assert(MinIntervalUs(120.0f, 0) == 8333);
        assert(MinIntervalUs(0.001f, 1) == MaxIntervalUs);
        std::printf("  [PASS] Test 2: no limit for 0 or invalid values; 8333 us at 120 fps without FG.\n");
    }

    // Test 3
    {
        assert(MinIntervalUs(120.0f, 2) == OldIntervalUs(120.0f, true));
        assert(MinIntervalUs(120.0f, 1) == OldIntervalUs(120.0f, false));
        assert(MinIntervalUs(120.0f, 4) == 2 * OldIntervalUs(120.0f, true));
        std::printf("  [PASS] Test 3: 2X and no-FG intervals unchanged; 4X waits twice as long as before.\n");
    }

    // Test 4
    {
        for (float limit : { 60.0f, 117.0f, 120.0f, 144.0f, 165.0f })
        {
            for (uint32_t m = 1; m <= 6; m++)
            {
                const double presentedFps = m * 1e6 / static_cast<double>(MinIntervalUs(limit, m));
                assert(std::fabs(presentedFps - limit) < 0.05);
            }
        }

        // Before: a 120 limit at 4X presented 240 fps.
        assert(std::fabs(4 * 1e6 / OldIntervalUs(120.0f, true) - 240.0) < 0.1);
        std::printf("  [PASS] Test 4: presented frame rate equals the limit at 1X..6X (was 2x the limit at 4X).\n");
    }

    // Test 5
    {
        const std::string limiter = ReadFile("OptiScaler/misc/FrameLimit.cpp");
        assert(limiter.find("FrameLimitPolicy::MinIntervalUs(") != std::string::npos);
        assert(limiter.find("*= 2") == std::string::npos);

        const std::string header = ReadFile("OptiScaler/misc/FrameLimit.h");
        assert(header.find("static void sleep(uint32_t outputMultiplier);") != std::string::npos);

        const std::string fgHooks = ReadFile("OptiScaler/hooks/FG_Hooks.cpp");
        assert(fgHooks.find("FrameLimit::sleep(FrameLimitPolicy::OutputMultiplier(") != std::string::npos);
        assert(fgHooks.find("fg->GetInterpolatedFrameCount()") != std::string::npos);

        for (const char* path : { "OptiScaler/hooks/Vulkan_Hooks.cpp", "OptiScaler/wrapped/wrapped_swapchain.cpp",
                                  "OptiScaler/hooks/FG_Hooks.cpp" })
        {
            const std::string text = ReadFile(path);
            assert(text.find("FrameLimit::sleep(false)") == std::string::npos);
            assert(text.find("FrameLimit::sleep(true)") == std::string::npos);
        }

        // FSR FG never sets a count: the default must stay 1 generated frame (2X).
        const std::string feature = ReadFile("OptiScaler/framegen/IFGFeature.cpp");
        assert(feature.find("return _framesToInterpolate < 0 ? 1 : _framesToInterpolate;") != std::string::npos);
        std::printf("  [PASS] Test 5: FrameLimit uses the policy and FGPresent passes the live multiplier.\n");
    }

    std::printf("All fallback FPS limiter multiplier tests passed.\n");
    return 0;
}
