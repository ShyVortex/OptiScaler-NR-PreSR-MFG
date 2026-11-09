#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace NVSmooth30Loader
{
struct Status
{
    bool Enabled = false;               // Config says to use it
    bool DllFound = false;              // nvsmooth30.dll found in OptiScaler directory
    bool DllLoaded = false;             // LoadLibrary succeeded
    bool SmoothMotionActive = false;    // NVIDIA Smooth Motion DRS setting applied
    bool SwapchainAttached = false;     // NvPresent64.dll detected on the current DXGI swapchain
    bool LoadedBeforeSwapchain = false; // nvsmooth30.dll was loaded before the current swapchain was created
    bool ProbeDone = false;             // The attachment probe window of the current swapchain has closed
    bool WaitingForDxgiHooks = false;   // Setup deferred until OptiScaler's DXGI factory hooks are installed
    int SwapchainId = 0;                // OptiScaler id of the current (most recently created) swapchain
    std::wstring LoadedDllPath;         // Absolute path of loaded DLL
    std::string ErrorMessage;           // Human-readable error if anything failed
};

Status LastStatus();
inline Status GetStatus() { return LastStatus(); }

/// Called after DLL initialization, once GPU/environment information is available.
void TrySetup();

/// Applies sanitized environment variables before loading nvsmooth30.dll.
void SanitizeEnvironment();

/// A new swapchain became current: reset its attachment state and start a new probe window.
void BeginSwapchain(int swapchainId, bool loadedBefore);

/// Records that NvPresent64.dll was detected on the swapchain. Ignored for a swapchain that is no longer current.
void SetSwapchainAttached(int swapchainId);

/// Records that the probe window of the swapchain closed. Ignored for a swapchain that is no longer current.
void SetProbeDone(int swapchainId);

/// nvsmooth30.dll creates a 16x16 D3D11 swapchain at load to patch the shared DXGI Present slots. OptiScaler rejects it
/// in its DXGI factory hooks (DxgiFactory_Hooks.cpp), so the DLL may only load once those hooks are installed.
enum class LoadGate
{
    Load,
    WaitForDxgiHooks
};
constexpr LoadGate DecideLoadGate(bool dxgiFactoryHooksInstalled)
{
    return dxgiFactoryHooksInstalled ? LoadGate::Load : LoadGate::WaitForDxgiHooks;
}

/// True while a setup is deferred until the DXGI factory hooks are installed (cheap, lock-free).
bool PendingDxgiHooks();

/// Runs a deferred setup. Called from OptiScaler's CreateDXGIFactory* hooks, which run on game calls only.
void RunPendingSetup();

/// True for NVIDIA's Smooth Motion presenter module, as Util::WhoIsTheCaller reports it (file name, any case).
inline bool IsNvPresentModule(std::string_view module)
{
    constexpr std::string_view name = "nvpresent64.dll";
    if (module.size() != name.size())
        return false;
    for (size_t i = 0; i < name.size(); i++)
    {
        char c = module[i];
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        if (c != name[i])
            return false;
    }
    return true;
}

/// Test switch (DLSS Unlocked #42): NvPresent's factory hook sits above OptiScaler's and wraps OptiScaler's swapchain
/// wrapper, while it is built to wrap the real swapchain. With [SmoothMotion] LeaveSmoothMotionSwapchainUnwrapped, a
/// swapchain NvPresent creates through OptiScaler's hook is handed back unwrapped (FG-NVSMOOTH30-INTEGRATION.md 6.13).
constexpr bool ShouldLeaveSwapchainUnwrapped(bool nvSmooth30Loaded, bool switchOn, bool callerIsNvPresent)
{
    return nvSmooth30Loaded && switchOn && callerIsNvPresent;
}

/// Whether NVSmooth30's SM86_DIAGNOSTICS logging is turned on for an OptiScaler log level (0 = Trace, 1 = Debug).
/// It only adds log lines in nvsmooth30.log (e.g. CUDA kernel loads it left unmodified).
constexpr bool WantsDiagnostics(int logLevel) { return logLevel <= 1; }

/// Evaluates whether an architecture ID represents Nvidia Ampere (SM86).
inline bool IsAmpereArch(uint32_t archId) { return (archId == 0x00000170) || ((archId & 0xFFF0) == 0x0170); }

/// Evaluates candidate paths for nvsmooth30.dll, strictly prioritizing the OptiScaler directory.
inline std::filesystem::path ResolveCandidatePath(const std::filesystem::path& basePath,
                                                  const std::filesystem::path& mainOverride = {})
{
    // 1. mainOverride / "nvsmooth30.dll"
    if (!mainOverride.empty())
    {
        auto candidate = mainOverride / L"nvsmooth30.dll";
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec))
            return candidate;
    }

    // 2. basePath / "OptiScaler" / "nvsmooth30.dll" (Mandatory placement inside OptiScaler directory)
    {
        auto candidate = basePath / L"OptiScaler" / L"nvsmooth30.dll";
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec))
            return candidate;
    }

    // 3. basePath / "nvsmooth30.dll" (fallback if game root has OptiScaler files directly)
    {
        auto candidate = basePath / L"nvsmooth30.dll";
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec))
            return candidate;
    }

    return {};
}

} // namespace NVSmooth30Loader
