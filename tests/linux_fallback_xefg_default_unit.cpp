// Linux 2X FG fallback of the RTX 20/30 unlocker: XeFG is the default pipeline since 0.9.94
// ([DLSSG] AmpereMfgLinuxFallbackType unset or auto), FSR FG stays selectable. Checks the production default, that
// every reader uses it, the ini and menu texts, and that the fallback session preloads OptiScaler's own libxess_fg.dll.
//
// Build and run from the repository root:
//   g++ -std=c++20 -Wall -Wextra -IOptiScaler -o /tmp/linux_fallback_xefg_default_unit
//     tests/linux_fallback_xefg_default_unit.cpp && /tmp/linux_fallback_xefg_default_unit

#include "../OptiScaler/proxies/XeFGPreloadPolicy.h"

#include <cstdio>
#include <cstring>
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

static bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

static size_t Count(const std::string& text, const std::string& part)
{
    size_t count = 0;
    for (auto at = text.find(part); at != std::string::npos; at = text.find(part, at + part.size()))
        count++;
    return count;
}

int main()
{
    // Test 1: the default.
    {
        const auto loader = Read("OptiScaler/framegen/dlssg/AmpereMfgLoader.h");
        Check(Has(loader, "inline constexpr const char* kDefaultLinuxFallbackType = \"xefg\";"),
              "Test 1: the default pipeline is XeFG");
        Check(Has(loader, "inline std::string ResolveFallbackFgType(const std::string& fallbackType = "
                          "kDefaultLinuxFallbackType)"),
              "Test 1: ResolveFallbackFgType defaults to it");
    }

    // Test 2: every reader of the key falls back to the same default.
    {
        for (const char* path : { "OptiScaler/dllmain.cpp", "OptiScaler/framegen/dlssg/AmpereMfgLoader.cpp",
                                  "OptiScaler/menu/menu_common.cpp" })
        {
            const auto text = Read(path);
            Check(!text.empty() && !Has(text, "FGDLSSGAmpereMfgLinuxFallbackType.value_or(\"fsrfg\")") &&
                      Has(text, "kDefaultLinuxFallbackType)"),
                  std::string("Test 2: ") + path + " reads the key with the shared default");
        }
        Check(Count(Read("OptiScaler/menu/menu_common.cpp"), "FGDLSSGAmpereMfgLinuxFallbackType.value_or(") == 3,
              "Test 2: the menu's three readers all go through the default");

        const auto config = Read("OptiScaler/Config.cpp");
        Check(Has(config, "FGDLSSGAmpereMfgLinuxFallbackType.value_for_config_or(\"auto\")"),
              "Test 2: Save writes auto when no pipeline was chosen, so a later default still applies");
        Check(Has(config, "else if (lstrcmpiA(fallbackType.value().c_str(), \"fsrfg\") == 0)"),
              "Test 2: an explicit fsrfg is still loaded as the user's choice");
    }

    // Test 3: ini and menu texts.
    {
        const auto ini = Read("OptiScaler.ini");
        Check(Has(ini, "\nAmpereMfgLinuxFallbackType=auto") && Has(ini, "; Default (auto) is xefg"),
              "Test 3: OptiScaler.ini ships auto and documents XeFG as the default");
        Check(!Has(ini, "requires libxess_fg.dll)"),
              "Test 3: the stale libxess_fg.dll requirement is gone from the ini");

        const auto menu = Read("OptiScaler/menu/menu_common.cpp");
        Check(Has(menu, "\"XeFG (Intel XeSS Frame Generation, default)\"") &&
                  !Has(menu, "\"XeFG (Intel XeSS FG, requires libxess_fg.dll)\"") &&
                  !Has(menu, "Requires libxess_fg.dll in game folder."),
              "Test 3: the menu names XeFG as the default without the stale requirement");
        const auto combo = menu.find("ImGui::Combo(\"Fallback Pipeline##sm86\"");
        const auto hdr =
            menu.find("ampereFallbackToFsrFg && currentPipeStr == \"xefg\" && state.xefgHdrFormatUnsupported");
        Check(combo != std::string::npos && hdr != std::string::npos && combo < hdr,
              "Test 3: a 16-bit float HDR game gets the FSR FG note under the pipeline combo");
    }

    // Test 4: the fallback session preloads OptiScaler's libxess_fg.dll (it is not External).
    {
        Check(XeFGPreloadPolicy::ShouldPreloadAtStartup(false), "Test 4: preload outside External mode");
        const auto dllmain = Read("OptiScaler/dllmain.cpp");
        const auto decided = dllmain.find("State::Instance().externalFrameGeneration =\n");
        const auto fallbackOff = dllmain.find("!ampereFallbackToFsrFg;", decided);
        const auto preload = dllmain.find("        CheckMemoryForProxies();");
        Check(decided != std::string::npos && fallbackOff != std::string::npos && preload != std::string::npos &&
                  decided < preload,
              "Test 4: External FG is decided (off for the fallback) before CheckMemoryForProxies preloads");
    }

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
