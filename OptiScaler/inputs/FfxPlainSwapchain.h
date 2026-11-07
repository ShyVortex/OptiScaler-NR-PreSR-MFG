#pragma once

#include "ffx_api.h"

// Stands in for AMD's FSR-FG swapchain while NVSmooth30 is loaded (FfxSwapchainBlockPolicy.h). The swapchain is created
// through OptiScaler's hooked DXGI factory from the game's own request. The context is an OptiScaler record holding
// its own swapchain reference until Destroy (as AMD's provider does), and every call on it is answered here, never by
// AMD's runtime.
namespace FfxPlainSwapchain
{
// Creates the plain swapchain for a FRAMEGENERATIONSWAPCHAIN FOR_HWND / NEW / WRAP desc.
ffxReturnCode_t Create(ffxContext* context, ffxCreateContextDescHeader* desc);

bool Owns(ffxContext context);

// True while at least one plain swapchain context is alive.
bool Active();

ffxReturnCode_t Destroy(ffxContext* context);
ffxReturnCode_t Configure(ffxConfigureDescHeader* desc);
ffxReturnCode_t Query(ffxQueryDescHeader* desc);
ffxReturnCode_t Dispatch(ffxDispatchDescHeader* desc);
} // namespace FfxPlainSwapchain
