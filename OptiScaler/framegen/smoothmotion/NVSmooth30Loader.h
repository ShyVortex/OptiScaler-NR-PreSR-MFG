#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

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
