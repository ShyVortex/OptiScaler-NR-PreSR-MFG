// RTX 40 (Ada) unlock FG preset ([DLSSG] AdaMfgPreset): the production DlssgPresetPolicy.h rules, and the wiring of
// the DRS answer in NvApiHooks.cpp, MfgUnlock, Config and the menu.
//
// Build and run from the repository root:
//   g++ -std=c++20 -Wall -Wextra -o /tmp/dlssg_preset_drs_unit tests/dlssg_preset_drs_unit.cpp &&
//     /tmp/dlssg_preset_drs_unit

#include "../OptiScaler/framegen/dlssg/DlssgPresetPolicy.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace DlssgPresetPolicy;

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
    // Test 1: the key and values match the NVAPI SDK header.
    {
        const auto settings = Read("external/nvapi/NvApiDriverSettings.h");
        Check(Has(settings, "NGX_DLSS_FG_OVERRIDE_RENDER_PRESET_SELECTION_ID = 0x10E41DF1") && kSettingId == 0x10E41DF1,
              "Test 1: preset key id");
        Check(Has(settings, "NGX_DLSS_FG_OVERRIDE_RENDER_PRESET_SELECTION_RENDER_PRESET_A = 1") &&
                  Has(settings, "NGX_DLSS_FG_OVERRIDE_RENDER_PRESET_SELECTION_RENDER_PRESET_B = 2") &&
                  Has(settings, "NGX_DLSS_FG_OVERRIDE_RENDER_PRESET_SELECTION_RENDER_PRESET_C = 3"),
              "Test 1: A = 1, B = 2, C = 3 in the SDK");
        Check(static_cast<uint32_t>(Preset::A) == 1 && static_cast<uint32_t>(Preset::B) == 2 &&
                  static_cast<uint32_t>(Preset::C) == 3 && static_cast<uint32_t>(Preset::Auto) == 0,
              "Test 1: Preset values");
        const auto common = Read("external/nvapi/nvapi_lite_common.h");
        Check(Has(common, "NVAPI_SETTING_NOT_FOUND                     = -160") && kNvapiSettingNotFound == -160,
              "Test 1: NVAPI_SETTING_NOT_FOUND");
    }

    // Test 2: parsing.
    {
        Check(Parse("auto") == Preset::Auto && Parse("AUTO") == Preset::Auto, "Test 2: auto");
        Check(Parse("a") == Preset::A && Parse("B") == Preset::B && Parse("c") == Preset::C, "Test 2: letters");
        Check(!Parse("d").has_value() && !Parse("").has_value() && !Parse("ab").has_value(),
              "Test 2: other text is unset");
        for (auto preset : { Preset::Auto, Preset::A, Preset::B, Preset::C })
            Check(Parse(Name(preset)) == preset, std::string("Test 2: Name round trips ") + Name(preset));
    }

    // Test 3: runtime version and Preset C.
    {
        Check(!ParseVersion("").has_value() && !ParseVersion("310.9").has_value() &&
                  !ParseVersion("310.x.16").has_value(),
              "Test 3: malformed versions are unknown");
        const auto v = ParseVersion("310.9.16");
        Check(v && v->major == 310 && v->minor == 9 && v->patch == 16, "Test 3: 310.9.16 parsed");
        Check(ImplementsPresetC(ParseVersion("310.9.16")) && ImplementsPresetC(ParseVersion("310.9.20")) &&
                  ImplementsPresetC(ParseVersion("310.10.0")) && ImplementsPresetC(ParseVersion("311.0.0")),
              "Test 3: 310.9.16 and newer have Preset C");
        Check(!ImplementsPresetC(ParseVersion("310.9.1")) && !ImplementsPresetC(ParseVersion("310.9.15")) &&
                  !ImplementsPresetC(ParseVersion("310.6.0")) && !ImplementsPresetC(std::nullopt),
              "Test 3: older or unknown runtimes do not");
    }

    // Test 4: what is answered.
    {
        const auto newer = ParseVersion("310.9.16");
        const auto older = ParseVersion("310.9.1");
        Check(!Override(Preset::Auto, true, newer).has_value(), "Test 4: Auto never overrides");
        Check(!Override(Preset::B, false, newer).has_value(), "Test 4: nothing without the RTX 40 unlock");
        Check(Override(Preset::A, true, older) == 1u && Override(Preset::B, true, older) == 2u,
              "Test 4: A and B on any runtime");
        Check(Override(Preset::C, true, newer) == 3u, "Test 4: C on 310.9.16");
        Check(!Override(Preset::C, true, older).has_value(), "Test 4: C on an older runtime keeps the game's preset");
        Check(!Override(Preset::C, true, std::nullopt).has_value(), "Test 4: C on an unknown runtime is not sent");
    }

    // Test 5: how a driver answer is handled.
    {
        constexpr uint32_t ver = 0x13020; // any struct version, compared for equality
        Check(Classify(kNvapiOk, true, ver, ver, kDwordType) == Action::OverwriteValue, "Test 5: DWORD in profile");
        Check(Classify(kNvapiSettingNotFound, true, ver, ver, 0xFFFF) == Action::FillMissing,
              "Test 5: key not in the profile is filled");
        Check(Classify(kNvapiOk, true, ver, ver, 1) == Action::Leave, "Test 5: non-DWORD type left alone");
        Check(Classify(-5, true, ver, ver, kDwordType) == Action::Leave, "Test 5: driver error left alone");
        Check(Classify(kNvapiOk, false, ver, ver, kDwordType) == Action::Leave, "Test 5: null setting left alone");
        Check(Classify(kNvapiSettingNotFound, true, ver + 1, ver, kDwordType) == Action::Leave,
              "Test 5: other struct version left alone");
    }

    // Test 6: production wiring.
    {
        const auto hooks = Read("OptiScaler/nvapi/NvApiHooks.cpp");
        const auto call = hooks.find("auto result = o_NvAPI_DRS_GetSetting(hSession, hProfile, settingId, pSetting);");
        const auto route = hooks.find("if (settingId == DlssgPresetPolicy::kSettingId)");
        Check(call != std::string::npos && route != std::string::npos && call < route,
              "Test 6: the preset key is answered after the driver's own read");
        Check(Has(hooks, "DlssgPresetPolicy::Override(preset, MfgUnlock::EnabledForSession(),") &&
                  Has(hooks, "DlssgPresetPolicy::ParseVersion(MfgUnlock::LastStatus().SnippetVersion)") &&
                  Has(hooks, "DlssgPresetPolicy::Classify("),
              "Test 6: the hook decides through DlssgPresetPolicy, RTX 40 unlock and runtime version");
        Check(Has(hooks, "pSetting->settingLocation = NVDRS_CURRENT_PROFILE_LOCATION;") &&
                  Has(hooks, "pSetting->u32CurrentValue = value.value();"),
              "Test 6: the answer is a current-profile DWORD");
        const auto answer = hooks.substr(hooks.find("static NvAPI_Status AnswerDlssgPreset("),
                                         hooks.find("NvAPI_Status __stdcall NvApiHooks::hkNvAPI_DRS_GetSetting(") -
                                             hooks.find("static NvAPI_Status AnswerDlssgPreset("));
        Check(!answer.empty() && !Has(answer, "SetSetting") && !Has(answer, "SaveSettings"),
              "Test 6: AnswerDlssgPreset never writes the saved profile");

        const auto unlock = Read("OptiScaler/framegen/dlssg/MfgUnlock.cpp");
        Check(Has(unlock, "uint32_t MfgUnlock::SessionPreset()") &&
                  Has(unlock, "DlssgPresetPolicy::Parse(Config::Instance()->FGDLSSGAdaMfgPreset.value_or(\"auto\"))"),
              "Test 6: the preset is latched once per session in MfgUnlock");

        const auto config = Read("OptiScaler/Config.cpp");
        Check(Has(config, "readString(\"DLSSG\", \"AdaMfgPreset\")") &&
                  Has(config, "ini.SetValue(\"DLSSG\", \"AdaMfgPreset\","),
              "Test 6: Config.cpp loads and saves AdaMfgPreset");
        Check(Has(Read("OptiScaler.ini"), "\nAdaMfgPreset=auto"), "Test 6: OptiScaler.ini documents AdaMfgPreset");

        const auto menu = Read("OptiScaler/menu/menu_common.cpp");
        const auto ada = menu.find("SeparatorWithHelpMarker(\"RTX 40 (Ada)\"");
        const auto combo = menu.find("ImGui::BeginCombo(\"FG Preset##ada\"");
        const auto ampere = menu.find("SeparatorWithHelpMarker(\"RTX 20 / 30 (Turing / Ampere)\"");
        Check(ada != std::string::npos && combo != std::string::npos && ada < combo && combo < ampere,
              "Test 6: the preset combo sits in the RTX 40 sub-block");
        Check(Has(menu, "NvApiHooks::dlssgPresetAnswered.load()") && Has(menu, "configuredPreset != session"),
              "Test 6: the menu shows the answered preset and a restart hint");

        Check(Has(Read("tests/mfg_unlock/Mocks.h"), "TestStringOption FGDLSSGAdaMfgPreset;"),
              "Test 6: the production-backed MfgUnlock tests' Config mock has the setting");
    }

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
