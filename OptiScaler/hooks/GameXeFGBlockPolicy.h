#pragma once

#include <cwctype>
#include <string_view>

// External FG with the RTX 20/30 unlocker (sdli1995 dlssg_sm86): a game whose XeSS-FG library is cross-vendor (any
// libxess_fg 1.3.x, e.g. after a DLSS Swapper upgrade of the Intel-only build the game shipped) creates an XeSS-FG
// context on a GeForce and wraps the swapchain. Streamline's DLSS-G then never sees a present and the unlocker
// generates nothing (FG-LSA-EXTERNAL-MFG-NO-FEATURE-CREATE.md). Refusing xefgSwapChainD3D12CreateContext with the SDK's
// "unsupported device" answer reproduces what the Intel-only build does on a GeForce, which the game already handles.
// Kept free of Windows and XeSS headers so the unit test runs the same rules.
namespace GameXeFGBlockPolicy
{
// XEFG_SWAPCHAIN_RESULT_ERROR_UNSUPPORTED_DEVICE in xefg_swapchain.h
constexpr int kUnsupportedDevice = -1;

// File-name match, case-insensitive, for a bare name or a path with \ or / separators.
inline bool FileNameIs(std::wstring_view path, std::wstring_view lowerName)
{
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring_view::npos)
        path.remove_prefix(slash + 1);

    if (path.size() != lowerName.size())
        return false;

    for (size_t i = 0; i < path.size(); ++i)
    {
        if (static_cast<wchar_t>(std::towlower(path[i])) != lowerName[i])
            return false;
    }

    return true;
}

inline bool IsXeFGLibrary(std::wstring_view path)
{
    return FileNameIs(path, L"libxess_fg.dll") || FileNameIs(path, L"igxess_fg.dll");
}

// Only while the game (and the external unlocker) owns frame generation: OptiScaler's own XeFG and XeMFG never run in
// External mode, so no OptiScaler path depends on the game's XeSS-FG there.
constexpr bool ShouldBlock(bool externalFrameGeneration, bool ampereUnlockEnabled, bool unlockerLoaded,
                           bool blockSetting)
{
    return externalFrameGeneration && ampereUnlockEnabled && unlockerLoaded && blockSetting;
}
} // namespace GameXeFGBlockPolicy
