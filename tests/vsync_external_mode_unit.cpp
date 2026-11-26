#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "../OptiScaler/misc/VsyncOverridePolicy.h"

// V-Sync override in External FG mode (docs/issues/FG/FG-VSYNC-EXTERNAL-FG-MODE-MENU.md,
// ShyVortex/DLSS-Unlocked#75).
//
// In External FG mode (dlssg_sm86) the game's DLSS-G presents and paces the generated frames, so OptiScaler leaves
// their sync interval and tearing flag alone (e5b129ad). The menu still offered "V-Sync On" / "Sync Int." there, and a
// reporter set them, saw tearing continue and opened an issue.
//
// 1. AppliesInMode: no override in External FG mode.
// 2. The menu text names the mode and the alternatives.
// 3. Both present sites (LocalPresent, FGHooks::FGPresent) decide the override through the policy.
//
// Build (repository root):
//   g++ -std=c++20 -Wall -Wextra -o /tmp/vsync_external_mode_unit tests/vsync_external_mode_unit.cpp
//   /tmp/vsync_external_mode_unit

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
    std::printf("V-Sync override External FG mode tests\n");

    // Test 1
    {
        static_assert(VsyncOverridePolicy::AppliesInMode(false));
        static_assert(!VsyncOverridePolicy::AppliesInMode(true));
        std::printf("  [PASS] Test 1: the override applies outside External FG mode only.\n");
    }

    // Test 2
    {
        const std::string message = VsyncOverridePolicy::ExternalModeMessage;
        assert(message.find("External FG mode") != std::string::npos);
        assert(message.find("NVIDIA Control Panel") != std::string::npos);
        assert(message.find("XeMFG") != std::string::npos);
        assert(message.find("Display VBlank Sync (Extra Pacing)") != std::string::npos);

        // Short lines: the menu prints it with TextColored, never wrapped at the column edge (rule 10).
        size_t lineStart = 0;
        while (lineStart <= message.size())
        {
            size_t lineEnd = message.find('\n', lineStart);
            if (lineEnd == std::string::npos)
                lineEnd = message.size();
            assert(lineEnd - lineStart <= 64);
            lineStart = lineEnd + 1;
        }
        std::printf("  [PASS] Test 2: the menu text names the mode and the alternatives in short lines.\n");
    }

    // Test 3
    {
        const std::string wrapped = ReadFile("OptiScaler/wrapped/wrapped_swapchain.cpp");
        const std::string localPresent = Between(wrapped, "static HRESULT LocalPresent(", "\n}\n");
        assert(Count(localPresent, "ForceVsync.has_value()") == 1);
        assert(localPresent.find("Config::Instance()->ForceVsync.has_value() &&\n        "
                                 "VsyncOverridePolicy::AppliesInMode(State::Instance().externalFrameGeneration)") !=
               std::string::npos);
        assert(wrapped.find("ForceVsync.has_value() && !State::Instance().externalFrameGeneration") ==
               std::string::npos);

        const std::string fgHooks = ReadFile("OptiScaler/hooks/FG_Hooks.cpp");
        const std::string fgPresent = Between(fgHooks, "HRESULT FGHooks::FGPresent(", "\n}\n");
        assert(Count(fgPresent, "ForceVsync.has_value()") == 1);
        assert(fgPresent.find("config->ForceVsync.has_value() &&\n        "
                              "VsyncOverridePolicy::AppliesInMode(state.externalFrameGeneration)") !=
               std::string::npos);

        for (const std::string* file : { &wrapped, &fgHooks })
            assert(file->find("#include <misc/VsyncOverridePolicy.h>") != std::string::npos);
        std::printf("  [PASS] Test 3: LocalPresent and FGPresent skip the override in External mode.\n");
    }

    std::printf("All V-Sync override External FG mode tests passed.\n");
    return 0;
}
