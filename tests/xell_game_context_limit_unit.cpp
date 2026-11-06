#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "../OptiScaler/hooks/FpsLimitSync.h"

// FPS limit through the game's own XeLL context (FG-FPS-LIMIT-XEMFG-PACING-AND-SYNC.md, section 4.7).
//
// XellHooks set gamesContextCanLimitFps once and never cleared it, not on xellDestroyContext and not when OptiScaler
// started blocking the game's XeLL calls. While set, the fallback limiter stayed off and the menu showed "Game's XeLL".
// The limit was cached globally (lastFpslimit), and hkxellSetSleepMode passed the game's values through, so a game
// writing its own sleep mode wiped the limit for good.
//
// 1. IntervalUs: 0 without a limit, rounded microseconds otherwise.
// 2. GameXellCanLimit: every condition must hold, re-evaluated each frame.
// 3. GameXellNeedsPush: untouched contexts get a push only with a limit; then on every change.
// 4. Session model: limit set, game rewrites its sleep mode, new context, reset to 0 restores the game's interval,
//    OptiScaler's XeFG XeLL takes over (blocked), context destroyed.
// 5. Production wiring in Xell_Hooks.

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

// Mirrors XellHooks::update, hkxellSetSleepMode, hkxellDestroyContext and blockExternalContexts.
struct GameXell
{
    int context = 0; // 0 = none
    bool lowLatency = true;
    uint32_t intervalUs = 0; // what the game's XeLL context holds

    bool blockExternal = false;
    bool canLimit = false;
    int sentTo = 0;
    float sentLimit = 0.0f;
    uint32_t gameIntervalUs = 0;

    void Update(float limit)
    {
        canLimit = GameXellCanLimit(true, blockExternal, context != 0, lowLatency);
        if (!canLimit)
            return;

        const bool same = sentTo == context;
        if (!GameXellNeedsPush(same, sentLimit, limit))
            return;

        if (!same)
            gameIntervalUs = intervalUs;

        intervalUs = IntervalToSend(IntervalUs(limit), gameIntervalUs);
        sentTo = context;
        sentLimit = limit;
    }

    void GameSetSleepMode(uint32_t requestedUs, float limit)
    {
        if (blockExternal)
            return;

        gameIntervalUs = requestedUs;
        const uint32_t opti = IntervalUs(limit);
        intervalUs = opti != 0 ? opti : requestedUs;
    }

    void Destroy()
    {
        context = 0;
        canLimit = false;
        sentTo = 0;
    }

    void Block()
    {
        blockExternal = true;
        canLimit = false;
    }
};

int main()
{
    std::printf("Game XeLL context FPS limit tests\n");

    // Test 1
    {
        static_assert(IntervalUs(0.0f) == 0);
        static_assert(IntervalUs(-10.0f) == 0);
        static_assert(IntervalUs(120.0f) == 8333);
        static_assert(IntervalUs(144.0f) == 6944);
        static_assert(IntervalUs(60.0f) == 16667);
        std::printf("  [PASS] Test 1: interval is 0 without a limit and rounded to the microsecond.\n");
    }

    // Test 2
    {
        static_assert(GameXellCanLimit(true, false, true, true));
        static_assert(!GameXellCanLimit(false, false, true, true));
        static_assert(!GameXellCanLimit(true, true, true, true));
        static_assert(!GameXellCanLimit(true, false, false, true));
        static_assert(!GameXellCanLimit(true, false, true, false));
        std::printf(
            "  [PASS] Test 2: the game's XeLL limits only when hooked, unblocked, present and low latency on.\n");
    }

    // Test 3
    {
        static_assert(!GameXellNeedsPush(false, 0.0f, 0.0f));
        static_assert(GameXellNeedsPush(false, 0.0f, 120.0f));
        static_assert(GameXellNeedsPush(false, 120.0f, 120.0f)); // a new context with the same limit
        static_assert(!GameXellNeedsPush(true, 120.0f, 120.0f));
        static_assert(GameXellNeedsPush(true, 120.0f, 0.0f));
        std::printf("  [PASS] Test 3: untouched contexts are left alone without a limit; changes are pushed.\n");
    }

    // Test 4
    {
        GameXell x;
        x.context = 1;
        x.intervalUs = 16667; // the game's own 60 fps cap

        // No OptiScaler limit: the game's cap is left alone.
        x.Update(0.0f);
        assert(x.canLimit && x.intervalUs == 16667);

        // Limit applied.
        x.Update(120.0f);
        assert(x.intervalUs == 8333);

        // The game rewrites its sleep mode (settings menu): the limit survives.
        x.GameSetSleepMode(16667, 120.0f);
        assert(x.intervalUs == 8333);
        x.Update(120.0f);
        assert(x.intervalUs == 8333);

        // Limit reset: the game's own cap comes back, not 0.
        x.Update(0.0f);
        assert(x.intervalUs == 16667);

        // New context from the game (resize, mode change): gets the limit again.
        x.Update(120.0f);
        x.context = 2;
        x.intervalUs = 0;
        x.Update(120.0f);
        assert(x.intervalUs == 8333);

        // OptiScaler's XeFG XeLL takes over and blocks the game's calls: no longer counted as a limiter.
        x.Block();
        x.Update(120.0f);
        assert(!x.canLimit);

        // Context destroyed: cleared, and a later context starts fresh.
        GameXell d;
        d.context = 3;
        d.Update(120.0f);
        assert(d.canLimit);
        d.Destroy();
        d.Update(120.0f);
        assert(!d.canLimit && d.sentTo == 0);
        std::printf("  [PASS] Test 4: the limit survives game writes and new contexts; the flag clears.\n");
    }

    // Test 5
    {
        const std::string xell = ReadFile("OptiScaler/hooks/Xell_Hooks.cpp");

        const std::string update = Between(xell, "bool XellHooks::update()", "\n}\n");
        assert(update.find("gamesContextCanLimitFps = FpsLimitSync::GameXellCanLimit(") != std::string::npos);
        assert(update.find("FpsLimitSync::GameXellNeedsPush(sameContext, gamesContextSentLimit, limit)") !=
               std::string::npos);
        assert(update.find("static float lastFpslimit") == std::string::npos);
        assert(update.find("gamesContextCanLimitFps = true;") == std::string::npos);

        const std::string destroy = Between(xell, "XellHooks::hkxellDestroyContext(", "\n}\n");
        assert(destroy.find("gamesContextCanLimitFps = false;") != std::string::npos);
        assert(destroy.find("gamesContextSentTo = nullptr;") != std::string::npos);

        const std::string block = Between(xell, "void XellHooks::blockExternalContexts(", "\n}\n");
        assert(block.find("gamesContextCanLimitFps = false;") != std::string::npos);

        const std::string setSleep = Between(xell, "XellHooks::hkxellSetSleepMode(", "\n}\n");
        assert(setSleep.find("if (shouldBlock(context))") < setSleep.find("gamesRequestedIntervalUs ="));
        assert(setSleep.find("params.minimumIntervalUs = optiIntervalUs;") != std::string::npos);
        std::printf("  [PASS] Test 5: Xell_Hooks re-evaluates, clears and injects through the policy.\n");
    }

    std::printf("All game XeLL context FPS limit tests passed.\n");
    return 0;
}
