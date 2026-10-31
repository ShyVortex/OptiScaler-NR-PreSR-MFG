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

    std::puts("All game-native NR unlock tests passed (6/6).");
    return 0;
}
