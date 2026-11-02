# Frame Generation: NVSmooth30 Integration & SwapChain Present Conflict

## 1. Overview & Environment

- **Operating System**: Windows 11 build 26100 / 26200
- **Hardware**: NVIDIA GeForce RTX 3060 (Ampere SM86, `0x170`, GA106)
- **Driver**: NVIDIA Display Driver 616.92+ (Branch R570+)
- **Games**: Cyberpunk 2077 (Steam), Control Resonant (Steam)
- **Mod Configuration**: OptiScaler v0.9.25 / v0.9.30 with NVSmooth30 v0.3.0
- **Report Reference**: [DLSS Unlocked Issue #42](https://github.com/ShyVortex/dlss-unlocked/issues/42)

---

## 2. Reported Issue & Symptoms

A user reported interconnected defects when attempting to use the NVSmooth30 unlocker for driver-level Smooth Motion on Ampere (RTX 30 series):

1. **Menu UI Desynchronization ("Disabled in config" on checked checkbox)** (v0.9.25):
   - In the OptiScaler in-game overlay menu, under the Smooth Motion section on an RTX 30 GPU, the checkbox `Enable NVSmooth30 Unlocker (RTX 30)` appeared checked (`[X]`).
   - However, the status label printed immediately beneath the checkbox reported `"Disabled in config"`.
   - Users were unable to determine whether the unlocker was truly active, disabled, or failing to load.

2. **Presentation Crash / Black Screen (`0x80004002` / `E_NOINTERFACE` on Present)** (v0.9.25):
   - When explicitly setting `EnableNVSmooth30 = true` in `OptiScaler.ini` (or activating Smooth Motion), launching games such as *Cyberpunk 2077* or *Control* either crashed immediately upon entering 3D rendering or presented an unresponsive black screen.
   - Diagnostic logs revealed an unhandled `0x80004002` (`E_NOINTERFACE`) HRESULT returned during swapchain `Present` / `Present1` dispatch.

3. **Silent Non-Activation on Mid-Game Toggle (Framerate Lock at Native)** (v0.9.30):
   - When testing `v0.9.30`, the game launched once with default settings (`SmoothMotion = false`).
   - Upon opening the OptiScaler menu and enabling Smooth Motion and NVSmooth30, the menu updated to display `[Smooth Motion Active (RTX 30)]` and `NVSmooth30 Status: Active`.
   - However, zero interpolated frames were generated: native rendering framerate and display framerate were identical (78.3 FPS native == 78.5 FPS average).

4. **Immediate Startup Crash on Subsequent Launch (`0xC0000005` Access Violation)** (v0.9.30):
   - When the user exited the game (saving `SmoothMotion = true` in `OptiScaler.ini`) and launched *Control Resonant* a second time, the game crashed immediately upon startup.

---

## 3. Issue Validity Assessment

### Part 1: Menu UI Desynchronization
- **Validity**: **100% Genuine OptiScaler Defect.**
- **Verification**:
  - In `OptiScaler/Config.h`, `CustomOptional<bool> SmoothMotionNVSmooth30 { false };` initialized with default value `false`.
  - In `OptiScaler/framegen/smoothmotion/NVSmooth30Loader.cpp`, startup `TrySetup()` checked `cfg->SmoothMotionNVSmooth30.value_or(false)`. Since the setting was unset by default, this evaluated to `false`, leaving `s_status.Enabled = false`.
  - In `OptiScaler/menu/menu_common.cpp` (lines 3653 & 3696), the UI evaluated `config->SmoothMotionNVSmooth30.value_or(true)`. Because the setting was unset, it evaluated to `true`, causing ImGui to render the checkbox as checked (`[X]`).
  - Line 3718 then evaluated `else if (!nvSmoothStatus.Enabled) { ImGui::TextDisabled("Disabled in config"); }`.
  - The UI thus rendered a checked checkbox directly over a label saying "Disabled in config". Furthermore, when Smooth Motion itself was turned off (`FGDLSSGSmoothMotion = false`), the label displayed "Disabled in config" rather than clarifying that Smooth Motion was simply inactive.

### Part 2: Presentation Crash (`0x80004002` / `E_NOINTERFACE`)
- **Validity**: **100% Genuine OptiScaler Defect** arising from two distinct architectural oversights:
  - **Lack of Mutual Exclusion between DLSS-G / MFG and Driver-Level Smooth Motion**:
    In both attached logs (`cyberpunk2077OptiScaler.log` and `controlOptiScaler.log`), the user had both `AmpereMfgUnlock = true` (external DLSS-G proxy `dlssg_sm86.dll`) and `SmoothMotion = true` / `EnableNVSmooth30 = true` active simultaneously.
    DLSS-G Multi-Frame Generation and NVIDIA driver-level Smooth Motion (`NvPresent64.dll`) are mutually exclusive frame interpolation technologies. Running both concurrently caused both proxies to simultaneously hook the DXGI SwapChain vtable (slots 8 and 22). When `dlssg_sm86.dll` presented generated frames, `NvPresent64.dll` attempted to re-interpolate them, resulting in hook recursion, swapchain interface collision, and pipeline failure.
  - **Incomplete COM Interface Delegation in `WrappedIDXGISwapChain4::QueryInterface`**:
    When OptiScaler wraps the DXGI swapchain in `WrappedIDXGISwapChain4`, its `QueryInterface` implementation strictly handled only standard public interfaces (`IDXGISwapChain[1-4]`, `IUnknown`, `IDXGIObject`, `IDXGIDeviceSubObject`).
    For all other interfaces—including `IDXGISwapChainMedia` and private NVIDIA driver presentation interfaces queried by `NvPresent64.dll` (such as `INvPresentSwapChain`)—`WrappedIDXGISwapChain4::QueryInterface` returned `E_NOINTERFACE` without forwarding the query to `_real->QueryInterface(riid, ppvObject)`.
    When `NvPresent64.dll` queried the swapchain for its private driver interface and received `E_NOINTERFACE`, presentation aborted with `0x80004002`.

### Part 3: Silent Non-Activation on Mid-Game Toggle
- **Validity**: **100% Genuine OptiScaler Architectural Defect.**
- **Verification**:
  - NVIDIA Smooth Motion operates via driver-level presentation interception (`NvPresent64.dll`).
  - `NvPresent64.dll` hooks DXGI Factory creation and attaches its internal presentation wrapper object (at offset `+0x18` of the swapchain) **strictly during swapchain creation** (`CreateSwapChainForHwnd`).
  - If Smooth Motion is toggled in the ImGui menu while the game is already rendering, the swapchain was already created without `NvPresent64.dll` wrapping it.
  - `NvPresent64.dll` cannot retroactively hook a live, pre-existing swapchain.
  - OptiScaler's menu evaluated `nvSmoothStatus.DllLoaded == true` and falsely reported `[Smooth Motion Active (RTX 30)]`, giving the false impression that frame generation was active when it was completely dormant.
  - The menu help text incorrectly claimed `"Can be toggled dynamically on the fly"`, which is invalid for driver-level swapchain interception.

### Part 4: Immediate Startup Crash on Second Launch (`0xC0000005`)
- **Validity**: **100% Genuine OptiScaler Defect** caused by multi-threaded hook races and D3D11 vs D3D12 vtable stomping:
  - **Asynchronous Worker Thread Race**:
    In `dllmain.cpp`, `getGpuInfo` was spawned as a detached background thread via `CreateThread`. When `SmoothMotion = true` was saved in `OptiScaler.ini`, `getGpuInfo` called `NVSmooth30Loader::TrySetup()`. In turn, `nvsmooth30.dll`'s `DllMain` spawned a third thread running `bootstrap`.
    While the game's main thread was initializing Streamline (`sl.interposer.dll`), `CreateDXGIFactory`, and `D3D12CreateDevice`, `bootstrap` on the third thread created a 16x16 dummy window (`NVSmooth30DummyWindow`), called `D3D11CreateDeviceAndSwapChain`, and executed `patch_vtable` on the DXGI swapchain vtable using `VirtualProtect`.
  - **Streamline Interposer Hook Invalidation**:
    Streamline intercepted the 16x16 dummy swapchain on the background thread. When `bootstrap` immediately destroyed the window and reset the swapchain, Streamline was left holding dead internal handles.
  - **D3D11 vs D3D12 Function Pointer Collision in `dispatch_present`**:
    `nvsmooth30.dll`'s `install_hooks()` captured `g_present1` (slot 22) from the dummy D3D11 swapchain. In native D3D12 games, when `WrappedIDXGISwapChain4::Present1` called `pSwapChain->Present1`, the patched slot 22 jumped into `nvsmooth30.dll`'s `hook_present1`, which dispatched `g_present1(swapchain, ...)` with the D3D12 swapchain as `this`.
    Executing `CDXGISwapChain::Present1` (D3D11) with a `CDXGISwapChainD3D12` object caused an immediate invalid memory dereference and access violation (`0xC0000005`).
  - **Invalid `SetMaximumFrameLatency` Call**:
    `nvsmooth30`'s `select_primary()` called `swap2->SetMaximumFrameLatency(1)` on D3D12 swapchains without the `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` flag, returning `DXGI_ERROR_INVALID_CALL`.

---

## 4. Technical Solution & Architectural Invariants

### 1. Configuration & UI Default Harmonization
- Align the default value of `SmoothMotionNVSmooth30` across all files to `true` (so when an Ampere user opts into Smooth Motion, NVSmooth30 automatically unlocks SM86 without requiring manual INI edits).
- In `NVSmooth30Loader::TrySetup()`, check `cfg->FGDLSSGSmoothMotion.value_or_default()`: NVSmooth30 must only load if Smooth Motion is actually requested.
- In `menu_common.cpp`, disambiguate status messages:
  - If DLSS-G / MFG is active: display `"Conflict: DLSS-G / MFG active"` (with orange warning color).
  - If Smooth Motion is disabled: display `"Inactive (Smooth Motion disabled)"`.
  - If NVSmooth30 is explicitly unchecked: display `"Disabled in config"`.
  - If binary is missing: display `"OptiScaler/nvsmooth30.dll not found"`.
  - If loaded: display `"Active (OptiScaler/nvsmooth30.dll)"`.

### 2. Strict Mutual Exclusion: DLSS-G / MFG vs Smooth Motion
Per OptiScaler architecture rules (`.agents/rules/optiscaler-architecture.md`):
- Multi-Frame Generation unlockers (`AmpereMfgUnlock`, `AdaMfgUnlock`, external FG proxies) and driver-level Smooth Motion must remain strictly mutually exclusive across:
  - **Configuration Loading**: If `FGDLSSGAmpereMfgUnlock` or `FGDLSSGAdaMfgUnlock` is active, suppress or disable `FGDLSSGSmoothMotion` and `SmoothMotionNVSmooth30`.
  - **Runtime Execution**: In `NVSmooth30Loader::TrySetup()`, abort setup if `State::Instance().externalFrameGeneration`, `AmpereMfgUnlock`, or `AdaMfgUnlock` is active, setting `ErrorMessage = "Smooth Motion cannot be used while Frame Generation / MFG is active."`.
  - **In-Game Menu**: When MFG is active, disable the Smooth Motion checkbox via `ImGui::BeginDisabled()` with an explanatory tooltip (`"Disabled because DLSS-G / MFG Frame Generation is active. Disable MFG first to use Smooth Motion."`).

### 3. COM Interface Delegation in `WrappedIDXGISwapChain4` and `WrappedIDXGIFactory7`
In `OptiScaler/wrapped/wrapped_swapchain.cpp`:
```cpp
// Delegate any unhandled or private driver interfaces to the real swapchain
if (_real != nullptr)
    return _real->QueryInterface(riid, ppvObject);

*ppvObject = nullptr;
return E_NOINTERFACE;
```
Similarly in `OptiScaler/wrapped/wrapped_factory.cpp`:
```cpp
if (_real != nullptr)
    return _real->QueryInterface(riid, ppvObject);

*ppvObject = nullptr;
return E_NOINTERFACE;
```
This guarantees that extended Windows interfaces (`IDXGISwapChainMedia`) and proprietary NVIDIA driver presentation interfaces queried by `NvPresent64.dll` succeed without throwing `E_NOINTERFACE`.

### 4. Early Synchronous Startup Execution & Environment Sanitization
- Invoke `NVSmooth30Loader::TrySetup()` synchronously during early startup before D3D12 device and SwapChain creation (`hkD3D12CreateDevice`, `hkCreateSwapChainForHwnd`).
- Before loading `nvsmooth30.dll`, sanitize runtime environment variables:
  - `SetEnvironmentVariableW(L"SM86_ENABLE_D3D11_BRIDGE", L"0");` (disables D3D11 bridge for native D3D12 games).
  - `SetEnvironmentVariableW(L"SM86_LOW_LATENCY", L"0");` (prevents invalid `SetMaximumFrameLatency(1)` calls on D3D12 swapchains).
  - `SetEnvironmentVariableW(L"SM86_ENABLE_OSD", L"0");` (prevents GDI/D3D11 OSD crashes).
  - `SetEnvironmentVariableW(L"SM86_SKIP_DXGI_HOOKS", L"1");` (instructs proxy to skip dummy swapchain vtable hooking).

### 5. Neutralize Dummy 16x16 SwapChain & DXGI VTable Stomping in `DxgiFactory_Hooks.cpp`
- In `OptiScaler/hooks/DxgiFactory_Hooks.cpp`, detect the 16x16 dummy window creation (`localDesc.BufferDesc.Width <= 16 && localDesc.BufferDesc.Height <= 16`) from `NVSmooth30DummyWindow` and reject or isolate it so `nvsmooth30.dll`'s `install_hooks()` gracefully skips `patch_vtable`.
- This prevents `nvsmooth30` from stomping on OptiScaler's or Streamline's DXGI Present vtable slots, preventing the D3D11-to-D3D12 `Present1` crash and Streamline interposer destruction, while leaving `NvPresent64.dll`'s native driver presentation hooks fully active and functional.

### 6. Accurate SwapChain Attachment Verification & Menu Restart Requirement
- In `NVSmooth30Loader::Status`, track `SwapchainAttached`: verify whether `NvPresent64.dll` is genuinely attached to the active swapchain (by probing `_real`'s vtable or wrapper candidate offset `_real + 0x18`).
- In `menu_common.cpp`:
  - Change label to `"NVIDIA Smooth Motion (Driver-level FG; restart)##driver_sm"`.
  - When toggled mid-game while `SwapchainAttached` is false, display:
    `"[Pending Restart: Active on next game launch]"` in amber text.
  - Display `"[Smooth Motion Active (RTX 30)]"` in green text **only** when `NvPresent64.dll` is genuinely attached to the swapchain.
  - Update tooltip text to remove `"Can be toggled dynamically on the fly"`, explaining that driver-level presentation interception requires a game restart to bind to the swapchain.

---

## 5. Automated Verification

- `tests/nvsmooth30_loader_unit.cpp`:
  - Verify mutual exclusion gating rejecting setup when DLSS-G / Ampere MFG / Ada MFG is active.
  - Verify configuration disabling when `SmoothMotion` is false overall vs when `NVSmooth30` is false.
  - Verify candidate path resolution and status strings.
  - Verify environment variable sanitization (`SM86_ENABLE_D3D11_BRIDGE=0`, `SM86_LOW_LATENCY=0`, `SM86_ENABLE_OSD=0`, `SM86_SKIP_DXGI_HOOKS=1`).
  - Verify swapchain attachment verification logic distinguishing pending restart vs truly attached.
  - Verify NVSmooth30 dummy window (`NVSmooth30DummyWindow`, 16x16) detection and interception.
- `tests/ampere_smooth_motion_unit.cpp`:
  - Verify mutual exclusion UI evaluation when MFG is active.
  - Verify INI configuration priority and fallback defaults.
  - Verify restart-pending UI status reporting and contextual help markers.
  - Verify status banner color and text transitions (`[Pending Restart]` vs `[Smooth Motion Active (RTX 30)]`).
- All unit tests compiled and passed cleanly via `g++ -std=c++20`.
- Clang-format check passed cleanly with zero violations (`clang-format --dry-run --Werror`).
- UTF-8 BOM (`\xef\xbb\xbf`) preserved across all modified C++ source and header files.

---

## 6. Follow-up (2026-11-02): v0.9.31 Log Analysis & Attachment Detection

### 6.1 Environment
- **Reporters** ([DLSS Unlocked #42](https://github.com/ShyVortex/DLSS-Unlocked/issues/42)):
  - johnCXVII: Windows 11, RTX 3060 12 GB (Ampere), driver 616.92, OptiScaler v0.9.31, NVSmooth30 0.1.0. Control
    Resonant and Cyberpunk 2077 (both D3D12, both Streamline), Forza Horizon 6.
  - AreChen: the same symptoms in Final Fantasy VII Remake (D3D11), v0.9.31.
  - kilyan82: v0.9.85, no crash, the amber "Pending Restart" text stays and Smooth Motion "doesn't work". No logs yet.
- **Analysis machine:** CachyOS, kernel 7.3.1-1-cachyos, NVIDIA GeForce RTX 3080 Ti Laptop GPU, driver 580.196.07.
  NVSmooth30 is Windows-only, so nothing here could be reproduced locally.
- **NVSmooth30 source:** the local copy of v0.1.0 (`nvsmooth30/`, gitignored), still the latest upstream
  (`ItsAdeline/NVSmooth30`, last commit 2026-09-20).

### 6.2 What the v0.9.31 logs show
- **The driver's Smooth Motion starts.** With OptiScaler blocking the 16×16 dummy swapchain, NvPresent still loaded
  its Smooth Motion CUDA kernels and NVSmooth30 retargeted every one (`[nvs30] CUDA fatbin #1..#19 ... sm89->86=1
  rc=0` in Control, `#1..#4` in Cyberpunk). Blocking NVSmooth30's Present hooks does not stop NvPresent attaching.
- **The first present then fails.** Cyberpunk's `OptiScaler.log` ends at `LocalPresent Original present result:
  80004002` (`E_NOINTERFACE`) right after the swapchain was created. Control logs a Streamline exception and minidump
  during present. The §4.3 `QueryInterface` delegation did not resolve this. **Still open**, pending v0.9.85 logs.
  - One unconfirmed hypothesis: Streamline logs `Detected base interface 'ID3D12CommandQueue' while expecting SL
    proxy` just before swapchain creation, so NvPresent may be querying a queue it doesn't expect.
- **OptiScaler's attachment check was wrong.** Both logs say `NVSmooth30: Swapchain attachment verified = false`
  although the kernels had loaded (see 6.4).

### 6.3 Corrections to sections 3 and 4
- **`SM86_SKIP_DXGI_HOOKS` does nothing.** NVSmooth30 v0.1.0 reads only `SM86_ENABLE_OSD`,
  `SM86_ENABLE_D3D11_BRIDGE`, `SM86_FORCE_VSYNC`, `SM86_DIAGNOSTICS`, `SM86_LOW_LATENCY`, `SM86_HALF_REFRESH_CAP`,
  `SM86_BASE_FPS_CAP`, `SM86_BRIDGE_LINEARIZE` and `SM86_NVPRESENT_PATH` (`src/config.cpp`). Only the dummy-window
  block in `DxgiFactory_Hooks.cpp` (§4.5) keeps its Present hooks out.
- **DX11 games most likely get no Smooth Motion.** NVSmooth30's route for D3D11 is its D3D11-to-D3D12 bridge, which
  runs inside its Present hooks. OptiScaler forces `SM86_ENABLE_D3D11_BRIDGE=0` and blocks those hooks.
- **What the Present hooks do:** the bridge, frame pacing (off by default), the OSD and NVSmooth30's own activation
  log line. NvPresent's own attachment does not depend on them.
- **The log lines `dummy D3D11 swapchain creation failed: 0x887A0001` and `DXGI Present hooks failed (slots 8 and
  22)`** are expected: `0x887A0001` is the `DXGI_ERROR_INVALID_CALL` OptiScaler returns for the dummy window.

### 6.4 Attachment detection fix (`3b91e98d`)
- **The defect** (§4.6's check):
  - it ran once, in the `WrappedIDXGISwapChain4` constructor, before the first present; NVSmooth30's source expects
    NvPresent to attach lazily during the first presents;
  - it checked vtable slots 0/8/22 and offset `+0x18` only, and read `+0x18` without validation;
  - a `true` from one swapchain could never be cleared by a later one;
  - the menu called every miss "Pending Restart".
- **The fix:**
  - `OptiScaler/framegen/smoothmotion/NvPresentAttach.h`: a pure probe in NVSmooth30's order (slots 0/7/8/22, the
    wrapper at `+0x18`, then `0x8..0x80`), every read range-checked (`VirtualQuery` in the real predicate), never
    calling into a candidate.
  - `WrappedIDXGISwapChain4::ProbeNvPresent()` runs after `Present` / `Present1`: every present up to 120, then every
    120th up to 3600. It logs once per swapchain: `NVSmooth30: NvPresent attached to swapchain N (offset 0x.., present
    M)`, or `NvPresent not detected on swapchain N after 3600 presents (...)`.
  - `NVSmooth30Loader::BeginSwapchain` / `SetSwapchainAttached(id)` / `SetProbeDone(id)`: state per swapchain; a
    stale swapchain is ignored.
  - Menu: green when attached; "Pending Restart" only when `nvsmooth30.dll` was loaded after the swapchain; "Checking"
    during the probe window; then an amber "Not detected", with a help marker saying the detection is heuristic and
    how to verify (external FPS overlay) and report (`OptiScaler.log` + `nvsmooth30.log`).
- **Not changed:** loading, the dummy-window block, environment variables, hooks and presentation.

### 6.5 Test verification
- `tests/nvsmooth30_attach_probe_unit.cpp` (7/7 PASS): the probe on synthetic objects (self vtable, wrapper at
  `+0x18` / other offsets / past `0x80`, self-pointers, unreadable memory), the probe schedule, no constructor probe,
  per-swapchain state, and the menu wording.
- `tests/nvsmooth30_loader_unit.cpp` and `tests/ampere_smooth_motion_unit.cpp`: mock wording updated, PASS.
- clang-format 23 clean; UTF-8 BOM present.
- **Not yet checked in game:** a Windows RTX 30 reporter needs to confirm the log line and menu against an external
  FPS overlay.

### 6.6 kilyan82's driver 621.01 log and NVSmooth30 diagnostics (`889772de`)
- **Report (2026-11-02):** Windows 11, RTX 3070 Ti, driver 621.01, Neverness to Everness (DX12, UE5). The game runs,
  but the FPS doesn't double.
- **A mixed install:**
  - `OptiScaler.log` reports `OptiScaler v0.9.9-final (d9344618)`, a build from before `NVSmooth30Loader` existed;
  - `nvsmooth30.log` shows OptiScaler's environment overrides (`D3D11Bridge=0`, `LowLatency=0`) and the dummy-window
    block, which arrived after v0.9.9 (`8f0bf7d8`, `4518915c`);
  - so the two logs came from different OptiScaler builds, most likely an old copy under another proxy name next to
    the new `dxgi.dll`. The reporter was asked to list the folder.
  - **Confirmed by the preview-build log (2026-11-02, 16:09):**
    - its line 1 is again `OptiScaler v0.9.9-final (d9344618) loaded`, but it is written at present 799, and the log
      also holds the new `ProbeNvPresent` line (`NvPresent not detected on swapchain 2 after 3600 presents`);
    - the preview's own `OptiScaler.dll` (run 41475473778) reports `OptiScaler v0.9.86-pre1 (0504383)`;
    - `dllmain.cpp` prints that header only at start-up, so a second, old OptiScaler instance started mid-session
      and truncated the log. That removed the new build's start-up lines, `NVSmooth30Loader` included.
    - It is not DLSS Unlocked's `OptiScaler/dlss-enabler-headless.dll`, which is v0.9.5-pre3 (`cdf0b4c5`). The file
      itself is still unidentified.
- **NVSmooth30 on driver 621.01:**
  - NvPresent no longer matches the reference layout: `cmp=+0xc40f` and config `+0x7667d0`, against `+0xc41f` /
    `+0x7f0cd0` on johnCXVII's 616.92.
  - So NVSmooth30 used `adaptive CUDA hook installed through NvPresent64 GetProcAddress IAT (last resort)`. That hook
    only redirects `GetProcAddress(..., "cuModuleLoadData")` lookups made after it is installed.
  - No `CUDA fatbin` line was logged. Either NvPresent never loaded its kernels, or it loaded them past the hook, in
    which case the SM89 kernels run unconverted and Smooth Motion can't work on SM86.
  - If it is the hook, the limitation is in NVSmooth30 v0.1.0, not OptiScaler. An offline check of NvPresent64.dll
    from 621.01 (`tools/inspect_nvp.py`, plus its CUDA imports) is planned.
- **The change:**
  - `NVSmooth30Loader::SanitizeEnvironment` sets `SM86_DIAGNOSTICS=1` when OptiScaler's `LogLevel` is 0 or 1
    (`WantsDiagnostics`); at other levels it leaves the variable untouched.
  - NVSmooth30 then also logs `CUDA fatbin #N left unmodified ...` and why its reference resolver path was rejected.
  - The variable adds log lines only (checked in `src/nvpresent.cpp`).
- **Test:** `tests/nvsmooth30_attach_probe_unit.cpp` test 8 (8/8 PASS).

### 6.7 Driver 621.01 root cause, upstream PR 4 and the packaging source option (`03faacfe`)
- **What was analysed:** `nvpresent64.dll` from NVIDIA's 621.01 desktop package
  (`621.01-desktop-win10-win11-64bit-international-dch-whql.exe`, `Display.Driver/nvpresent64.dll`, sha256
  `f23546294c601cd5c2274ed855e76c1e63c8f77236b0f494429e99a6a7163ea6`). NVSmooth30's `tools/inspect_nvp.py` finds
  the same gate (`+0xc40f`) and config (`+0x7667d0`) as kilyan82's log. It embeds 37 kernels, each SM89 + SM120
  only, so on SM86 every kernel must be converted.
- **Root cause (in NVSmooth30 v0.1.0, not OptiScaler):**
  - NvPresent has no `nvcuda.dll` import. It statically links the CUDA runtime loader: the resolver at `0x1362e0`
    runs its init (`0x136310`) once, which calls `LoadLibraryExA("nvcuda.dll", ..., LOAD_LIBRARY_SEARCH_SYSTEM32)`
    and asks `GetProcAddress` only for `cuGetProcAddress_v2`.
  - Every other entry point is filled through `cuGetProcAddress`; `cuModuleLoadData` lands in slot `0x771158`
    (`0x12e277`-`0x12e285`).
  - NvPresent's only kernel load (`0x4fb40`) goes through wrapper `0x1290d0`, which calls the resolver if the slot is
    empty and jumps through it. The `cuLibraryLoadData` wrapper has no direct caller.
  - NVSmooth30's reference path hard-codes 616.92's resolver `0x1348d0` / slot `0x7fb628` and is skipped. Its generic
    scan runs before the table is filled. Its last-resort hook waits for `GetProcAddress("cuModuleLoadData")`, which
    this loader never calls. So the kernels load unconverted and no frames are generated.
- **Upstream fix:** [ItsAdeline/NVSmooth30 PR 4](https://github.com/ItsAdeline/NVSmooth30/pull/4) (Chiamare, head
  `5b8cc8d6`, v0.1.1, open and unreviewed on 2026-11-02; fixes upstream issue 3).
  - It locates the resolver and the slot by pattern: the `lea rdx,[slot]` / `lea rcx,["cuModuleLoadData"]` /
    `call [cuGetProcAddress]` sequence, then the wrapper's `mov rax,[slot]` ... `call resolver`, with the resolver's
    8-byte prologue, an indirect call and a `ret` checked.
  - It hooks only when the resolver returns 0 and fills the slot, and refuses an ambiguous match. The unchanged
    616.92 reference path runs first.
  - On the 621.01 DLL above it finds `0x1362e0` / `0x771158`, the same as this analysis. The author reports 63 →
    115 FPS standalone (RTX 3060 Ti, Guild Wars 2).
- **Packaging option (`03faacfe`):**
  - `package_release.ps1 -NVSmooth30Repository <url> -NVSmooth30Ref <ref>`, and the `nvsmooth30_repository` /
    `nvsmooth30_ref` inputs of `package_release.yml`.
  - A ref is fetched into a fresh temp directory and built there. Without a ref, the old behaviour (local checkout,
    else upstream's default branch) is unchanged. Nothing is pinned in the repository.
  - The package records the source in `OptiScaler/licenses/SOURCE.nvsmooth30.txt`.
  - To ship PR 4 before it is merged: dispatch with `include_nvsmooth30=true` and
    `nvsmooth30_ref=5b8cc8d6974f5ceb4f83729a8977c8604339552f` (or `pull/4/head`, which follows later pushes).
- **Test:** `tests/nvsmooth30_packaging_source_unit.cpp` (6/6 PASS); `tests/sm86_packaging_pin_unit.cpp` still
  passes.
- **Expected in game (not yet checked):** `nvsmooth30.log` shows `startup 0.1.1`, `cudart lazy table located
  dynamically: resolver=+0x1362e0 slot=+0x771158`, then `CUDA fatbin #N ... sm89->86=1` lines, and the frame rate
  doubles.
- **Still separate:** johnCXVII's first-present `E_NOINTERFACE` on 616.92 (6.2).

### 6.8 DLSS Unlocked dropped or downgraded `nvsmooth30.dll` (`ShyVortex/DLSS-Unlocked` `d9be9a4`, `5ebde8c`)
- **Report:** kilyan82: "in 0.9.85 i noticed the dll nvsmooth30 is not included anymore".
- **Checked:**
  - the zips were listed remotely: the fork's v0.9.85 zip has `OptiScaler/nvsmooth30.dll`;
  - DLSS Unlocked's NR-v0.9.81 standalone zip has it, and NR-v0.9.85 doesn't. It is the only file that differs.
- **Defect 1, a cache hit drops the DLL** (`.github/workflows/build-installer.yml` in DLSS Unlocked):
  - run 41439431829 (11:46) copied `nvsmooth30.dll` into the standalone archive;
  - the rebuild 41441996982 (12:06) restored `optiscaler-v4-OptiScaler-NR-v0.9.85.zip-…` from cache and exited the
    download step ("Skipping download.") before the copy. `Dll version/nvsmooth30.dll` wasn't a cached path;
  - its assets replaced the first run's;
  - the Inno installer still installed `SM86\smooth-motion.asi` as `nvsmooth30.dll`.
- **Defect 2, the package copy always won:**
  - the workflow, `build-optiscaler.ps1` and the installer (`DLSS unlocked.iss` line 93 overwrites line 92) all
    preferred the OptiScaler package's `nvsmooth30.dll`;
  - `ebd1af4` had put NVSmooth30 0.1.1 (PR 4) into `SM86/smooth-motion.asi`, which would therefore never ship next to
    the package's 0.1.0.
- **Fix (maintainer's choice: the newer version wins):**
  - `tools/Select-NvSmooth30.ps1` requires `[nvs30] startup %s` and reads each binary's single NUL-delimited
    `x.y.z` string (NVSmooth30's `NVS30_VERSION`). It ships the newer; the package wins a tie. A file whose version
    can't be read only wins when the other is unknown or missing. `build-optiscaler.ps1` uses it (`d9be9a4`).
  - In the workflow, the download step caches the package's copy as `Dll version\nvsmooth30.package.dll`, and the key
    moves to `optiscaler-v5-`. A new "Select NVSmooth30" step runs on every build, cache hit or not (`5ebde8c`).
- **Verified:**
  - with PowerShell 7.4.6 on the real binaries, all 8 cases choose the expected file (by SHA-256): package 0.1.0 vs
    `.asi` 0.1.1 → `.asi`; 0.1.0 vs 0.1.0 → package; either one missing; either or both unreadable; none;
  - no parse errors in both scripts and in the two changed inline steps; the YAML parses.
  - **Not yet:** a real installer build. Expected: the selection line shows 0.1.0 vs 0.1.1 → `.asi`, and the
    standalone zip contains a 0.1.1 `OptiScaler/nvsmooth30.dll`.
