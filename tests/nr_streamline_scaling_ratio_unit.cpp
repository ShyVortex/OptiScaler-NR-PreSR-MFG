#include "../OptiScaler/dlssnr/DlssNr_GameNr.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

// Where Winds Meet runs its NR through Streamline's sl.dlss_nr (ShyVortex/OptiScaler-NR-PreSR-MFG#39). On an RTX 30
// the driver disqualifies the game's nvngx_dlssnr.dll ("snippet is using newer GPU arch 1B0 > 170"), so it never calls
// the module's PopulateParameters_Impl. The NGX parameter blocks then lack DLSSNRComputeScalingRatioCallback, and
// sl.dlss_nr stops before CreateFeature(18) with "scaling-ratio computation callback is missing" on every frame.
// Once the game-NR unlock is active, OptiScaler lets the direct runtime publish its callbacks into those blocks.
//
// Validates:
// 1. ShouldPublishCallbacks: only when this session unlocked feature 18 and the block lacks the callback.
// 2. The reporter's case (Windows, result 0x1, AdapterUnsupported, gate open, candidate found) unlocks; a Windows
//    FAIL_OutOfDate still passes through (DLSS Unlocked #60).
// 3. GetFeatureRequirements(18) records the unlock only on Decision::Unlock.
// 4. Both GetCapabilityParameters branches and PopulateParameters_Impl publish the callbacks.
// 5. CompatibilityRuntime::Populate calls the module's optional PopulateParameters_Impl export under the module mutex
//    and CallerScope; the export is not required to open a runtime.
// 6. PublishCallbacks opens the runtime through CompatibilityRuntime::TryOpen, retains it and checks the key.
//
// g++ -std=c++20 -o nr_streamline_scaling_ratio_unit tests/nr_streamline_scaling_ratio_unit.cpp
// ./nr_streamline_scaling_ratio_unit   (from the repository root)

using namespace DlssNr::GameNr;

static constexpr unsigned int AdapterUnsupported = 4;

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

// The body of the function whose (squashed) signature starts at `signature`, braces included.
static std::string Body(const std::string& source, const std::string& signature)
{
    const auto start = source.find(signature);
    assert(start != std::string::npos);
    const auto open = source.find('{', start);
    assert(open != std::string::npos);
    int depth = 0;
    for (size_t i = open; i < source.size(); ++i)
    {
        if (source[i] == '{')
            ++depth;
        else if (source[i] == '}' && --depth == 0)
            return source.substr(open, i - open + 1);
    }
    assert(false);
    return {};
}

static bool Has(const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; }

static void TestPublishRule()
{
    assert(ShouldPublishCallbacks(true, false));
    assert(!ShouldPublishCallbacks(false, false)); // never unlocked: a supported GPU or the setting off
    assert(!ShouldPublishCallbacks(true, true));   // the driver (or an earlier call) already published it
    assert(!ShouldPublishCallbacks(false, true));
    assert(std::string(ScalingRatioCallbackKey) == "DLSSNRComputeScalingRatioCallback");
    std::puts("PASS 1: callbacks are published only when unlocked and missing");
}

static void TestReporterCaseUnlocks()
{
    assert(Decide(false, true, true, false, NgxResultSuccess, AdapterUnsupported) == Decision::Unlock);
    assert(Decide(false, true, true, false, DlssNr::DriverGate::NgxResultOutOfDate, 0) == Decision::PassThrough);
    assert(Decide(false, true, true, false, NgxResultSuccess, 0) == Decision::PassThrough);
    std::puts("PASS 2: an RTX 30 on driver 621.35 unlocks; FAIL_OutOfDate on Windows still passes through");
}

static void TestUnlockIsRecorded()
{
    const auto ngx = Squash(ReadFile("OptiScaler/inputs/NVNGX_DLSS_Dx12.cpp"));
    const auto body = Body(ngx, "staticNVSDK_NGX_ResultGameNrFeatureRequirements(");
    const auto passThrough = body.find("if(decision!=DlssNr::GameNr::Decision::Unlock)returnresult;");
    const auto note = body.find("DlssNr::GameNr::NoteUnlocked();");
    assert(passThrough != std::string::npos && note != std::string::npos && passThrough < note);
    std::puts("PASS 3: GetFeatureRequirements(18) records the unlock only after the pass-through return");
}

static void TestEntryPointsPublish()
{
    const auto ngx = Squash(ReadFile("OptiScaler/inputs/NVNGX_DLSS_Dx12.cpp"));

    const auto caps = Body(ngx, "NVSDK_NGX_APINVSDK_NGX_ResultNVSDK_NGX_D3D12_GetCapabilityParameters(");
    const auto native = caps.find("SetNGXParamAllocType(*(*OutParameters),NGX_AllocTypes::NVDynamic);"
                                  "DlssNr::GameNr::PublishCallbacks(D3D12Device,*OutParameters);"
                                  "returnNVSDK_NGX_Result_Success;");
    const auto custom =
        caps.find("*OutParameters=&params;DlssNr::GameNr::PublishCallbacks(D3D12Device,*OutParameters);");
    assert(native != std::string::npos);
    assert(custom != std::string::npos);

    const auto populate = Body(ngx, "NVSDK_NGX_APINVSDK_NGX_ResultNVSDK_NGX_D3D12_PopulateParameters_Impl(");
    assert(Has(populate, "DlssNr::GameNr::PublishCallbacks(D3D12Device,InParameters);"));
    std::puts("PASS 4: both GetCapabilityParameters branches and PopulateParameters_Impl publish the callbacks");
}

static void TestRuntimePopulate()
{
    const auto runtime = Squash(ReadFile("OptiScaler/dlssnr/DlssNr_CompatibilityRuntime.cpp"));
    const auto populate = Body(runtime, "NVSDK_NGX_ResultCompatibilityRuntime::Populate(");
    const auto lock = populate.find("std::lock_guardlock(module->mutex);");
    const auto scope = populate.find("CallerScopecaller;");
    const auto call = populate.find("returnmodule->populate(params);");
    assert(lock != std::string::npos && scope != std::string::npos && call != std::string::npos);
    assert(lock < scope && scope < call);
    assert(Has(populate, "if(!module->populate)returnNVSDK_NGX_Result_FAIL_FeatureNotSupported;"));

    assert(Has(runtime, "symbol(\"NVSDK_NGX_D3D12_PopulateParameters_Impl\")"));
    const auto required =
        runtime.find("if(!loaded->init||!loaded->create||!loaded->evaluate||!loaded->release||!loaded->shutdown)");
    assert(required != std::string::npos);
    assert(!Has(runtime, "||!loaded->populate")); // optional: older runtimes still open
    std::puts("PASS 5: Populate runs the optional export under the module mutex and CallerScope");
}

static void TestPublishCallbacks()
{
    const auto runtime = Squash(ReadFile("OptiScaler/dlssnr/DlssNr_GameNrRuntime.cpp"));
    const auto body = Body(runtime, "voidPublishCallbacks(ID3D12Device*device,NVSDK_NGX_Parameter*params)");
    assert(Has(body, "if(!params||!Unlocked())return;"));
    assert(Has(body, "params->Get(ScalingRatioCallbackKey,&callback)"));
    assert(Has(body, "if(!ShouldPublishCallbacks(true,hasCallback()))return;"));
    const auto open = body.find("CompatibilityRuntime::TryOpen(device)");
    const auto retain = body.find("RetainLocked(runtime);");
    const auto populate = body.find("runtime->Populate(params)");
    assert(open != std::string::npos && retain != std::string::npos && populate != std::string::npos);
    assert(open < retain && retain < populate);
    assert(Has(Squash(ReadFile("OptiScaler/dlssnr/DlssNr_GameNrRuntime.h")),
               "voidPublishCallbacks(ID3D12Device*device,NVSDK_NGX_Parameter*params);"));
    std::puts("PASS 6: PublishCallbacks opens and retains the direct runtime, then lets it populate the block");
}

int main()
{
    TestPublishRule();
    TestReporterCaseUnlocks();
    TestUnlockIsRecorded();
    TestEntryPointsPublish();
    TestRuntimePopulate();
    TestPublishCallbacks();
    std::puts("All nr_streamline_scaling_ratio tests passed (6/6)");
    return 0;
}
