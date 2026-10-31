#pragma once

// The present paths read the upscaler's GPU time through IFeature::ReadUpscalerTime(void*). A D3D11 feature takes an
// ID3D11DeviceContext and a native D3D12 feature an ID3D12CommandQueue; the Dx11wDx12 / VkwDx12 wrappers ignore the
// argument. Choosing by swapchain interop handed a D3D11 context to a native D3D12 feature when a game runs NGX
// through D3D12 but presents through D3D11 (Metro 2033 Redux), which crashed inside d3d11
// (FG-DX11WDX12-NATIVE-DX12-GPUTIME-CRASH.md). Kept free of Windows headers so the unit test runs the same rule.
namespace FgGpuTimeSource
{
enum class FeatureKind
{
    Dx11,        // API::DX11
    Dx12Native,  // API::DX12 && !IsWithDx12()
    Dx12Wrapper, // API::DX12 && IsWithDx12(): ignores the argument
    Other
};

enum class Source
{
    None,
    D3D11Context,
    D3D12Queue
};

constexpr FeatureKind Classify(bool isDx11Api, bool isDx12Api, bool isWithDx12)
{
    if (isDx11Api)
        return FeatureKind::Dx11;
    if (!isDx12Api)
        return FeatureKind::Other;
    return isWithDx12 ? FeatureKind::Dx12Wrapper : FeatureKind::Dx12Native;
}

// One read per present: the GPU-time ring buffers advance on every read.
constexpr Source Resolve(FeatureKind feature, bool hasD3D11Device, bool hasQueue)
{
    switch (feature)
    {
    case FeatureKind::Dx12Native:
        return hasQueue ? Source::D3D12Queue : Source::None;
    case FeatureKind::Dx11:
        return hasD3D11Device ? Source::D3D11Context : Source::None;
    case FeatureKind::Dx12Wrapper:
        return hasD3D11Device ? Source::D3D11Context : hasQueue ? Source::D3D12Queue : Source::None;
    default:
        return Source::None;
    }
}
} // namespace FgGpuTimeSource
