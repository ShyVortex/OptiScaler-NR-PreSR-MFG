#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "../OptiScaler/hooks/FpsLimitSync.h"

// Generated-frame count flapping (FG-FPS-LIMIT-XEMFG-PACING-AND-SYNC.md, section 4.5).
//
// hkNvAPI_D3D_SetLatencyMarker zeroed ReflexHooks' generated-frame count whenever async (out-of-band) markers looked
// stale, which is always the case for a game that never sends them. The DLSS-G path set the count back on the next
// frame, so it flipped 3 -> 0 -> 3: "DLSS FG no longer detected" about 300 times in 6 minutes in Lost Soul Aside,
// each flip resending the limit (and with fakenvapi as the main nvapi, reloading low latency and forcing Reflex off).
//
// 1. A game that never sent async markers never resets the count.
// 2. RTSS markers never reset it.
// 3. A game whose async markers stop still resets it after 10 frames (the original purpose).
// 4. Frame model: no flips without async markers (the old rule flipped every frame); one reset when they stop.
// 5. Production wiring, and the paths that still set the count to 0 when FG turns off.

using namespace FpsLimitSync;

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

static bool OldRule(bool isRtssMarker, uint64_t lastAsync, uint64_t frameId)
{
    return !isRtssMarker && lastAsync + 10 < frameId;
}

// Per frame: the frame's first latency marker (SIMULATION_START), the DLSS-G path setting the count (SetOptions /
// evaluate), present sampling the count (ReflexHooks::update), then the late markers (PRESENT_END). The DLSS-G path
// runs on the game's thread and the sample on the present path, so on some frames (here every 7th) the count is set
// only after present read it. Returns how many times the sampled count changed.
template <typename Rule>
static int CountFlips(Rule rule, bool sendsAsyncMarkers, uint64_t asyncStopFrame, bool fgOnAfterStop, int frames,
                      uint8_t* finalCount = nullptr)
{
    uint8_t count = 0;
    uint8_t lastSampled = 0;
    uint64_t lastAsync = 0;
    int flips = 0;

    for (uint64_t frame = 100; frame < 100 + static_cast<uint64_t>(frames); frame++)
    {
        const bool fgOn = frame < asyncStopFrame || fgOnAfterStop;
        const bool lateSet = (frame % 7) == 0;

        if (sendsAsyncMarkers && frame < asyncStopFrame)
            lastAsync = frame;

        if (rule(false, lastAsync, frame)) // SIMULATION_START
            count = 0;

        if (fgOn && !lateSet)
            count = 3;

        if (count != lastSampled) // present
        {
            flips++;
            lastSampled = count;
        }

        if (fgOn && lateSet)
            count = 3;

        if (rule(false, lastAsync, frame)) // PRESENT_END
            count = 0;
    }

    if (finalCount != nullptr)
        *finalCount = lastSampled;

    return flips;
}

int main()
{
    std::printf("Generated-frame count flapping tests\n");

    // Test 1
    {
        for (uint64_t frameId = 0; frameId < 100000; frameId += 7)
            assert(!AsyncMarkersStopped(false, 0, frameId));

        assert(OldRule(false, 0, 11)); // the old rule fired from frame 11 on
        std::printf("  [PASS] Test 1: no async marker ever sent: the count is never reset.\n");
    }

    // Test 2
    {
        assert(!AsyncMarkersStopped(true, 5, 1000));
        assert(!AsyncMarkersStopped(true, 0, 1000));
        std::printf("  [PASS] Test 2: RTSS markers never reset the count.\n");
    }

    // Test 3
    {
        assert(!AsyncMarkersStopped(false, 500, 505));
        assert(!AsyncMarkersStopped(false, 500, 510));
        assert(AsyncMarkersStopped(false, 500, 511));
        std::printf("  [PASS] Test 3: async markers that stop still reset the count after 10 frames.\n");
    }

    // Test 4
    {
        // No async markers, FG on for the whole run.
        assert(CountFlips(AsyncMarkersStopped, false, ~0ull, true, 1000) == 1); // 0 -> 3 once
        assert(CountFlips(OldRule, false, ~0ull, true, 1000) > 1);

        // A game that sends async markers and stops when it turns DLSS-G off: one reset, then the count stays 0.
        uint8_t finalCount = 255;
        assert(CountFlips(AsyncMarkersStopped, true, 600, false, 1000, &finalCount) == 2); // 0 -> 3, 3 -> 0
        assert(finalCount == 0);
        std::printf("  [PASS] Test 4: no flips without async markers; one reset when they stop.\n");
    }

    // Test 5
    {
        const std::string reflex = ReadFile("OptiScaler/hooks/Reflex_Hooks.cpp");
        assert(reflex.find("FpsLimitSync::AsyncMarkersStopped(isRtssMarker, _lastAsyncMarkerFrameId, "
                           "pSetLatencyMarkerParams->frameID)") != std::string::npos);
        assert(reflex.find("_lastAsyncMarkerFrameId + 10 < pSetLatencyMarkerParams->frameID") == std::string::npos);

        // FG off still reaches the count through the paths that own it.
        const std::string streamline = ReadFile("OptiScaler/hooks/Streamline_Hooks.cpp");
        assert(streamline.find("ReflexHooks::setDlssgFrameCount(newOptions.mode == sl::DLSSGMode::eOff ? 0") !=
               std::string::npos);
        const std::string dlssg = ReadFile("OptiScaler/framegen/dlssg/DLSSG_Dx12.cpp");
        assert(dlssg.find("ReflexHooks::setDlssgFrameCount(acceptedPacing);") != std::string::npos);
        const std::string ngx = ReadFile("OptiScaler/inputs/NVNGX_DLSS_Dx12.cpp");
        assert(ngx.find("ReflexHooks::setDlssgFrameCount(0);") != std::string::npos);
        std::printf("  [PASS] Test 5: Reflex_Hooks uses the rule; FG-off paths still set the count to 0.\n");
    }

    std::printf("All generated-frame count flapping tests passed.\n");
    return 0;
}
