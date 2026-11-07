#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "../OptiScaler/hooks/FpsLimitSync.h"

// Reflex update ownership on DXVK / vkd3d-proton (FG-FPS-LIMIT-XEMFG-PACING-AND-SYNC.md, section 4.6).
//
// A D3D game's present on DXVK runs ReflexHooks::update twice: from the DXGI present with the real FG state, then
// from hkvkQueuePresentKHR with fgActive = false. With frame generation on and the game's Reflex off the two disagree,
// so reflexLimitsFps flipped every frame and each flip reset and resent the limit. The Vulkan hook now leaves update to
// the DXGI side for D3D swapchains. The Vulkan-side fallback sleep stays: under DXVK it is the only fallback sleep
// (48087c67).
//
// 1. Policy: only a D3D swapchain on DXVK skips the Vulkan-side update.
// 2. Frame model: reflexLimitsFps flipped every frame before, is stable now, for every FG / Reflex combination.
// 3. Production wiring: the Vulkan hook gates update, keeps the fallback sleep, DXGI sites still skip under DXVK, and
//    LocalPresent sets swapchainApi before its own update.

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

// ReflexHooks::update's D3D branch on real nvapi: reflexLimitsFps = !fgActive || bLowLatencyMode.
static bool ReflexLimits(bool fgActive, bool gameReflexOn) { return !fgActive || gameReflexOn; }

// Counts reflexLimitsFps changes over a run of D3D presents on DXVK. Each change resets and resends the limit.
static int Toggles(bool vulkanDrivesUpdate, bool fgActive, bool gameReflexOn, int frames)
{
    bool limits = ReflexLimits(fgActive, gameReflexOn);
    int toggles = 0;

    for (int i = 0; i < frames; i++)
    {
        const bool dxgi = ReflexLimits(fgActive, gameReflexOn); // LocalPresent: update(fg->IsActive(), false)
        toggles += dxgi != limits;
        limits = dxgi;

        if (vulkanDrivesUpdate) // hkvkQueuePresentKHR: update(false, true), D3D branch
        {
            const bool vk = ReflexLimits(false, gameReflexOn);
            toggles += vk != limits;
            limits = vk;
        }
    }

    return toggles;
}

int main()
{
    std::printf("Reflex update ownership on DXVK tests\n");

    // Test 1
    {
        static_assert(!VulkanPresentDrivesReflex(true, true));  // D3D game on DXVK / vkd3d-proton
        static_assert(VulkanPresentDrivesReflex(true, false));  // native Vulkan game under Proton
        static_assert(VulkanPresentDrivesReflex(false, false)); // native Vulkan game on Windows
        static_assert(VulkanPresentDrivesReflex(false, true));  // no DXVK: D3D presents never reach the Vulkan hook
        std::printf("  [PASS] Test 1: only a D3D swapchain on DXVK leaves the update to the DXGI side.\n");
    }

    // Test 2
    {
        constexpr int Frames = 1000;
        const bool d3dOnDxvk = VulkanPresentDrivesReflex(true, true);

        // FG on, game Reflex off: flipped twice per frame before (the first DXGI update matches the start state).
        assert(Toggles(true, true, false, Frames) == 2 * Frames - 1);
        assert(Toggles(d3dOnDxvk, true, false, Frames) == 0);

        // The other combinations never disagreed and stay stable.
        for (bool fg : { false, true })
            for (bool reflex : { false, true })
                assert(Toggles(d3dOnDxvk, fg, reflex, Frames) == 0);
        std::printf("  [PASS] Test 2: reflexLimitsFps no longer flips every frame (was 2 per frame).\n");
    }

    // Test 3
    {
        const std::string vulkan = ReadFile("OptiScaler/hooks/Vulkan_Hooks.cpp");
        const std::string present = Between(vulkan, "VALIDATE_HOOK(hkvkQueuePresentKHR", "\n}\n");

        const size_t gate = present.find(
            "FpsLimitSync::VulkanPresentDrivesReflex(IdentifyGpu::getPrimaryGpu().usesDxvk, scApi == DX11 || scApi == "
            "DX12)");
        assert(gate != std::string::npos);
        assert(gate < present.find("ReflexHooks::update(false, true);"));

        // The fallback sleep is still there, gated only by reflexLimitsFps and the mode (48087c67 kept).
        const std::string sleep = Between(present, "o_QueuePresentKHR(", "LOG_FUNC_RESULT");
        assert(sleep.find("FrameLimit::sleep(1);") != std::string::npos);
        assert(sleep.find("usesDxvk") == std::string::npos);

        // DXGI-side fallback sleeps still skip under DXVK.
        const std::string wrapped = ReadFile("OptiScaler/wrapped/wrapped_swapchain.cpp");
        const std::string present0 = Between(wrapped, "WrappedIDXGISwapChain4::Present(UINT SyncInterval", "\n}\n");
        const std::string present1 = Between(wrapped, "WrappedIDXGISwapChain4::Present1(UINT SyncInterval", "\n}\n");
        assert(present0.find("!IdentifyGpu::getPrimaryGpu().usesDxvk") != std::string::npos);
        assert(present1.find("!IdentifyGpu::getPrimaryGpu().usesDxvk") != std::string::npos);

        // LocalPresent marks the swapchain as D3D before its own update, so the Vulkan hook sees it.
        const std::string local = Between(wrapped, "LocalPresent(", "// Upscaler GPU time computation");
        const size_t dx12 = local.find("State::Instance().swapchainApi = DX12;");
        const size_t dx11 = local.find("State::Instance().swapchainApi = DX11;");
        const size_t update = local.find("ReflexHooks::update(");
        assert(dx12 != std::string::npos && dx11 != std::string::npos && update != std::string::npos);
        assert(dx12 < update && dx11 < update);
        std::printf("  [PASS] Test 3: Vulkan hook gates update and keeps the fallback; DXGI owns D3D games.\n");
    }

    std::printf("All Reflex update ownership on DXVK tests passed.\n");
    return 0;
}
