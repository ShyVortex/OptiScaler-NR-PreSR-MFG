# Pragmata: DLSS-G Out of VRAM on a 6 GB GPU (Black Screens) — Mitigation Tried and Abandoned

## 1. Overview & Environment

- **Issue**: [DLSS Unlocked #66](https://github.com/ShyVortex/DLSS-Unlocked/issues/66), "Black screen appearing
  during Pragmata gameplay on 2nd chapter while DLSS MFG is on and game crashing while XeMFG on". Closed on
  2026-11-07 with XeMFG `Source=Game` as the recommended mode.
- **Reporter's environment** (from the issue and its logs):
  - Windows 11 (10.0.26300), NVIDIA GeForce RTX 2060 Mobile 6 GB, driver 621.01.
  - Pragmata 1.3.0.0 (Steam), RE Engine. REFramework fork build bundled with DLSS Unlocked (`fork-release-16`).
  - DLSS Unlocked NR-v0.9.86 (OptiScaler 0.9.86-final, `c70356cb`); test build v0.9.87-pre2 (`bdb30a6`). RTX 20/30
    unlocker sdli1995 0.4.1, External mode.
- **Analysis environment** (verified by command): CachyOS, kernel 7.3.1-1-cachyos, NVIDIA GeForce RTX 3080 Ti
  Laptop GPU, driver 580.196.07.
- **Outcome**: no OptiScaler change. The mitigation (`d21d46fc`, branch `fix/dlssg-zero-frames-off`) failed in game
  and was not merged.

## 2. Reported Symptoms

1. With DLSS MFG (RTX 20/30 unlocker), black screens appear during gameplay at the start of chapter 2, while panning
   the camera. They also happen at 2X.
2. The game crashes with XeMFG.

## 3. Findings That Are Not OptiScaler Defects

- **XeMFG crash: configuration.** `[XeMFG] Source=OptiScaler` is meant for games without frame generation. Pragmata
  has native DLSS-G, and its Streamline DLSS-G kept running next to OptiScaler's own XeFG. The XeMFG log ends with a
  Streamline exception after a second swapchain was created. With `Source=Game` the reporter confirmed that XeMFG
  works in the same scene.
- **REFramework is installed correctly.** Its log reaches "REFramework initialized". The "Could not find … DD2 /
  RE9" `IntegrityCheckBypass` errors are signature scans for other games.
  - It copies the game-root DLLs into `_storage_` at every launch, so OptiScaler appears as
    `_storage_\dxgi.dll`. The 0.9.85 file version seen there came from sessions before the update.
  - It hooks `LdrLoadDll` (`[XeFG][LoaderHandoff]`), so OptiScaler logs `dinput8.dll` as the caller of every load.
  - With the external unlocker, dlssg_sm86's `foreign_fg_mod` event only notes OptiScaler; nothing is changed.

## 4. Root Cause: Not Enough Video Memory for DLSS-G

Pragmata stays close to 6 GB even at the lowest settings (maintainer's observation), which leaves no room for DLSS-G
on a 6 GB card.

| | 3X session (0.9.86) | 2X session (0.9.86) | 2X session (0.9.87-pre2, mitigation) |
|---|---|---|---|
| VRAM used vs Streamline budget | 5.19 / 5.00 GB | 5.40 / 5.00 GB | — |
| `eWarnOutOfVRAM` refusals | 1 (2 generated frames) | 3 (1 generated frame) | 2 (1 generated frame) |
| Calls with 0 frames, rejected `eErrorInvalidParameter` | 378 | 1,919 | 878 (sent as `eOff` + 0) |

- In the 0.9.86 sessions, after the first `eWarnOutOfVRAM` the game sent `eOn` with `numFramesToGenerate 0` until
  it exited. Streamline rejects that request ("numFramesToGenerate (0) must be greater than 0").
- The 0 does not come from OptiScaler. `hkslDLSSGSetOptions` only changes the count for a menu override, and a menu
  override of 0 already sends `eOff`.

## 5. Mitigation Tried and Why It Was Abandoned

**Tried** (`d21d46fc`, test build v0.9.87-pre2): in `hkslDLSSGSetOptions`, turn `eOn` + 0 frames into `eOff`, and
clear `eRetainResourcesWhenOff` so Streamline frees DLSS-G's memory.

**Result in game (reporter, 2026-11-06): black screens unchanged.** The log shows why:

1. **Streamline validates `numFramesToGenerate > 0` even for `eOff`.** All 878 converted calls were rejected
   (`mode eOff, generated 0, result eErrorInvalidParameter`), so DLSS-G kept its last accepted options and the
   change had no effect. The unit test only checked that the hook set `eOff`, not what Streamline accepts. Any
   future "off" sent on the game's behalf must carry a valid frame count (≥ 1).
2. **The game switches frame generation on and off by itself.**
   - It starts sending 0 frames at 05:18:47, six seconds before the first `eWarnOutOfVRAM` (05:18:53).
   - It then alternates between 0 and ≥ 1 frames every 25–500 ms for more than a minute (05:18:47–05:19:53).
   - Correcting point 1 (`eOff` with 1 frame, resources released) would make Streamline free and re-create DLSS-G
     several times per second. That was judged likely worse than the black screen: hitches, flicker, more memory
     churn.
   - Keeping the resources while off would avoid the churn but free nothing.
   - A real fix would need hysteresis that overrides the game's own frame generation decisions over time. That is a
     lot of risk for one game on an under-spec card.

**Decision:** no OptiScaler change. The branch `fix/dlssg-zero-frames-off` stays unmerged.

## 6. Resolution for Users

- On 6 GB GPUs, Pragmata does not have the video memory for DLSS-G (native or through the RTX 20/30 unlocker).
- Use XeMFG with `[XeMFG] Source=Game`: the game's DLSS-G inputs are routed to XeSS-FG. The reporter confirmed it
  works in the same scene.
- If this pattern (`eOn` + 0 frames after `eWarnOutOfVRAM`) shows up in another game, start from section 5: a
  converted request must use `numFramesToGenerate ≥ 1`, and check first whether the game alternates on its own.
