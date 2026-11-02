#include "../OptiScaler/dlssnr/DlssNr_GameNr.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

// Metro 2033 Redux asks NGX for feature 18 (Neural Rendering) and hides its option when the driver refuses it for
// the GPU. The fork unlocks it on Windows and Linux, except on Windows drivers without NR support, where forcing the
// direct runtime hung the GPU (DLSS Unlocked #60).
//
// Validates:
// 1. Windows with a capable driver and an unsupported GPU (AdapterUnsupported) is unlocked.
// 2. Windows without NR in the driver (FAIL_OutOfDate, DriverVersionUnsupported, below the 610.88 gate) passes through.
// 3. Wine is unlocked for any driver answer, including FAIL_OutOfDate.
// 4. A driver that already supports NR, a disabled setting, or no nvngx_dlssnr.dll pass through.
// 5. Support bits are ignored when the query itself failed.
// 6. GetFeatureRequirements routes feature 18 through the rule, before the generic driver query, and the setting is
//    loaded, saved and documented.
// 7. CreateFeature(18) uses the direct runtime only after the driver produced no feature and the rule unlocks it.
// 8. Create, evaluate, release and shutdown route the game's NR handles to the direct runtime: by handle pointer,
//    before id-based routing, and releasing one never touches frame generation.
// 9. The game's NR wins: OptiScaler's own NR on DX12 runs only through DlssNr::OwnNrEnabled() (enabled and no live
//    game NR feature) and the gate reads a lock-free counter.
// 10. The Windows-only harnesses that compile production slices provide the new seams.
// 11. When the game's NR feature is created, OptiScaler's NR is turned off for the session (volatile, the saved
//     setting kept); while it runs the "Enable Neural Rendering" checkbox is disabled with a yellow explanation and
//     the NR hotkey cannot turn it back on, so OptiScaler's NR never resumes on its own.
// 12. "Preload NR" (default off) loads the direct runtime at the game's NGX D3D12 init after the driver init
//     succeeded, never on a Windows driver below the 610.88 gate, and keeps it until NGX shutdown.
// 13. After a successful create, the game's NR model settings are logged read-only, naming the model the preset hint
//     selects (unset/0/2 -> Optimized, 1 -> Original).

using namespace DlssNr::GameNr;

static constexpr unsigned int Success = NgxResultSuccess;
static constexpr unsigned int OutOfDate = DlssNr::DriverGate::NgxResultOutOfDate;
static constexpr unsigned int FeatureNotSupported = 0xBAD00004; // NVSDK_NGX_Result_FAIL_FeatureNotSupported
static constexpr unsigned int AdapterUnsupported = 4;
static constexpr unsigned int DriverVersionUnsupported = 2;

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

int main()
{
    // Decide(onWine, enabled, hasCandidate, driverBlocked, driverResult, supportBits)

    // Test 1
    static_assert(Decide(false, true, true, false, Success, AdapterUnsupported) == Decision::Unlock);
    static_assert(Decide(false, true, true, false, FeatureNotSupported, AdapterUnsupported) == Decision::Unlock);
    std::puts("Test 1 PASS: Windows unsupported GPU on a capable driver is unlocked");

    // Test 2
    static_assert(Decide(false, true, true, false, OutOfDate, 0) == Decision::PassThrough);
    static_assert(Decide(false, true, true, false, Success, DriverVersionUnsupported) == Decision::PassThrough);
    static_assert(Decide(false, true, true, false, Success, DriverVersionUnsupported | AdapterUnsupported) ==
                  Decision::PassThrough);
    static_assert(Decide(false, true, true, true, Success, AdapterUnsupported) == Decision::PassThrough);
    std::puts("Test 2 PASS: Windows drivers without NR support are never unlocked");

    // Test 3
    static_assert(Decide(true, true, true, false, OutOfDate, 0) == Decision::Unlock);
    static_assert(Decide(true, true, true, false, Success, DriverVersionUnsupported) == Decision::Unlock);
    static_assert(Decide(true, true, true, false, FeatureNotSupported, AdapterUnsupported) == Decision::Unlock);
    std::puts("Test 3 PASS: Wine is unlocked for any driver answer");

    // Test 4
    static_assert(Decide(false, true, true, false, Success, 0) == Decision::PassThrough);
    static_assert(Decide(true, true, true, false, Success, 0) == Decision::PassThrough);
    static_assert(Decide(false, false, true, false, Success, AdapterUnsupported) == Decision::PassThrough);
    static_assert(Decide(true, false, true, false, OutOfDate, 0) == Decision::PassThrough);
    static_assert(Decide(false, true, false, false, Success, AdapterUnsupported) == Decision::PassThrough);
    static_assert(Decide(true, true, false, false, OutOfDate, 0) == Decision::PassThrough);
    std::puts("Test 4 PASS: native support, a disabled setting or a missing runtime pass through");

    // Test 5
    static_assert(!DriverLacksNr(FeatureNotSupported, DriverVersionUnsupported));
    static_assert(!DriverSupports(FeatureNotSupported, 0));
    std::puts("Test 5 PASS: support bits only count when the query succeeded");

    // Test 6
    const auto ngx = Squash(ReadFile("OptiScaler/inputs/NVNGX_DLSS_Dx12.cpp"));
    assert(ngx.find("#include<dlssnr/DlssNr_GameNr.h>") != std::string::npos);
    const auto exportAt = ngx.find("NVSDK_NGX_APINVSDK_NGX_ResultNVSDK_NGX_D3D12_GetFeatureRequirements(");
    assert(exportAt != std::string::npos);
    const auto exported = ngx.substr(exportAt, ngx.find("staticNVSDK_NGX_ResultTryEvaluateOptiFeature") - exportAt);
    const auto route =
        exported.find("if(FeatureDiscoveryInfo->FeatureID==(NVSDK_NGX_Feature)DlssNr::GameNr::FeatureId)returnGameNr"
                      "FeatureRequirements(Adapter,FeatureDiscoveryInfo,OutSupported);");
    const auto generic = exported.find("NVNGXProxy::D3D12_GetFeatureRequirements()(Adapter");
    assert(route != std::string::npos && generic != std::string::npos && route < generic &&
           "feature 18 is decided before the generic driver query");
    const auto helper = ngx.substr(ngx.find("staticNVSDK_NGX_ResultGameNrFeatureRequirements("), exportAt);
    assert(helper.find("DlssNr::GameNr::Decide(State::Instance().isRunningOnLinux,Config::Instance()->"
                       "DlssNrUnlockGameNr.value_or_default(),!DlssNr::CompatibilityRuntime::CandidatePaths().empty(),"
                       "DlssNr::DriverGate::Blocked(),") != std::string::npos);
    assert(helper.find("if(decision!=DlssNr::GameNr::Decision::Unlock)returnresult;") != std::string::npos &&
           "anything but Unlock returns the driver's answer unchanged");

    const auto config = Squash(ReadFile("OptiScaler/Config.cpp"));
    assert(config.find("DlssNrUnlockGameNr.set_from_config(readBool(\"DlssNr\",\"UnlockGameNR\"));") !=
           std::string::npos);
    assert(config.find("ini.SetValue(\"DlssNr\",\"UnlockGameNR\",") != std::string::npos);
    assert(Squash(ReadFile("OptiScaler/Config.h")).find("CustomOptional<bool>DlssNrUnlockGameNr{true};") !=
           std::string::npos);
    assert(ReadFile("OptiScaler.ini").find("UnlockGameNR=auto") != std::string::npos);
    std::puts("Test 6 PASS: feature 18 goes through the rule; the setting is loaded, saved and documented");

    // Test 7 - ShouldCreateDirect(onWine, enabled, hasCandidate, driverBlocked, createResult, gotHandle)
    static_assert(ShouldCreateDirect(true, true, true, false, OutOfDate, false));
    static_assert(ShouldCreateDirect(false, true, true, false, FeatureNotSupported, false));
    static_assert(!ShouldCreateDirect(false, true, true, false, OutOfDate, false), "Windows OutOfDate stays refused");
    static_assert(!ShouldCreateDirect(false, true, true, true, FeatureNotSupported, false), "below the 610.88 gate");
    static_assert(!ShouldCreateDirect(true, true, true, false, Success, true), "the driver created it");
    static_assert(!ShouldCreateDirect(true, true, true, false, OutOfDate, true), "a handle came back");
    static_assert(!ShouldCreateDirect(true, false, true, false, OutOfDate, false), "setting off");
    static_assert(!ShouldCreateDirect(true, true, false, false, OutOfDate, false), "no nvngx_dlssnr.dll");
    std::puts("Test 7 PASS: the direct runtime is used only after a refused create the rule unlocks");

    // Test 8
    assert(ngx.find("#include<dlssnr/DlssNr_GameNrRuntime.h>") != std::string::npos);
    const auto failed = ngx.find("LOG_INFO(\"NativeCreateFeaturefailed:0x{:X}\",(uint32_t)res);");
    const auto direct = ngx.find("returnDlssNr::GameNr::CreateDirect(InCmdList,effectiveParameters,OutHandle);");
    assert(failed != std::string::npos && direct != std::string::npos && failed < direct);
    assert(ngx.find("DlssNr::GameNr::ShouldCreateDirect(state.isRunningOnLinux,cfg.DlssNrUnlockGameNr."
                    "value_or_default(),") != std::string::npos);

    const auto releaseAt = ngx.find("NVSDK_NGX_APINVSDK_NGX_ResultNVSDK_NGX_D3D12_ReleaseFeature(");
    const auto releaseRoute = ngx.find("if(DlssNr::GameNr::Owns(InHandle))returnDlssNr::GameNr::Release(InHandle);");
    const auto fgCleanup = ngx.find("//Cleanupframegen", releaseAt);
    assert(releaseAt < releaseRoute && releaseRoute < fgCleanup && "game NR release never reaches the FG cleanup");

    const auto evaluateAt = ngx.find("NVSDK_NGX_APINVSDK_NGX_ResultNVSDK_NGX_D3D12_EvaluateFeature(");
    const auto evaluateRoute = ngx.find("if(DlssNr::GameNr::Owns(InFeatureHandle))returnDlssNr::GameNr::Evaluate("
                                        "InCmdList,InFeatureHandle,InParameters);");
    const auto idLookup = ngx.find("HandleToFeature.Read(handleId)", evaluateAt);
    assert(evaluateAt < evaluateRoute && evaluateRoute < idLookup && "game NR evaluate precedes id-based routing");

    const auto shutdownAt = ngx.find("staticNVSDK_NGX_ResultShutdownDx12(");
    const auto gameShutdown = ngx.find("DlssNr::GameNr::Shutdown();", shutdownAt);
    const auto contextsCleared = ngx.find("Dx12Contexts.clear();", shutdownAt);
    assert(gameShutdown != std::string::npos && gameShutdown < contextsCleared);

    const auto runtime = Squash(ReadFile("OptiScaler/dlssnr/DlssNr_GameNrRuntime.cpp"));
    assert(runtime.find("std::unordered_map<constNVSDK_NGX_Handle*,std::shared_ptr<CompatibilityRuntime>>live;") !=
               std::string::npos &&
           "handles are keyed by pointer");
    assert(runtime.find("autoruntime=CompatibilityRuntime::TryOpen(device);") != std::string::npos);
    const auto releaseBody = runtime.substr(runtime.find("NVSDK_NGX_ResultRelease(NVSDK_NGX_Handle*handle)"));
    assert(releaseBody.find("RetainLocked(runtime);") < releaseBody.find("runtime->Release(handle)") &&
           "a released feature's runtime stays loaded until NGX shutdown");
    std::puts("Test 8 PASS: the game's NR handles are routed to the direct runtime ahead of id-based paths");

    // Test 9
    const auto ownNr = Squash(ReadFile("OptiScaler/dlssnr/DlssNr_OwnNr.h"));
    assert(ownNr.find("inlineboolOwnNrEnabled(){returnConfig::Instance()->DlssNrEnabled.value_or_default()&&!GameNr::"
                      "Active();}") != std::string::npos);
    assert(runtime.find("boolActive(){returnliveCount.load(std::memory_order_acquire)!=0;}") != std::string::npos &&
           "the per-frame gate takes no lock");
    for (const char* path :
         { "OptiScaler/dlssnr/DlssNr_StreamlinePicture.cpp", "OptiScaler/dlssnr/DlssNr_Pipeline_Dx12.cpp",
           "OptiScaler/shaders/dlssnr/DlssNr_Dx12_FinishedQueue.cpp",
           "OptiScaler/shaders/dlssnr/DlssNr_Dx12_FinishedCompose.cpp",
           "OptiScaler/shaders/dlssnr/DlssNr_Dx12_Evaluate.cpp", "OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp",
           "OptiScaler/shaders/dlssnr/DlssNr_Dx12_Hold.cpp", "OptiScaler/upscalers/IFeature_Dx12.cpp" })
    {
        const auto source = ReadFile(path);
        if (source.find("DlssNrEnabled.value_or_default()") != std::string::npos ||
            source.find("#include <dlssnr/DlssNr_OwnNr.h>") == std::string::npos)
        {
            std::fprintf(stderr, "%s reads DlssNrEnabled directly instead of DlssNr::OwnNrEnabled()\n", path);
            return 1;
        }
    }
    std::puts("Test 9 PASS: OptiScaler's own DX12 NR yields to the game's NR");

    // Test 10
    assert(Squash(ReadFile("tests/nr_streamline_hooks.cpp")).find("inlineboolOwnNrEnabled()") != std::string::npos);
    assert(Squash(ReadFile("tests/nr_private_seam/PrivateSeamTests.cpp")).find("inlineboolOwnNrEnabled()") !=
           std::string::npos);
    assert(Squash(ReadFile("tests/nr_shutdown_smoke.cpp")).find("namespaceGameNr{voidShutdown(){}}") !=
           std::string::npos);
    std::puts("Test 10 PASS: Windows harnesses provide the OwnNrEnabled / GameNr::Shutdown seams");

    // Test 11
    const auto create = runtime.substr(runtime.find("NVSDK_NGX_ResultCreateDirect("),
                                       runtime.find("boolOwns(") - runtime.find("NVSDK_NGX_ResultCreateDirect("));
    const auto failReturn = create.find("returnresult==NVSDK_NGX_Result_Success?NVSDK_NGX_Result_Fail:result;");
    const auto recorded = create.find("live[*handle]=std::move(runtime);");
    const auto forcedOff = create.find("Config::Instance()->DlssNrEnabled.set_volatile_value(false);");
    assert(failReturn != std::string::npos && recorded != std::string::npos && forcedOff != std::string::npos);
    assert(failReturn < recorded && recorded < forcedOff &&
           "OptiScaler's NR is turned off only after a successful create");
    assert(runtime.find("DlssNrEnabled=") == std::string::npos && "never a saved assignment");

    const auto menu = Squash(ReadFile("OptiScaler/dlssnr/DlssNr_Menu.cpp"));
    assert(menu.find("constboolgameNr=GameNr::Active();") != std::string::npos);
    assert(menu.find("ImGui::TextWrapped(\"%s\",driverBlocked?driverMessage.c_str():GameNrActiveMessage);") !=
           std::string::npos);
    const auto yellow = menu.find("ImVec4(textColor.x,textColor.y*0.8f,0.0f,textColor.w));ImGui::TextWrapped(\"%s\","
                                  "driverBlocked?driverMessage.c_str():GameNrActiveMessage);");
    assert(yellow != std::string::npos && "the explanation uses the driver-gate yellow");
    assert(menu.find("constboolenableLocked=driverBlocked||gameNr;") != std::string::npos);
    assert(menu.find("ImGui::BeginDisabled(enableLocked);") != std::string::npos);
    assert(menu.find("&enabled,toggleWidth)&&!enableLocked)") != std::string::npos &&
           "the checkbox never writes DlssNrEnabled while locked");
    assert(menu.find("GameNrPausedMessage") == std::string::npos && "no auto-resume wording left");

    const auto common = Squash(ReadFile("OptiScaler/menu/menu_common.cpp"));
    const auto hotkeyGuard = common.find("if(inputDlssNr&&DlssNr::GameNr::Active())");
    const auto toggle = common.find("config->DlssNrEnabled=!config->DlssNrEnabled.value_or_default();");
    assert(hotkeyGuard != std::string::npos && toggle != std::string::npos && hotkeyGuard < toggle);
    std::puts("Test 11 PASS: the game's NR turns OptiScaler's off for the session and locks the checkbox and hotkey");

    // Test 12
    static_assert(ShouldPreload(true, true, false));
    static_assert(!ShouldPreload(false, true, false), "opt-in");
    static_assert(!ShouldPreload(true, false, false), "no nvngx_dlssnr.dll");
    static_assert(!ShouldPreload(true, true, true), "never below the 610.88 gate");
    assert(Squash(ReadFile("OptiScaler/Config.h")).find("CustomOptional<bool>DlssNrPreload{false};") !=
           std::string::npos);
    assert(config.find("DlssNrPreload.set_from_config(readBool(\"DlssNr\",\"Preload\"));") != std::string::npos);
    assert(config.find("ini.SetValue(\"DlssNr\",\"Preload\",") != std::string::npos);
    assert(ReadFile("OptiScaler.ini").find("Preload=auto") != std::string::npos);
    assert(menu.find("PipelineUi::CheckboxWrapped(\"PreloadNR(restart)\",&preload,toggleWidth))config->DlssNrPreload="
                     "preload;HelpMarker(") != std::string::npos);

    const auto initAt = ngx.find("NVSDK_NGX_APINVSDK_NGX_ResultNVSDK_NGX_D3D12_Init_Ext(");
    const auto inited = ngx.find("NVNGXProxy::SetDx12Inited(true);", initAt);
    const auto preloadCall = ngx.find("DlssNr::GameNr::Preload(InDevice);", initAt);
    const auto preloadRule =
        ngx.find("DlssNr::GameNr::ShouldPreload(Config::Instance()->DlssNrPreload.value_or_default(),"
                 "!DlssNr::CompatibilityRuntime::CandidatePaths().empty(),DlssNr::DriverGate::"
                 "Blocked()))",
                 initAt);
    assert(inited != std::string::npos && preloadRule != std::string::npos && preloadCall != std::string::npos);
    assert(inited < preloadRule && preloadRule < preloadCall && "preload only after the driver init succeeded");
    assert(ngx.substr(initAt, preloadCall - initAt).find("if(result==NVSDK_NGX_Result_Success){") != std::string::npos);

    const auto preloadBody = runtime.substr(runtime.find("voidPreload(ID3D12Device*device)"));
    assert(preloadBody.find("autoruntime=CompatibilityRuntime::TryOpen(device);") <
               preloadBody.find("RetainLocked(runtime);") &&
           "the preloaded runtime is kept until NGX shutdown");
    std::puts("Test 12 PASS: Preload NR loads the runtime at NGX init only when allowed and keeps it");

    // Test 13
    assert(ModelName(std::nullopt) == "Optimized (Small FP8)");
    assert(ModelName(0u) == "Optimized (Small FP8)");
    assert(ModelName(2u) == "Optimized (Small FP8)");
    assert(ModelName(1u) == "Original (Honest Tench)");
    assert(ModelName(7u) == "unknown preset 7");
    const auto logCall = create.find("LogGameSettings(params);");
    assert(logCall != std::string::npos && failReturn < logCall && "logged only after a successful create");
    assert(runtime.find("Read<unsignedint>(params,\"DLSSNR.Hint.Render.Preset\")") != std::string::npos);
    assert(runtime.find("params->Set(") == std::string::npos && "the game's parameters are only read");
    std::puts("Test 13 PASS: the game's NR settings are logged read-only with the selected model");

    std::puts("All game-native NR unlock tests passed (13/13).");
    return 0;
}
