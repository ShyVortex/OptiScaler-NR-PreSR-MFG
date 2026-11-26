# V-Sync Settings Look Active in External FG Mode but Are Never Applied

## 1. Overview & Environment

- **Report**: jinshi10, [DLSS Unlocked #75](https://github.com/ShyVortex/DLSS-Unlocked/issues/75) (2026-11-26),
  "Screen Tearing issues in Wuthering Waves even with VSYNC ON".
- **Reporter environment**: Windows 11 (10.0.26300), NVIDIA GeForce RTX 3050 6GB Laptop GPU (Ampere), driver 621.40.
  - Wuthering Waves (Unreal Engine 4.26, Kuro Games launcher), DX12, game DLSS-G through Streamline.
  - OptiScaler v0.9.51-final (`a4aaf07a`), External FG mode, RTX 20/30 MFG unlocker with the SilyNoMeta v0.3.5-5
    mod. The reporter had tried newer builds and went back to 0.9.51, where the tearing was "not as bad".
  - `OptiScaler.ini`: `[V-Sync] ForceVsync=true`, `SyncInterval=1`, `[Framerate] FramerateLimit=144`, `LogLevel=2`.
- **Analysis environment**: log and source analysis only, no repro. CachyOS, kernel 7.3.6-1-cachyos, NVIDIA GeForce
  RTX 3080 Ti Laptop GPU, driver 580.196.07.

## 2. Reported Symptoms
- With the game's DLSS Frame Generation on, frame generation works but the picture tears.
- V-Sync on, V-Sync off and Fast Sync in the NVIDIA Control Panel make no difference.
- The reporter also forced V-Sync on in OptiScaler (`ForceVsync=true`, `SyncInterval=1`), with no effect.

## 3. Issue Validity Assessment

### 3.1 The tearing: not shown to be an OptiScaler defect
- In External FG mode the game's own DLSS-G, running inside the unlocker mod, presents the real and generated
  frames. Under the unlocker it presents with `SyncInterval 0` and the tearing flag
  ([FG-LSA-EXTERNAL-MFG-NO-FEATURE-CREATE.md](FG-LSA-EXTERNAL-MFG-NO-FEATURE-CREATE.md), section on the
  Streamline relaxations).
- `hkdlssg_slOnPluginLoad` sets `vsync.supported = true` for the unlocker, so DLSS-G still runs when the game's V-Sync
  is on; it does not make DLSS-G wait for the display.
- On RTX 40 and 50 the NVIDIA driver paces DLSS-G frames itself, which is why the Control Panel V-Sync works there.
  Probable, not verified: the driver does not do this on Ampere, where DLSS-G is not expected. On a panel without
  G-Sync, or an Optimus laptop where the integrated GPU drives the display, tearing is then expected whatever the
  Control Panel says. Different mod versions pace differently, which fits "less tearing on 0.9.51".
- The log at `LogLevel=2` records no present flags, so this is not confirmed. The attached `dlssg_sm86.log` comes
  from an older session (2026-11-01, sdli1995 0.4.1, OptiScaler 0.9.81).

### 3.2 The menu: a real usability defect
- OptiScaler deliberately skips its V-Sync override in External FG mode: `LocalPresent` in
  `wrapped_swapchain.cpp` has checked `externalFrameGeneration` since `e5b129ad` (2026-09-16, so 0.9.51 included).
  `FGHooks` creates no FG swapchain in that mode, so `FGHooks::FGPresent` never runs there.
- The "V-Sync Settings" section still offered "V-Sync On", "V-Sync Off" and "Sync Int." with no hint. A user sets
  them, sees no change and reports a bug.
- The FPS limiter had the same problem and was fixed the same way
  ([FG-FPS-LIMIT-XEMFG-PACING-AND-SYNC.md](FG-FPS-LIMIT-XEMFG-PACING-AND-SYNC.md), section 4.4). The reporter's
  `FramerateLimit=144` also had no effect, but on 0.9.51 the menu did not yet say so.

## 4. Root Cause
The External FG rule existed only as an inline check in one present path. The menu had no way to know about it, so
it rendered the controls as if they applied.

## 5. Solution Applied
No runtime behaviour changes.
- `OptiScaler/misc/VsyncOverridePolicy.h` (pure, no Windows headers):
  - `AppliesInMode(externalFrameGeneration)`, false in External FG mode;
  - `ExternalModeMessage`, which names the alternatives: the game's or the NVIDIA Control Panel V-Sync, or XeMFG with
    "Display VBlank Sync (Extra Pacing)".
- `LocalPresent` (`wrapped_swapchain.cpp`) and `FGHooks::FGPresent` (`FG_Hooks.cpp`) decide the override only through
  `AppliesInMode`. The `FGPresent` check makes the rule explicit; that path was already unreachable in External mode.
- `menu_common.cpp`, "V-Sync Settings": in External FG mode the amber note is shown and "V-Sync On", "V-Sync Off" and
  "Sync Int." are disabled. "Reset" stays enabled so a saved value can still be cleared. Nothing is written to the
  config, so the saved values come back when External FG mode is off. The layout is unchanged (no item placed
  relative to the right edge, rule §10).
- `OptiScaler.ini` `[V-Sync]`: the `ForceVsync` comment says the setting is not applied in External FG mode.

### 5.1 What was deliberately not done
- **No forced V-Sync on DLSS-G presents.** Forcing `SyncInterval=1` and stripping the tearing flag on frame
  generation presents was tried for XeMFG. It did not fix uneven frame spacing and was kept only as the opt-in
  `ExtraPacing`, default off ([FG-XEMFG-PRESENTATION-PACING.md](FG-XEMFG-PRESENTATION-PACING.md),
  [FG-XEMFG-PR21-REGRESSIONS.md](FG-XEMFG-PR21-REGRESSIONS.md)). On the unlocker's DLSS-G it would also cap the output
  at the refresh rate and fight the mod's own pacing.
- `OverrideVsync` (the swapchain `ALLOW_TEARING` flag at creation) is out of scope.

### 5.2 Advice for affected users
- Turn off the RTX 20/30 unlocker and External FG mode, and use XeMFG (`[XeMFG] UnlockMFG=true`, `Source=Game`).
  OptiScaler then controls presentation itself.
- For a V-Sync-like result, turn on "Display VBlank Sync (Extra Pacing)" and set OptiScaler's FPS limit a few FPS
  below the refresh rate. With XeMFG the limit counts the frames shown on screen, generated ones included
  (`FpsLimitSync::OutputCapFps`).

## 6. Test Verification
- `tests/vsync_external_mode_unit.cpp`, 4/4 PASS (g++ 16.2.1, `-std=c++20 -Wall -Wextra`):
  1. `AppliesInMode` is false only in External FG mode.
  2. The message names the mode and the alternatives, in lines of at most 64 characters.
  3. `LocalPresent` and `FGPresent` decide the override through the policy; the old inline check is gone.
  4. The menu shows the note before the controls, disables V-Sync On / Off / Sync Int., keeps Reset outside the
     disabled block, and the INI carries the note.
- Related tests still pass: `fps_limit_external_mode_unit` (5/5), `menu_width_unit` (7/7),
  `mfg_section_policy_unit` (54 checks).
- clang-format 23.1.5 `--dry-run --Werror` clean; UTF-8 BOM present on every changed C++ file.
- Pending: a Windows CI build and an in-game check of the menu (External FG on: the note is shown and the controls are
  greyed out; External FG off: unchanged).
