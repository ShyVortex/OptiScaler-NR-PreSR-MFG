#include "../OptiScaler/hooks/GameXeFGBlockPolicy.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

// Lost Soul Aside with the external RTX 20/30 unlocker: a cross-vendor libxess_fg.dll in the game folder (DLSS Swapper
// upgrade of the shipped Intel-only 1.1.0.19) let the game's XeSS-FG wrap the swapchain on a GeForce, so DLSS-G never
// saw a present (FG-LSA-EXTERNAL-MFG-NO-FEATURE-CREATE.md). In External mode with the unlocker loaded, OptiScaler now
// answers the game's xefgSwapChainD3D12CreateContext with "unsupported device", as Intel-only builds do on GeForce.
//
// Validates:
// 1. Gate truth table: blocks only with External FG + unlocker enabled + unlocker loaded + setting on.
// 2. Library name matching: libxess_fg.dll / igxess_fg.dll, any case, bare or full path; nothing else.
// 3. The answer is XEFG_SWAPCHAIN_RESULT_ERROR_UNSUPPORTED_DEVICE (-1).
// 4. Hook wiring (GameXeFG_Hooks.h): the detour refuses without calling the original, clears the handle, re-checks the
//    gate at call time, hooks only CreateContext, and never hooks OptiScaler's own copy.
// 5. Load wiring (LibraryLoad_Hooks.cpp): OnOriginalLoad reaches the hook before the interposer early return, and
//    CheckModulesInMemory covers an already-loaded library in External mode only.
// 6. Config: default on (auto), loaded, saved, documented in OptiScaler.ini, and exposed in the menu.

static int g_failures = 0;

#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            std::printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                               \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

static std::string ReadSource(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static size_t CountOf(const std::string& text, const std::string& needle)
{
    size_t count = 0;
    for (size_t pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + needle.size()))
        ++count;
    return count;
}

static std::string FunctionBody(const std::string& source, const std::string& signature)
{
    const size_t start = source.find(signature);
    if (start == std::string::npos)
        return {};
    const size_t open = source.find('{', start);
    if (open == std::string::npos)
        return {};
    int depth = 0;
    for (size_t i = open; i < source.size(); ++i)
    {
        if (source[i] == '{')
            ++depth;
        else if (source[i] == '}' && --depth == 0)
            return source.substr(open, i - open + 1);
    }
    return {};
}

static void Test1_Gate()
{
    using GameXeFGBlockPolicy::ShouldBlock;
    static_assert(ShouldBlock(true, true, true, true));
    for (int mask = 0; mask < 16; ++mask)
    {
        const bool external = mask & 1, enabled = mask & 2, loaded = mask & 4, setting = mask & 8;
        CHECK(ShouldBlock(external, enabled, loaded, setting) == (mask == 15));
    }
    std::printf("Test 1 done\n");
}

static void Test2_Names()
{
    using GameXeFGBlockPolicy::IsXeFGLibrary;
    CHECK(IsXeFGLibrary(L"libxess_fg.dll"));
    CHECK(IsXeFGLibrary(L"LIBXESS_FG.DLL"));
    CHECK(IsXeFGLibrary(L"igxess_fg.dll"));
    CHECK(IsXeFGLibrary(
        L"D:\\SteamLibrary\\Lost Soul Aside\\Engine\\Plugins\\Runtime\\Intel\\XeSS\\Binaries\\ThirdParty\\Win64\\"
        L"libxess_fg.dll"));
    CHECK(IsXeFGLibrary(L"D:/Game/libxess_fg.dll"));
    CHECK(!IsXeFGLibrary(L"libxess.dll"));
    CHECK(!IsXeFGLibrary(L"libxell.dll"));
    CHECK(!IsXeFGLibrary(L"libxess_fg.dll.dlsss"));
    CHECK(!IsXeFGLibrary(L"xlibxess_fg.dll"));
    CHECK(!IsXeFGLibrary(L""));
    std::printf("Test 2 done\n");
}

static void Test3_Answer()
{
    static_assert(GameXeFGBlockPolicy::kUnsupportedDevice == -1);
    std::printf("Test 3 done\n");
}

static void Test4to6_Wiring()
{
    const std::string hooks = ReadSource("OptiScaler/hooks/GameXeFG_Hooks.h");
    const std::string load = ReadSource("OptiScaler/hooks/LibraryLoad_Hooks.cpp");
    const std::string configH = ReadSource("OptiScaler/Config.h");
    const std::string configCpp = ReadSource("OptiScaler/Config.cpp");
    const std::string ini = ReadSource("OptiScaler.ini");
    const std::string menu = ReadSource("OptiScaler/menu/menu_common.cpp");
    CHECK(!hooks.empty() && !load.empty() && !configH.empty() && !configCpp.empty() && !ini.empty() && !menu.empty());
    if (hooks.empty() || load.empty())
    {
        std::printf("Tests 4-6 skipped: run from the repository root\n");
        return;
    }

    // 4
    const std::string detour = FunctionBody(
        hooks,
        "static xefg_swapchain_result_t hkCreateContext(ID3D12Device* pDevice, xefg_swapchain_handle_t* phSwapChain)");
    CHECK(!detour.empty());
    const size_t gate = detour.find("if (!BlockActive())");
    const size_t original = detour.find("return o_CreateContext(pDevice, phSwapChain);");
    CHECK(gate != std::string::npos && original != std::string::npos && gate < original);
    CHECK(CountOf(detour, "o_CreateContext(") == 1);
    CHECK(detour.find("*phSwapChain = nullptr;") != std::string::npos);
    CHECK(detour.find("GameXeFGBlockPolicy::kUnsupportedDevice") != std::string::npos);

    const std::string onLoad =
        FunctionBody(hooks, "static void OnLibraryLoaded(std::wstring_view path, HMODULE module)");
    CHECK(!onLoad.empty());
    CHECK(onLoad.find("GameXeFGBlockPolicy::IsXeFGLibrary(path)") != std::string::npos);
    CHECK(onLoad.find("BlockActive()") != std::string::npos);
    CHECK(onLoad.find("IsOptiScalerOwnCopy(module)") != std::string::npos);
    CHECK(onLoad.find("\"xefgSwapChainD3D12CreateContext\"") != std::string::npos);
    CHECK(CountOf(onLoad, "DetourAttach") == 1);

    const std::string active = FunctionBody(hooks, "static bool BlockActive()");
    CHECK(active.find("State::Instance().externalFrameGeneration") != std::string::npos);
    CHECK(active.find("FGDLSSGAmpereMfgUnlock") != std::string::npos);
    CHECK(active.find("AmpereMfgLoader::GetStatus().DllLoaded") != std::string::npos);
    CHECK(active.find("FGDLSSGAmpereMfgBlockGameXeFG") != std::string::npos);
    std::printf("Test 4 done\n");

    // 5
    const std::string original5 =
        FunctionBody(load, "void LibraryLoadHooks::OnOriginalLoad(std::wstring_view name, HMODULE module)");
    const size_t xefg = original5.find("GameXeFGHooks::OnLibraryLoaded(name, module);");
    const size_t interposerReturn = original5.find("return;");
    CHECK(xefg != std::string::npos && interposerReturn != std::string::npos && xefg < interposerReturn);

    const std::string check = FunctionBody(load, "void LibraryLoadHooks::CheckModulesInMemory()");
    const size_t externalGate = check.find("if (State::Instance().externalFrameGeneration)");
    const size_t poll = check.find("GameXeFGHooks::OnLibraryLoaded(xefgName, xefgModule);");
    CHECK(externalGate != std::string::npos && poll != std::string::npos && externalGate < poll);
    std::printf("Test 5 done\n");

    // 6
    CHECK(configH.find("CustomOptional<bool> FGDLSSGAmpereMfgBlockGameXeFG { true };") != std::string::npos);
    CHECK(configCpp.find(
              "FGDLSSGAmpereMfgBlockGameXeFG.set_from_config(readBool(\"DLSSG\", \"AmpereMfgBlockGameXeFG\"));") !=
          std::string::npos);
    CHECK(configCpp.find("ini.SetValue(\"DLSSG\", \"AmpereMfgBlockGameXeFG\",") != std::string::npos);
    CHECK(ini.find("\nAmpereMfgBlockGameXeFG=auto") != std::string::npos);
    CHECK(menu.find("config->FGDLSSGAmpereMfgBlockGameXeFG = blockGameXeFG;") != std::string::npos);
    std::printf("Test 6 done\n");
}

int main()
{
    Test1_Gate();
    Test2_Names();
    Test3_Answer();
    Test4to6_Wiring();

    if (g_failures != 0)
    {
        std::printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }

    std::printf("All 6 tests passed\n");
    return 0;
}
