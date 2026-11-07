#pragma once

// A game's FSR 3 frame-generation swapchain presents from AMD's own presenter thread. With NVIDIA Smooth Motion
// unlocked by NVSmooth30 on RTX 30, NvPresent attaches to that path and crashed inside D3D12Core (DLSS Unlocked #42,
// Neverness to Everness creates the swapchain even with its FSR FG setting off). While NVSmooth30 is loaded, OptiScaler
// refuses the game's FSR-FG swapchain context so the game falls back to a plain DXGI swapchain
// (FG-NVSMOOTH30-INTEGRATION.md 6.10).
namespace FfxSwapchainBlockPolicy
{
// isSwapchainCreationType: FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12, _NEW_DX12 or _WRAP_DX12.
// optiScalerTakesOver: OptiScaler's own FSR-FG takeover handles the context (that branch runs first and is unchanged).
constexpr bool ShouldRefuseGameFsrFgSwapchain(bool isSwapchainCreationType, bool nvSmooth30Loaded, bool allowedByUser,
                                              bool optiScalerTakesOver)
{
    return isSwapchainCreationType && nvSmooth30Loaded && !allowedByUser && !optiScalerTakesOver;
}
} // namespace FfxSwapchainBlockPolicy
