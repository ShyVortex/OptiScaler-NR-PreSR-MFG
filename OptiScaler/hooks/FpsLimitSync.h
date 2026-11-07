#pragma once

// Pure decisions behind ReflexHooks' FPS limit delivery, kept free of Windows headers so unit tests can include them.
// See docs/issues/FG/FG-FPS-LIMIT-XEMFG-PACING-AND-SYNC.md, section 4.2.

#include <cstdint>

namespace FpsLimitSync
{
// Where the game's D3D Reflex calls go: the driver, or fakenvapi (whose low-latency tech is XeLL, forced by XeFG)
// while XeFG is active.
enum class Backend : uint8_t
{
    None, // nothing received the limit yet (no Reflex device seen)
    Driver,
    Fakenvapi,
};

// The backend and low-latency context generation that last received the limit.
struct Pushed
{
    Backend backend = Backend::None;
    uint32_t generation = 0;
};

constexpr Backend Route(bool xefgActive) { return xefgActive ? Backend::Fakenvapi : Backend::Driver; }

// A limit sent to one backend does not reach the other, and a new XeLL context starts without it. Resend when the
// route moved or the context changed since the last delivery. Without a Reflex device nothing can be sent.
constexpr bool NeedsResend(const Pushed& last, Backend route, uint32_t generation, bool hasDevice)
{
    if (!hasDevice)
        return false;

    return last.backend != route || last.generation != generation;
}

// minimumIntervalUs to send: OptiScaler's limit when one is set, otherwise the game's own Reflex interval, so turning
// OptiScaler's limit off (or resending it as 0) never wipes a cap the game set itself.
constexpr uint32_t IntervalToSend(uint32_t optiIntervalUs, uint32_t gameIntervalUs)
{
    return optiIntervalUs != 0 ? optiIntervalUs : gameIntervalUs;
}

// The driver keeps enforcing a Reflex limit it received while XeFG was inactive (an alt-tab turns DLSS-G off, so the
// route becomes the driver), even after the game's calls moved back to fakenvapi. Two limiters ran at once (112 fps at
// a 120 cap), and a reset sent only to fakenvapi left the driver's 120 in place (pre3 log,
// FG-FPS-LIMIT-XEMFG-PACING-AND-SYNC.md, section 4.2). While fakenvapi carries the limit, the driver must not hold one
// of OptiScaler's. Nothing is sent to fakenvapi while XeFG is inactive: XeLL does not sleep then, and calling it could
// create a low-latency context in a game that never uses XeFG.
constexpr bool ClearDriverLimit(Backend route, uint32_t driverOptiIntervalUs)
{
    return route == Backend::Fakenvapi && driverOptiIntervalUs != 0;
}

// Output cap the XeFG pacing engine spaces generated frames by. The limit counts presented frames, generated ones
// included. External FG mode never runs OptiScaler's XeFG and never applies the limit.
constexpr double OutputCapFps(float limitFps, bool externalFrameGeneration)
{
    if (externalFrameGeneration || !(limitFps > 0.0f))
        return 0.0;

    return static_cast<double>(limitFps);
}

// Whether a latency marker means the game turned DLSS-G off: some games stop sending async (out-of-band) markers when
// it is disabled. A game that never sent one has nothing that stopped, but the old check fired on every marker there.
// The generated-frame count then flips 3 -> 0 -> 3 (seen about twice a second in Lost Soul Aside), resending the limit
// each time and, with fakenvapi as the main nvapi, reloading low latency and forcing Reflex off
// (FG-FPS-LIMIT-XEMFG-PACING-AND-SYNC.md, section 4.5).
// Streamline SetOptions, NGX evaluate and DLSSG_Dx12 still set the count, 0 included, whenever FG turns off.
constexpr bool AsyncMarkersStopped(bool isRtssMarker, uint64_t lastAsyncMarkerFrameId, uint64_t markerFrameId)
{
    if (isRtssMarker || lastAsyncMarkerFrameId == 0)
        return false;

    return lastAsyncMarkerFrameId + 10 < markerFrameId;
}

// Whether the Vulkan present hook should drive ReflexHooks::update. On DXVK / vkd3d-proton a D3D game's present also
// reaches vkQueuePresentKHR, after the DXGI side already ran update with the real FG state. A second update from the
// Vulkan side with fgActive = false disagreed with it and could flip reflexLimitsFps every frame, resetting and
// resending the limit (FG-FPS-LIMIT-XEMFG-PACING-AND-SYNC.md, section 4.6). Native Vulkan games (no DXGI swapchain)
// keep it. The Vulkan-side fallback sleep is not affected: it is the fallback owner on DXVK (48087c67).
constexpr bool VulkanPresentDrivesReflex(bool usesDxvk, bool d3dSwapchain) { return !(usesDxvk && d3dSwapchain); }

// Reflex / XeLL minimumIntervalUs for an FPS limit; 0 = no limit.
constexpr uint32_t IntervalUs(float limitFps)
{
    if (!(limitFps > 0.0f))
        return 0;

    return static_cast<uint32_t>(1000000.0 / static_cast<double>(limitFps) + 0.5);
}

// The game's own XeLL context (XellHooks) can carry the limit only while OptiScaler is not blocking it (no XeFG XeLL of
// ours) and the game has low latency on. Re-evaluated every frame: it used to be set once and never cleared
// (FG-FPS-LIMIT-XEMFG-PACING-AND-SYNC.md, section 4.7).
constexpr bool GameXellCanLimit(bool hooksReady, bool blockExternal, bool hasGameContext, bool gameLowLatencyOn)
{
    return hooksReady && !blockExternal && hasGameContext && gameLowLatencyOn;
}

// Whether XellHooks::update must write the limit into the game's XeLL context. A context we never wrote gets it only
// when there is a limit, so the game's own interval is left alone otherwise; afterwards, whenever the limit changes.
constexpr bool GameXellNeedsPush(bool sameContext, float lastSentFps, float limitFps)
{
    if (!sameContext)
        return limitFps > 0.0f;

    return lastSentFps != limitFps;
}
} // namespace FpsLimitSync
