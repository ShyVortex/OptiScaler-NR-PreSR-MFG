# Frame Generation: One DLSS Frame Generation Section and RTX 40 Option Parity

## 1. Overview & Environment
- **Report**: private requests to the maintainer (email, DMs), not tracked on GitHub. RTX 40 users found the RTX 40 MFG
  unlock neglected next to the RTX 20/30 unlocker.
- **Work branch**: `feat/unified-dlssg-section`, from `main` (`6bc488d4`, v0.9.91).
  - Commits: `00df9c6e` (selection), `a11ce345` (menu), `b403f503` (ceiling), `1397ca97` (preset), `93f7e89a`
    (arch-spoof check).
- **Development environment**: CachyOS rolling, kernel 7.3.4-1-cachyos, NVIDIA GeForce RTX 3080 Ti Laptop GPU (PCI
  device `0x2460`), driver 580.196.07. Wine 11.22 (registry check, section 4.1), clang-format 23.1.5, GCC 16.2.1.
- **Not available here**: an RTX 40 GPU, and an MSVC build. The RTX 40 options are checked by unit tests only, until
  RTX 40 testers confirm them (section 7).
- **Considered first and parked**: replacing both unlockers with dashdogy's MFG Unlocker. That plan was parked,
  because:
  - it cannot be built from source;
  - its RTX 30 path is broken under Proton in v1.7.1 (dashdogy/MFG-Unlocker issue 172);
  - it overlaps OptiScaler's XeMFG and NR (DLSS 5 = NGX feature 18).

## 2. Reported Issue
- The two unlockers lived in different parts of the frame generation menu:
  - RTX 40: one checkbox and "Retarget Blackwell kernels";
  - RTX 20/30: its own header with about ten options.
- RTX 40 had no FG preset, no multiplier ceiling and no plain-language status.
- Users had to know which unlocker matched their GPU, and the two had separate ini switches
  (`AdaMfgUnlock`, `AmpereMfgUnlock`).

## 3. Validity Assessment
- **Valid as a UX and feature-parity gap**, not a crash or a regression:
  - `Config.h` had 2 RTX 40 settings and 14 RTX 20/30 settings;
  - the menu placed them in unrelated blocks.
- Both unlockers already checked the architecture before patching, so a single selector can only route to the
  matching one. Every gate kept reading the existing `FGDLSSGAdaMfgUnlock` / `FGDLSSGAmpereMfgUnlock` flags, so the
  change is confined to config load, the menu and two RTX 40 options.

### 3.1 RTX 20/30 options and RTX 40

| RTX 20/30 option | RTX 40 | Reason |
|---|---|---|
| `AmpereMfgPreset` | **Ported** (`AdaMfgPreset`) | DLSS-G reads its preset from the public driver profile key `0x10E41DF1`. |
| `AmpereMfgMaxFrames` | **Ported** (`AdaMfgMaxFrames`) | A ceiling on what is advertised. |
| Plain-language status | **Ported** | Status lines in the RTX 40 sub-block. |
| `AmpereMfgSpoofArchToGame` | **Not needed** | Section 5. |
| `AmpereMfgMode` | Not ported | The RTX 40 patch runs only while OptiScaler owns DLSS-G; never in External mode. |
| `KernelImage`, `Router`, `HardwareBilinear`, `Optimized`, `LogLevel` | Not ported | Options of sdli1995's own kernels and runtime. RTX 40 runs NVIDIA's kernels; its counterpart is `AdaBlackwellKernels`. |
| `LinuxFsrFallback`, `LinuxFallbackType` | Not ported | A workaround for the sdli1995 Linux 2X problem; nothing like it is reported for RTX 40. |
| `AmpereMfgBlockGameXeFG` | Not ported | Protects External mode only. |
| Dynamic MFG on RTX 40 | Later | It needs more provider patching. That is new work, not a port. |

## 4. Solution Applied

### 4.1 One switch, chosen by the GPU (`00df9c6e`)
- **Keys:** `[DLSSG] MfgUnlock` (true / false) and `MfgUnlocker` (`auto`, `ada`, `turing-ampere`).
  - `auto` starts the RTX 40 unlock on RTX 40 and the RTX 20/30 unlocker on RTX 20/30.
  - A manual choice for another GPU family starts nothing, so External FG is never forced on the wrong card.
  - A manual choice applies as given when the GPU was not identified.
  - Pure rules: `OptiScaler/framegen/dlssg/MfgUnlockerSelect.h`.
- **Detection before IdentifyGpu.** External FG mode is decided in `DllMain` (`dllmain.cpp`, "Initial state of FG"),
  long before the `getGpuInfo` worker identifies the GPU. `misc/EarlyGpuArch` reads PCI ids at the first config
  load:
  1. D3DKMT `KMTQAITYPE_PHYSICALADAPTERDEVICEIDS` for the present adapters, when gdi32 is already loaded;
  2. otherwise the `Display` entries of `HKLM\SYSTEM\CurrentControlSet\Enum\PCI`. Wine fills them in: under Wine 11.22
     this machine has `VEN_10DE&DEV_2460&SUBSYS_00000000&REV_00` with `Class=Display`. On Windows they also keep
     removed GPUs, so they are only a fallback.
- **Plain calls only.** The first config load runs before `NtdllProxy` / `KernelBaseProxy::Init`, so detection uses
  only plain kernel32/advapi32 calls and loads nothing.
- **Device-id table.** It was checked against every NVIDIA display entry of `/usr/share/hwdata/pci.ids`: all 288
  TU1xx / GA102-107 / AD1xx ids map to their family. GA100, GH100, GB1xx/GB2xx and older chips map to "no unlocker".
- **Effect on the flags.** The resolved owner sets `FGDLSSGAdaMfgUnlock` / `FGDLSSGAmpereMfgUnlock` with
  `set_from_config`. Downstream gates (rules §1, §4, §8) are unchanged.
- **Legacy keys.** `AdaMfgUnlock` / `AmpereMfgUnlock` are still read while `MfgUnlock=auto`. Both true now leave the
  choice to the GPU; before, RTX 20/30 won. Save writes the new keys and the legacy keys as `auto`.
  - Consequence: after a save, a hand edit of `AmpereMfgUnlock=false` is ignored. `MfgUnlock=false` is the switch, and
    the docs say so.
- **Save keeps a setting that could not apply.** If no unlocker could start in this session (no NVIDIA GPU, a
  mismatched choice), Save keeps the stored switch and choice (`ToSaved`).
- **Startup log.** The worker logs `DLSS FG unlocker: enabled=… choice=… arch=… (source …, adapters […]) -> …`. It warns
  when IdentifyGpu reports another NVIDIA family than the startup detection.
- **Packaging** refuses a package with `MfgUnlock`, `AdaMfgUnlock` or `AmpereMfgUnlock` switched on.

### 4.2 The "DLSS Frame Generation" section (`a11ce345`)
- **Layout.** One collapsing header holds the enable switch, an "Unlocker" combo and the sub-block of the configured
  unlocker:
  - RTX 40: Blackwell kernels, ceiling, preset and status;
  - RTX 20 / 30: every existing control, moved without changes.
- **Pure rules:** `OptiScaler/menu/MfgSectionPolicy.h`.
- **Locked** without an NVIDIA GPU, on RTX 50 ("runs MFG natively") and on older NVIDIA cards. An enabled switch can
  still be turned off there.
- **Combo entries.** The entry for the other GPU family is greyed out; both stay choosable when the GPU was not
  identified.
- **Notes:** a mismatched saved choice, the RTX 40 unlock in an External FG session, and a startup detection that
  disagrees with the driver. A restart hint appears when the configured unlocker differs from the session's.
- **Writing the flags.** A menu change writes the flags the way config load resolves them, with the side effects of
  the old checkboxes: XeMFG and Smooth Motion off, External FG for the RTX 20/30 unlocker. Enabling XeMFG also turns
  `MfgUnlock` off.
- **Placement and width.** The section stays before the External FG early return (rule §3). It uses a plain
  `CollapsingHeader` + `Indent`, like its XeMFG sibling, bounded status lines and `SeparatorWithHelpMarker`. One width
  mark, "FG: DLSS FG unlocker", replaces "FG: RTX 40 unlock" and "FG: RTX 20/30 unlock" (rule §10).

### 4.3 RTX 40 multiplier ceiling (`b403f503`)
- **Key:** `[DLSSG] AdaMfgMaxFrames`: 0 = unlocked maximum (6X), 1 = 2X … 5 = 6X. Pure math in
  `framegen/dlssg/AdaMfgCeiling.h`.
- **Only `EffectiveMax` changes.** The patch still opens 5 generated frames, and `UnlockedMax()` still reports them
  (rule §4). `MfgUnlock::EffectiveMax` reports min(ceiling, unlocked), never below the runtime's own maximum. That is
  the value the NGX parameters, Streamline state and DLSS-G capability paths read.
- **Unchanged:** a failed patch still reports 1. The game, or Override DLSSG Ratio, still picks the multiplier: a
  ceiling, never a forced count (lesson of `FG-FF7-REBIRTH-MULTIPLIER-LOCK.md`).
- **Startup-only:** latched per session (`SessionMaxFramesSetting`), because the game learns the capability once.

### 4.4 RTX 40 FG preset (`1397ca97`)
- **Key:** `[DLSSG] AdaMfgPreset` (auto / a / b / c). Pure rules in `framegen/dlssg/DlssgPresetPolicy.h`.
- **The answer.** `hkNvAPI_DRS_GetSetting` answers `NGX_DLSS_FG_OVERRIDE_RENDER_PRESET_SELECTION_ID` (`0x10E41DF1`;
  A = 1, B = 2, C = 3 in `external/nvapi/NvApiDriverSettings.h`) after the driver's own read:
  - the profile value is replaced, or a missing key (`NVAPI_SETTING_NOT_FOUND`) is filled as a current-profile DWORD;
  - errors, other types and other struct versions are left alone;
  - nothing is written to the saved NVIDIA profile.
- **Preset C** (performance mode) is sent only to DLSS-G 310.9.16 or newer (`MfgUnlock::Status::SnippetVersion`).
  Older or unknown runtimes keep the game's preset, and the menu says so.
- **Startup-only:** latched per session (`MfgUnlock::SessionPreset`). The menu shows the preset answered, or how many
  reads came without one.
- **Hook coverage.** In a non-External session, `hkNvAPI_QueryInterface` returns the DRS hook for
  `NvAPI_DRS_GetSetting`. MFG Unlocker also hooks a cached preset reader inside `nvngx_dlssg.dll`, for live switching.
  Whether our restart-only answer reaches the runtime without that hook is open (section 7).
- **Labels** stay neutral, matching MFG Unlocker's documentation: A and B are NVIDIA's original FG models, C is its
  performance mode. The UI-recomposition meanings belong to sdli1995's runtime options only.

### 4.5 Linux 2X fallback: XeFG by default (after 0.9.94-pre1)
- **Request.** The maintainer confirmed RTX 20/30 on Windows and Linux in 0.9.94-pre1 and asked for XeFG instead of
  FSR FG as the default `AmpereMfgLinuxFallbackType`.
- **Why FSR FG was the default.** `23c1df38` chose it as "fast & stable, zero extra DLLs", with XeFG needing
  `libxess_fg.dll`. That reason is gone:
  - `package_release.ps1` stages `libxess_fg.dll` / `libxell.dll` in `OptiScaler\`;
  - a fallback session is not External, so `CheckMemoryForProxies` preloads them (after the External decision);
  - XeFG's Linux problems are fixed: `FG-OPTIFG-XEFG-FRAMETIME-AND-PACING.md` (vkd3d-proton pacing),
    `FG-XEMFG-SWAPCHAIN-INIT-INVALID-ARGUMENT.md` (Ampere under Wine) and rule §9 (Metro 2033 Redux on Proton).
- **Nothing lost in coverage:** both outputs are DX12 only (`FSRFG_Dx12`, `XeFG_Dx12`), and both stay 2X here, because
  XeMFG is off while the RTX 20/30 unlocker is active.
- **Change:**
  - `AmpereMfgLoader::kDefaultLinuxFallbackType = "xefg"` replaces the six `"fsrfg"` defaults;
  - `auto`, empty or unknown text resolve to it, and Save writes `auto` when nothing was chosen;
  - an explicit `fsrfg` is still the user's choice. Older builds always saved `fsrfg`, so those installs keep FSR FG
    (the maintainer chose not to migrate them).
- **HDR.** XeFG has no `R16G16B16A16_FLOAT` support. During the fallback the menu then asks for FSR FG under the
  "Fallback Pipeline" combo (`state.xefgHdrFormatUnsupported`).
- **Still to check in game:** the OptiScaler FPS limit with XeFG under Linux. It is covered by unit tests only
  (rule §8).
- **Tests:** `tests/linux_fallback_xefg_default_unit.cpp` (14 checks). `tests/dlssg_sm86_ini_smoke.cpp` expects `xefg`
  for auto, empty and unknown text.

## 5. RTX 40 and the Architecture Spoof (`93f7e89a`, no code change)
- **RTX 20/30.** Streamline refuses the DLSS-G plugin below Ada ("Ignoring plugin 'sl.dlss_g'"). So the RTX 20/30
  unlocker spoofs the plugin load (`ampereMfgActive` in `hkdlssg_slOnPluginLoad`), and sdli1995's `SpoofArchToGame`
  reports RTX 50.
- **RTX 40** passes the plugin check natively. `StreamlineHooks::spoofArch` leaves AD100 and newer alone for
  `kFeatureDLSS_G` on purpose.
- **How the game learns the maximum.** It comes from `slDLSSGGetState`: `hkslDLSSGGetState` raises
  `numFramesToGenerateMax` through `MfgUnlock::EffectiveMax`, so the ceiling of 4.3 follows. The hook is handed out in
  every non-External session, through `hkslGetFeatureFunction` and the plugin's own function table. The RTX 40 unlock
  never runs in External mode.
- **Remaining risk:** a game that decides on MFG from the GPU name (RTX 50) instead of the Streamline maximum. None is
  reported, and MFG Unlocker renames the GPU only on RTX 30.

## 6. Unit Tests
- New:

  | Test | Checks |
  |---|---|
  | `tests/mfg_unlocker_select_unit.cpp` | 71 |
  | `tests/mfg_section_policy_unit.cpp` | 54 |
  | `tests/ada_mfg_ceiling_unit.cpp` | 29 |
  | `tests/dlssg_preset_drs_unit.cpp` | 37 |
  | `tests/ada_mfg_streamline_caps_unit.cpp` | 9 |

- **Updated** to the new header and width mark: `ampere_mfg_status_text_unit`, `menu_width_unit`,
  `menu_header_width_unit`. The `tests/mfg_unlock` Config mock gets `FGDLSSGAdaMfgMaxFrames` and `FGDLSSGAdaMfgPreset`.
- **Full Linux suite:** 106 pass, 0 fail. The 28 Windows-only tests build on neither `main` nor the branch.
  `menu_header_width_unit` was built with g++ and the bundled ImGui and passes.
- **Formatting:** clang-format 23 and the UTF-8 BOM checks pass on every modified C++ file. `NvApiHooks.h` got its
  missing BOM.

## 7. Verification Still Needed
- **MSVC build** of the branch: the new files and the menu are not compiled on Linux.
- **RTX 20/30 on Windows and on Linux (maintainer):**
  - an old ini with `AmpereMfgUnlock=true` must behave as before;
  - Auto must pick the RTX 20/30 unlocker (log `DLSS FG unlocker: … arch=RTX 30 (Ampere) (source d3dkmt …)`; under
    Proton `source registry` is expected);
  - every RTX 20/30 control must still work, the Linux 2X fallback included.
- **RTX 40 (testers):**
  - Auto picks the RTX 40 unlock;
  - the ceiling hides the in-game options above it;
  - `AdaMfgPreset=b` gives `DLSS-G preset: answered the driver profile read with preset B` in `OptiScaler.log`, and the
    menu line "FG preset B applied". Without that line, DLSS-G did not read the preset through the hooked
    `NvAPI_DRS_GetSetting`: the preset needs another route.
- **No NVIDIA GPU and RTX 50:** the section must show locked.
- The architecture rules and `.agents/CONTEXT.md` are updated only after these confirmations (workflow rule §8).
