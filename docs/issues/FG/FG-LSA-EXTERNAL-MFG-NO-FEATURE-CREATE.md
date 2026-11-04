# Frame Generation: Lost Soul Aside Never Creates DLSS-G With the RTX 20/30 Unlocker (External Mode)

## 1. Overview & Environment
- **Report**: maintainer testing, 2026-11-02 and 2026-11-03. No public issue.
- **Test environment** (Windows; values from the mod's `fg_gate_environment` event and the OptiScaler log header):
  - Windows 11 (10.0.26300), NVIDIA GeForce RTX 3080 Ti Laptop GPU (Ampere, SM86, arch `0x170`), driver 621.12
    (branch `r620_97`), HAGS on.
  - OptiScaler v0.9.85-final (`c1f882ff`) as `dxgi.dll`; sdli1995 `dlssg_sm86` **v0.4.1**.
  - Lost Soul Aside (Steam), Unreal Engine 4, `ProjectlsaSteam-Win64-Shipping.exe`. It ships Streamline
    `sl.interposer.dll` **2.7.32** and DLSS-G `nvngx_dlssg.dll` 310.9.16 in
    `Engine/Plugins/Runtime/Nvidia/Streamline/Binaries/ThirdParty/Win64`, plus XeSS 2 (`libxess`, `libxell`).
  - NVIDIA App DLSS overrides are on in the global profile: `Enable DLSS-FG override` = 1 and
    `Override DLSS-FG preset` = 3. The DLSS SR override is on as well.
- **Configuration**:
  - `FrameGen.External=true`, `DLSSG.AmpereMfgUnlock=true`, `AmpereMfgMode=External`.
  - `AmpereMfgMaxFrames=4`, `AmpereMfgOptimized=2`, `AmpereMfgPreset=C`, `AmpereMfgLogLevel=3` (third run).
  - OptiScaler `LogLevel=0`.
- **Reference game, same machine**: The Last of Us Part II Remastered. Streamline `sl.interposer.dll` **2.12.0**,
  same unlocker, same NVIDIA App overrides.
- **Analysis environment**: CachyOS (rolling), kernel 7.3.1-1-cachyos, NVIDIA GeForce RTX 3080 Ti Laptop GPU, driver
  580.196.07 (verified with `uname -r`, `/etc/os-release`, `nvidia-smi`). Logs only; the game was not run on Linux.

## 2. Reported Symptoms
- With the unlocker, the game lists the MFG multipliers (2X to 5X) in its frame generation option.
- Choosing any multiplier changes nothing: the frame rate stays at the base rate.
- XeMFG (`[XeMFG] UnlockMFG=true`, `Source=Game`) in the same game does interpolate, with mild stutter.
- With the unlocker, the game hangs on exit and has to be force-closed (see section 7).

## 3. Issue Validity Assessment
**Genuine: the game never asks NGX for frame generation, so the unlocker has nothing to run. The reason is not yet
known. OptiScaler cannot currently show it, because Streamline's own log is not captured in External FG mode.**

### 3.1 The unlocker is ready
From the level-3 mod log (`dlssg_sm86.log`, session pid 9440):
- `backend_loaded` (`dlssg-310.9.16-d3d12-sm86`), `kernel_pack` accepted (71/71), `install` with
  `frame_generation_refused: false`.
- `runtime_redirect`: both the game's runtime and the NVIDIA App model-store snippet
  (`ngx\models\dlssg\versions\20318480\files/160_E658700.bin`) are replaced by the bundled 310.9.16 runtime.
- `fg_gate_capability`, three times: `FrameGeneration.Available=1`, `NeedsUpdatedDriver=0`,
  `DLSSG.MultiFrameCountMax=4`.

### 3.2 Frame generation is never requested
- The whole session has **one** `fg_gate_create_feature`, for `feature_id 1` (super resolution), which succeeds.
- There is **no** `feature_id 11` create.
- There is no `foreign_fg_mod` event either, and the mod writes that one on the first FG create.
- In Witcher 3 under Proton ([DLSS Unlocked #63](https://github.com/ShyVortex/DLSS-Unlocked/issues/63)) the
  game asks 3,225 times and the NGX core refuses. Here the request never arrives.
- `fg_gate_summary` / `fg_gate_exit` are missing because the process was force-closed (section 7).

### 3.3 The game does turn DLSS-G on
In the XeMFG run, OptiScaler hooks Streamline and logs 46,174 `slDLSSGSetOptions` calls:
- long `eOn` runs during gameplay (15,457 calls in a row);
- `eOff` during menus and cutscenes;
- `numFramesToGenerate` set as selected.

So the game requests frame generation. In that run XeFG consumes the requests, so `sl.dlss_g`'s own create path
is never exercised.

### 3.4 What is ruled out
| Candidate | Evidence | Verdict |
|---|---|---|
| NVIDIA App FG override / Ada OTA Streamline plugins | TLOU2 uses the same override and NVIDIA's Ada `sl.dlss_g` (`sl_dlss_g_override_0\versions\134672\files\190_E658700.dll`, 2.14.16, `plugin_match: ota_override`) and creates feature 11 successfully, with evaluates at `multi_frame_count 4` | ruled out |
| Arch spoof to Ada (`arch_spoof_applied` for `sl.interposer`, `sl.common`, the OTA plugin) | identical in TLOU2, which works | ruled out |
| Runtime generation | game runtime 310.9.16, generation B, `the bundled runtime takes over` | ruled out |
| `kernel_create status -14`, `image: original`, `routed: false` | appears in TLOU2 too, right before a successful FG create | harmless |
| OptiScaler internal FG | External mode: `CheckForFGStatus ... skipping OptiScaler internal FG swapchain creation` | not involved |

### 3.5 Remaining differences
| | TLOU2 (works) | Lost Soul Aside (no FG) |
|---|---|---|
| Game `sl.interposer.dll` | 2.12.0 | 2.7.32 |
| Active `sl.dlss_g` | NVIDIA 2.14.16, `sl_dlss_g_override_0` channel | NVIDIA 2.14.16, `sl_dlss_g_0` channel (same version 134672) |
| `fg_gate_create_feature` 11 | succeeded | never called |

The likely suspects are the old 2.7.32 interposer under NVIDIA's 2.14.16 plugins, or something in the game's
Streamline use (swapchain, tags, options). Neither can be confirmed from the logs available today.

## 4. Root Cause (Diagnosability Gap)
In External FG mode, `StreamlineHooks::hookInterposer` returns immediately
(`OptiScaler/hooks/Streamline_Hooks.cpp`, the `externalFrameGeneration` check at the top). `hkslInit` is never
attached, so Streamline's log callback is never redirected into `OptiScaler.log`:
- SM86 run (External mode): 0 Streamline log lines.
- XeMFG run: 140 Streamline log lines.

Streamline's log normally states why a plugin refuses a feature (unsupported feature, version mismatch, missing
swapchain or tag). Without it, the reason `sl.dlss_g` never creates feature 11 is invisible.

Side observation: `isInterposerHooked()` stays false in External mode. As a result
`LibraryLoadHooks::CheckModulesInMemory` calls `hookInterposer` again on every check: the SM86 log has 434
"sl.interposer.dll already in memory" lines.

## 5. Solution Proposed (Diagnostic, Not Yet the Fix)
In External FG mode, attach only a log-only `slInit` hook (Streamline 2 and Streamline 1):
- It sets the log level and callback and chains the game's own callback.
- It does not touch `pathsToPlugins`, `featuresToLoad`, the engine type or the flags, and installs no DLSS-G,
  tag, option or capability hook. Architecture rule §4 (never clear or own the game's DLSS-G hooks for the
  external unlocker, `FG-WRC-GENERATIONS-EXTERNAL-MFG.md`) is preserved.
- It uses separate original-function pointers, so `isInterposerHooked()` and the full hook set are unaffected.
- The preference rewrite becomes a pure helper (`OptiScaler/hooks/StreamlineLogPrefs.h`) shared with the full
  `hkslInit`, unit tested in `tests/streamline_external_log_capture_unit.cpp`.
- Limit: if the game calls `slInit` before OptiScaler sees `sl.interposer.dll`, nothing is captured. An attach line
  in the log makes that case visible.

The functional fix will be planned after the next run shows Streamline's reason.

### 5.1 First in-game run (`4f53087c`): the hook attached too late in Lost Soul Aside
- The log shows `External FG: Streamline 2.7.32 log capture only`, but `hkslInit_logOnly` never ran.
- External mode skips every `sl.*` load in `LibraryLoadHooks::LoadLibraryCheckW` (the `externalFrameGeneration`
  check at the top, upstream `a60f3c93`). OptiScaler therefore found the interposer only through
  `CheckModulesInMemory` polling, and Lost Soul Aside had already called `slInit`.
- TLOU2 calls `slInit` about 0.6 s after OptiScaler finds the interposer. Capture worked there (1,253 Streamline
  lines), and FG was unaffected.
- TLOU2 also shows that `hkcommon_slOnPluginLoad` / `hkdlssg_slOnPluginLoad` do not run in External mode
  (`hookCommon` attaches after the plugins loaded), yet FG works. Their absence is not the Lost Soul Aside cause.
  VSync is off (`SyncInterval 0`, tearing flag) and HAGS is on, so their relaxations are not needed here.

### 5.2 Follow-up (`3f325df3`): notify after the original load
- `NtdllHooks::hkLdrLoadDll` / `hkNtLoadDll` call `LibraryLoadHooks::OnOriginalLoad` after the original loader
  succeeded.
- In External mode, for `sl.interposer.dll` only (`StreamlineLogPrefs::ShouldNotifyInterposerLoad`), that attaches
  the log-only hook before the caller reaches `slInit`.
- The load itself is unchanged. `slInterposerModule` is still set by `CheckModulesInMemory`, because `Dxgi_Hooks`
  delays factory calls from that module when factory wrapping is on.

## 5.3 Root Cause (confirmed in game, 2026-11-03)
**The game's own XeSS Frame Generation takes over the swapchain, so Streamline's DLSS-G never sees a present and
never creates feature 11.**

- Lost Soul Aside ships XeSS-FG **1.1.0.19** (`Engine/Plugins/Runtime/Intel/XeSS/Binaries/ThirdParty/Win64`). On a
  GeForce it fails to initialise and leaves the swapchain alone.
- On the test machine DLSS Swapper had replaced it with **1.3.1.78**, keeping the original as
  `libxess_fg.dll.dlsss` (it also swapped `libxess` 2.0.1.41 to 2.0.2.68 and `libxell` 1.1.0.6 to 1.3.2.10).
  OptiScaler ships the same 1.3.1.78 build (identical SHA-256) and **preloads it at startup** from `OptiScaler/`
  (`XeFGProxy::InitXeFG()` in `CheckMemoryForProxies`), even in External FG mode.
- The game asks for `libxess_fg.dll` by name and gets the module already in the process: OptiScaler's preloaded
  copy. The hang dump showed only `OptiScaler\libxess_fg.dll` loaded. 1.3.1 runs on NVIDIA, so the game's XeSS-FG
  wraps the swapchain:
  - `WrappedIDXGISwapChain4::QueryInterface` / `AddRef` from `libxess_fg.dll` 0.1 s after creation, held all session;
  - `SetFullscreenState` from `libxess_fg.dll` at shutdown.
- Streamline evidence from the `3f325df3` build, with only OptiScaler's copy renamed (the game's 1.3.1 still in
  place):
  - `sl.dlss_g` 2.14.16 registers its D3D12 swapchain and present hooks `OK`;
  - the only later DLSS-G messages are `Repeated slDLSSGSetOptions() call for the frame 1`, 35 s into the session,
    and one `slSetTag`;
  - there is no `dlfgPresent` / `presentCommon` line, and no feature-11 create.
- Removing the game's `libxess_fg.dll` makes the game crash at launch: its XeSS plugin needs the file.

| Run (External mode, RTX 20/30 unlocker) | `libxess_fg` in process | Swapchain wrapped by XeSS-FG | `fg_gate_create_feature` 11 | DLSS-G present lines |
|---|---|---|---|---|
| OptiScaler 1.3.1 preloaded (game 1.3.1) | OptiScaler's 1.3.1 | yes | none | 0 |
| OptiScaler copy renamed, game 1.3.1 | game's 1.3.1 | yes | none | 0 |
| OptiScaler copy renamed, game **1.1.0.19** restored | game's 1.1 (only 3 factory probes) | **no** | **2 succeeded**, 11,040 evaluates, `multi_frame_count` 3 | 353 |

The last run is the maintainer's in-game confirmation: DLSS MFG works.

## 5.4 Fix (awaiting in-game check)
- **`44fb95cb`: no startup preload in External mode.** `CheckMemoryForProxies` skips `XeFGProxy::InitXeFG()` when
  `externalFrameGeneration` is set (`XeFGPreloadPolicy`). OptiScaler's own XeFG and XeMFG never run there; the
  game loads its own `libxess_fg`, or none. Test: `tests/xefg_external_preload_unit.cpp`.
- **`f1513672`: the game's XeSS-FG is refused while the unlocker runs.** This covers game folders upgraded with DLSS
  Swapper.
  - Gate: External FG, `AmpereMfgUnlock` enabled and loaded, and `[DLSSG] AmpereMfgBlockGameXeFG` (`auto` = on).
  - `GameXeFGHooks` detours the game library's `xefgSwapChainD3D12CreateContext` and returns
    `XEFG_SWAPCHAIN_RESULT_ERROR_UNSUPPORTED_DEVICE` (-1) without calling it, which is what the Intel-only builds
    answer on a GeForce.
  - The hook is installed from `LibraryLoadHooks::OnOriginalLoad` (and `CheckModulesInMemory` for an
    already-loaded library), never on OptiScaler's own copy, and never outside External mode, so architecture
    rule 8 (XeMFG) is untouched.
  - The menu has a "Keep game XeSS FG off the swapchain" checkbox and a status line once a context was refused.
  - Test: `tests/game_xefg_block_unit.cpp`.

## 6. Verification
- [ ] `tests/streamline_external_log_capture_unit.cpp` passes; the existing `streamline_*` suites still pass.
- [x] In game: Lost Soul Aside shows Streamline log lines in `OptiScaler.log` (`3f325df3`; 193 lines).
- [ ] In game: with OptiScaler's `libxess_fg.dll` back in place and the game's 1.3.1 (DLSS Swapper state), DLSS MFG
  works and `OptiScaler.log` shows `refused the game's XeSS-FG context`.
- [x] Regression: TLOU2 still creates feature 11 and generates frames (`4f53087c`). Re-check with the final build.

## 7. Open: Exit Hang With the Unlocker
- SM86 run: the OptiScaler log ends at `hk_xessDestroyContext` (08:55:24), after the swapchain and DLSS were
  released normally. There is no DLL detach, and the process had to be force-closed.
- XeMFG run: the same sequence ends with `hk_xessDestroyContext` (09:15:38). About 49 s later
  `XeMfgLoader::Shutdown` runs and the process exits.
- Full memory dump taken during the hang (4.2 GB), frames resolved with Microsoft public symbols for `ntdll`,
  `KERNELBASE`, `D3D12` and `D3D12Core`:
  - The process had not reached exit: all 36 threads were alive, including the Streamline and OTA `sl.common`
    workers.
  - The render thread (tid 20984) was in the game's own shutdown, releasing D3D12 pipeline states:
    game exe → `CUseCountedObject::Release` (D3D12) → `CPipelineState::~CPipelineState` → `CShader` →
    `CShaderBytecode` → `ShaderParser::~ShaderParser` → `RtlFreeHeap` → `RtlpInsertFreeBlock`.
  - Its instruction pointer was inside the heap code, not in a wait. No OptiScaler, unlocker or Streamline frame was
    on that thread.
  - The only OptiScaler thread (tid 18084) was in `SleepEx` → `ZwDelayExecution`: a sleeping worker loop.
- One dump cannot tell a very slow pipeline-state teardown from a spin on a corrupted heap. The XeMFG run's ~49 s
  exit suggests the teardown is slow in any case. Next checks: CPU usage during the hang, a second dump ~30 s later,
  or waiting 2-3 minutes before force-closing. Tracked separately from the frame generation issue.
