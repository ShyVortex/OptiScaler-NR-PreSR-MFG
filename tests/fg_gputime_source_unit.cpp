#include "../OptiScaler/hooks/FgGpuTimeSource.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

// Metro 2033 Redux runs NGX through D3D12 but presents through D3D11, so OptiScaler uses a Dx11wDx12 swapchain with
// a native D3D12 DLSS feature. LocalPresent (wrapped_swapchain.cpp) and FGPresent (FG_Hooks.cpp) handed that feature
// an ID3D11DeviceContext as its ID3D12CommandQueue, which crashed inside d3d11 once the game created DLSS after the
// intro (FG-DX11WDX12-NATIVE-DX12-GPUTIME-CRASH.md).
//
// Validates:
// 1. A native D3D12 feature reads through the D3D12 queue only, never the D3D11 context.
// 2. A D3D11 feature reads through the D3D11 context only, never a D3D12 queue.
// 3. The wrappers, which ignore the argument, keep reading with whatever object is available.
// 4. Classify() maps Api()/IsWithDx12() to the right kind; unknown kinds skip the read.
// 5. FGPresent decides through the helper.
// 6. LocalPresent decides through the helper, reads once per present, and no longer has the unconditional
//    Dx11wDx12 D3D11-context read.
// 7. Metro Redux (metro.exe) forces borderless with XeFG only: it uses exclusive fullscreen, where XeSS-FG does not
//    interpolate (confirmed in game with [XeMFG] ForceBorderless=true).

using namespace FgGpuTimeSource;

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
    // Test 1
    static_assert(Resolve(FeatureKind::Dx12Native, true, true) == Source::D3D12Queue);
    static_assert(Resolve(FeatureKind::Dx12Native, true, false) == Source::None);
    static_assert(Resolve(FeatureKind::Dx12Native, false, false) == Source::None);
    std::puts("Test 1 PASS: a native D3D12 feature only ever gets the D3D12 queue");

    // Test 2
    static_assert(Resolve(FeatureKind::Dx11, true, true) == Source::D3D11Context);
    static_assert(Resolve(FeatureKind::Dx11, false, true) == Source::None);
    std::puts("Test 2 PASS: a D3D11 feature only ever gets the D3D11 context");

    // Test 3
    static_assert(Resolve(FeatureKind::Dx12Wrapper, true, false) == Source::D3D11Context);
    static_assert(Resolve(FeatureKind::Dx12Wrapper, true, true) == Source::D3D11Context);
    static_assert(Resolve(FeatureKind::Dx12Wrapper, false, true) == Source::D3D12Queue);
    static_assert(Resolve(FeatureKind::Dx12Wrapper, false, false) == Source::None);
    std::puts("Test 3 PASS: the wrappers keep reading with the available object");

    // Test 4
    static_assert(Classify(true, false, false) == FeatureKind::Dx11);
    static_assert(Classify(false, true, false) == FeatureKind::Dx12Native);
    static_assert(Classify(false, true, true) == FeatureKind::Dx12Wrapper);
    static_assert(Classify(false, false, false) == FeatureKind::Other);
    static_assert(Resolve(FeatureKind::Other, true, true) == Source::None);
    std::puts("Test 4 PASS: features are classified by Api() and IsWithDx12()");

    // Test 5
    const auto hooks = Squash(ReadFile("OptiScaler/hooks/FG_Hooks.cpp"));
    assert(hooks.find("#include<hooks/FgGpuTimeSource.h>") != std::string::npos);
    const auto fgResolve = hooks.find("gts::Resolve(kind,hasD3D11Device,state.currentCommandQueue!=nullptr)");
    const auto fgContext = hooks.find("currentFeature->ReadUpscalerTime(context)");
    const auto fgQueue = hooks.find("currentFeature->ReadUpscalerTime(state.currentCommandQueue)");
    assert(fgResolve != std::string::npos && fgContext != std::string::npos && fgQueue != std::string::npos);
    assert(fgResolve < fgContext && fgResolve < fgQueue && "the source is decided before any read");
    assert(hooks.find("if(source==gts::Source::D3D11Context)") != std::string::npos);
    assert(hooks.find("elseif(source==gts::Source::D3D12Queue)") != std::string::npos);
    std::puts("Test 5 PASS: FGPresent picks the read through the helper");

    // Test 6
    const auto swapchain = Squash(ReadFile("OptiScaler/wrapped/wrapped_swapchain.cpp"));
    assert(swapchain.find("#include<hooks/FgGpuTimeSource.h>") != std::string::npos);
    const auto block = swapchain.substr(swapchain.find("//UpscalerGPUtimecomputation"),
                                        swapchain.find("//FallbackwhenFGPresentisnothookedforV-sync") -
                                            swapchain.find("//UpscalerGPUtimecomputation"));
    assert(block.find("gts::Resolve(kind,d3d11Device!=nullptr,cq!=nullptr)") != std::string::npos);
    assert(block.find("if(source==gts::Source::D3D12Queue)") != std::string::npos);
    assert(block.find("elseif(source==gts::Source::D3D11Context)") != std::string::npos);
    size_t reads = 0;
    for (auto at = block.find("ReadUpscalerTime("); at != std::string::npos;
         at = block.find("ReadUpscalerTime(", at + 1))
        ++reads;
    assert(reads == 2 && "one queue read and one context read, in exclusive branches");
    assert(block.find("State::Instance().currentD3D11Device->GetImmediateContext") == std::string::npos &&
           "the unconditional Dx11wDx12 context read is gone");
    std::puts("Test 6 PASS: LocalPresent reads once, with the object the feature expects");

    // Test 7
    const auto quirks = Squash(ReadFile("OptiScaler/misc/Quirks.h"));
    assert(quirks.find("QUIRK_ENTRY(\"metro.exe\",GameQuirk::ForceBorderlessWhenUsingXeFG),") != std::string::npos &&
           "metro.exe carries only the borderless-with-XeFG quirk");
    std::puts("Test 7 PASS: Metro Redux forces borderless only when XeFG is used");

    std::puts("All FG GPU-time source tests passed (7/7).");
    return 0;
}
