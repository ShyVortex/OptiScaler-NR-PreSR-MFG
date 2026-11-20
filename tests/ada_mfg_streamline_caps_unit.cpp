// RTX 40 (Ada) MFG unlock: how the game learns it can offer 3X-6X, without the architecture spoof RTX 20/30 needs.
//
// RTX 20/30: Streamline refuses the DLSS-G plugin below Ada ("Ignoring plugin 'sl.dlss_g'"), so the plugin load is
// spoofed (ampereMfgActive in hkdlssg_slOnPluginLoad) and sdli1995's SpoofArchToGame reports RTX 50 to the game.
// RTX 40 passes the plugin check natively: OptiScaler's DLSS-G spoof leaves Ada and newer alone on purpose, and the
// unlocked maximum reaches the game through slDLSSGGetState (numFramesToGenerateMax via MfgUnlock::EffectiveMax).
// These checks keep that path from regressing.
//
// Build and run from the repository root:
//   g++ -std=c++20 -Wall -Wextra -o /tmp/ada_mfg_streamline_caps_unit tests/ada_mfg_streamline_caps_unit.cpp &&
//     /tmp/ada_mfg_streamline_caps_unit

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

static std::string Between(const std::string& text, const std::string& from, const std::string& to)
{
    const auto start = text.find(from);
    if (start == std::string::npos)
        return {};
    const auto end = text.find(to, start + from.size());
    return text.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

static bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

int main()
{
    const auto sl = Read("OptiScaler/hooks/Streamline_Hooks.cpp");
    Check(!sl.empty(), "Streamline_Hooks.cpp found");

    // Test 1: the DLSS-G architecture spoof never touches Ada and newer.
    {
        const auto spoof = Between(sl, "void StreamlineHooks::spoofArch(", "\n}\n");
        const auto dlssg = Between(spoof, "else if (feature == sl::kFeatureDLSS_G)", "else if (feature ==");
        Check(Has(dlssg, "if (currentArch < NV_GPU_ARCHITECTURE_AD100)") &&
                  Has(dlssg, "return setArch(maxArch, altSystemCaps);"),
              "Test 1: DLSS-G plugin spoof only below Ada");
    }

    // Test 2: the game's DLSS-G state query reports the RTX 40 unlocked maximum.
    {
        const auto getState = Between(sl, "sl::Result StreamlineHooks::hkslDLSSGGetState(", "\nsl::Result ");
        Check(Has(getState, "state.numFramesToGenerateMax = MfgUnlock::EffectiveMax(state.numFramesToGenerateMax);"),
              "Test 2: numFramesToGenerateMax goes through EffectiveMax");
        Check(Has(getState, "if (originalStructVersion >= 2)"),
              "Test 2: only for struct versions that have the field (rule 4: ABI per version)");
        Check(Has(getState, "optiState.dlssgMfgMax = static_cast<int>(MfgUnlock::EffectiveMax("),
              "Test 2: the menu's cached maximum follows the same value");
    }

    // Test 3: the game gets the GetState hook in every non-External session, the only mode the RTX 40 unlock runs in.
    {
        const auto featureFunction = Between(sl, "sl::Result StreamlineHooks::hkslGetFeatureFunction(", "\n}\n");
        const auto nonExternal =
            Between(featureFunction, "if (!State::Instance().externalFrameGeneration && feature == sl::kFeatureDLSS_G)",
                    "auto result = o_slGetFeatureFunction(");
        Check(Has(nonExternal, "if (strcmp(functionName, \"slDLSSGGetState\") == 0)") &&
                  Has(nonExternal, "function = &hkslDLSSGGetState;"),
              "Test 3: slGetFeatureFunction hands out the GetState hook outside External mode");

        const auto pluginFunction =
            Between(sl, "o_slDLSSGGetState = (decltype(&slDLSSGGetState)) o_dlssg_slGetPluginFunction(functionName);",
                    "\n    }\n");
        Check(Has(pluginFunction, "return &hkslDLSSGGetState;"),
              "Test 3: the DLSS-G plugin's own function table hands out the GetState hook");
        Check(!Has(nonExternal, "AdaMfgUnlock") && !Has(pluginFunction, "AdaMfgUnlock"),
              "Test 3: with no RTX 40-specific condition");
    }

    // Test 4: the RTX 40 unlock refuses External FG mode, where OptiScaler leaves sl.dlss_g unhooked.
    {
        const auto unlock = Read("OptiScaler/framegen/dlssg/MfgUnlock.cpp");
        const auto session = Between(unlock, "bool MfgUnlock::EnabledForSession()", "\n}\n");
        Check(Has(session, "!State::Instance().externalFrameGeneration"),
              "Test 4: EnabledForSession excludes External");
    }

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
