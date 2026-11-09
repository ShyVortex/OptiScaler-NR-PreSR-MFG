#pragma once

// ffx-api descriptors newer than the SDK headers in external/FidelityFX-SDK, shared by the FSR-FG takeover
// (FfxApi_Dx12_FG.cpp) and the plain swapchain (FfxPlainSwapchain.cpp).

#include "ffx_api.h"
#include "ffx_api_types.h"

#include <cstdint>

#define FFX_API_QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_GPU_MEMORY_USAGE_DX12_V2 0x3000A
struct ffxQueryFrameGenerationSwapChainGetGPUMemoryUsageDX12V2
{
    ffxQueryDescHeader header;
    void* device; ///< For DX12: pointer to ID3D12Device. App needs to fill out before Query() call.
    struct FfxApiDimensions2D displaySize; ///< App needs to fill out before Query() call.
    uint32_t backBufferFormat; ///< The surface format for the backbuffer. One of the values from FfxApiSurfaceFormat.
                               ///< App needs to fill out before Query() call.
    uint32_t
        backBufferCount; ///< The number of backbuffers in the swapchain. App needs to fill out before Query() call.
    struct FfxApiDimensions2D uiResourceSize; ///< This is the resolution of the resource that will be used for UI
                                              ///< composition. Set to (0,0) if providing null uiResource in
                                              ///< ffxConfigureDescFrameGenerationSwapChainRegisterUiResourceDX12. App
                                              ///< needs to fill out before Query() call.
    uint32_t uiResourceFormat; ///< The surface format for the uiResource. One of the values from FfxApiSurfaceFormat.
                               ///< Set to FFX_API_SURFACE_FORMAT_UNKNOWN(0) if providing null uiResource in
                               ///< ffxConfigureDescFrameGenerationSwapChainRegisterUiResourceDX12. App needs to fill
                               ///< out before Query() call.
    uint32_t flags; ///< Zero or combination of values from FfxApiUiCompositionFlags. App needs to fill out before
                    ///< Query() call.
    struct FfxApiEffectMemoryUsage* gpuMemoryUsageFrameGenerationSwapchain;
};
