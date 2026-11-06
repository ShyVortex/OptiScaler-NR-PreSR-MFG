# FPS Limit: Squeezed XeMFG Bursts Under a Cap, Limit Lost Between Reflex Backends, External-Mode Gaps

## 1. Overview & Environment

- **Report**: private report from a tester, reproduced by the maintainer. OptiScaler's own FPS limit
  (`[Framerate] FramerateLimit`, menu "FPS Limit") does not work reliably with Intel XeMFG and with the Turing/Ampere
  MFG unlocker (dlssg_sm86, External FG mode). Not tracked on GitHub.
- **Test environment** (Windows run, from the logs and the maintainer):
  - Windows 11 (10.0.26300), NVIDIA GeForce RTX 3080 Ti Laptop GPU, driver 621.14.
  - Lost Soul Aside (`ProjectlsaSteam-Win64-Shipping.exe`, UE 4.27), Steam. OptiScaler as `dxgi.dll`.
  - OptiScaler v0.9.87-pre1 (`4bf7690`). `[XeMFG] UnlockMFG=true`, `Source=Game` (`FGInput=DLSSG`, `FGOutput=XeFG`),
    `MaxInterpolatedFrames=4`, played at 3X and 4X. XeSS-FG provider 1.3.1.78 (5/5 patches, 3/3 pacing thunks),
    `libxell.dll` 1.3.2, fakenvapi low latency "XeLL (forced)".
  - The game's NVIDIA Reflex option was ON. It is locked ON while frame generation is active.
- **Analysis environment** (verified by command): CachyOS, kernel 7.3.1-1-cachyos, NVIDIA GeForce RTX 3080 Ti Laptop
  GPU 16 GB, driver 580.196.07.
- **Status**: investigation complete; fixes planned on `fix/fps-limiter-xemfg` (section 6). Nothing confirmed in
  game yet.

## 2. Reported Symptoms

- With XeMFG, the FPS limit:
  - sometimes applies for a while and then removes itself;
  - sometimes does not apply at all;
  - sometimes applies "wrongly".
- With VSync off, 4X XeMFG reached 140–150 fps, but motion felt like the base frame rate.
- Capping at 120 with OptiScaler's limit gave 120 fps on the counter. Smoothness was the same as without FG, or worse.
- Capping at 120 with RTSS instead held 120 fps all the time and was smooth.
- The FPS limit does not work properly with the dlssg_sm86 unlocker either.

## 3. Evidence

### 3.1 Run 1 (LogLevel 2, long session): the cap never applied

The log of this run was overwritten by run 2. The figures below come from the analysis done on it at the time.

- The only capped window is 13:19:21 to 13:19:58. In it, `ReflexHooks::setFPSLimit Set FPS Limit to: 120` was
  logged 21 times while XeFG was active at 4X.
- XeFG's pacing reports for that window:
  - base 53–76 fps (`real frame` 13–19 ms);
  - output 159–175 presents/s (`gap` 5.7–6.3 ms avg).
- A 120 output cap at 4X means a base rate of 30. The cap had no effect.
- No `Set FPS Limit to: 0` appeared inside that window, so `reflexLimitsFps` did not toggle. The paired `120` lines,
  6–8 ms apart, come from the frame-count branch of `ReflexHooks::update` (section 4.5).
- XeFG created its context once in that session (12:27:02). The context-recreation path of section 4.2 was not
  exercised.

### 3.2 Run 2 (LogLevel 0, short session): the cap applied, the bursts were squeezed

Counts per second come from `IFGFeature::StartNewFrame` (base frames) and `LocalPresent SyncInterval` (real
presents).

| Window | Cap | Base fps | Presents/s | XeFG `ring` / `med` | XeFG step target | Gap spread (sd / mean) |
|---|---|---|---|---|---|---|
| 12:00:00–12:01:05 | none, VSync off | 55 | 218 | 8.0 / 18.4 ms | about 2.0 ms | 1.67 / 4.59 ms (36%) |
| 12:01:38–12:01:47 | none, heavier scene | 38 | 153 | n/a | n/a | 3.48 / 6.58 ms (53%) |
| 12:02:00–12:03:00 | 120 (OptiScaler) | 30 | 120 | 19.8 / 33.1 ms | 4.93 ms | 4.30 / 8.34 ms (52%) |

- **Uncapped**, the gaps between the 4 presents of one base frame repeat as about 3 / 4.5 / 4.3 / 6.3 ms, at times
  3 / 3.3 / 3.3 / 8.5 ms. The ideal spacing is 4.6 ms.
- **Capped at 120**, they repeat as about 16 / 9 / 4 / 4 ms instead of 8.33 ms each:
  - 47% of gaps are 3–6 ms, 24% are over 11 ms;
  - all 4 frames land in roughly the first half of each 33 ms base frame, followed by an empty gap. On screen that is
    one 30 fps step per burst.
- One capped stats line:
  ```
  XeFG pacing: 4X, real frame 19.72 ms (50.7 fps), target 4.93 ms/frame; gap 6.67 avg / 0.44 min / 28.93 max ms
  XeFG recent burst trace (4X, ring 19.80ms, med 33.07ms)
  ```
- "DLSS FG detected, mode: 4x" and "DLSS FG no longer detected" were logged 263 and 297 times in about 6 minutes
  (section 4.5).
- The earlier idea that something capped the output at 150 was wrong: 150 presents/s is the base rate of about 38
  times 4.

### 3.3 RTSS comparison

RTSS at 120 was smooth in the same game and mode. RTSS waits in the real swapchain's Present until 8.33 ms after the
previous present, so it re-spaces every present after XeFG has issued it. It does not depend on Reflex, XeLL or the
pacing engine's step.

## 4. Validity Assessment & Root Cause

All the items below are code defects, not configuration errors. 4.1 and 4.2 cause the reported XeMFG symptoms. 4.4
causes the dlssg_sm86 symptoms.

### 4.1 The pacing step ignores the real cadence (squeezed bursts)

`OptiScaler/framegen/xefg/XeFGPacing.h`:

- `NoteFrame` samples `PushPeriod(XeFGPacingPolicy::PureRenderNs(delta, blockNs))`. That is the time between bursts
  minus the time spent waiting inside XeFG's scheduler and OptiScaler's pacing.
- `TsDetour` steps by `g_renderTimeNs / countPlus1`.
- `g_intervalQpc` is `g_periodNs / (count + 1)`.

So the spacing comes from the engine's unblocked render time, not from the real base interval:

- **Capped**, the real base interval (33 ms at 120 for 4X) is set by the limiter. The pure render time (19.8 ms) does
  not include the limiter's sleep. Step = 4.93 ms instead of 8.33.
- **Uncapped**, the base interval (18.4 ms) still includes time the engine thread spends waiting on XeFG. Pure
  render time (8.0 ms) gives about 2 ms steps instead of 4.6.

This is the trade-off made by
[FG-XEMFG-RESONANT-LOCK-AND-TIMING-DECOUPLING.md](FG-XEMFG-RESONANT-LOCK-AND-TIMING-DECOUPLING.md):

- Pacing waits run on the thread that calls XeFG's present, which in these games is the engine thread.
- Spreading a burst over the whole measured cycle lengthens the cycle and feeds back into the measurement: the
  71.4 fps lock. Stepping by the provider median does exactly that, and stays prohibited.
- Stepping by pure render time breaks the loop, but bursts are never evenly spaced. That document's claim of uniform
  spacing does not hold in game (see the correction note there).

Under a cap, the base period is known from outside: multiplier / limit. It does not depend on our own waits, so
spacing by 1 / limit cannot feed back. That is the planned fix.

### 4.2 The limit is sent once, to one of two backends, and never re-synced

`OptiScaler/hooks/Reflex_Hooks.cpp`:

- `setFPSLimit` sends `minimumIntervalUs` either to fakenvapi (`nvapi_calls::NvAPI_D3D_SetSleepMode`, whose
  low-latency tech is XeLL, forced by XeFG) or to the driver (`o_NvAPI_D3D_SetSleepMode`). It picks by
  `IsXeFGActive()` at the moment of the call.
- `update()` resends only when the computed value changes (`lastFps`), on a Reflex toggle, or on a frame-count change.
- The game's own Reflex calls switch backend whenever XeFG becomes active, pauses, or the game turns FG off.
- A cap set while XeFG is inactive reaches only the driver. A cap set while it is active reaches only XeLL.
- The game's own `SetSleepMode` is the only other resend: `hkNvAPI_D3D_SetSleepMode` injects `_minimumIntervalUs`.
  Games call it rarely.

`OptiScaler/framegen/xefg/XeFG_Dx12.cpp`, `CreateSwapchainContext`:

- `fakenvapi::forceMode(device, LowLatencyMode::XeLL)` creates a new XeLL context.
- `XeLLProxy::SetSleepMode` is then called with `minimumIntervalUs = 0`.
- Every context recreation (resize, alt-tab, mode change) therefore clears the cap, and `ReflexHooks` does not
  resend it. This matches "applies for a while, then removes itself".

`OptiScaler/low_latency/low_latency_tech/ll_xell.cpp`, `XeLL::set_sleep_mode`:

- It caches the last values in function-level statics shared by every XeLL instance.
- After a fakenvapi reinit, a new context can skip its first call because the cached values already match.

Run 1 failed while the cap was sent 21 times during active 4X. The log does not show which backend received it or
whether XeLL slept. `LOWLATENCY_TRACE_LOGGING` is compiled out, and LogLevel 2 hides the debug lines.

### 4.3 The fallback limiter assumes 2X

`OptiScaler/misc/FrameLimit.cpp`:

- `FrameLimit::sleep(fgActive)` doubles the interval whenever FG is active.
- It runs once per base frame from `FGHooks::FGPresent`.
- The output becomes limit × multiplier / 2: right at 2X, 1.5× the limit at 3X, 2× at 4X.

### 4.4 External FG mode (dlssg_sm86)

- `ReflexHooks::update` and `setFPSLimit` return early in External mode (upstream `a60f3c93`):
  - `reflexLimitsFps` is forced false;
  - the game's Reflex never receives the cap.
- `WrappedIDXGISwapChain4::Present1` skips `FrameLimit::sleep` in External mode (`e5b129ad`). `Present` does not, so:
  - games that call `Present1` get no limit;
  - games that call `Present` get a sleep after every present, on the path the game's DLSS-G presents through.
- `hkvkQueuePresentKHR` (`OptiScaler/hooks/Vulkan_Hooks.cpp`) runs `FrameLimit::sleep(false)` in every mode, External
  included.
- The menu reports "Current method: Fallback" even when nothing limits.

### 4.5 Frame-count flapping

`hkNvAPI_D3D_SetLatencyMarker` sets `_FgNumFramesToGenerate = 0` whenever `_lastAsyncMarkerFrameId + 10 < frameID`.
That condition is always true for a game that never sends out-of-band (async) markers. The DLSS-G path sets the count
back on the next evaluate.

Effects:
- `update()` sees the count flip and calls `setFPSLimit` (the paired log lines).
- On systems where fakenvapi is the main nvapi (AMD/Intel), each flip also:
  - sets `fakenvapiReloadLowLatency`;
  - can force `FN_ForceReflex = ForceDisable`;
  - changes the divided limit.

### 4.6 Linux (DXVK / vkd3d-proton)

- The three DXGI-side fallback sleeps skip when `usesDxvk` is set (`48087c67`).
- `hkvkQueuePresentKHR` calls `ReflexHooks::update(false, true)`. For a D3D12 game `_lastVkSleepDev` is null, so it
  takes the D3D branch with `fgActive = false`. That disagrees with the DXGI-side call (`fgActive = true`).
  - With the game's Reflex off, `reflexLimitsFps` can flip every frame. Each flip resets the limit to 0 and sends it
    again.
- The Vulkan-side `FrameLimit::sleep(false)` runs on every real present, generated frames included. That is
  RTSS-like output pacing.
  - It can hide squeezed bursts in capped tests under Proton. This is why the Linux confirmations in the
    resonant-lock document did not reveal 4.1.
- Not yet verified from a log that the Vulkan hook sees vkd3d-proton's presents.

### 4.7 The game's own XeLL context

`OptiScaler/hooks/Xell_Hooks.cpp`:

- `gamesContextCanLimitFps` is set once and never cleared: not on `xellDestroyContext`, and not when OptiScaler
  starts blocking the game's XeLL calls. While it stays set, the fallback stays off and the menu shows "Game's XeLL".
- `XellHooks::update` caches `lastFpslimit`, and `hkxellSetSleepMode` passes the game's values through without
  adding the limit. After the game writes its own sleep mode, the cap is gone.

## 5. Cross-Reference

- [FG-XEMFG-RESONANT-LOCK-AND-TIMING-DECOUPLING.md](FG-XEMFG-RESONANT-LOCK-AND-TIMING-DECOUPLING.md): source of the
  pure-render step and of the prohibition on provider-median stepping (4.1). Rule §8 (`PureRenderNs`) is kept.
- [FG-XEMFG-2X-RENDER-TIME-NOT-MEASURED.md](FG-XEMFG-2X-RENDER-TIME-NOT-MEASURED.md): `MeasureOnlyLastFrame`; no
  second wait on scheduler-paced frames (`fd8f6741`, `9e593639`). Kept.
- [FG-XEMFG-SR-SWAPCHAIN-ALLOCATION-AND-PACING-OVERHAUL.md](FG-XEMFG-SR-SWAPCHAIN-ALLOCATION-AND-PACING-OVERHAUL.md)
  §3.3: an earlier case of frame-count resets triggering `setFPSLimit(0)` (4.5).
- [FG-RTSS-XEFG-PACING-PROTECTION.md](FG-RTSS-XEFG-PACING-PROTECTION.md): RTSS marker filtering; unaffected.
- [FG-EXTERNAL-MFG-SWAPCHAIN-STARTUP-GATE.md](FG-EXTERNAL-MFG-SWAPCHAIN-STARTUP-GATE.md) and rule §4: External mode
  ownership (4.4).
- `docs/DLSS-FRAME-GENERATION.md`: the Reflex limit caps total output, generated frames included. That is the
  semantics kept here.

## 6. Planned Solution

On `fix/fps-limiter-xemfg`, one task per commit:

1. This document and the correction note in the resonant-lock document.
2. **Cap-aware pacing step.** A pure `XeFGPacingPolicy` step function: `max(renderNs / countPlus1, 1e9 / capFps)`
   when an OptiScaler cap is applied, then the existing clamp. Used in `TsDetour` and for `g_intervalQpc`.
   `PureRenderNs` and the frame render time given to XeFG are unchanged.
3. **Limit re-sync.**
   - Track which Reflex backend holds the limit, and resend when the routing changes or XeFG creates a new XeLL
     context.
   - XeFG initialises XeLL with the current limit, not 0.
   - Per-instance cache in `XeLL::set_sleep_mode`.
   - With no OptiScaler limit (0), the game's own Reflex `minimumIntervalUs` is sent instead of 0, so a reset or a
     resend never wipes a cap the game set itself.
   - `XeFG_Dx12::Present` feeds the limit to `XeFGPacing::SetOutputCapFps` (0 in External FG mode).
4. **Fallback limiter multiplier.** `FrameLimit::sleep` takes the real output multiplier.
5. **External FG mode.**
   - No limiter sleep in `Present`, `Present1` or the Vulkan hook.
   - The menu says the limit is not available in External mode and points to RTSS or the game's own limit.
6. **Frame-count flapping.** Reset on stale async markers only if the game has sent async markers.
7. **DXVK.** For D3D swapchains, the Vulkan present hook no longer runs its own `ReflexHooks::update`; the DXGI side's
   update, which has the real FG state, decides alone. The Vulkan-side fallback sleep stays: under DXVK it is the only
   fallback sleep (`48087c67` turned the DXGI-side ones off there), one per real present.
8. **The game's XeLL context path.** Clear the stuck flag; inject the limit in `hkxellSetSleepMode`.
9. Test build and in-game verification.

Out of scope: even spacing without a cap. It needs pacing off the engine thread, a separate investigation.

## 7. Verification

Pending. Each task adds its unit test. In-game checks:
- Lost Soul Aside, 4X, OptiScaler cap 120:
  - even spacing (about 8.33 ms between presents);
  - the cap survives alt-tab and resize.
- Uncapped: behaviour unchanged. The unit tests model the 71.4 fps lock case; Helldivers 2 can no longer be used in game
  (it now ships Easy Anti-Cheat).
- dlssg_sm86 in External mode.
