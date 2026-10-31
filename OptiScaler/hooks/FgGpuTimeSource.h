#pragma once

// FGPresent reads the upscaler's GPU time through IFeature::ReadUpscalerTime(void*). D3D11 features and the
// Dx11wDx12 wrapper take an ID3D11DeviceContext; a native D3D12 feature takes an ID3D12CommandQueue. Choosing by
// swapchain interop alone handed a D3D11 context to a native D3D12 feature when a game runs NGX through D3D12 but
// presents through D3D11 (Metro 2033 Redux), which crashed inside d3d11 (FG-DX11WDX12-NATIVE-DX12-GPUTIME-CRASH.md).
// Kept free of Windows headers so the unit test runs the same rule.
namespace FgGpuTimeSource
{
enum class Interop
{
    None,
    Dx11wDx12,
    Other
};

enum class FeatureKind
{
    Dx11,        // API::DX11
    Dx12Native,  // API::DX12 && !IsWithDx12()
    Dx12Wrapper, // API::DX12 && IsWithDx12(): owns its D3D12 queue and ignores the argument
    Other
};

enum class Source
{
    None,
    D3D11Context,
    D3D12Queue
};

constexpr Source Resolve(Interop interop, FeatureKind feature, bool hasD3D11Device, bool hasQueue)
{
    switch (interop)
    {
    case Interop::Dx11wDx12:
        if (feature == FeatureKind::Dx12Native)
            return hasQueue ? Source::D3D12Queue : Source::None;
        if (feature == FeatureKind::Dx11 || feature == FeatureKind::Dx12Wrapper)
            return hasD3D11Device ? Source::D3D11Context : Source::None;
        return Source::None;
    case Interop::None:
        return hasQueue ? Source::D3D12Queue : Source::None;
    default:
        return Source::None;
    }
}
} // namespace FgGpuTimeSource
