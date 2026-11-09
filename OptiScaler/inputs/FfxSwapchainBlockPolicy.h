#pragma once

#include <cstdint>

// A game's FSR 3 frame-generation swapchain presents from AMD's own presenter thread. With NVIDIA Smooth Motion
// unlocked by NVSmooth30 on RTX 30, NvPresent attaches to that path and crashed inside D3D12Core (DLSS Unlocked #42,
// Neverness to Everness creates the swapchain even with its FSR FG setting off). Refusing the request left the game
// with no swapchain at all, so while NVSmooth30 is loaded OptiScaler answers the request with a plain DXGI swapchain
// and handles the game's later calls on that context itself (FfxPlainSwapchain, FG-NVSMOOTH30-INTEGRATION.md 6.11).
namespace FfxSwapchainBlockPolicy
{
// isSwapchainCreationType: FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12, _NEW_DX12 or _WRAP_DX12.
// optiScalerTakesOver: OptiScaler's own FSR-FG takeover handles the context (that branch runs first and is unchanged).
constexpr bool ShouldReplaceGameFsrFgSwapchain(bool isSwapchainCreationType, bool nvSmooth30Loaded, bool allowedByUser,
                                               bool optiScalerTakesOver)
{
    return isSwapchainCreationType && nvSmooth30Loaded && !allowedByUser && !optiScalerTakesOver;
}

// ffx-api descriptor ids (ffx_api_dx12.h / ffx_framegeneration.h)
constexpr uint64_t CreateSwapchainWrap = 0x30001u;
constexpr uint64_t CreateSwapchainNew = 0x30005u;
constexpr uint64_t CreateSwapchainForHwnd = 0x30006u;
constexpr uint64_t CreateFrameGeneration = 0x20001u;
constexpr uint64_t QueryInterpolationCommandList = 0x30003u;
constexpr uint64_t QueryInterpolationTexture = 0x30004u;
constexpr uint64_t QuerySwapchainGpuMemory = 0x30009u;
constexpr uint64_t QuerySwapchainGpuMemoryV2 = 0x3000Au;
constexpr uint64_t QueryFrameGenerationGpuMemory = 0x20007u;

constexpr bool IsSwapchainCreation(uint64_t type)
{
    return type == CreateSwapchainWrap || type == CreateSwapchainNew || type == CreateSwapchainForHwnd;
}

// While a plain swapchain stands in for AMD's, AMD's frame generation can't run: its context needs AMD's swapchain.
constexpr bool ShouldRefuseFrameGenerationContext(uint64_t type, bool plainSwapchainActive)
{
    return plainSwapchainActive && type == CreateFrameGeneration;
}

enum class QueryAnswer
{
    ZeroMemory,      // GPU memory usage: report 0 bytes, OK
    NullCommandList, // interpolation command list: none, error
    NullTexture,     // interpolation texture: none, error
    Unsupported      // anything else: error
};

constexpr QueryAnswer DecideQuery(uint64_t type)
{
    if (type == QuerySwapchainGpuMemory || type == QuerySwapchainGpuMemoryV2 || type == QueryFrameGenerationGpuMemory)
        return QueryAnswer::ZeroMemory;
    if (type == QueryInterpolationCommandList)
        return QueryAnswer::NullCommandList;
    if (type == QueryInterpolationTexture)
        return QueryAnswer::NullTexture;
    return QueryAnswer::Unsupported;
}
} // namespace FfxSwapchainBlockPolicy
