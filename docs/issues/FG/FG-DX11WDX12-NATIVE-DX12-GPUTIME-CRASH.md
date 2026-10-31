# Frame Generation: Crash After the Intro in Metro 2033 Redux (D3D11 Context Passed to a Native D3D12 Feature)

## 1. Overview & Environment
- **Found by**: the maintainer on 2026-10-31, while testing Neural Rendering in Metro 2033 Redux.
- **Environment**:
  - CachyOS, kernel 7.3.1-1-cachyos;
  - NVIDIA GeForce RTX 3080 Ti Laptop GPU, driver 580.196.07;
  - Proton (`Running on Wine 11.0`).
- **Game**: Metro 2033 Redux (Steam, `metro.exe`, 4A Engine), with the Next-Gen update. It ships DLSS 310.9.16 and
  `nvngx_dlssnr.dll`.
- **Configuration**:
  - `FrameGen.External=false`, `AmpereMfgUnlock=false`;
  - XeMFG `UnlockMFG=true`, `Source=OptiScaler`, `MaxInterpolatedFrames=1`;
  - DLSS at DLAA, 2560x1440.
- **Builds**:
  - crash seen on v0.9.81-final `911ec78` and `9be8495`;
  - fixed in `b4dbb44`.

## 2. Reported Symptoms
- The game crashes right after the intro video is skipped. No error is logged.
- OptiScaler's log stops inside a present.
- The Proton log (`PROTON_LOG=1`):
  ```
  Unhandled exception: page fault on read access to 0x0000000000000290 in 64-bit code
  =>0 d3d11 (+0x14f1c8)
    1 d3d11 (+0x156e7e)
    2 dxgi (+0x3e695)      <- OptiScaler (installed as dxgi.dll)
    ...
    6 libxess_fg (+0x21f965)
    ...
    15 dxgi (+0x133503)
    16 metro (+0x8259a6)
  ```

## 3. Issue Validity Assessment
**A genuine OptiScaler defect, in upstream code** (`9c7348f6` / `c64533a2`, "Per shader GPU time"). It is unrelated to
the Neural Rendering work: NR wasn't created in the crashing runs.

- **The game uses two APIs.**
  - It creates a D3D11 device and its own D3D12 device (`hkD3D12CreateDevice ... Caller: metro.exe`).
  - It initialises NGX and creates DLSS through **D3D12** (`NVSDK_NGX_D3D12_CreateFeature`), so `currentFeature`
    is a native `DLSSFeatureDx12`.
  - It presents through a **D3D11** swapchain ("Failed to get ID3D12CommandQueue from pDevice, creating Dx11
    swapchain!"). OptiScaler wraps that in `Dx11wDx12SC` and runs XeFG on the game's D3D12 device.
- **Mapping the backtrace.** The backtrace offsets were mapped onto the user's `dxgi.dll` through the `.pdata`
  function ranges and the log strings each function references. Chain: `Dx11wDx12SC::Present` →
  `FGHooks::hkFGPresent` → `FGHooks::FGPresent` → `hkFGPresent1` → `libxess_fg` → `WrappedIDXGISwapChain4` →
  `LocalPresent` → the GPU-time read → `GpuTime_Dx12::ReadGpuTime` (function `0x3e637`) → `d3d11`.
- **Audit:** no `docs/issues/` entry covers the GPU-time read. This is not a previously tried fix.

## 4. Root Cause
- **The GPU-time read.** Both present paths read the upscaler's GPU time through
  `IFeature::ReadUpscalerTime(void*)` / `ReadDetailedGpuTimes(void*)`.
  - A D3D11 feature casts the argument to `ID3D11DeviceContext*`.
  - A native D3D12 feature casts it to `ID3D12CommandQueue*`.
  - The `Dx11wDx12` / `VkwDx12` wrappers ignore it.
- **`LocalPresent` (`wrapped_swapchain.cpp`)** runs the read while frame generation is inactive or paused.
  - It first read correctly through the D3D12 queue for a native D3D12 feature.
  - Then an unconditional `if (swapchainInteropApi == Dx11wDx12 && currentD3D11Device)` block read again with the
    **D3D11 context**.
  - `GpuTime_Dx12::ReadGpuTime` called `GetTimestampFrequency` through it: a D3D12 vtable slot on a D3D11 object.
    That runs an unrelated `d3d11` function, which dereferences null + 0x290.
- **`FGPresent` (`FG_Hooks.cpp`)** had the same mistake on the active-FG path: it picked the D3D11 context purely
  because the swapchain was `Dx11wDx12`.
- **Why only after the intro:** skipping the intro makes the game recreate its swapchain, and then create the DLSS
  feature. Before that, `currentFeature` is null and nothing is read.

## 5. Applied Solution
- **`9be8495c`** fixed `FGPresent` only. The retest still crashed, because the crashing call is in `LocalPresent`.
- **`b4dbb440`** fixes both sites.
  - `OptiScaler/hooks/FgGpuTimeSource.h` (no Windows headers) decides by **feature type only**:
    - `Classify(Api()==DX11, Api()==DX12, IsWithDx12())`;
    - `Resolve(kind, hasD3D11Device, hasQueue)`:
      - native D3D12 → D3D12 queue;
      - D3D11 → D3D11 context;
      - wrapper → whichever is available;
      - anything else → no read.
  - `LocalPresent` uses the swapchain's D3D11 device, or `currentD3D11Device` on a `Dx11wDx12` swapchain, and `cq`.
    It now reads **once** per present. Before, D3D11 features were read twice, which advanced the timer ring twice
    per frame.
  - `FGPresent` passes the D3D11 device only on a `Dx11wDx12` swapchain, and `currentCommandQueue`.
- **Not changed:**
  - `GpuTime_Dx11` / `GpuTime_Dx12`;
  - the feature classes;
  - pacing;
  - XeFG lifecycle.

## 6. XeFG and Exclusive Fullscreen
- After the swapchain recreation, XeFG logs "Interpolation is not supported in fullscreen mode. Interpolation won't
  be enabled", because the game creates an exclusive-fullscreen swapchain.
- With `[XeMFG] ForceBorderless=true` XeFG interpolates. The maintainer confirmed this in game on `b4dbb44`.
- Metro Exodus Enhanced Edition, on a newer version of the same engine, already has the
  `ForceBorderlessWhenUsingXeFG` quirk.
- `misc/Quirks.h` now has `QUIRK_ENTRY("metro.exe", GameQuirk::ForceBorderlessWhenUsingXeFG)`.
  - It sets a volatile `FGXeFGForceBorderless=true` only when XeFG is the FG output, and never when the user has set
    `[XeMFG] ForceBorderless` themselves.
  - Metro Last Light Redux is expected to share `metro.exe`, so it gets the same quirk. That is unverified there.
- Test 7 in `tests/fg_gputime_source_unit.cpp` checks the entry.

## 7. Test Verification
- `tests/fg_gputime_source_unit.cpp` (7/7 PASS; test 7 is the quirk, see section 6):
  1. Native D3D12 features only get the queue.
  2. D3D11 features only get the context.
  3. The wrappers read with whatever is available.
  4. Classification, and unknown kinds skip the read.
  5. A source check of `FGPresent`.
  6. A source check of `LocalPresent`: one read in exclusive branches, and the unconditional `Dx11wDx12` context
     read is gone.
- `dlssg_swapchain_external_fg_unit`, `nr_finished_picture_gate_unit`, `optifg_xefg_frametime_pacing_unit` and
  `swapchain_present_reentrancy_unit` still pass. clang-format 23 clean; UTF-8 BOM present.
- **In game (maintainer, 2026-10-31, `b4dbb44`):** skipping the intro no longer crashes, and with
  `ForceBorderless=true` XeFG interpolates.

## 8. Out of Scope
- The crash with `FrameGen.External=true` + `AmpereMfgUnlock=true` (sdli1995 0.4.1) is a different configuration. No
  Proton log has been taken for it yet.
- The XeFG `ERROR_POINTER_STILL_IN_USE` logged on swapchain destroy.
