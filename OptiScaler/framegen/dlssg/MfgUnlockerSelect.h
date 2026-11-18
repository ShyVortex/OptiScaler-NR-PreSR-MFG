#pragma once

// DLSS Frame Generation unlocker selection ([DLSSG] MfgUnlock + MfgUnlocker).
//
// One switch and one choice pick the unlocker that matches the GPU:
//   - RTX 40 (Ada): MfgUnlock::TryApply (in-memory nvngx_dlssg patch).
//   - RTX 20/30 (Turing/Ampere): AmpereMfgLoader (sdli1995 dlssg_sm86).
// The result is written into the existing FGDLSSGAdaMfgUnlock / FGDLSSGAmpereMfgUnlock flags, so every gate that
// reads them keeps working unchanged. Pure: no Windows, Config or State dependencies.
//
// The choice is resolved while the config loads, inside DllMain, before IdentifyGpu has run: External FG mode (the
// RTX 20/30 unlocker) has to be decided there. The architecture comes from the PCI device id (EarlyGpuArch).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace MfgUnlockerSelect
{

enum class NvArch : uint32_t
{
    Unknown, // detection failed, or NVIDIA GPUs of different supported architectures
    None,    // no NVIDIA GPU
    Turing,  // RTX 20, GTX 16
    Ampere,  // RTX 30
    Ada,     // RTX 40
    Other,   // any other NVIDIA GPU: RTX 50 (native MFG), older cards, data-center chips
};

enum class Choice : uint32_t
{
    Auto,
    Ada,
    TuringAmpere,
};

enum class Owner : uint32_t
{
    None,
    Ada,
    TuringAmpere,
};

inline constexpr uint32_t kNvidiaVendorId = 0x10DE;

// Consumer display functions only. Checked against every NVIDIA display entry of pci.ids (hwdata, 2026-10):
// all 288 TU1xx / GA102-GA107 / AD1xx ids land in their family and every other NVIDIA display id lands in Other
// (GA100 0x20xx, GH100 0x23xx, GB1xx/GB2xx and everything older).
constexpr NvArch ClassifyNvidiaDeviceId(uint32_t deviceId)
{
    if ((deviceId >= 0x1E00 && deviceId <= 0x1FFF) || (deviceId >= 0x2180 && deviceId <= 0x21FF))
        return NvArch::Turing;
    if ((deviceId >= 0x2200 && deviceId <= 0x223F) || (deviceId >= 0x2400 && deviceId <= 0x25FF))
        return NvArch::Ampere;
    if (deviceId >= 0x2600 && deviceId <= 0x28FF)
        return NvArch::Ada;
    return NvArch::Other;
}

struct PciId
{
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
};

// Non-NVIDIA adapters (a laptop's iGPU, WARP) are ignored. NVIDIA GPUs of one supported architecture give that
// architecture, even next to unsupported NVIDIA ones. Two different supported architectures are ambiguous (Unknown):
// the user picks the unlocker.
inline NvArch CombineAdapters(const std::vector<PciId>& adapters)
{
    bool anyNvidia = false;
    NvArch supported = NvArch::None;

    for (const auto& adapter : adapters)
    {
        if (adapter.vendorId != kNvidiaVendorId)
            continue;

        anyNvidia = true;
        const auto arch = ClassifyNvidiaDeviceId(adapter.deviceId);
        if (arch == NvArch::Other)
            continue;

        if (supported != NvArch::None && supported != arch)
            return NvArch::Unknown;

        supported = arch;
    }

    if (!anyNvidia)
        return NvArch::None;

    return supported == NvArch::None ? NvArch::Other : supported;
}

inline char LowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

inline bool EqualsIgnoreCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;

    for (size_t i = 0; i < a.size(); i++)
    {
        if (LowerAscii(a[i]) != LowerAscii(b[i]))
            return false;
    }

    return true;
}

// Reads "VEN_10DE&DEV_2460&SUBSYS_..." (a registry Enum\PCI key name or a hardware id, with or without "PCI\").
inline std::optional<PciId> ParsePciHardwareId(std::string_view text)
{
    auto hexField = [&](std::string_view tag) -> std::optional<uint32_t>
    {
        for (size_t i = 0; i + tag.size() + 4 <= text.size(); i++)
        {
            if (!EqualsIgnoreCase(text.substr(i, tag.size()), tag))
                continue;

            uint32_t value = 0;
            for (size_t j = 0; j < 4; j++)
            {
                const char c = LowerAscii(text[i + tag.size() + j]);
                uint32_t digit = 0;
                if (c >= '0' && c <= '9')
                    digit = static_cast<uint32_t>(c - '0');
                else if (c >= 'a' && c <= 'f')
                    digit = static_cast<uint32_t>(c - 'a' + 10);
                else
                    return std::nullopt;

                value = (value << 4) | digit;
            }

            return value;
        }

        return std::nullopt;
    };

    const auto vendor = hexField("VEN_");
    const auto device = hexField("DEV_");
    if (!vendor || !device)
        return std::nullopt;

    return PciId { *vendor, *device };
}

// [DLSSG] MfgUnlocker. Unknown text leaves the key unset (auto).
inline std::optional<Choice> ParseChoice(std::string_view text)
{
    if (EqualsIgnoreCase(text, "auto"))
        return Choice::Auto;
    if (EqualsIgnoreCase(text, "ada") || EqualsIgnoreCase(text, "rtx40"))
        return Choice::Ada;
    if (EqualsIgnoreCase(text, "turing-ampere") || EqualsIgnoreCase(text, "turingampere") ||
        EqualsIgnoreCase(text, "ampere") || EqualsIgnoreCase(text, "turing") || EqualsIgnoreCase(text, "rtx20-30"))
        return Choice::TuringAmpere;
    return std::nullopt;
}

inline const char* ChoiceName(Choice choice)
{
    switch (choice)
    {
    case Choice::Ada:
        return "ada";
    case Choice::TuringAmpere:
        return "turing-ampere";
    default:
        return "auto";
    }
}

inline const char* ArchName(NvArch arch)
{
    switch (arch)
    {
    case NvArch::None:
        return "no NVIDIA GPU";
    case NvArch::Turing:
        return "RTX 20 (Turing)";
    case NvArch::Ampere:
        return "RTX 30 (Ampere)";
    case NvArch::Ada:
        return "RTX 40 (Ada)";
    case NvArch::Other:
        return "other NVIDIA GPU";
    default:
        return "unknown";
    }
}

struct Request
{
    bool enabled = false;
    Choice choice = Choice::Auto;
    bool fromLegacyKeys = false; // AdaMfgUnlock / AmpereMfgUnlock decided it (MfgUnlock absent or auto)
};

// MfgUnlock set (true/false) wins. Without it the legacy keys decide: AdaMfgUnlock=true picks Ada,
// AmpereMfgUnlock=true picks Turing/Ampere, both true leave the choice to the GPU (Auto).
inline Request FromIni(std::optional<bool> mfgUnlock, std::optional<Choice> choice, std::optional<bool> legacyAda,
                       std::optional<bool> legacyAmpere)
{
    Request request;
    request.choice = choice.value_or(Choice::Auto);

    if (mfgUnlock.has_value())
    {
        request.enabled = *mfgUnlock;
        return request;
    }

    const bool ada = legacyAda.value_or(false);
    const bool ampere = legacyAmpere.value_or(false);
    if (!ada && !ampere)
        return request;

    request.enabled = true;
    request.fromLegacyKeys = true;
    if (!choice.has_value())
        request.choice = (ada && ampere) ? Choice::Auto : (ada ? Choice::Ada : Choice::TuringAmpere);

    return request;
}

struct Resolution
{
    Owner owner = Owner::None;
    NvArch arch = NvArch::Unknown;
    bool enabled = false;
    Choice choice = Choice::Auto;
    bool choiceMismatch = false; // explicit choice for another architecture than the detected one
    std::string reason;
};

inline bool IsTuringAmpere(NvArch arch) { return arch == NvArch::Turing || arch == NvArch::Ampere; }

// Auto follows the detected architecture. An explicit choice applies on its own architecture, and when detection
// failed (Unknown); for another detected GPU it gives no unlocker, so nothing is patched or forced (External FG).
inline Resolution Resolve(bool enabled, Choice choice, NvArch arch)
{
    Resolution result;
    result.arch = arch;
    result.enabled = enabled;
    result.choice = choice;

    if (!enabled)
    {
        result.reason = "unlocker disabled";
        return result;
    }

    if (choice == Choice::Auto)
    {
        if (arch == NvArch::Ada)
            result.owner = Owner::Ada;
        else if (IsTuringAmpere(arch))
            result.owner = Owner::TuringAmpere;

        if (result.owner != Owner::None)
            result.reason = std::string("auto: ") + ArchName(arch);
        else if (arch == NvArch::Unknown)
            result.reason = "auto: the GPU could not be identified at startup, choose the unlocker manually";
        else if (arch == NvArch::None)
            result.reason = "auto: no NVIDIA GPU";
        else
            result.reason = "auto: this NVIDIA GPU has no unlocker (RTX 20, 30 or 40 needed; RTX 50 runs MFG natively)";

        return result;
    }

    const Owner wanted = choice == Choice::Ada ? Owner::Ada : Owner::TuringAmpere;
    const bool matches = choice == Choice::Ada ? arch == NvArch::Ada : IsTuringAmpere(arch);

    if (matches || arch == NvArch::Unknown)
    {
        result.owner = wanted;
        result.reason = std::string("manual: ") + ChoiceName(choice) +
                        (matches ? "" : " (GPU not identified at startup, the unlocker checks it again)");
        return result;
    }

    result.choiceMismatch = true;
    result.reason = std::string("manual: ") + ChoiceName(choice) + " does not match the detected " + ArchName(arch) +
                    ", no unlocker started";
    return result;
}

struct Saved
{
    bool enabled = false;
    Choice choice = Choice::Auto;
};

// What Save writes, from the unlocker flags as the menu left them (at most one is true), the stored switch and
// choice, and the owner this session started with.
//   - A flag on: enabled, with the choice that flag means. The choice stays Auto while that flag is the one Auto
//     picks, so a GPU change keeps working.
//   - Both flags off after a session that had an owner: the menu turned the unlocker off.
//   - Both flags off and no owner this session (no matching GPU, detection failed): the stored switch and choice
//     are kept, so saving never drops a setting that could not apply here.
inline Saved ToSaved(bool adaFlag, bool ampereFlag, bool storedEnabled, Choice storedChoice, Owner sessionOwner,
                     NvArch arch)
{
    Saved saved;
    saved.choice = storedChoice;

    if (!adaFlag && !ampereFlag)
    {
        saved.enabled = sessionOwner == Owner::None && storedEnabled;
        return saved;
    }

    saved.enabled = true;

    const Owner flagged = adaFlag ? Owner::Ada : Owner::TuringAmpere;
    const Owner byAuto = Resolve(true, Choice::Auto, arch).owner;

    if (storedChoice == Choice::Auto && flagged == byAuto)
        return saved;

    saved.choice = flagged == Owner::Ada ? Choice::Ada : Choice::TuringAmpere;
    return saved;
}

// The architecture IdentifyGpu reports later (NV_GPU_ARCHITECTURE_* ids), for the startup cross-check.
constexpr NvArch FromNvapiArchitecture(uint32_t architectureId)
{
    switch (architectureId)
    {
    case 0x160:
        return NvArch::Turing;
    case 0x170:
        return NvArch::Ampere;
    case 0x190:
        return NvArch::Ada;
    default:
        return NvArch::Other;
    }
}

} // namespace MfgUnlockerSelect
