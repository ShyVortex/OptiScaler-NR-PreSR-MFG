#include "../OptiScaler/inputs/FfxSwapchainBlockPolicy.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

// DLSS Unlocked #42 (kilyan82, Neverness to Everness): NvPresent crashed in D3D12Core on AMD FSR-FG's presenter thread.
// The game creates its FSR-FG swapchain even with bFSRFrameGeneration=False. Refusing the request (0f566d5e) left the
// game with no swapchain at all (audio, no picture), so OptiScaler now answers it with a plain swapchain and handles
// the game's later calls on that context itself (FG-NVSMOOTH30-INTEGRATION.md 6.11).
//
// Validates:
// 1. The replace gate: swapchain-creation desc, NVSmooth30 loaded, no opt-out, no OptiScaler takeover.
// 2. Descriptor ids and DecideQuery / ShouldRefuseFrameGenerationContext answers.
// 3. ffxCreateContext_Dx12 keeps OptiScaler's own FSR-FG takeover first, then hands the request to
//    FfxPlainSwapchain::Create before AMD's runtime, and refuses AMD's FG context while a plain swapchain is active.
// 4. Destroy / Configure / Query / Dispatch hand plain-swapchain calls to FfxPlainSwapchain before any FfxApiProxy
//    call.
// 5. FfxPlainSwapchain creates the swapchain from the game's own request through the factory and never calls AMD's
//    runtime; the V2 memory query uses its own struct.
// 6. Config, INI load/save, the shipped OptiScaler.ini and the menu carry the opt-out.
// 7. Each plain context is its own record holding a swapchain reference, like AMD's provider ("reference tracked by
//    internal_context"): Create AddRefs after every successful path, Destroy Releases once and only for an owned
//    record. Without it the game's Release freed the swapchain right after creation (6.12).

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

static std::string Function(const std::string& squashed, const std::string& signature)
{
    const auto start = At(squashed, signature);
    const auto next = squashed.find("ffxReturnCode_tffx", start + signature.size());
    return squashed.substr(start, next == std::string::npos ? std::string::npos : next - start);
}

int main()
{
    using namespace FfxSwapchainBlockPolicy;

    // Test 1
    static_assert(ShouldReplaceGameFsrFgSwapchain(true, true, false, false));
    static_assert(!ShouldReplaceGameFsrFgSwapchain(false, true, false, false));
    static_assert(!ShouldReplaceGameFsrFgSwapchain(true, false, false, false));
    static_assert(!ShouldReplaceGameFsrFgSwapchain(true, true, true, false));
    static_assert(!ShouldReplaceGameFsrFgSwapchain(true, true, false, true));
    std::puts("Test 1 PASS: replace only a game's FSR-FG swapchain while NVSmooth30 is loaded");

    // Test 2
    static_assert(IsSwapchainCreation(0x30001u) && IsSwapchainCreation(0x30005u) && IsSwapchainCreation(0x30006u));
    static_assert(!IsSwapchainCreation(0x20001u) && !IsSwapchainCreation(0x30002u));
    static_assert(ShouldRefuseFrameGenerationContext(0x20001u, true));
    static_assert(!ShouldRefuseFrameGenerationContext(0x20001u, false));
    static_assert(!ShouldRefuseFrameGenerationContext(0x30006u, true));
    static_assert(DecideQuery(0x30009u) == QueryAnswer::ZeroMemory);
    static_assert(DecideQuery(0x3000Au) == QueryAnswer::ZeroMemory);
    static_assert(DecideQuery(0x20007u) == QueryAnswer::ZeroMemory);
    static_assert(DecideQuery(0x30003u) == QueryAnswer::NullCommandList);
    static_assert(DecideQuery(0x30004u) == QueryAnswer::NullTexture);
    static_assert(DecideQuery(0x12345u) == QueryAnswer::Unsupported);
    std::puts("Test 2 PASS: descriptor ids, query answers and the FG-context refusal");

    const auto ffx = Squash(ReadFile("OptiScaler/inputs/FfxApi_Dx12.cpp"));

    // Test 3
    {
        const auto fn = Function(ffx, "ffxReturnCode_tffxCreateContext_Dx12(ffxContext*context");
        const auto takeover = At(fn, "if(optiScalerTakesOver){autoresult=ffxCreateContext_Dx12FG(context,desc,memCb);");
        const auto replace =
            At(fn, "if(FfxSwapchainBlockPolicy::ShouldReplaceGameFsrFgSwapchain(FfxSwapchainBlockPolicy::"
                   "IsSwapchainCreation(desc->type),NVSmooth30Loader::LastStatus().DllLoaded,Config::"
                   "Instance()->SmoothMotionAllowGameFsrFgSwapchain.value_or_default(),"
                   "optiScalerTakesOver)){returnFfxPlainSwapchain::Create(context,desc);}");
        const auto refuseFg = At(fn, "if(FfxSwapchainBlockPolicy::ShouldRefuseFrameGenerationContext(desc->type,"
                                     "FfxPlainSwapchain::Active())){");
        const auto original = At(fn, "autoffxApiResult=FfxApiProxy::D3D12_CreateContext(context,desc,memCb);");
        assert(takeover < replace && replace < refuseFg && refuseFg < original);
        assert(fn.find("refused:NVSmooth30SmoothMotionisactive") == std::string::npos);
    }
    std::puts("Test 3 PASS: the takeover stays first; the plain swapchain answers before AMD's runtime");

    // Test 4
    {
        const auto destroy = Function(ffx, "ffxReturnCode_tffxDestroyContext_Dx12(");
        assert(At(destroy, "if(FfxPlainSwapchain::Owns(*context))returnFfxPlainSwapchain::Destroy(context);") <
               At(destroy, "FfxApiProxy::D3D12_DestroyContext("));
        for (const auto& [sig, call] : { std::pair { "ffxReturnCode_tffxConfigure_Dx12(", "Configure" },
                                         std::pair { "ffxReturnCode_tffxQuery_Dx12(", "Query" },
                                         std::pair { "ffxReturnCode_tffxDispatch_Dx12(", "Dispatch" } })
        {
            const auto fn = Function(ffx, sig);
            const auto route = At(fn, std::string("if((context!=nullptr&&FfxPlainSwapchain::Owns(*context))||"
                                                  "FfxPlainSwapchain::Active())returnFfxPlainSwapchain::") +
                                          call + "(desc);");
            assert(route < At(fn, "FfxApiProxy::D3D12_"));
        }
    }
    std::puts("Test 4 PASS: plain-swapchain calls never reach AMD's runtime");

    // Test 5
    {
        const auto plain = Squash(ReadFile("OptiScaler/inputs/FfxPlainSwapchain.cpp"));
        assert(plain.find("factory->CreateSwapChainForHwnd(cDesc->gameQueue,cDesc->hwnd,cDesc->desc,cDesc->"
                          "fullscreenDesc,nullptr,(IDXGISwapChain1**)cDesc->swapchain);") != std::string::npos);
        assert(plain.find("cDesc->dxgiFactory->CreateSwapChain(cDesc->gameQueue,cDesc->desc,(IDXGISwapChain**)cDesc->"
                          "swapchain);") != std::string::npos);
        assert(plain.find("FfxApiProxy::D3D12_") == std::string::npos);
        assert(plain.find("(ffxQueryFrameGenerationSwapChainGetGPUMemoryUsageDX12V2*)desc") != std::string::npos);
        assert(plain.find("replacedbyaplainswapchain") != std::string::npos);
        const auto types = ReadFile("OptiScaler/inputs/FG/FfxApi_Dx12_FG_Types.h");
        assert(types.find("struct ffxQueryFrameGenerationSwapChainGetGPUMemoryUsageDX12V2") != std::string::npos);
        const auto fg = ReadFile("OptiScaler/inputs/FG/FfxApi_Dx12_FG.cpp");
        assert(fg.find("#include \"FfxApi_Dx12_FG_Types.h\"") != std::string::npos);
        assert(fg.find("struct ffxQueryFrameGenerationSwapChainGetGPUMemoryUsageDX12V2\n") == std::string::npos);
    }
    std::puts("Test 5 PASS: the plain swapchain comes from the game's request through the factory");

    // Test 6
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
        const auto menu = Squash(ReadFile("OptiScaler/menu/menu_common.cpp"));
        assert(At(menu, "\"EnableNVSmooth30Unlocker(RTX30)##nv_smooth30\"") <
               At(menu, "\"LetthegameuseitsFSRFGswapchain(restart)##nv_smooth30_fsr\""));
        assert(menu.find("config->SmoothMotionAllowGameFsrFgSwapchain=allowFsrSc;") != std::string::npos);
    }
    std::puts("Test 6 PASS: the opt-out in config, INI and menu");

    // Test 7
    {
        const auto plain = Squash(ReadFile("OptiScaler/inputs/FfxPlainSwapchain.cpp"));
        assert(plain.find("Sentinel") == std::string::npos && plain.find("s_contextTag") == std::string::npos);
        assert(plain.find("structPlainContext{IUnknown*swapchain=nullptr;};") != std::string::npos);
        const auto create = Function(plain, "ffxReturnCode_tCreate(");
        for (const char* path : { "nullptr,(IDXGISwapChain1**)cDesc->swapchain);factory->Release();created=(IUnknown**)"
                                  "cDesc->swapchain;",
                                  "(IDXGISwapChain**)cDesc->swapchain);created=(IUnknown**)cDesc->swapchain;",
                                  "?S_OK:E_INVALIDARG;created=(IUnknown**)cDesc->swapchain;" })
            assert(create.find(path) != std::string::npos);
        const auto guard = At(create, "if(result!=S_OK||created==nullptr||*created==nullptr)");
        const auto addref = At(create, "autorecord=newPlainContext{*created};record->swapchain->AddRef();");
        const auto ok = At(create, "*context=reinterpret_cast<ffxContext>(record);");
        assert(guard < addref && addref < ok);
        const auto destroy = Function(plain, "ffxReturnCode_tDestroy(");
        const auto erase = At(destroy, "if(s_contexts.erase(record)==0)returnFFX_API_RETURN_OK;");
        const auto release = At(destroy, "if(record->swapchain!=nullptr)record->swapchain->Release();deleterecord;");
        assert(erase < release);
        assert(destroy.find("->Release()") == destroy.rfind("->Release()"));
        const auto owns = plain.substr(At(plain, "boolOwns(ffxContextcontext){"), 200);
        assert(owns.find("returns_contexts.contains(reinterpret_cast<PlainContext*>(context));") != std::string::npos);
    }
    std::puts("Test 7 PASS: each plain context holds a swapchain reference until Destroy");

    std::puts("All 7 NVSmooth30 FSR-FG swapchain tests passed.");
    return 0;
}
