#pragma once

// DLSS-G render preset for the RTX 40 (Ada) MFG unlock ([DLSSG] AdaMfgPreset: Auto / A / B / C).
//
// NVIDIA's DLSS-G reads its preset from the driver profile key NGX_DLSS_FG_OVERRIDE_RENDER_PRESET_SELECTION_ID
// (external/nvapi/NvApiDriverSettings.h: Off = 0, A = 1, B = 2, C = 3) through NvAPI_DRS_GetSetting, which
// NvApiHooks::hkNvAPI_DRS_GetSetting already intercepts. The preset is answered there for this process only; the
// saved NVIDIA profile is never written. DLSS-G reads it when frame generation starts, so a change needs a restart.
// Preset C (NVIDIA's performance mode) exists only in DLSS-G 310.9.16 and newer; older runtimes keep the game or
// driver value. Pure: no NVAPI, Config or State dependencies.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace DlssgPresetPolicy
{

inline constexpr uint32_t kSettingId = 0x10E41DF1; // NGX_DLSS_FG_OVERRIDE_RENDER_PRESET_SELECTION_ID
inline constexpr int32_t kNvapiOk = 0;
inline constexpr int32_t kNvapiSettingNotFound = -160; // NVAPI_SETTING_NOT_FOUND
inline constexpr uint32_t kDwordType = 0;              // NVDRS_DWORD_TYPE

enum class Preset : uint32_t
{
    Auto = 0, // game / driver profile (never overridden)
    A = 1,
    B = 2,
    C = 3, // performance mode, DLSS-G 310.9.16+
};

inline char Lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// AdaMfgPreset. Unknown text leaves the key unset (Auto).
inline std::optional<Preset> Parse(std::string_view text)
{
    if (text.size() != 1 && !(text.size() == 4 && Lower(text[0]) == 'a' && Lower(text[1]) == 'u' &&
                              Lower(text[2]) == 't' && Lower(text[3]) == 'o'))
        return std::nullopt;

    if (text.size() == 4)
        return Preset::Auto;

    switch (Lower(text[0]))
    {
    case 'a':
        return Preset::A;
    case 'b':
        return Preset::B;
    case 'c':
        return Preset::C;
    default:
        return std::nullopt;
    }
}

inline const char* Name(Preset preset)
{
    switch (preset)
    {
    case Preset::A:
        return "A";
    case Preset::B:
        return "B";
    case Preset::C:
        return "C";
    default:
        return "Auto";
    }
}

struct Version
{
    int major = 0;
    int minor = 0;
    int patch = 0;
};

// "310.9.16" (MfgUnlock::Status::SnippetVersion). Empty or malformed text: unknown.
inline std::optional<Version> ParseVersion(std::string_view text)
{
    Version version;
    int* fields[] = { &version.major, &version.minor, &version.patch };
    size_t field = 0;
    bool digits = false;

    for (char c : text)
    {
        if (c >= '0' && c <= '9')
        {
            *fields[field] = *fields[field] * 10 + (c - '0');
            digits = true;
        }
        else if (c == '.' && digits && field < 2)
        {
            field++;
            digits = false;
        }
        else
        {
            return std::nullopt;
        }
    }

    if (field != 2 || !digits)
        return std::nullopt;

    return version;
}

inline bool ImplementsPresetC(const std::optional<Version>& runtime)
{
    if (!runtime.has_value())
        return false;

    const auto& v = *runtime;
    if (v.major != 310)
        return v.major > 310;
    if (v.minor != 9)
        return v.minor > 9;
    return v.patch >= 16;
}

// The value to answer DLSS-G's preset read with, or nothing to leave the driver's answer alone.
inline std::optional<uint32_t> Override(Preset configured, bool adaUnlockActive, const std::optional<Version>& runtime)
{
    if (!adaUnlockActive || configured == Preset::Auto)
        return std::nullopt;

    if (configured == Preset::C && !ImplementsPresetC(runtime))
        return std::nullopt;

    return static_cast<uint32_t>(configured);
}

enum class Action
{
    Leave,          // driver error, other type or struct version: the original result stands
    OverwriteValue, // the profile has the key: replace its current value
    FillMissing,    // the profile lacks the key: answer it as a current-profile DWORD
};

inline Action Classify(int32_t driverResult, bool settingPresent, uint32_t structVersion, uint32_t expectedVersion,
                       uint32_t settingType)
{
    if (!settingPresent || structVersion != expectedVersion)
        return Action::Leave;
    if (driverResult == kNvapiOk)
        return settingType == kDwordType ? Action::OverwriteValue : Action::Leave;
    if (driverResult == kNvapiSettingNotFound)
        return Action::FillMissing;
    return Action::Leave;
}

} // namespace DlssgPresetPolicy
