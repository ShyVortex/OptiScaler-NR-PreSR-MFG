#include "../OptiScaler/inputs/FfxSwapchainBlockPolicy.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

// DLSS Unlocked #42 (kilyan82, v0.9.87-pre1, Neverness to Everness): with the load-order fix NVSmooth30's Present hooks
// were blocked and NvPresent attached, but the game crashed in D3D12Core on AMD FSR-FG's presenter thread
// (amd_fidelityfx_framegeneration_dx12.dll → NvPresent64.dll → D3D12Core.dll). The game creates its FSR-FG swapchain
// even with bFSRFrameGeneration=False (FG-NVSMOOTH30-INTEGRATION.md 6.10).
//
// Validates:
// 1. The gate refuses only a swapchain-creation desc, with NVSmooth30 loaded, no user opt-out, and no OptiScaler
//    takeover.
// 2. ffxCreateContext_Dx12 keeps OptiScaler's own FSR-FG takeover branch first, then applies the gate before calling
//    AMD's runtime, and the refusal returns without calling it.
// 3. The gate covers FOR_HWND, NEW and WRAP and reads the loader status and the opt-out.
// 4. Config, INI load/save and the shipped OptiScaler.ini carry [SmoothMotion] AllowGameFsrFgSwapchain (default false).
// 5. The menu offers the opt-out in the NVSmooth30 subsection.

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

static size_t At(const std::string& text, const std::string& needle)
{
    const auto at = text.find(needle);
    if (at == std::string::npos)
    {
        std::fprintf(stderr, "missing: %s\n", needle.c_str());
        std::exit(1);
    }
    return at;
}

int main()
{
    using FfxSwapchainBlockPolicy::ShouldRefuseGameFsrFgSwapchain;

    // Test 1
    static_assert(ShouldRefuseGameFsrFgSwapchain(true, true, false, false));
    static_assert(!ShouldRefuseGameFsrFgSwapchain(false, true, false, false));
    static_assert(!ShouldRefuseGameFsrFgSwapchain(true, false, false, false));
    static_assert(!ShouldRefuseGameFsrFgSwapchain(true, true, true, false));
    static_assert(!ShouldRefuseGameFsrFgSwapchain(true, true, false, true));
    std::puts("Test 1 PASS: refuse only a game's FSR-FG swapchain while NVSmooth30 is loaded");

    const auto ffx = Squash(ReadFile("OptiScaler/inputs/FfxApi_Dx12.cpp"));
    const auto fn = ffx.substr(At(ffx, "ffxReturnCode_tffxCreateContext_Dx12(ffxContext*context"));

    // Test 2
    {
        const auto takeover = At(fn, "if(optiScalerTakesOver){autoresult=ffxCreateContext_Dx12FG(context,desc,memCb);");
        const auto gate = At(fn, "if(FfxSwapchainBlockPolicy::ShouldRefuseGameFsrFgSwapchain(");
        const auto original = At(fn, "autoffxApiResult=FfxApiProxy::D3D12_CreateContext(context,desc,memCb);");
        assert(takeover < gate && gate < original);
        const auto refuse = fn.substr(gate, At(fn.substr(gate), "returnFFX_API_RETURN_ERROR_RUNTIME_ERROR;}"));
        assert(refuse.find("D3D12_CreateContext") == std::string::npos);
    }
    std::puts("Test 2 PASS: the takeover stays first; the refusal never reaches AMD's runtime");

    // Test 3
    {
        assert(fn.find("constboolanySwapchainType=swapchainCreationType||desc->type==FFX_API_CREATE_CONTEXT_DESC_TYPE_"
                       "FRAMEGENERATIONSWAPCHAIN_WRAP_DX12;") != std::string::npos);
        assert(fn.find("FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_NEW_DX12||desc->type==FFX_API_CREATE_"
                       "CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12") != std::string::npos);
        assert(
            fn.find("ShouldRefuseGameFsrFgSwapchain(anySwapchainType,NVSmooth30Loader::LastStatus().DllLoaded,Config::"
                    "Instance()->SmoothMotionAllowGameFsrFgSwapchain.value_or_default(),optiScalerTakesOver)") !=
            std::string::npos);
    }
    std::puts("Test 3 PASS: FOR_HWND, NEW and WRAP are covered; loader status and opt-out are read");

    // Test 4
    {
        const auto h = Squash(ReadFile("OptiScaler/Config.h"));
        assert(h.find("CustomOptional<bool>SmoothMotionAllowGameFsrFgSwapchain{false};") != std::string::npos);
        const auto cpp = Squash(ReadFile("OptiScaler/Config.cpp"));
        assert(cpp.find("SmoothMotionAllowGameFsrFgSwapchain.set_from_config(readBool(\"SmoothMotion\","
                        "\"AllowGameFsrFgSwapchain\"));") != std::string::npos);
        assert(cpp.find("ini.SetValue(\"SmoothMotion\",\"AllowGameFsrFgSwapchain\",GetBoolValue(Instance()->"
                        "SmoothMotionAllowGameFsrFgSwapchain.value_for_config()).c_str());") != std::string::npos);
        const auto ini = ReadFile("OptiScaler.ini");
        const auto section = At(ini, "[SmoothMotion]");
        assert(At(ini, "AllowGameFsrFgSwapchain=auto") > section);
        assert(ini.find("\n[", section + 1) == std::string::npos); // last section: its keys can't fall into another
    }
    std::puts("Test 4 PASS: [SmoothMotion] AllowGameFsrFgSwapchain in config, load/save and the shipped INI");

    // Test 5
    {
        const auto menu = Squash(ReadFile("OptiScaler/menu/menu_common.cpp"));
        const auto nvs = At(menu, "\"EnableNVSmooth30Unlocker(RTX30)##nv_smooth30\"");
        const auto box = At(menu, "\"LetthegameuseitsFSRFGswapchain(restart)##nv_smooth30_fsr\"");
        assert(nvs < box);
        assert(menu.find("config->SmoothMotionAllowGameFsrFgSwapchain=allowFsrSc;") != std::string::npos);
    }
    std::puts("Test 5 PASS: the menu offers the opt-out");

    std::puts("All 5 NVSmooth30 FSR-FG swapchain tests passed.");
    return 0;
}
