# XeMFG 2X: Render Time Never Measured, Frame Rate Stays at the No-FG Level

## 1. Overview & Environment

- **Issue**: [DLSS Unlocked #64](https://github.com/ShyVortex/DLSS-Unlocked/issues/64), "XeMFG 2X doesn't start
  running as soon as the game is launched".
- **Reporter's environment** (from the issue and its log):
  - Windows 11 (10.0.26200), NVIDIA GeForce RTX 3070 Ti Laptop GPU 8 GB, driver 621.12.
  - God of War (2018), Steam. DX11 game, run through OptiScaler's `Dx11wDx12` bridge.
  - DLSS Unlocked NR-v0.9.85 and NR-v0.9.86. The attached log is OptiScaler v0.9.85-final (`c1f882ff`), LogLevel 1.
  - `[XeMFG] UnlockMFG=true`, `Source=OptiScaler`, `MaxInterpolatedFrames=1` (2X); `FGInput=Upscaler`,
    `FGOutput=XeFG`; XeSS-FG provider 1.3.1.78, 5/5 XeMFG patches, 3/3 pacing thunks hooked.
- **Analysis environment** (verified by command): CachyOS, kernel 7.3.1-1-cachyos, NVIDIA GeForce RTX 3080 Ti
  Laptop GPU, driver 580.196.07; GCC 16.2.1, clang-format 23.1.4.
- **Fix**: `4e513fb7` on `fix/xemfg-2x-render-time`, first shipped in test build v0.9.87-pre1. Confirmed in game by
  the reporter (section 7).

## 2. Reported Symptoms

> If I enable XeMFG (UnlockMFG = true), set "Source = OptiScaler" and "MaxInterpolatedFrames = 1" (2X), and start
> the game, the FPS remains the same as when playing without Frame Generation (about 98 FPS). However, after the
> game starts, if I access the OptiScaler menu and change the Max Interpolated Frames slider to 3X and then back
> to 2X, XeFG MFG starts to work at 2X and the FPS jumps to about 143 FPS.

## 3. Validity Assessment

**Valid defect in `OptiScaler/framegen/xefg/XeFGPacing.h`. It affects XeMFG at 2X in any game, not only God of
War.**

XeFG is not off at launch:
- It activates, is deactivated around the game's `ResizeBuffers` (the usual 10-frame delay), and reactivates.
- It then dispatches about 2,500 times with `framesPresented=2`, `frameGenResult=SUCCESS` before the slider is
  touched.

The difference is in what the pacing engine measures and feeds to XeFG (`XeFGPacing::ReportStats`, 5-second
windows):

| | 2X from launch | 2X after 3X → 2X |
|---|---|---|
| `real frame` / `render-est` | 0.00 ms | 4.17 ms, identical in every window (09:40:50 → 09:41:25) |
| `fed` (frame render time given to XeFG) | 14.8 / 23.3 / 17.1 ms | 4.17 ms |
| Time inside XeFG's scheduler per call | 12.3–13.8 ms | 10.8 → 7.3 ms |
| Presents per second | about 84–124 | about 143 |
| Largest gap between presents | up to 864 ms | about 14 ms |
| `deadlines` counter | 0 | frozen at 1478 (set during the 3X period) |

The 143 presents per second after the workaround match the reporter's figure.

## 4. Root Cause

1. The engine's render time (`g_renderTimeNs`, `g_periodNs`) is only sampled in `NoteFrame`, which is reached from
   `ScheduleFrame` (intermediate generated frames) and `PaceFrame`. `NoteFrame` samples only on index ≤ 1, once per
   burst.
2. At 2X (`count == 1`) the only generated frame is the last one (`isLast`, index 1). When XeFG's own scheduler is
   usable (`g_schedNative != nullptr && SchedulerUsable(ctx)`), `TryPace` scheduled intermediate frames and
   **returned for the last frame**, since XeFG schedules that one itself (`0x220411 → 0x3100`). So at 2X nothing
   ever reached `NoteFrame`.
3. With `RenderTimeMs() == 0`, `XeFG_Dx12::Dispatch` fell back to `_ftDelta[fIndex]`. That is the time between
   presents, and it includes XeFG's own scheduler wait (about 13 ms). Feeding that back as `frameRenderTime` is the
   feedback loop described in [FG-XEMFG-RESONANT-LOCK-AND-TIMING-DECOUPLING.md](FG-XEMFG-RESONANT-LOCK-AND-TIMING-DECOUPLING.md):
   XeFG paces against an inflated frame time and holds frames longer, and the next delta is inflated again.
4. At 3X, index 1 is an intermediate frame, so `ScheduleFrame` sampled 4.17 ms. Back at 2X nothing sampled again,
   so the value stayed frozen. The menu workaround only "works" because a frozen, small value is better than the
   inflated one. It does not follow the game, and a heavier scene would still be fed 4.17 ms.

## 5. Prior Attempts (Do Not Repeat)

- `88c85f61` (2026-10-14) made `NoteFrame` run at 2X by letting the last frame fall through to `PaceFrame` while the
  scheduler was usable.
- `9e593639` (2026-10-16) restored the early return, because that fall-through made OptiScaler wait a second time
  on a frame XeFG had already scheduled (Flaw 2 in FG-XEMFG-RESONANT-LOCK-AND-TIMING-DECOUPLING.md). That
  reopened the 2X measurement gap.
- `fd8f6741` (the same last-frame fall-through, for 6X) was reverted in `3eba8a67`.
- So letting the last frame fall through to `PaceFrame` is excluded. The fix below measures without pacing.

## 6. Solution

- New header `OptiScaler/framegen/xefg/XeFGPacingPolicy.h`, with no Windows dependency:
  - `MeasureOnlyLastFrame(schedulerUsable, isLast, index)`: true when XeFG's scheduler handles the last frame and
    that frame starts the burst (`index <= 1`). The last frame has `index == count`, so this only matches at 2X.
  - `PureRenderNs(totalDeltaNs, blockNs)`: the existing `NoteFrame` arithmetic, unchanged. Deltas outside
    [1 ms, 500 ms] are not sampled; otherwise the result is `total − wait`, or a 2 ms floor when the wait covers
    the delta.
- `XeFGPacing::TryPace`, scheduler branch: for the last frame, when `MeasureOnlyLastFrame(...)` is true, call
  `NoteFrame(index, count, now)` and return. There is no `PaceFrame`, no `WaitUntil` and no deadline change.
- `XeFGPacing::NoteFrame` uses `PureRenderNs`, with the same behaviour as before.
- Why the 2X sample is clean:
  - XeFG's own scheduler call for the last frame goes through the `SchedForwarder` hook (thunk `0x3100`), which
    adds its wait to `g_burstBlockQpc` before the present detour at `0x220462`, where `NoteFrame` now runs and
    subtracts it.
  - The base-frame spacing wait happens after `NoteFrame` resets the recorded wait, so it counts toward the next
    real frame.
- Unchanged:
  - 3X–6X, where index 1 is still measured by `ScheduleFrame` and the last frame never matches.
  - `TsDetour`, which still ignores 2X (`countPlus1 < 3`).
  - The `frameRenderTime` floor and clamp in `XeFG_Dx12::Dispatch`, and live multiplier changes from the menu.

Not changed by this fix: after a 3X → 2X switch, the `g_burstStepNs` left over from 3X keeps the base-frame spacing
branch of `Detour` active at 2X. That was already true before the fix and is not part of this report.

## 7. Verification

- `tests/xefg_2x_render_time_unit.cpp` (8/8). It includes the production `XeFGPacingPolicy.h` and reads
  `XeFGPacing.h`:
  1. 2X last frame with the scheduler usable is measure-only.
  2. 3X–6X last frames are never measure-only.
  3. No measure-only without the scheduler or for intermediate frames.
  4. Render time excludes the recorded wait (15 ms delta, 5 ms wait → 10 ms); 2 ms floor when the wait covers the
     delta.
  5. Deltas outside [1 ms, 500 ms] are not sampled.
  6. With the scheduler usable, 2X–6X each sample exactly once per burst.
  7. A 3X → 2X switch keeps tracking the render time instead of freezing it.
  8. `XeFGPacing.h` wires the policy in `NoteFrame` and in the scheduler branch of `TryPace`, with no `PaceFrame` /
     `WaitUntil` there. This test fails on the unfixed header.
- Existing suites pass: `xefg_pacing_engine_unit`, `xemfg_multiplier_and_burst_pacing_unit`,
  `xefg_main_pacing_alignment_unit`, `xemfg_loader_unit`, `xefg_menu_multiplier_queue_unit`.
- clang-format 23.1.4 clean; UTF-8 BOM present on all modified C++ and project files.
- `XeFGPacing.h` passes a clang syntax check with a stub `windows.h`. The only error is the existing
  `std::clamp(int64_t, long long)` in `TsDetour`, which only occurs where `int64_t` is `long` (Linux), not on MSVC.
- **In game: confirmed by the reporter on 2026-11-05.**
  - Build: OptiScaler v0.9.87-pre1 (CI test build containing `4e513fb7`).
  - God of War does not load OptiScaler as `dxgi.dll`, so the reporter loaded it as `winmm.dll`.
  - Result: "XeMFG 2X is working properly and now runs every time the game starts", without the 3X → 2X slider
    workaround.
