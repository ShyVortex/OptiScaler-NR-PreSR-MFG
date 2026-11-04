#pragma once

#include "SysUtils.h"
#include <Config.h>
#include <State.h>
#include <Util.h>

#include <framegen/dlssg/AmpereMfgLoader.h>
#include <hooks/GameXeFGBlockPolicy.h>
#include <proxies/KernelBase_Proxy.h>

#include <xefg_swapchain.h>
#include <xefg_swapchain_d3d12.h>

#include "detours/detours.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string_view>

// Keeps a game's own XeSS-FG off the swapchain while the external RTX 20/30 unlocker drives DLSS-G
// (FG-LSA-EXTERNAL-MFG-NO-FEATURE-CREATE.md). Only the game's library is touched: OptiScaler's own libxess_fg.dll in
// its own folder is never hooked, and nothing here runs outside External FG mode.
class GameXeFGHooks
{
  public:
    // Called after the original loader loaded `path` (LibraryLoadHooks::OnOriginalLoad) and for an XeSS-FG library
    // that is already in the process (LibraryLoadHooks::CheckModulesInMemory).
    static void OnLibraryLoaded(std::wstring_view path, HMODULE module)
    {
        if (module == nullptr || !GameXeFGBlockPolicy::IsXeFGLibrary(path) || !BlockActive())
            return;

        std::scoped_lock lock(_mutex);

        if (_hookedModule == module)
            return;

        if (IsOptiScalerOwnCopy(module))
            return;

        if (_hookedModule != nullptr)
            Unhook();

        auto createContext = reinterpret_cast<decltype(&xefgSwapChainD3D12CreateContext)>(
            KernelBaseProxy::GetProcAddress_()(module, "xefgSwapChainD3D12CreateContext"));

        if (createContext == nullptr)
        {
            LOG_WARN("External FG: {} has no xefgSwapChainD3D12CreateContext, game XeSS-FG not blocked",
                     wstring_to_string(std::wstring(path)));
            return;
        }

        o_CreateContext = createContext;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) o_CreateContext, hkCreateContext);

        if (const auto result = DetourTransactionCommit(); result != NO_ERROR)
        {
            LOG_ERROR("External FG: hooking the game's XeSS-FG failed: {:X}", result);
            o_CreateContext = nullptr;
            return;
        }

        _hookedModule = module;
        LOG_INFO("External FG: the game's XeSS-FG ({}) will report an unsupported device while the RTX 20/30 "
                 "unlocker owns frame generation",
                 wstring_to_string(std::wstring(path)));
    }

    // Number of CreateContext calls refused this session (menu status).
    static uint32_t BlockedCount() { return _blocked.load(std::memory_order_relaxed); }

  private:
    inline static std::mutex _mutex;
    inline static HMODULE _hookedModule = nullptr;
    inline static decltype(&xefgSwapChainD3D12CreateContext) o_CreateContext = nullptr;
    inline static std::atomic<uint32_t> _blocked { 0 };

    static bool BlockActive()
    {
        auto* cfg = Config::Instance();
        return GameXeFGBlockPolicy::ShouldBlock(
            State::Instance().externalFrameGeneration, cfg->FGDLSSGAmpereMfgUnlock.value_or_default(),
            AmpereMfgLoader::GetStatus().DllLoaded, cfg->FGDLSSGAmpereMfgBlockGameXeFG.value_or_default());
    }

    static bool IsOptiScalerOwnCopy(HMODULE module)
    {
        wchar_t modulePath[MAX_PATH] {};
        if (GetModuleFileNameW(module, modulePath, MAX_PATH) == 0)
            return false;

        const auto optiPath = Config::Instance()->MainDllPath.value_or(L"");
        if (optiPath.empty())
            return false;

        return Util::IsSubpath(std::filesystem::path(modulePath).lexically_normal(),
                               std::filesystem::path(optiPath).lexically_normal());
    }

    static void Unhook()
    {
        if (o_CreateContext == nullptr)
            return;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(&(PVOID&) o_CreateContext, hkCreateContext);
        DetourTransactionCommit();

        o_CreateContext = nullptr;
        _hookedModule = nullptr;
    }

    static xefg_swapchain_result_t hkCreateContext(ID3D12Device* pDevice, xefg_swapchain_handle_t* phSwapChain)
    {
        if (!BlockActive())
            return o_CreateContext(pDevice, phSwapChain);

        if (phSwapChain != nullptr)
            *phSwapChain = nullptr;

        if (_blocked.fetch_add(1, std::memory_order_relaxed) == 0)
            LOG_INFO("External FG: refused the game's XeSS-FG context (unsupported device) so DLSS-G keeps the "
                     "swapchain; set [DLSSG] AmpereMfgBlockGameXeFG=false to allow it");

        return static_cast<xefg_swapchain_result_t>(GameXeFGBlockPolicy::kUnsupportedDevice);
    }
};
