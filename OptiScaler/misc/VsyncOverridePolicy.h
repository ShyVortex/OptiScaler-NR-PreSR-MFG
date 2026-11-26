#pragma once

// Pure decisions behind OptiScaler's V-Sync override ([V-Sync] ForceVsync / SyncInterval), kept free of Windows
// headers so unit tests can include them directly. See docs/issues/FG/FG-VSYNC-EXTERNAL-FG-MODE-MENU.md.

namespace VsyncOverridePolicy
{
// External FG mode (dlssg_sm86 and other game-owned frame generation): the game's DLSS-G presents and paces the
// generated frames, so OptiScaler leaves their sync interval and tearing flag alone (e5b129ad). The menu says so
// instead of offering controls that do nothing (ShyVortex/DLSS-Unlocked#75).
constexpr bool AppliesInMode(bool externalFrameGeneration) { return !externalFrameGeneration; }

inline constexpr const char* ExternalModeMessage = "OptiScaler's V-Sync override is not applied in External FG mode:\n"
                                                   "the game's frame generation controls presentation.\n"
                                                   "Use the game's or the NVIDIA Control Panel V-Sync, or XeMFG\n"
                                                   "with Display VBlank Sync (Extra Pacing).";
} // namespace VsyncOverridePolicy
