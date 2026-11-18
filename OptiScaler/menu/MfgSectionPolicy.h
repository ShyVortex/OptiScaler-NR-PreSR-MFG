#pragma once

// The "DLSS Frame Generation" menu section: what it shows and what can be changed. Pure (no ImGui, Config or State).
//
// The menu edits [DLSSG] MfgUnlock / MfgUnlocker, then writes the unlocker flags (FGDLSSGAdaMfgUnlock /
// FGDLSSGAmpereMfgUnlock) the way config load will resolve them on the next start (MfgUnlockerSelect::Resolve with
// the architecture found at startup), so Save and the rest of the menu see the same owner the next session gets.

#include <framegen/dlssg/MfgUnlockerSelect.h>

#include <cstdint>
#include <string>

namespace MfgSectionPolicy
{

using MfgUnlockerSelect::Choice;
using MfgUnlockerSelect::NvArch;
using MfgUnlockerSelect::Owner;

inline constexpr uint32_t kNvapiArchBlackwell = 0x1B0;

struct Inputs
{
    NvArch startupArch = NvArch::Unknown; // State::mfgUnlocker.arch (EarlyGpuArch at config load)
    bool primaryIsNvidia = false;         // IdentifyGpu primary GPU
    uint32_t primaryNvapiArch = 0;        // its NV_GPU_ARCHITECTURE_* id
    bool configuredEnabled = false;       // FGDLSSGMfgUnlock
    Choice configuredChoice = Choice::Auto;
    Owner sessionOwner = Owner::None; // State::mfgUnlocker.owner
    bool xeMfgActive = false;         // XeMfgLoader::EnabledForSession
    bool adaBuilt = true;             // OPTISCALER_RTX40_MFG
    bool sessionExternal = false;     // State::externalFrameGeneration
};

struct View
{
    NvArch arch = NvArch::Unknown; // the family the section works with

    bool locked = false; // no unlocker can run on this system
    std::string lockReason;

    // The enable checkbox. While locked it stays usable only to turn a saved setting off.
    bool enableInteractive = true;
    std::string enableBlockedReason;

    // Combo entries, index = Choice. A disabled entry is greyed out (another GPU family, not in this build).
    std::string autoLabel;
    bool choiceSelectable[3] = { true, true, true };

    Owner configuredOwner = Owner::None; // what the next start runs
    std::string choiceNote;              // non-empty: shown under the combo (amber)
    bool restartNeeded = false;          // configured owner differs from this session's

    bool adaBlockedByExternal = false; // RTX 40 unlock chosen, but this session runs External FG
};

// The startup detection, or IdentifyGpu when the startup detection could not tell.
inline NvArch MenuArch(const Inputs& in)
{
    if (in.startupArch != NvArch::Unknown)
        return in.startupArch;
    if (in.primaryIsNvidia)
        return MfgUnlockerSelect::FromNvapiArchitecture(in.primaryNvapiArch);
    return NvArch::Unknown;
}

inline View Resolve(const Inputs& in)
{
    View view;
    view.arch = MenuArch(in);

    // Locks: only when it is certain that no unlocker can run here.
    if (view.arch == NvArch::None)
    {
        view.locked = true;
        view.lockReason = "Requires an NVIDIA RTX 20, 30 or 40 series GPU.";
    }
    else if (view.arch == NvArch::Other)
    {
        view.locked = true;
        view.lockReason = (in.primaryIsNvidia && in.primaryNvapiArch >= kNvapiArchBlackwell)
                              ? "RTX 50 series runs DLSS Multi Frame Generation natively: no unlocker is needed."
                              : "This NVIDIA GPU has no DLSS Frame Generation unlocker (RTX 20, 30 or 40 needed).";
    }

    if (view.locked)
    {
        view.enableInteractive = in.configuredEnabled; // can still be turned off
        view.enableBlockedReason = view.lockReason;
    }
    else if (in.xeMfgActive && !in.configuredEnabled)
    {
        view.enableInteractive = false;
        view.enableBlockedReason = "Disabled because Intel XeMFG is active.\n"
                                   "Turn off the XeMFG unlocker first, Save Settings and restart.";
    }

    // Combo entries: a known family greys out the other unlocker; an unknown one leaves both choosable.
    const bool known = view.arch == NvArch::Ada || MfgUnlockerSelect::IsTuringAmpere(view.arch);
    view.choiceSelectable[static_cast<size_t>(Choice::Ada)] =
        in.adaBuilt && (!known || view.arch == NvArch::Ada) && !view.locked;
    view.choiceSelectable[static_cast<size_t>(Choice::TuringAmpere)] =
        (!known || MfgUnlockerSelect::IsTuringAmpere(view.arch)) && !view.locked;
    view.choiceSelectable[static_cast<size_t>(Choice::Auto)] = !view.locked;

    if (view.arch == NvArch::Ada)
        view.autoLabel = "Auto (RTX 40 unlock on this GPU)";
    else if (MfgUnlockerSelect::IsTuringAmpere(view.arch))
        view.autoLabel = "Auto (RTX 20 / 30 unlocker on this GPU)";
    else if (view.arch == NvArch::Unknown)
        view.autoLabel = "Auto (GPU not identified: choose one)";
    else
        view.autoLabel = "Auto";

    // What the next start runs: the same rule config load applies, with the startup architecture.
    const auto next = MfgUnlockerSelect::Resolve(in.configuredEnabled, in.configuredChoice, in.startupArch);
    view.configuredOwner = next.owner;

    if (in.configuredEnabled && view.configuredOwner == Owner::Ada && !in.adaBuilt)
    {
        view.configuredOwner = Owner::None;
        view.choiceNote = "This build does not include the RTX 40 unlock.";
    }
    else if (in.configuredEnabled && next.choiceMismatch)
    {
        view.choiceNote = std::string(MfgUnlockerSelect::ChoiceName(in.configuredChoice)) +
                          " does not match this GPU: no unlocker will start. Choose Auto.";
    }
    else if (in.configuredEnabled && !view.locked && view.configuredOwner == Owner::None)
    {
        view.choiceNote = "The GPU was not identified at startup: choose the unlocker for your card.";
    }

    view.restartNeeded = view.configuredOwner != in.sessionOwner;
    view.adaBlockedByExternal = view.configuredOwner == Owner::Ada && in.sessionExternal;
    return view;
}

// The flags to write after a menu change: the configured owner, never both.
struct Flags
{
    bool ada = false;
    bool turingAmpere = false;
};

inline Flags FlagsFor(Owner owner) { return Flags { owner == Owner::Ada, owner == Owner::TuringAmpere }; }

} // namespace MfgSectionPolicy
