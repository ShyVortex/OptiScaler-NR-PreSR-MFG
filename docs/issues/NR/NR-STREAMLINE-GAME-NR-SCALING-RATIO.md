# DLSS-NR: A Game's Streamline NR Does Nothing on an Unsupported GPU (Where Winds Meet)

## 1. Overview & Environment

- **Report**: zephyralistair, [fork #39](https://github.com/ShyVortex/OptiScaler-NR-PreSR-MFG/issues/39) (2026-11-24).
- **Reporter environment**:
  - Game: Where Winds Meet (Global, official launcher). It added its own NR option on 2026-11-24.
  - OS: Windows 11.
  - GPU: RTX 3060 Laptop GPU (Ampere, `0x170`), NVIDIA driver 621.35.
  - OptiScaler-NR-PreSR-MFG v0.9.95-final (`1c8d76b1`), automated install. The game's `nvngx_dlssnr.dll` was copied in.
  - The game uses Streamline 2.14.16 (`sl.dlss`, `sl.dlss_d`, `sl.dlss_g`, `sl.dlss_nr`, `sl.reflex`, `sl.pcl`).
- **Analysis environment**: log and source analysis only, no copy of the game. CachyOS (rolling), kernel
  7.3.5-2-cachyos, NVIDIA GeForce RTX 3080 Ti Laptop GPU, driver 580.196.07.

## 2. Reported Symptoms

- The game's NR option is shown, thanks to OptiScaler's game-NR unlock.
- Turning it on changes nothing in the picture.

## 3. Issue Validity Assessment

This is a real OptiScaler limitation. The game-NR unlock ([NR-GAME-NATIVE-NR-UNLOCK.md](NR-GAME-NATIVE-NR-UNLOCK.md))
was built for games that call NGX feature 18 directly (Metro 2033 Redux). Its §4.6 and §6 left Streamline NR plugins
out. Streamline's `sl.dlss_nr` needs one more piece that the driver withholds on an unsupported GPU.

### 3.1 Evidence from `OptiScaler.log`

| Time | Event |
|---|---|
| 20:43:14.02 | `DLSS-NR (game): GetFeatureRequirements(18) driver result 0x00000001, support bits 0x4 -> unlock`. Bits `0x4` = AdapterUnsupported. |
| 20:43:14.08 | Streamline loads `sl.dlss_nr` 2.14.16. |
| 20:43:16.06 | The driver's NGX loads `Streamline/nvngx_dlssnr.dll`, then: `GPU architecture : 0x170, Snippet expects at least : 0x1B0`, `error: snippet is using newer GPU arch 1B0 > 170`, `Selected feature module (best choice) is disqualified. Feature is not available.` |
| 20:43:17.38 / 17.51 | `GetCapabilityParameters` returns the driver's block (through `NVNGXProxy`). |
| 20:44:59.89 | NR is turned on. `dlss_nrShared.cpp:51 CG2R runtime created with NGX backend`, `DLSSNR runtime created for viewport 0`. |
| 20:44:59.89 onwards | `dlss_nrBackendNGX.cpp:77 [setDLSSNRScalingRatio] NGX DLSSNR scaling-ratio computation callback is missing`, 1407 times, about once per frame. |
| — | No `CreateFeature(18)` reaches OptiScaler, so `DlssNr::GameNr::CreateDirect` never runs. |

### 3.2 Root cause

- `nvngx_dlssnr.dll` publishes `DLSSNRComputeScalingRatioCallback` into an NGX parameter block from its export
  `NVSDK_NGX_D3D12_PopulateParameters_Impl`. This is the same mechanism DLSS uses for `DLSSOptimalSettingsCallback`.
  - The callback reads `PerfQualityValue` and writes `DLSSNR.ScalingRatio`.
  - Upstream recorded this contract in `OptiScaler/dlssnr/forwarder/dlssnr_forwarder.cpp` (`4c5ec5a0`).
- The driver's NGX core calls that export only for a module it accepted. This NR build declares Blackwell (`0x1B0`)
  as its minimum, so on Ampere the module is disqualified, and the capability blocks have no callback.
- `sl.dlss_nr`'s NGX backend asks for the ratio before it creates the feature. Without the callback it stops there on
  every frame, and the game shows no NR.
- Metro doesn't ask for the ratio, so the direct-runtime path was enough there.

### 3.3 Prior-attempt audit

- No document in `docs/issues/` and no commit publishes a module's callbacks for a game.
- Upstream's Streamline NR commits (`7543d143`, `6c42ad3a`, `42127e2d`) cover OptiScaler's own NR through Streamline,
  not a game's `sl.dlss_nr` on an unsupported GPU.
- Rule §6: the Windows refusal after `FAIL_OutOfDate` ([DLSS Unlocked #60](https://github.com/ShyVortex/DLSS-Unlocked/issues/60)) does not apply. This driver answers
  `0x1`, and 621.35 is above the 610.88 gate.

## 4. Applied Solution (`04f03527`)

When this session has unlocked feature 18, OptiScaler does what the driver does for an accepted module: it lets the
game's `nvngx_dlssnr.dll` publish its callbacks into the parameter blocks the game gets from NGX.

- `OptiScaler/dlssnr/DlssNr_GameNr.h`:
  - `ScalingRatioCallbackKey` (`"DLSSNRComputeScalingRatioCallback"`);
  - `ShouldPublishCallbacks(unlocked, hasCallback)`: only when unlocked and the block lacks the callback.
- `OptiScaler/dlssnr/DlssNr_CompatibilityRuntime.h/.cpp`:
  - The module's `NVSDK_NGX_D3D12_PopulateParameters_Impl` is resolved as an optional export, so older runtimes
    still open.
  - `CompatibilityRuntime::Populate(params)` calls it under the module mutex and `CallerScope`, like
    `Create` / `Evaluate`. Without the export it returns `FAIL_FeatureNotSupported`.
- `OptiScaler/dlssnr/DlssNr_GameNrRuntime.h/.cpp`:
  - `NoteUnlocked()` / `Unlocked()`: an atomic flag.
  - `PublishCallbacks(device, params)`:
    - opens the direct runtime with `CompatibilityRuntime::TryOpen(device)` and retains it until NGX shutdown, like
      Preload, so it's the same initialised module that `CreateDirect` uses later;
    - calls `Populate`, then reads the key back;
    - logs once, either `DLSS-NR (game): published NR callbacks into the NGX parameters (...)` or a warning naming
      the populate result.
- `OptiScaler/inputs/NVNGX_DLSS_Dx12.cpp`:
  - `GameNrFeatureRequirements` calls `NoteUnlocked()` only on `Decision::Unlock`.
  - `GetCapabilityParameters` (driver and custom block) and `PopulateParameters_Impl` call `PublishCallbacks`.
- After that, `sl.dlss_nr` should call `CreateFeature(18)`. The driver refuses it, and the existing
  `ShouldCreateDirect` path creates the feature through the direct runtime.

### 4.1 Not changed

- GPUs and drivers that support NR: they never unlock, so nothing is published.
- The Windows refusal after `FAIL_OutOfDate`.
- Vulkan and DX11 NGX entry points.

### 4.2 Open risks

- `sl.dlss_nr` may need more than this callback: another key, or a step of `CreateFeature` / `Evaluate` through the
  direct runtime. The new log lines and the existing `DLSS-NR (game): direct runtime CreateFeature(18)` line show how
  far it gets.
- The direct runtime is confirmed on Linux (Metro) only. This is the first Windows report for it (RTX 30, 621.35).
- Opening the runtime at `GetCapabilityParameters` costs what Preload costs: about 81 ms with a warm cache, a few
  seconds on a cold disk (276 MB DLL). It happens once per session.
- Where Winds Meet is an online game; OptiScaler's multiplayer warning applies.

## 5. Verification

### 5.1 Unit tests

- `tests/nr_streamline_scaling_ratio_unit.cpp` (6/6):
  1. `ShouldPublishCallbacks` is true only for unlocked + missing.
  2. The reporter's case (`0x1`, bits `0x4`, Windows, gate open) unlocks. Windows `FAIL_OutOfDate` passes through.
  3. `NoteUnlocked()` runs only after the pass-through return in `GameNrFeatureRequirements`.
  4. Both `GetCapabilityParameters` branches and `PopulateParameters_Impl` call `PublishCallbacks`.
  5. `Populate` takes the module mutex, then `CallerScope`, then calls the export. The export isn't required to open
     a runtime.
  6. `PublishCallbacks` opens, retains, then populates.
  - The source checks fail on the unfixed sources.
- Related suites: `nr_game_native_unit` (13/13), `nr_driver_gate_unit` (14/14).
- Not compiled with MSVC locally. The CI build is the compile check.

```bash
g++ -std=c++20 -o nr_streamline_scaling_ratio_unit tests/nr_streamline_scaling_ratio_unit.cpp
./nr_streamline_scaling_ratio_unit
```

### 5.2 In-game confirmation (confirmed, 2026-11-24)

The reporter confirmed that the game's NR works with v0.9.96-pre3 (`52f3ec3`, CI run 45418143387) on the same RTX
3060 Laptop and Windows 11 setup. They attached a LogLevel 0 log:

| Time | Line |
|---|---|
| 02:24:21.47 | `GetFeatureRequirements(18) driver result 0x00000001, support bits 0x4 -> unlock` |
| 02:24:25.36 | `published NR callbacks into the NGX parameters (DLSSNRComputeScalingRatioCallback=0x7ff92ecc2740), populate result=0x00000001` |
| 02:25:50.94 | `direct runtime CreateFeature(18) result=0x00000001`, 1920x1080 |

- `scaling-ratio computation callback is missing` appears 0 times (1407 before the fix).
- There are no NR warnings or errors after the create. Streamline logs one `Unable to find 'dlssnr' constants for frame
  3338`, which it marks as harmless.
- This is the first Windows confirmation of the direct runtime (§4.2's second risk).
- The game's preset hint is `2147483650` (`0x80000002`), which `ModelName` reports as an unknown preset. It works;
  the meaning of the high bit is not known.
