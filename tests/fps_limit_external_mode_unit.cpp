#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "../OptiScaler/misc/FrameLimitPolicy.h"

// FPS limit in External FG mode (FG-FPS-LIMIT-XEMFG-PACING-AND-SYNC.md, section 4.4).
//
// In External FG mode (dlssg_sm86) Reflex limiting is off (a60f3c93). Present1 skipped the fallback sleep, Present
// did not, and the Vulkan present hook slept in every mode: games got either no limit or a sleep on the path the
// game's DLSS-G presents generated frames through. The menu still said "Fallback". OptiScaler now applies no limit in
// External FG mode and says so.
//
// 1. AppliesInMode: no limit in External FG mode.
// 2. The menu text names the mode and points to RTSS / the game's limit.
// 3. Every fallback sleep site checks the mode: Present, Present1, the Vulkan present hook.
// 4. Reflex stays off in External FG mode (a60f3c93 kept), and the XeFG pacing cap is 0 there.
// 5. Menu: method label, warning, disabled controls; the VRR calculator still computes a value.

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

int main()
{
    std::printf("FPS limit External FG mode tests\n");

    // Test 1
    {
        static_assert(FrameLimitPolicy::AppliesInMode(false));
        static_assert(!FrameLimitPolicy::AppliesInMode(true));
        std::printf("  [PASS] Test 1: the limit applies outside External FG mode only.\n");
    }

    // Test 2
    {
        const std::string message = FrameLimitPolicy::ExternalModeMessage;
        assert(message.find("External FG mode") != std::string::npos);
        assert(message.find("RTSS") != std::string::npos);
        assert(message.find("game's own frame limit") != std::string::npos);
        assert(std::string(FrameLimitPolicy::ExternalModeMethod).find("External FG mode") != std::string::npos);
        std::printf("  [PASS] Test 2: the menu text names the mode and the alternatives.\n");
    }

    // Test 3
    {
        const std::string wrapped = ReadFile("OptiScaler/wrapped/wrapped_swapchain.cpp");
        const std::string present = Between(wrapped, "WrappedIDXGISwapChain4::Present(UINT SyncInterval", "\n}\n");
        const std::string present1 = Between(wrapped, "WrappedIDXGISwapChain4::Present1(UINT SyncInterval", "\n}\n");

        for (const std::string* body : { &present, &present1 })
        {
            assert(Count(*body, "FrameLimit::sleep(1);") == 1);
            assert(body->find("FrameLimitPolicy::AppliesInMode(State::Instance().externalFrameGeneration)") !=
                   std::string::npos);
        }

        assert(Count(wrapped, "FrameLimit::sleep(") == Count(wrapped, "FrameLimitPolicy::AppliesInMode("));

        const std::string vulkan = ReadFile("OptiScaler/hooks/Vulkan_Hooks.cpp");
        const std::string queuePresent = Between(vulkan, "VALIDATE_HOOK(hkvkQueuePresentKHR", "\n}\n");
        assert(Count(queuePresent, "FrameLimit::sleep(1);") == 1);
        assert(queuePresent.find("FrameLimitPolicy::AppliesInMode(State::Instance().externalFrameGeneration)") !=
               std::string::npos);
        std::printf(
            "  [PASS] Test 3: Present, Present1 and the Vulkan present hook skip the sleep in External mode.\n");
    }

    // Test 4
    {
        const std::string reflex = ReadFile("OptiScaler/hooks/Reflex_Hooks.cpp");
        const std::string update = Between(reflex, "void ReflexHooks::update(", "\n}\n");
        const size_t externalReturn = update.find("if (State::Instance().externalFrameGeneration)");
        assert(externalReturn != std::string::npos);
        assert(externalReturn < update.find("_updatesWithoutMarker++;"));

        const std::string setLimit = Between(reflex, "void ReflexHooks::setFPSLimit(", "\n}\n");
        assert(setLimit.find("if (State::Instance().externalFrameGeneration)\n        return;") != std::string::npos);

        const std::string xefg = ReadFile("OptiScaler/framegen/xefg/XeFG_Dx12.cpp");
        assert(xefg.find("State::Instance().externalFrameGeneration));") != std::string::npos);
        std::printf("  [PASS] Test 4: Reflex limiting stays off in External mode; the XeFG cap follows the mode.\n");
    }

    // Test 5
    {
        const std::string menu = ReadFile("OptiScaler/menu/menu_common.cpp");
        const std::string framerate = Between(menu, "void MenuCommon::RenderFramerateSettings(", "\n}\n");

        assert(framerate.find("FrameLimitPolicy::AppliesInMode(state.externalFrameGeneration)") != std::string::npos);
        assert(framerate.find("currentMethod = FrameLimitPolicy::ExternalModeMethod;") != std::string::npos);
        assert(framerate.find("FrameLimitPolicy::ExternalModeMessage") != std::string::npos);
        assert(Count(framerate, "ImGui::BeginDisabled(!limitAvailable);") == 2);
        assert(Count(framerate, "ImGui::EndDisabled();") == 2);

        // The method label is decided before the Reflex / XeLL / Fallback labels.
        assert(framerate.find("if (!limitAvailable)") < framerate.find("else if (state.reflexLimitsFps)"));

        // The slider and both buttons are inside the first disabled block, the calculator's value outside.
        const std::string controls =
            Between(framerate, "ImGui::BeginDisabled(!limitAvailable);", "ImGui::EndDisabled();");
        assert(controls.find("ImGui::SliderFloat(\"FPS Limit\"") != std::string::npos);
        assert(controls.find("ImGui::Button(\"Apply Limit\")") != std::string::npos);
        assert(controls.find("ImGui::Button(\"Reset Limit\")") != std::string::npos);
        assert(controls.find("Calculated Cap") == std::string::npos);

        const std::string calculator = Between(framerate, "VRR Frame Cap Calculator", "ImGui::EndDisabled();");
        assert(calculator.find("ImGui::Text(\"Calculated Cap: %.1f\"") < calculator.find("ImGui::BeginDisabled("));
        assert(calculator.find("ImGui::Button(\"Set as FPS Limit\")") > calculator.find("ImGui::BeginDisabled("));
        std::printf("  [PASS] Test 5: the menu labels External mode, warns, and disables applying a limit.\n");
    }

    std::printf("All FPS limit External FG mode tests passed.\n");
    return 0;
}
