// RTX 40 (Ada) MFG unlock multiplier ceiling ([DLSSG] AdaMfgMaxFrames): the production AdaMfgCeiling.h math, and
// that it is only used through MfgUnlock::EffectiveMax (a ceiling, never a forced frame count).
//
// Build and run from the repository root:
//   g++ -std=c++20 -Wall -Wextra -o /tmp/ada_mfg_ceiling_unit tests/ada_mfg_ceiling_unit.cpp &&
//     /tmp/ada_mfg_ceiling_unit

#include "../OptiScaler/framegen/dlssg/AdaMfgCeiling.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace AdaMfgCeiling;

static int g_failures = 0;
static int g_checks = 0;

static void Check(bool condition, const std::string& name)
{
    g_checks++;
    if (!condition)
    {
        g_failures++;
        std::printf("FAIL: %s\n", name.c_str());
    }
}

static std::string Read(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

static bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

int main()
{
    // Test 1: setting -> ceiling.
    {
        Check(Ceiling(0) == 5, "Test 1: 0 is the unlocked maximum (6X)");
        for (int i = 1; i <= 5; i++)
            Check(Ceiling(i) == static_cast<unsigned int>(i), "Test 1: " + std::to_string(i) + " is kept");
        Check(Ceiling(-1) == 5 && Ceiling(6) == 5, "Test 1: out of range is the unlocked maximum");
    }

    // Test 2: a complete patch reports the ceiling.
    {
        Check(Effective(1, false, true, 0) == 5, "Test 2: unlocked maximum without a ceiling (unchanged behaviour)");
        Check(Effective(1, false, true, 3) == 3, "Test 2: ceiling 3 reports 4X");
        Check(Effective(1, false, true, 1) == 1, "Test 2: ceiling 1 reports 2X only");
    }

    // Test 3: never below the runtime's own maximum.
    {
        Check(Effective(8, false, true, 3) == 8, "Test 3: a larger native maximum is kept");
        Check(Effective(3, false, true, 1) == 3, "Test 3: the ceiling never lowers the native maximum");
        Check(Effective(8, false, false, 3) == 8, "Test 3: no patch: the native maximum stands");
        Check(Effective(1, false, false, 5) == 1, "Test 3: no patch: the ceiling raises nothing");
    }

    // Test 4: a failed patch stays fail-closed (rule 4, Ada invariants).
    {
        Check(Effective(5, true, false, 0) == 1, "Test 4: failed patch reports 1");
        Check(Effective(5, true, true, 3) == 1, "Test 4: failed patch reports 1 whatever the ceiling");
    }

    // Test 5: production wiring.
    {
        const auto unlock = Read("OptiScaler/framegen/dlssg/MfgUnlock.cpp");
        Check(Has(unlock, "#include \"AdaMfgCeiling.h\"") && Has(unlock, "AdaMfgCeiling::Effective(nativeMaximum,"),
              "Test 5: EffectiveMax computes through AdaMfgCeiling");
        Check(Has(unlock, "static const int setting = Config::Instance()->FGDLSSGAdaMfgMaxFrames.value_or_default();"),
              "Test 5: the ceiling is latched once per session");
        Check(Has(unlock, "static_assert(AdaMfgCeiling::kUnlockedFrames == kMaxGeneratedFrames);"),
              "Test 5: the ceiling's maximum matches the patched frame count");
        Check(Has(unlock, "return status.AdvertiseMatched && status.ValidateMatched && !status.PatchFailed ? "
                          "kMaxGeneratedFrames : 0;"),
              "Test 5: UnlockedMax still reports the patch capability (rule 4)");

        // A ceiling only: the frame-count paths never read the setting, they go through EffectiveMax.
        for (const char* path : { "OptiScaler/hooks/Streamline_Hooks.cpp", "OptiScaler/framegen/dlssg/DLSSG_Dx12.cpp",
                                  "OptiScaler/inputs/NVNGX_DLSS_Dx12.cpp", "OptiScaler/NVNGX_Parameter.cpp" })
        {
            const auto text = Read(path);
            Check(!text.empty() && !Has(text, "FGDLSSGAdaMfgMaxFrames") && Has(text, "MfgUnlock::EffectiveMax("),
                  std::string("Test 5: ") + path + " uses EffectiveMax, not the setting");
        }

        const auto config = Read("OptiScaler/Config.cpp");
        Check(Has(config, "FGDLSSGAdaMfgMaxFrames.set_from_config(readInt(\"DLSSG\", \"AdaMfgMaxFrames\"));") &&
                  Has(config, "FGDLSSGAdaMfgMaxFrames.value() < 0 || FGDLSSGAdaMfgMaxFrames.value() > 5") &&
                  Has(config, "ini.SetValue(\"DLSSG\", \"AdaMfgMaxFrames\","),
              "Test 5: Config.cpp loads (bounded 0..5) and saves AdaMfgMaxFrames");

        const auto ini = Read("OptiScaler.ini");
        Check(Has(ini, "\nAdaMfgMaxFrames=auto"), "Test 5: OptiScaler.ini documents AdaMfgMaxFrames");

        const auto menu = Read("OptiScaler/menu/menu_common.cpp");
        const auto ada = menu.find("SeparatorWithHelpMarker(\"RTX 40 (Ada)\"");
        const auto slider = menu.find("ImGui::SliderInt(\"Max Multiplier Ceiling##ada\"");
        const auto ampere = menu.find("SeparatorWithHelpMarker(\"RTX 20 / 30 (Turing / Ampere)\"");
        Check(ada != std::string::npos && slider != std::string::npos && ampere != std::string::npos && ada < slider &&
                  slider < ampere,
              "Test 5: the slider sits in the RTX 40 sub-block");
        Check(Has(menu, "adaMaxFrames != MfgUnlock::SessionMaxFramesSetting()"),
              "Test 5: the menu asks for a restart when the ceiling changes");

        const auto mocks = Read("tests/mfg_unlock/Mocks.h");
        Check(Has(mocks, "TestIntOption FGDLSSGAdaMfgMaxFrames;"),
              "Test 5: the production-backed MfgUnlock tests' Config mock has the setting");
    }

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
