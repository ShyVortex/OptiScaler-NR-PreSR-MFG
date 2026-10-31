#include "pch.h"
#include "DlssNr_GameNrRuntime.h"
#include "DlssNr_CompatibilityRuntime.h"

#include <Logger.h>
#include <d3d12.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace DlssNr::GameNr
{
namespace
{
std::mutex mutex;
std::unordered_map<const NVSDK_NGX_Handle*, std::shared_ptr<CompatibilityRuntime>> live;
// Read every frame by OptiScaler's own NR gate; mirrors live.size() without taking the lock.
std::atomic<size_t> liveCount { 0 };
// A runtime stays loaded until NGX shuts down: toggling NR in the game must not re-initialise it on the device, and
// work recorded by the game's last create or evaluate may still be executing.
std::vector<std::shared_ptr<CompatibilityRuntime>> retained;

void RetainLocked(const std::shared_ptr<CompatibilityRuntime>& runtime)
{
    if (runtime && std::find(retained.begin(), retained.end(), runtime) == retained.end())
        retained.push_back(runtime);
}
} // namespace

NVSDK_NGX_Result CreateDirect(ID3D12GraphicsCommandList* commands, NVSDK_NGX_Parameter* params,
                              NVSDK_NGX_Handle** handle)
{
    if (!commands || !handle)
        return NVSDK_NGX_Result_Fail;

    ID3D12Device* device = nullptr;
    if (FAILED(commands->GetDevice(IID_PPV_ARGS(&device))) || !device)
    {
        LOG_ERROR("DLSS-NR (game): cannot get the D3D12 device from the command list");
        return NVSDK_NGX_Result_Fail;
    }

    auto runtime = CompatibilityRuntime::TryOpen(device);
    device->Release();
    if (!runtime)
        return NVSDK_NGX_Result_FAIL_FeatureNotSupported;

    *handle = nullptr;
    const auto result = runtime->Create(commands, params, handle);
    LOG_INFO("DLSS-NR (game): direct runtime CreateFeature(18) result=0x{:08X} handle={}", (unsigned int) result,
             (void*) *handle);

    std::lock_guard lock(mutex);
    RetainLocked(runtime);
    if (result != NVSDK_NGX_Result_Success || *handle == nullptr)
        return result == NVSDK_NGX_Result_Success ? NVSDK_NGX_Result_Fail : result;

    live[*handle] = std::move(runtime);
    liveCount.store(live.size(), std::memory_order_release);
    return result;
}

bool Owns(const NVSDK_NGX_Handle* handle)
{
    if (!handle)
        return false;
    std::lock_guard lock(mutex);
    return live.contains(handle);
}

NVSDK_NGX_Result Evaluate(ID3D12GraphicsCommandList* commands, const NVSDK_NGX_Handle* handle,
                          NVSDK_NGX_Parameter* params)
{
    std::shared_ptr<CompatibilityRuntime> runtime;
    {
        std::lock_guard lock(mutex);
        if (auto it = live.find(handle); it != live.end())
            runtime = it->second;
    }
    if (!runtime)
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;

    const auto result = runtime->Evaluate(commands, handle, params);
    if (result != NVSDK_NGX_Result_Success)
        LOG_WARN("DLSS-NR (game): direct runtime EvaluateFeature(18) result=0x{:08X}", (unsigned int) result);
    return result;
}

NVSDK_NGX_Result Release(NVSDK_NGX_Handle* handle)
{
    std::shared_ptr<CompatibilityRuntime> runtime;
    {
        std::lock_guard lock(mutex);
        if (auto it = live.find(handle); it != live.end())
        {
            runtime = std::move(it->second);
            live.erase(it);
            liveCount.store(live.size(), std::memory_order_release);
            RetainLocked(runtime);
        }
    }
    if (!runtime)
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;

    const auto result = runtime->Release(handle);
    LOG_INFO("DLSS-NR (game): direct runtime ReleaseFeature(18) result=0x{:08X}", (unsigned int) result);
    return result;
}

bool Active() { return liveCount.load(std::memory_order_acquire) != 0; }

void Shutdown()
{
    decltype(live) features;
    decltype(retained) runtimes;
    {
        std::lock_guard lock(mutex);
        features.swap(live);
        liveCount.store(0, std::memory_order_release);
        runtimes.swap(retained);
    }

    for (auto& [handle, runtime] : features)
    {
        const auto result = runtime->Release(const_cast<NVSDK_NGX_Handle*>(handle));
        LOG_INFO("DLSS-NR (game): released feature left by the game at shutdown, result=0x{:08X}",
                 (unsigned int) result);
    }
    // features and runtimes go out of scope here; the last owner shuts the runtime down on its device.
}
} // namespace DlssNr::GameNr
