#include "../OptiScaler/hooks/FgGpuTimeSource.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

// Metro 2033 Redux runs NGX through D3D12 but presents through D3D11, so OptiScaler uses a Dx11wDx12 swapchain with
// a native D3D12 DLSS feature. FGPresent handed that feature an ID3D11DeviceContext as its ID3D12CommandQueue and
// crashed inside d3d11 once frame generation became active (FG-DX11WDX12-NATIVE-DX12-GPUTIME-CRASH.md).
//
// Validates:
// 1. Dx11wDx12 + native D3D12 feature reads through the D3D12 queue, never the D3D11 context.
// 2. Dx11wDx12 + D3D11 feature or the Dx11wDx12 wrapper keeps the D3D11 context (previous behaviour).
// 3. No interop keeps the D3D12 queue for every feature (previous behaviour).
// 4. Missing device/queue or an unknown interop/feature skips the read.
// 5. FGPresent decides through the helper and classifies features by Api() and IsWithDx12().

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
    static_assert(Resolve(Interop::Dx11wDx12, FeatureKind::Dx12Native, true, true) == Source::D3D12Queue);
    static_assert(Resolve(Interop::Dx11wDx12, FeatureKind::Dx12Native, true, false) == Source::None);
    std::puts("Test 1 PASS: a native D3D12 feature on a Dx11wDx12 swapchain gets the D3D12 queue");

    // Test 2
    static_assert(Resolve(Interop::Dx11wDx12, FeatureKind::Dx11, true, true) == Source::D3D11Context);
    static_assert(Resolve(Interop::Dx11wDx12, FeatureKind::Dx12Wrapper, true, false) == Source::D3D11Context);
    std::puts("Test 2 PASS: D3D11 features and the Dx11wDx12 wrapper keep the D3D11 context");

    // Test 3
    static_assert(Resolve(Interop::None, FeatureKind::Dx12Native, false, true) == Source::D3D12Queue);
    static_assert(Resolve(Interop::None, FeatureKind::Dx12Wrapper, true, true) == Source::D3D12Queue);
    static_assert(Resolve(Interop::None, FeatureKind::Dx11, true, true) == Source::D3D12Queue);
    std::puts("Test 3 PASS: without interop the D3D12 queue is used as before");

    // Test 4
    static_assert(Resolve(Interop::Dx11wDx12, FeatureKind::Dx11, false, true) == Source::None);
    static_assert(Resolve(Interop::Dx11wDx12, FeatureKind::Other, true, true) == Source::None);
    static_assert(Resolve(Interop::None, FeatureKind::Dx12Native, true, false) == Source::None);
    static_assert(Resolve(Interop::Other, FeatureKind::Dx12Native, true, true) == Source::None);
    std::puts("Test 4 PASS: missing objects or unknown kinds skip the read");

    // Test 5
    const auto hooks = Squash(ReadFile("OptiScaler/hooks/FG_Hooks.cpp"));
    assert(hooks.find("#include<hooks/FgGpuTimeSource.h>") != std::string::npos);
    const auto resolve = hooks.find("gts::Resolve(interop,kind,state.currentD3D11Device!=nullptr,"
                                    "state.currentCommandQueue!=nullptr)");
    const auto contextRead = hooks.find("currentFeature->ReadUpscalerTime(context)");
    const auto queueRead = hooks.find("currentFeature->ReadUpscalerTime(state.currentCommandQueue)");
    assert(resolve != std::string::npos && contextRead != std::string::npos && queueRead != std::string::npos);
    assert(resolve < contextRead && resolve < queueRead && "the source is decided before any read");
    assert(hooks.find("if(source==gts::Source::D3D11Context)") != std::string::npos);
    assert(hooks.find("elseif(source==gts::Source::D3D12Queue)") != std::string::npos);
    assert(hooks.find("currentFeature->IsWithDx12()?gts::FeatureKind::Dx12Wrapper:gts::FeatureKind::Dx12Native") !=
           std::string::npos);
    assert(hooks.find("api==API::DX11?gts::FeatureKind::Dx11") != std::string::npos);
    assert(hooks.find("if(state.swapchainInteropApi==SwapchainInteropApi::Dx11wDx12&&state.currentD3D11Device!="
                      "nullptr)") == std::string::npos &&
           "the read is no longer chosen by swapchain interop alone");
    std::puts("Test 5 PASS: FGPresent picks the read by feature type through the helper");

    std::puts("All FG GPU-time source tests passed (5/5).");
    return 0;
}
