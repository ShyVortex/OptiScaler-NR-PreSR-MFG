# Neural Rendering: Unlocking a Game's Own DLSS NR Option on Unsupported GPUs (Metro 2033 Redux)

## 1. Overview & Environment
- **Found by**: the maintainer on 2026-10-31.
- **Environment**:
  - CachyOS, kernel 7.3.1-1-cachyos;
  - NVIDIA GeForce RTX 3080 Ti Laptop GPU (Ampere), driver 580.196.07;
  - Proton (`Running on Wine 11.0`).
- **Game**: Metro 2033 Redux (Steam, `metro.exe`, 4A Engine), with the Next-Gen update. It ships DLSS 310.9.16 and its
  own DLSS Neural Rendering option.
  - It runs NGX through **D3D12** and loads `nvngx.dll` from its own folder, which OptiScaler (installed as
    `dxgi.dll`) answers with its NGX exports.
  - It doesn't use Streamline for NR.
- **Builds**: v0.9.81, `e655fe43` (requirements), `4b1eb4da` (routing), `0575318b` and `5c4ccfd9` (OptiScaler's NR
  yields), `996ac78d` (Preload NR).

## 2. Reported Symptoms
- The game's NR option is hidden on an RTX 3080 Ti, a GPU NVIDIA does not officially support for NR.
- OptiScaler's own NR works in the same game (on Linux since `6a7ad0ec`, see `NR-OLD-DRIVER-HANG.md` section 8).

## 3. Issue Validity Assessment
**A real OptiScaler limitation, not a misconfiguration.**
- **Start-up:** the game asks `NVSDK_NGX_D3D12_GetFeatureRequirements` for feature **18**, which is Neural
  Rendering: the same ID the fork creates in `DlssNr_Proxy.cpp`.
  - OptiScaler faked support only for SR and FG, and forwarded 18 to the driver.
  - The answer was `0xBAD0000C` (`FAIL_OutOfDate`) with support bits `0x4` (`AdapterUnsupported`). `nvngx.log`:
    "required feature is not supported by NGX runtime, please update display driver".
  - So the game hides the option.
- **With the option shown** (checkpoint after `e655fe43`):
  - the game called `CreateFeature(18)` once per toggle;
  - OptiScaler passed it to the driver (`Passthrough to native NGX CreateFeature for feature 18`);
  - it failed (`0xBAD0000C`), and the game silently kept NR off.
  - No other NR-specific NGX calls were made (no scratch-size or capability-key queries).
- **The fork already has a runtime that works on unsupported GPUs:** `DlssNr::CompatibilityRuntime`, which loads
  `nvngx_dlssnr.dll` directly.
- **Audit:**
  - No earlier `docs/issues/` entry or commit routed a game-created NR feature.
  - Constraint (architecture §6, `NR-OLD-DRIVER-HANG.md`): on **Windows**, a driver without NR support
    (`FAIL_OutOfDate`, or below the 610.88 gate) must never reach the direct runtime. It hung the GPU until reboot on
    [DLSS Unlocked #60](https://github.com/ShyVortex/DLSS-Unlocked/issues/60).

## 4. Applied Solution
Per the maintainer's decisions (2026-10-31 / 2026-11-02):
- the unlock is **universal**: Windows and Linux;
- **the game's NR wins** over OptiScaler's own;
- OptiScaler's NR must **not** resume on its own afterwards.

### 4.1 The rule (`OptiScaler/dlssnr/DlssNr_GameNr.h`, pure)
`Decide(onWine, enabled, hasCandidate, driverBlocked, driverResult, supportBits)` returns `PassThrough` or `Unlock`.
- **PassThrough** in these cases:
  - the driver already supports NR for the GPU;
  - `[DlssNr] UnlockGameNR` is off;
  - no `nvngx_dlssnr.dll` candidate exists;
  - **on Windows only**, the driver lacks NR (`FAIL_OutOfDate`, the `DriverVersionUnsupported` bit, or
    `DriverGate::Blocked()`).
- **Unlock** otherwise:
  - on Windows, an unsupported GPU on a capable driver;
  - on Wine, any answer, because the Linux NGX has no NR feature at all.
- Support bits are only trusted when the query itself succeeded.
- `ShouldCreateDirect` applies the same rule after the driver's `CreateFeature(18)` produced no feature.

### 4.2 Requirements (`e655fe43`)
- `NVSDK_NGX_D3D12_GetFeatureRequirements(18)` asks the driver, then applies `Decide`. On Unlock it reports Success
  / Supported.
- Every decision is logged: `DLSS-NR (game): GetFeatureRequirements(18) driver result ..., support bits ... -> ...`.

### 4.3 Routing (`4b1eb4da`, `OptiScaler/dlssnr/DlssNr_GameNrRuntime.h/.cpp`)
- **Create:** after a refused driver create, `CreateFeature(18)` creates the feature through `CompatibilityRuntime`
  with the game's (driver-allocated) parameters.
- **Handles:** they are keyed by **pointer** and not recorded in `HandleToFeature`: their ids come from
  `nvngx_dlssnr.dll` and may collide with driver handle ids.
- **Evaluate / Release:** for those handles, these calls are routed before any id-based path. Releasing one never
  reaches the FG cleanup that `ReleaseFeature` runs for other handles.
- **Lifetime:** a released feature's runtime stays loaded until NGX shutdown, so toggles don't re-initialise it.
  Shutdown releases anything the game left.

### 4.4 The game's NR wins (`0575318b`, `5c4ccfd9`)
- `OptiScaler/dlssnr/DlssNr_OwnNr.h`: `OwnNrEnabled() = DlssNrEnabled && !GameNr::Active()`.
  - Every DX12 runtime check of OptiScaler's NR uses it: 15 sites in 8 files.
  - `GameNr::Active()` is an atomic counter, because it's read several times per frame.
- When the game's feature is created, `DlssNrEnabled.set_volatile_value(false)`. This is the driver-gate mechanism:
  `value_for_config()` keeps the saved value, so the INI is never rewritten.
- While the game's NR runs:
  - the "Enable Neural Rendering" checkbox is disabled, with a yellow explanation (`GameNrActiveMessage`) above it
    and in its tooltip;
  - the NR hotkey shows the message as a warning instead of toggling.
- When the game turns its NR off, the checkbox is usable again but stays unticked. OptiScaler's NR returns only when
  the user ticks it. The next launch uses the saved value.
- **Why not auto-resume** (the first version, `0575318b`): resuming switched from the game's NR to OptiScaler's
  without being asked, and recreated OptiScaler's models mid-game.

### 4.5 Preload NR (`996ac78d`)
- **Measured first enable without preload:**
  - the driver refusal → `nvngx_dlssnr.dll` loaded: **6.4 s**;
  - `Init_Ext`: 0.01 s;
  - first `CreateFeature(18)`: 1.3 s.

  Later toggles took 0.19 s.
- **The setting:** `[DlssNr] Preload` (default false), and "Preload NR (restart)" with a help marker in the NR menu.
- **What it does:** when on, the game's NGX D3D12 init (right after the driver init succeeds) opens the runtime on
  that device and keeps it until NGX shutdown. `ShouldPreload` also requires a candidate, and is never true below the
  610.88 gate.
- **Measured with preload:** **81 ms** with a warm file cache. The 6.4 s was mostly a cold disk read of the 276 MB
  DLL, so preloading helps most on the first launch after a reboot.
- **What it can't remove:** the 1.3 s first model build needs the game's parameters.

### 4.6 Not changed
- Windows drivers without NR support.
- Drivers that already support NR for the GPU.
- Streamline NR plugins.
- Vulkan and DX11 NGX entry points (Metro is DX12).
- OptiScaler's NR placement and model code.

### 4.7 Windows-only harness seams
The harnesses that compile production slices got matching seams: `OwnNrEnabled` in `nr_streamline_hooks` and
`nr_private_seam`, and `GameNr::Shutdown` in `nr_shutdown_smoke`.

## 5. Test Verification
- `tests/nr_game_native_unit.cpp` (12/12 PASS):
  1. Windows unlock.
  2. Windows refusals.
  3. Wine unlock.
  4. Pass-through cases.
  5. Support bits ignored on a failed query.
  6. Requirements routing, config and INI.
  7. The create rule.
  8. Routing order and pointer keys.
  9. The `OwnNrEnabled` gate on the DX12 runtime sites.
  10. Harness seams.
  11. The volatile off, the locked checkbox and the hotkey guard.
  12. The Preload rule, config, menu, init placement and retention.
- `tests/nr_driver_gate_unit.cpp` (14/14): test 9 now expects the shared checkbox lock (`driverBlocked || gameNr`).
- The related suites still pass. clang-format 23 clean; UTF-8 BOM present.
- **In game (maintainer, Linux, Metro 2033 Redux):**
  - the option appears;
  - the game's NR runs through the direct runtime;
  - it toggles cleanly, keeps XeFG working, and shuts down cleanly;
  - OptiScaler's NR is turned off and its checkbox greyed with the yellow text while the game's NR runs, and stays
    off afterwards;
  - Preload loads at start-up (81 ms).
  - An apparent frame-rate drop with Preload on could not be reproduced (other apps were open; the log showed no NR
    activity before the first toggle).
- **Not yet checked:** Windows in game (an unsupported GPU on a ≥ 610.88 driver).

## 6. Out of Scope
- Streamline-based NR integrations in other games.
- Vulkan / DX11 game-native NR.
- The 610.88–616.63 note in `NR-OLD-DRIVER-HANG.md`: if such a driver answers `FAIL_OutOfDate`, it stays refused.
