#include "../OptiScaler/framegen/smoothmotion/NVSmooth30Loader.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

// DLSS Unlocked #42 (kilyan82, v0.9.88-pre3): the first real Present returned E_NOINTERFACE and NvPresent crashed in
// D3D12Core. Offline (nvpresent64.dll 621.01) NvPresent hands out its own swapchain proxy wrapping an inner swapchain,
// and the logs show NvPresent QIs OptiScaler's WrappedIDXGISwapChain4: NvPresent's factory hook sits above OptiScaler's
// and wraps OptiScaler's wrapper. The [SmoothMotion] LeaveSmoothMotionSwapchainUnwrapped test switch hands NvPresent's
// swapchain back unwrapped (FG-NVSMOOTH30-INTEGRATION.md 6.13). Confirmed in game (95 -> 170 fps), now default on
// (6.14).
//
// Validates:
// 1. IsNvPresentModule matches nvpresent64.dll in any case, and nothing else.
// 2. The gate needs NVSmooth30 loaded, the switch on and NvPresent as the caller.
// 3. CreateSwapChain and CreateSwapChainForHwnd check it right after the NVSmooth30 dummy block, before any wrapping,
//    and pass through via o_CreateSwapChain* inside ScopedSkipDxgiLoadChecks / ScopedSkipParentWrapping.
// 4. The helper logs the caller whenever NVSmooth30 is loaded, switch on or off, and reads the switch.
// 5. Config, INI load/save and the shipped OptiScaler.ini (default auto = true since the 2026-11-09 in-game
//    confirmation, in [SmoothMotion]); the menu help explains the trade-off.

static std::string ReadFile(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        std::fprintf(stderr, "cannot open %s (run from the repository root)\n", path);
        std::exit(1);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static std::string Squash(const std::string& text)
{
    std::string out;
    for (char c : text)
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t')
            out += c;
    return out;
}

static size_t At(const std::string& text, const std::string& needle, size_t from = 0)
{
    const auto at = text.find(needle, from);
    if (at == std::string::npos)
    {
        std::fprintf(stderr, "missing: %s\n", needle.c_str());
        std::exit(1);
    }
    return at;
}

int main()
{
    using namespace NVSmooth30Loader;

    // Test 1
    assert(IsNvPresentModule("NvPresent64.dll") && IsNvPresentModule("nvpresent64.dll") &&
           IsNvPresentModule("NVPRESENT64.DLL"));
    assert(!IsNvPresentModule("") && !IsNvPresentModule("NvPresent64.dl") && !IsNvPresentModule("HTGame.exe") &&
           !IsNvPresentModule("amd_fidelityfx_framegeneration_dx12.dll") && !IsNvPresentModule("xNvPresent64.dll"));
    std::puts("Test 1 PASS: NvPresent64.dll is recognised in any case");

    // Test 2
    static_assert(ShouldLeaveSwapchainUnwrapped(true, true, true));
    static_assert(!ShouldLeaveSwapchainUnwrapped(false, true, true));
    static_assert(!ShouldLeaveSwapchainUnwrapped(true, false, true));
    static_assert(!ShouldLeaveSwapchainUnwrapped(true, true, false));
    std::puts("Test 2 PASS: the gate needs NVSmooth30 loaded, the switch on and NvPresent as the caller");

    const auto hooks = Squash(ReadFile("OptiScaler/hooks/DxgiFactory_Hooks.cpp"));

    // Test 3
    {
        const auto sc = At(hooks, "HRESULTDxgiFactoryHooks::CreateSwapChain(IDXGIFactory*realFactory");
        const auto scDummy = At(hooks, "returnDXGI_ERROR_INVALID_CALL;}", sc);
        const auto scGate =
            At(hooks,
               "if(LeaveUnwrappedForNvPresent(\"CreateSwapChain\",_ReturnAddress())){"
               "ScopedSkipDxgiLoadChecksskipDxgiLoadChecks{};ScopedSkipParentWrappingskipParentWrapping{};"
               "returno_CreateSwapChain(realFactory,pDevice,pDesc,ppSwapChain);}",
               sc);
        assert(scDummy < scGate);
        assert(scGate < At(hooks, "newWrappedIDXGISwapChain4(", sc));

        const auto hw = At(hooks, "HRESULTDxgiFactoryHooks::CreateSwapChainForHwnd(IDXGIFactory2*realFactory");
        const auto hwDummy = At(hooks, "returnDXGI_ERROR_INVALID_CALL;}", hw);
        const auto hwGate =
            At(hooks,
               "if(LeaveUnwrappedForNvPresent(\"CreateSwapChainForHwnd\",_ReturnAddress())){"
               "ScopedSkipDxgiLoadChecksskipDxgiLoadChecks{};ScopedSkipParentWrappingskipParentWrapping{};"
               "returno_CreateSwapChainForHwnd(realFactory,pDevice,hWnd,pDesc,pFullscreenDesc,"
               "pRestrictToOutput,ppSwapChain);}",
               hw);
        assert(hwDummy < hwGate);
        assert(hwGate < At(hooks, "newWrappedIDXGISwapChain4(", hw));
    }
    std::puts("Test 3 PASS: both hooks pass NvPresent's swapchain through before any wrapping");

    // Test 4
    {
        const auto fn =
            hooks.substr(At(hooks, "staticboolLeaveUnwrappedForNvPresent(constchar*hook,void*returnAddress){"), 900);
        const auto loaded = At(fn, "constboolloaded=NVSmooth30Loader::LastStatus().DllLoaded;if(!loaded)returnfalse;");
        const auto log = At(fn, "LOG_INFO(\"NVSmooth30:{}calledby{}{}\"");
        assert(loaded < log);
        assert(fn.find("Config::Instance()->SmoothMotionLeaveSwapchainUnwrapped.value_or_default()") !=
               std::string::npos);
        assert(fn.find("NVSmooth30Loader::IsNvPresentModule(caller)") != std::string::npos);
    }
    std::puts("Test 4 PASS: the caller is logged whenever NVSmooth30 is loaded");

    // Test 5
    {
        const auto h = Squash(ReadFile("OptiScaler/Config.h"));
        assert(h.find("CustomOptional<bool>SmoothMotionLeaveSwapchainUnwrapped{true};") != std::string::npos);
        const auto cpp = Squash(ReadFile("OptiScaler/Config.cpp"));
        assert(cpp.find("SmoothMotionLeaveSwapchainUnwrapped.set_from_config(readBool(\"SmoothMotion\","
                        "\"LeaveSmoothMotionSwapchainUnwrapped\"));") != std::string::npos);
        assert(cpp.find("ini.SetValue(\"SmoothMotion\",\"LeaveSmoothMotionSwapchainUnwrapped\",GetBoolValue(Instance()"
                        "->SmoothMotionLeaveSwapchainUnwrapped.value_for_config()).c_str());") != std::string::npos);
        const auto ini = ReadFile("OptiScaler.ini");
        const auto section = At(ini, "[SmoothMotion]");
        assert(At(ini, "LeaveSmoothMotionSwapchainUnwrapped=auto") > section);
        assert(ini.find("\n[", section + 1) == std::string::npos);
        assert(ini.find("; true or false - Default (auto) is true\nLeaveSmoothMotionSwapchainUnwrapped=auto") !=
                   std::string::npos ||
               ini.find("; true or false - Default (auto) is true\r\nLeaveSmoothMotionSwapchainUnwrapped=auto") !=
                   std::string::npos);
        const auto menu = Squash(ReadFile("OptiScaler/menu/menu_common.cpp"));
        assert(menu.find("WhileNVSmooth30isactive,OptiScalerleavesSmoothMotion'sswapchainunwrapped") !=
               std::string::npos);
    }
    std::puts(
        "Test 5 PASS: [SmoothMotion] LeaveSmoothMotionSwapchainUnwrapped, default true, documented in INI and menu");

    std::puts("All 5 NVSmooth30 unwrapped swapchain tests passed.");
    return 0;
}
