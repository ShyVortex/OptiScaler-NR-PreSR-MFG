#include "pch.h"
#include "FfxPlainSwapchain.h"
#include "FfxSwapchainBlockPolicy.h"

#include "proxies/FfxApi_Proxy.h"
#include "ffx_framegeneration.h"
#include "dx12/ffx_api_dx12.h"
#include "FG/FfxApi_Dx12_FG_Types.h"

#include <atomic>
#include <mutex>
#include <set>

namespace FfxPlainSwapchain
{
namespace
{
// One record per plain context; the context value is the record's address. Like AMD's provider
// (ffx_provider_framegenerationswapchain_dx12.cpp: "reference tracked by internal_context"), the record holds its own
// reference to the swapchain until Destroy: the game releases the extra reference it got, and without this one the
// swapchain was freed right after creation (DLSS Unlocked #42, FG-NVSMOOTH30-INTEGRATION.md 6.12).
struct PlainContext
{
    IUnknown* swapchain = nullptr;
};

std::mutex s_contextsMutex;
std::set<PlainContext*> s_contexts;
std::atomic<int> s_live { 0 };
std::mutex s_logMutex;
std::set<uint64_t> s_loggedTypes;

void LogOnce(const char* what, uint64_t type)
{
    std::lock_guard lock(s_logMutex);
    if (s_loggedTypes.insert(type).second)
        LOG_INFO("FFX plain swapchain: {} {} answered by OptiScaler", what, FfxApiProxy::GetTypeName(type));
}
} // namespace

bool Owns(ffxContext context)
{
    if (context == nullptr)
        return false;

    std::lock_guard lock(s_contextsMutex);
    return s_contexts.contains(reinterpret_cast<PlainContext*>(context));
}

bool Active() { return s_live.load() > 0; }

ffxReturnCode_t Create(ffxContext* context, ffxCreateContextDescHeader* desc)
{
    if (context == nullptr || desc == nullptr)
        return FFX_API_RETURN_ERROR_PARAMETER;

    HRESULT result = E_INVALIDARG;
    IUnknown** created = nullptr;

    if (desc->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12)
    {
        auto cDesc = (ffxCreateContextDescFrameGenerationSwapChainForHwndDX12*) desc;
        if (cDesc->dxgiFactory == nullptr || cDesc->swapchain == nullptr)
            return FFX_API_RETURN_ERROR_PARAMETER;

        IDXGIFactory2* factory = nullptr;
        cDesc->dxgiFactory->QueryInterface(IID_PPV_ARGS(&factory));
        if (factory == nullptr)
            return FFX_API_RETURN_ERROR_PARAMETER;

        result = factory->CreateSwapChainForHwnd(cDesc->gameQueue, cDesc->hwnd, cDesc->desc, cDesc->fullscreenDesc,
                                                 nullptr, (IDXGISwapChain1**) cDesc->swapchain);
        factory->Release();
        created = (IUnknown**) cDesc->swapchain;
    }
    else if (desc->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_NEW_DX12)
    {
        auto cDesc = (ffxCreateContextDescFrameGenerationSwapChainNewDX12*) desc;
        if (cDesc->dxgiFactory == nullptr || cDesc->swapchain == nullptr)
            return FFX_API_RETURN_ERROR_PARAMETER;

        result =
            cDesc->dxgiFactory->CreateSwapChain(cDesc->gameQueue, cDesc->desc, (IDXGISwapChain**) cDesc->swapchain);
        created = (IUnknown**) cDesc->swapchain;
    }
    else if (desc->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WRAP_DX12)
    {
        // The game's own swapchain stays as it is: nothing to wrap it in.
        auto cDesc = (ffxCreateContextDescFrameGenerationSwapChainWrapDX12*) desc;
        result = (cDesc->swapchain != nullptr && *cDesc->swapchain != nullptr) ? S_OK : E_INVALIDARG;
        created = (IUnknown**) cDesc->swapchain;
    }

    if (result != S_OK || created == nullptr || *created == nullptr)
    {
        LOG_ERROR("FFX plain swapchain: creating {} failed: {:X}", FfxApiProxy::GetTypeName(desc->type), (UINT) result);
        return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    }

    auto record = new PlainContext { *created };
    record->swapchain->AddRef(); // reference tracked by the context, released in Destroy

    {
        std::lock_guard lock(s_contextsMutex);
        s_contexts.insert(record);
    }

    *context = reinterpret_cast<ffxContext>(record);
    s_live++;
    LOG_WARN("FSR-FG swapchain ({}) replaced by a plain swapchain: NVSmooth30 Smooth Motion is active",
             FfxApiProxy::GetTypeName(desc->type));
    return FFX_API_RETURN_OK;
}

ffxReturnCode_t Destroy(ffxContext* context)
{
    if (context == nullptr || *context == nullptr)
        return FFX_API_RETURN_OK;

    auto record = reinterpret_cast<PlainContext*>(*context);
    {
        std::lock_guard lock(s_contextsMutex);
        if (s_contexts.erase(record) == 0)
            return FFX_API_RETURN_OK;
    }

    // Drop the context's own reference; the game keeps or releases its own.
    if (record->swapchain != nullptr)
        record->swapchain->Release();
    delete record;

    if (s_live.load() > 0)
        s_live--;
    *context = nullptr;
    LOG_INFO("FFX plain swapchain: context destroyed");

    return FFX_API_RETURN_OK;
}

ffxReturnCode_t Configure(ffxConfigureDescHeader* desc)
{
    // No frame generation: frame-generation settings, key-values, UI and distortion resources are ignored.
    LogOnce("configure", desc->type);
    return FFX_API_RETURN_OK;
}

ffxReturnCode_t Query(ffxQueryDescHeader* desc)
{
    using FfxSwapchainBlockPolicy::QueryAnswer;

    LogOnce("query", desc->type);

    switch (FfxSwapchainBlockPolicy::DecideQuery(desc->type))
    {
    case QueryAnswer::ZeroMemory:
    {
        FfxApiEffectMemoryUsage* usage = nullptr;
        if (desc->type == FFX_API_QUERY_DESC_TYPE_FRAMEGENERATION_GPU_MEMORY_USAGE)
            usage = ((ffxQueryDescFrameGenerationGetGPUMemoryUsage*) desc)->gpuMemoryUsageFrameGeneration;
        else if (desc->type == FFX_API_QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_GPU_MEMORY_USAGE_DX12_V2)
            usage = ((ffxQueryFrameGenerationSwapChainGetGPUMemoryUsageDX12V2*) desc)
                        ->gpuMemoryUsageFrameGenerationSwapchain;
        else
            usage =
                ((ffxQueryFrameGenerationSwapChainGetGPUMemoryUsageDX12*) desc)->gpuMemoryUsageFrameGenerationSwapchain;

        if (usage != nullptr)
        {
            usage->totalUsageInBytes = 0;
            usage->aliasableUsageInBytes = 0;
        }
        return FFX_API_RETURN_OK;
    }
    case QueryAnswer::NullCommandList:
    {
        auto cDesc = (ffxQueryDescFrameGenerationSwapChainInterpolationCommandListDX12*) desc;
        if (cDesc->pOutCommandList != nullptr)
            *cDesc->pOutCommandList = nullptr;
        return FFX_API_RETURN_ERROR_PARAMETER;
    }
    case QueryAnswer::NullTexture:
    {
        auto cDesc = (ffxQueryDescFrameGenerationSwapChainInterpolationTextureDX12*) desc;
        if (cDesc->pOutTexture != nullptr)
            *cDesc->pOutTexture = {};
        return FFX_API_RETURN_ERROR_PARAMETER;
    }
    default:
        return FFX_API_RETURN_ERROR_PARAMETER;
    }
}

ffxReturnCode_t Dispatch(ffxDispatchDescHeader* desc)
{
    // WAIT_FOR_PRESENTS has nothing to wait for; frame-generation dispatches have nothing to run.
    LogOnce("dispatch", desc->type);
    return FFX_API_RETURN_OK;
}
} // namespace FfxPlainSwapchain
