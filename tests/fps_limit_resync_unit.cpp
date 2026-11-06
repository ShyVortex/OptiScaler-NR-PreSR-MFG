#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "../OptiScaler/hooks/FpsLimitSync.h"

// FPS limit delivery across Reflex backends (FG-FPS-LIMIT-XEMFG-PACING-AND-SYNC.md, section 4.2).
//
// ReflexHooks sent the limit once, to the driver or to fakenvapi/XeLL depending on whether XeFG was active at that
// moment, and only resent it when the value changed. XeFG's context creation reset XeLL to no limit. The limit
// applied for a while and then disappeared, or never reached the backend the game's Reflex calls went to.
//
// 1. Route: XeFG active -> fakenvapi, otherwise the driver.
// 2. NeedsResend: no device, first delivery, same route, route change, context change.
// 3. Session model: cap set before XeFG starts, XeFG takes over, context recreated, paused, resumed. Every backend
//    the game's calls reach holds the limit; no resend while nothing changes.
// 4. IntervalToSend: OptiScaler's limit wins, otherwise the game's own interval is kept.
// 5. OutputCapFps: the limit, 0 without one or in External FG mode.
// 6. Production wiring in Reflex_Hooks, XeFG_Dx12 and XeLL.
// 7. pre3 in-game log (Lost Soul Aside 4X, cap 120): an alt-tab turns DLSS-G off, so the driver got the limit too, and
//    the driver kept enforcing it after XeFG resumed. Two limiters gave 112 fps; a reset sent only to fakenvapi left
//    the driver's 120. While fakenvapi carries the limit, the driver holds none of OptiScaler's.

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

static std::string Between(const std::string& text, const std::string& from, const std::string& to)
{
    const size_t start = text.find(from);
    assert(start != std::string::npos);
    const size_t end = text.find(to, start + from.size());
    assert(end != std::string::npos);
    return text.substr(start, end - start);
}

static size_t Count(const std::string& text, const std::string& what)
{
    size_t n = 0;
    for (size_t pos = text.find(what); pos != std::string::npos; pos = text.find(what, pos + what.size()))
        n++;
    return n;
}

// Mirrors ReflexHooks::update + setFPSLimit for one present.
struct Session
{
    Pushed last {};
    uint32_t generation = 0;
    float lastFps = 0.0f;
    uint32_t driverIntervalUs = 0;
    uint32_t xellIntervalUs = 0;
    uint32_t driverOptiIntervalUs = 0; // ReflexHooks::_driverOptiIntervalUs
    uint32_t gameIntervalUs = 0;       // the game's own Reflex interval
    int sends = 0;

    void Present(float limitFps, bool xefgActive, bool hasDevice = true)
    {
        const Backend route = Route(xefgActive);
        const bool resend = NeedsResend(last, route, generation, hasDevice);

        if (limitFps != lastFps || resend)
        {
            const uint32_t interval = limitFps > 0.0f ? static_cast<uint32_t>(1000000.0f / limitFps + 0.5f) : 0;

            if (hasDevice)
            {
                const uint32_t toSend = IntervalToSend(interval, gameIntervalUs);

                if (route == Backend::Fakenvapi)
                {
                    xellIntervalUs = toSend;

                    if (ClearDriverLimit(route, driverOptiIntervalUs))
                    {
                        driverIntervalUs = gameIntervalUs;
                        driverOptiIntervalUs = 0;
                    }
                }
                else
                {
                    driverIntervalUs = toSend;
                    driverOptiIntervalUs = interval;
                }

                last = { route, generation };
                sends++;
            }
            else
            {
                last = {};
            }

            lastFps = limitFps;
        }
    }

    // Limiters enforcing an OptiScaler interval while XeFG runs. The driver enforces its limit even then.
    int ActiveLimitersWhileXeFG() const { return (xellIntervalUs != 0) + (driverIntervalUs != 0); }

    // XeFG_Dx12::CreateSwapchainContext: new XeLL context, initialised with the current limit, generation bumped.
    void NewXeLLContext(float limitFps)
    {
        xellIntervalUs = limitFps > 0.0f ? static_cast<uint32_t>(1000000.0f / limitFps + 0.5f) : 0;
        generation++;
    }
};

int main()
{
    std::printf("FPS limit re-sync tests\n");

    // Test 1
    {
        static_assert(Route(true) == Backend::Fakenvapi);
        static_assert(Route(false) == Backend::Driver);
        std::printf("  [PASS] Test 1: XeFG active routes to fakenvapi, otherwise to the driver.\n");
    }

    // Test 2
    {
        assert(!NeedsResend({}, Backend::Driver, 0, false));
        assert(NeedsResend({}, Backend::Driver, 0, true));
        assert(!NeedsResend({ Backend::Driver, 3 }, Backend::Driver, 3, true));
        assert(NeedsResend({ Backend::Driver, 3 }, Backend::Fakenvapi, 3, true));
        assert(NeedsResend({ Backend::Fakenvapi, 3 }, Backend::Fakenvapi, 4, true));
        std::printf("  [PASS] Test 2: resend only on a new route or context, never without a device.\n");
    }

    // Test 3
    {
        constexpr uint32_t Interval120 = 8333;
        Session s;

        // Cap applied in the main menu, XeFG not active yet: the driver gets it.
        s.Present(120.0f, false);
        assert(s.driverIntervalUs == Interval120 && s.sends == 1);

        // XeFG starts: new XeLL context (already capped by its initialisation), then the game's calls move to it.
        s.NewXeLLContext(120.0f);
        s.Present(120.0f, true);
        assert(s.xellIntervalUs == Interval120 && s.sends == 2);

        // Steady state: no resend over many presents.
        for (int i = 0; i < 1000; i++)
            s.Present(120.0f, true);
        assert(s.sends == 2);

        // Alt-tab / resize: XeFG recreates its context. Previously this left XeLL at 0 for good.
        s.xellIntervalUs = 0; // what the old initialisation wrote
        s.generation++;
        s.Present(120.0f, true);
        assert(s.xellIntervalUs == Interval120 && s.sends == 3);

        // Pause (menu, loading): the game's calls go back to the driver, which gets the limit.
        s.Present(120.0f, false);
        assert(s.driverIntervalUs == Interval120 && s.sends == 4);

        // Resume: XeLL carries it again and the driver's copy is cleared (one limiter).
        s.Present(120.0f, true);
        assert(s.xellIntervalUs == Interval120 && s.sends == 5);
        assert(s.driverIntervalUs == 0 && s.ActiveLimitersWhileXeFG() == 1);

        // Limit reset to 0 while active: sent once.
        s.Present(0.0f, true);
        assert(s.xellIntervalUs == 0 && s.sends == 6);
        s.Present(0.0f, true);
        assert(s.sends == 6);

        // No Reflex device: nothing sent, nothing remembered, delivered once a device appears.
        Session n;
        n.Present(120.0f, true, false);
        n.Present(120.0f, true, false);
        assert(n.sends == 0);
        n.Present(120.0f, true, true);
        assert(n.sends == 1 && n.xellIntervalUs == Interval120);
        std::printf("  [PASS] Test 3: the limit follows the game's calls across XeFG start, recreation and pause.\n");
    }

    // Test 4
    {
        static_assert(IntervalToSend(8333, 0) == 8333);
        static_assert(IntervalToSend(8333, 16667) == 8333);
        static_assert(IntervalToSend(0, 16667) == 16667);
        static_assert(IntervalToSend(0, 0) == 0);
        std::printf("  [PASS] Test 4: OptiScaler's limit wins; without one the game's own interval is kept.\n");
    }

    // Test 5
    {
        assert(OutputCapFps(120.0f, false) == 120.0);
        assert(OutputCapFps(0.0f, false) == 0.0);
        assert(OutputCapFps(-5.0f, false) == 0.0);
        assert(OutputCapFps(120.0f, true) == 0.0);
        std::printf("  [PASS] Test 5: pacing cap is the limit, 0 without one or in External FG mode.\n");
    }

    // Test 6
    {
        const std::string reflex = ReadFile("OptiScaler/hooks/Reflex_Hooks.cpp");
        const std::string update = Between(reflex, "void ReflexHooks::update(", "\n}\n");
        assert(update.find("FpsLimitSync::NeedsResend(_lastPushed, FpsLimitSync::Route(IsXeFGActive())") !=
               std::string::npos);
        assert(update.find("if (currentFps != lastFps || resend)") != std::string::npos);

        const std::string setLimit = Between(reflex, "void ReflexHooks::setFPSLimit(", "\n}\n");
        assert(setLimit.find("FpsLimitSync::IntervalToSend(_minimumIntervalUs, _lastSleepParams.minimumIntervalUs)") !=
               std::string::npos);
        assert(setLimit.find("_lastPushed = { route, _lowLatencyGeneration.load(") != std::string::npos);
        assert(setLimit.find("_lastPushed = {};") != std::string::npos);
        assert(reflex.find("void ReflexHooks::lowLatencyContextChanged()") != std::string::npos);

        const std::string xefg = ReadFile("OptiScaler/framegen/xefg/XeFG_Dx12.cpp");
        const std::string create = Between(xefg, "bool XeFG_Dx12::CreateSwapchainContext(", "\n}\n");
        assert(create.find("sleepParams.minimumIntervalUs = 0;") == std::string::npos);
        assert(Count(create, "sleepParams.minimumIntervalUs = ReflexHooks::minimumIntervalUs();") == 2);
        assert(Count(create, "ReflexHooks::lowLatencyContextChanged();") == 2);

        const std::string present = Between(xefg, "bool XeFG_Dx12::Present()", "\n}\n");
        const size_t cap = present.find("XeFGPacing::SetOutputCapFps(FpsLimitSync::OutputCapFps(");
        assert(cap != std::string::npos && cap < present.find("if (_passthrough)"));

        const std::string xellCpp = ReadFile("OptiScaler/low_latency/low_latency_tech/ll_xell.cpp");
        assert(xellCpp.find("static uint32_t last_") == std::string::npos);
        const std::string xellH = ReadFile("OptiScaler/low_latency/low_latency_tech/ll_xell.h");
        assert(xellH.find("uint32_t last_minimumIntervalUs = ~0u;") != std::string::npos);
        std::printf("  [PASS] Test 6: Reflex_Hooks, XeFG_Dx12 and XeLL use the re-sync policy.\n");
    }

    // Test 7
    {
        constexpr uint32_t Interval120 = 8333;
        Session s;
        s.NewXeLLContext(0.0f);

        // 18:49:56: 120 applied with XeFG active, XeLL only: 120 out.
        s.Present(120.0f, true);
        assert(s.xellIntervalUs == Interval120 && s.driverIntervalUs == 0);

        // Three alt-tabs: eOff (XeFG inactive) then eOn. After each one, exactly one limiter while XeFG runs
        // (before: the driver kept 120 next to XeLL's 120, 112 out).
        for (int altTab = 0; altTab < 3; altTab++)
        {
            s.Present(120.0f, false);
            assert(s.driverIntervalUs == Interval120);
            s.Present(120.0f, true);
            assert(s.ActiveLimitersWhileXeFG() == 1 && s.xellIntervalUs == Interval120);
        }

        // 18:51:40: limit reset with XeFG active. Before, the driver's 120 stayed until the next alt-tab.
        s.Present(0.0f, true);
        assert(s.ActiveLimitersWhileXeFG() == 0);

        // The game's own Reflex cap is what the driver gets back, never 0 over it.
        Session g;
        g.gameIntervalUs = 16667;
        g.Present(120.0f, false);
        g.Present(120.0f, true);
        assert(g.driverIntervalUs == 16667 && g.driverOptiIntervalUs == 0);

        // Without XeFG (never active), the driver keeps the limit: it is the only backend.
        Session d;
        for (int i = 0; i < 100; i++)
            d.Present(120.0f, false);
        assert(d.driverIntervalUs == Interval120 && d.xellIntervalUs == 0);

        const std::string reflex = ReadFile("OptiScaler/hooks/Reflex_Hooks.cpp");
        const std::string setLimit = Between(reflex, "void ReflexHooks::setFPSLimit(", "\n}\n");
        const size_t fake = setLimit.find("nvapi_calls::NvAPI_D3D_SetSleepMode(_lastSleepDev, &temp);");
        const size_t clear = setLimit.find("FpsLimitSync::ClearDriverLimit(route, _driverOptiIntervalUs)");
        assert(fake != std::string::npos && clear != std::string::npos && fake < clear);
        assert(setLimit.find("driverParams.minimumIntervalUs = _lastSleepParams.minimumIntervalUs;") !=
               std::string::npos);
        assert(setLimit.find("_driverOptiIntervalUs = _minimumIntervalUs;") != std::string::npos);

        const std::string hook = Between(reflex, "ReflexHooks::hkNvAPI_D3D_SetSleepMode(", "\n}\n");
        assert(hook.find("_driverOptiIntervalUs = _minimumIntervalUs;") > hook.find("if (IsXeFGActive())"));
        std::printf("  [PASS] Test 7: one limiter after alt-tabs; a reset clears both; the game's cap is kept.\n");
    }

    std::printf("All FPS limit re-sync tests passed.\n");
    return 0;
}
