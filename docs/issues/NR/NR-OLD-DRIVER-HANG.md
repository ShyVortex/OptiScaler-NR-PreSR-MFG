# Neural Rendering: GPU Hang on Drivers Without NR Support (DLSS Unlocked #60)

## 1. Overview & Environment
- **Report**: DLSS Unlocked issue #60 (https://github.com/ShyVortex/dlss-unlocked/issues/60), "TLOU 2 freezes when
  Neural rendering is enabled".
- **Reporter environment**:
  - Windows 10 (10.0.19045), NVIDIA GeForce RTX 2080 Ti (Turing), driver **576.88** (NGX dispatcher `_nvngx.dll`
    32.0.15).
  - The Last of Us Part II, DLSS Unlocked 1.7.2 from Nexus (OptiScaler v0.9.72, `80948410`).
  - External FG with the SM75 MFG unlocker active.
  - `nvngx_dlssnr.dll` 310.9.16 beside the game.
- **Working comparison** (maintainer): Windows 11 (10.0.26300), RTX 3080 Ti Laptop GPU, newer driver (dispatcher
  32.0.16), same game and runtime.
- **Analysis environment** (logs only): CachyOS rolling, kernel 7.3.0-4-cachyos, NVIDIA GeForce RTX 3080 Ti Laptop
  GPU, driver 580.178.04.

## 2. Reported Symptoms
Enabling Neural Rendering freezes the game, and only a PC restart recovers. The maintainer could not reproduce it.

## 3. Issue Validity Assessment
Genuine. The reporter's driver is too old, but OptiScaler turned that into a hard GPU hang instead of refusing.

Reporter's log:
```text
[NVSDK_NGX_CreateFeature_Validate:661] error: required feature is not supported by NGX runtime, please update display driver
NR diagnostic CreateFeature(18): result=0xBAD0000C handle=0x0      <- NVSDK_NGX_Result_FAIL_OutOfDate
NR compatibility: loaded ...\nvngx_dlssnr.dll with 2 named caller-path imports, no helper DLL
NR compatibility: Init_Ext result=0x00000001
```
After that the render thread never logs again. Only the input thread continues, for about 50 s.

Maintainer's log:
```text
[NVSDK_NGX_CreateFeature_Validate:762] app id is 142241640                   <- validation passes
NR diagnostic CreateFeature(18): result=0xBAD0000B handle=0x0      <- FAIL_UnableToInitializeFeature
NR compatibility: candidate ...\nvngx_dlssnr.dll CreateFeature(18) result=0x00000001 handle=0x2
DLSS-NR: feature created at 1512x848 through direct compatibility runtime
```

## 4. Root Cause
- The direct compatibility runtime (`docs/NR-DIRECT-RUNTIME.md`) exists for a driver that knows NR but whose signed
  loader rejects the RTX 20/30/40 `nvngx_dlssnr.dll`. That rejection is `FAIL_UnableToInitializeFeature`, the
  maintainer's case.
- Upstream `47e134cb` widened the fallback to any failure without a handle (`docs/NR-RESTRICTION-AUDIT.md`). That
  included `FAIL_OutOfDate`, where the driver has no NR support at all.
- On 576.88 OptiScaler therefore created the model directly against a driver without NR support, and the GPU/driver
  hung.
- The cutoff:
  - 616.64 is the first driver with official NR support.
  - The maintainer verified that NR works on 610.88.
  - 576.88 fails.

## 5. Prior-Attempt Audit
- No earlier `docs/issues/` entry covers driver support.
- `NR-RESTRICTION-AUDIT.md` removed an NR **DLL** version/hash/size gate in v0.8.2. The fix below gates on the
  **driver** and leaves that DLL gate removed.
- The fork's contextual post-SR driver error (architecture rule §6) is preserved.

## 6. Solution Applied
- **`97f35314`: version gate.**
  - `IdentifyGpu::queryNvapi` reads `NvAPI_SYS_GetDriverAndBranchVersion` into `GpuInformation::nvidiaDriverVersion`.
  - `dlssnr/DlssNr_DriverGate.h` sets the minimum to 610.88 (`61088`, the oldest version verified to work) and
    recommends 616.64 (`61664`, the first official).
  - On native Windows below the minimum, the GPU worker sets a volatile `DlssNrEnabled = false`:
    - every NR path stays off;
    - the user's INI is never rewritten (`value_for_config` saves their own value);
    - the block survives `Config::Reload` and profile loads (`set_from_config` never replaces a set value).
  - Wine/Proton is exempt, because dxvk-nvapi reports Linux driver numbers. An unknown version does not block.
  - The NR proxy also refuses creation before any NGX call while blocked.
- **`521138df`: menu and hotkey.**
  - The "Enable Neural Rendering" checkbox is disabled while blocked.
  - An amber line and tooltip read: "Neural Rendering requires NVIDIA driver 610.88 or newer (616.64+ recommended;
    detected X). Update your driver to enable it."
  - The NR hotkey shows that message as a warning toast and consumes the key instead of toggling.
- **`dec66857`: no direct runtime after `FAIL_OutOfDate`.**
  - Every other failure still falls back.
  - The status reason becomes "the NVIDIA driver does not support Neural Rendering; update to 610.88 or newer
    (616.64+ recommended)".
  - As first committed this also covered Wine. That was wrong and broke NR on Linux; since `6a7ad0ec` the refusal
    applies on Windows only, including unknown versions. See section 8.
- `OptiScaler.ini` documents the requirement under `[DlssNr] Enabled`. `NR-RESTRICTION-AUDIT.md` lists the
  OutOfDate exclusion as retained.

## 7. Verification
- `tests/nr_driver_gate_unit.cpp` (13/13 PASS at the time; 14/14 since `6a7ad0ec`) covers:
  - the 61087/61088 boundaries, with 616.64 allowed;
  - Wine exempt and unknown allowed;
  - the messages;
  - the NVAPI source;
  - startup order;
  - reload survival;
  - the proxy refusal before NGX;
  - the menu and hotkey guards, and that only those two sites write `DlssNrEnabled`;
  - the OutOfDate exclusion with every other failure still falling back;
  - the status reason, with the contextual display-resolution message kept.

  Each group fails against the pre-fix sources.
- These suites still pass:
  - `nr_status_reporting`, `nr_format_bouncing`, `nr_candidate_paths_fallback`, `nr_pipeline_setup`,
    `nr_model_preset`, `nr_halo_dx12`, `nr_placement_config`, `nr_finished_picture_gate`, `nr_model_preset_menu`;
  - the `dlssg_sm86`, `turing_*` and XeFG/XeMFG suites.
- clang-format 23 clean; UTF-8 BOM present. `IdentifyGpu.h/.cpp` were missing it and now have it.
- **Pending:**
  - An MSVC build.
  - In game:
    - NR still enables on a driver ≥ 610.88;
    - the #60 reporter (576.88) sees the disabled checkbox and message, with no hang.

## 8. Follow-up: Linux Regression in v0.9.80–v0.9.81 (`6a7ad0ec`)
- **Found by**: the maintainer on 2026-10-31, in Metro 2033 Redux under Proton (`Running on Wine 11.0`), with
  OptiScaler v0.9.81-final (`5a799ec7`).
- **Environment**: CachyOS, kernel 7.3.1-1-cachyos, NVIDIA GeForce RTX 3080 Ti Laptop GPU, driver 580.196.07.
- **Symptom**: OptiScaler's own Neural Rendering never runs. The status reads "the NVIDIA driver does not support
  Neural Rendering; update to 610.88 or newer (616.64+ recommended)", and the log shows:
  ```
  DLSS-NR (driver): the NVIDIA driver does not support Neural Rendering; ...; not trying the direct runtime
  DLSS-NR driver creation for pass 1 failed: 0xBAD0000C (FAIL_OutOfDate)
  ```
- **Cause**:
  - The Linux driver's NGX has no Neural Rendering feature and answers `FAIL_OutOfDate` to feature 18 every time.
    The game's own query in the same session gets the same answer (`nvngx.log`: "required feature is not
    supported by NGX runtime, please update display driver").
  - Under Wine the direct compatibility runtime (`DlssNr::CompatibilityRuntime`, loading `nvngx_dlssnr.dll` itself)
    is therefore the only NR path. `dec66857` refused it after `FAIL_OutOfDate` on every platform.
  - The hang it prevents was on a Windows driver (576.88), and the version gate already exempted Wine.
- **Fix** (`6a7ad0ec`):
  - `DriverGate::RefusesDirectRuntime(result, onWine)` is true only for `FAIL_OutOfDate` off Wine.
  - The NR proxy and the "update to 610.88" status in `DlssNr_Dx12_Models.cpp` both decide through it, with
    `State::isRunningOnLinux`.
  - On Windows nothing changes: `FAIL_OutOfDate` never reaches the direct runtime. On Wine it falls back as it did
    before v0.9.80.
- **Verification**:
  - Test 14 in `tests/nr_driver_gate_unit.cpp`: the truth table (Windows refuses, Wine falls back, other failures
    and success are never refused), and source checks that both sites decide only through the helper.
  - `nr_halo_dx12` and `nr_motion_scale` still pass. clang-format 23 clean; UTF-8 BOM present.
  - **Pending**: an MSVC build, then NR on Linux in game (expected: `NR compatibility: candidate ...
    CreateFeature(18) result=0x00000001`).
- **Not addressed here**: the game's own NR option (Metro Redux) stays hidden, because NGX reports feature 18 as
  unsupported to the game. Exposing it would be a separate change.
