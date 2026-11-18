# RTX 40 MFG Unlock

The built-in RTX 40 (Ada) MFG unlock is compiled into OptiScaler: no extra helper, ASI loader or External FG mode is
needed. It is one of the two unlockers of the in-game **DLSS Frame Generation** section; the other is the RTX 20/30
unlocker (sdli1995 `dlssg_for_sm86`).

## Build

The build flag `OptiScalerRtx40Mfg` defaults to `true` in `OptiScaler.vcxproj` and defines `OPTISCALER_RTX40_MFG`
(architecture rule §4). A build with `/p:OptiScalerRtx40Mfg=false` leaves out the unlock implementation, hooks,
capability overrides and its menu block; the DLSS Frame Generation section then greys out the RTX 40 entry. Ordinary
DLSS FG/MFG remains available.

```powershell
# Default: the unlock is compiled in
MSBuild OptiScaler.sln /p:Configuration=Release /p:Platform=x64

# Without the unlock
MSBuild OptiScaler.sln /p:Configuration=Release /p:Platform=x64 /p:OptiScalerRtx40Mfg=false
```

Enabled builds use `x64/Release-RTX40-MFG`; standard builds use `x64/Release`. Separate intermediate folders prevent
mixing objects/PCH files. The runtime switch still defaults off.

## Enable at runtime

1. In the OptiScaler overlay, open **DLSS Frame Generation**, tick **Enable DLSS FG / MFG unlocker** and leave
   **Unlocker** on **Auto**: on an RTX 40 card it shows "Auto (RTX 40 unlock on this GPU)". In `OptiScaler.ini`:

   ```ini
   [DLSSG]
   MfgUnlock=true
   MfgUnlocker=auto
   ```

   The older `AdaMfgUnlock=true` still works while `MfgUnlock=auto`; saving settings writes the new keys.
2. The RTX 40 options sit under **RTX 40 (Ada)** in the same section:
   - **Retarget Blackwell kernels** (`AdaBlackwellKernels`, default on): retargets NVIDIA's Blackwell (`sm_120`) PTX
     interpolation kernels in `nvngx_dlssg.dll` to Ada (`sm_89`). **Recommended for 3X–6X generation** to eliminate
     frame pacing micro-stutter.
   - **Max Multiplier Ceiling** (`AdaMfgMaxFrames`, 0 = up to 6X, 1 = 2X … 5 = 6X): the highest multiplier offered to
     the game. The game's own setting still picks the multiplier. At 1 the game hides its 3X+ options.
   - **FG Preset** (`AdaMfgPreset`: auto, a, b, c): the DLSS Frame Generation preset, answered to the game's NVIDIA
     driver profile read for this game only. A and B are NVIDIA's original frame generation models; C is its
     performance mode and needs DLSS-G 310.9.16 or newer. The menu shows when the preset was applied.
   - Save Settings and restart the game after any change.
3. The unlock runs only while OptiScaler owns DLSS-G: keep `[FrameGen] External=false`. Enable the game's DLSS FG or
   configure OptiScaler's normal DLSSG output. Select 3X or 4X and verify smoothness.

The option defaults off and only patches RTX 40/Ada. It requires a supported NVIDIA DLSSG runtime (such as v310.9); game multiplier overrides need Streamline 2.7.1+. Keep the game's working runtime. This package does not include NVIDIA FG/NR DLLs or an external MFG unlocker.

The patch retargets compatible Blackwell interpolation kernels for Ada and changes two frame-count gates in memory. It exposes up to five generated frames (6X including the real frame) only when both gates and a kernel group match. Unknown/ambiguous signatures remain unchanged. Disabling also requires a restart; it does not undo a live patch.

On RTX 20/30, **Auto** picks the RTX 20/30 unlocker instead (sdli1995 `dlssg_for_sm86`, External FG mode). RTX 50
runs MFG natively, and the section is locked there.

## Frame Pacing, High-Refresh Displays & 3X+ Multi-Frame Generation

### Why Blackwell Kernel Retargeting (`AdaBlackwellKernels`) is Recommended
In `nvngx_dlssg.dll` (v310.9), NVIDIA's embedded stock Ada (`sm_89`) cubin kernels were designed for 2X Frame Generation (1 generated frame). With 3X, 4X, or 5X generation, the stock Ada interpolation kernel (`Kernel_EstimateIntermMvecsScatter`) reads only a single scalar float from its parameter block, causing timing collisions where generated sub-frames are not positioned evenly along the motion delta. Furthermore, multi-pass execution latency increases significantly.

When `AdaBlackwellKernels = true` is enabled:
- OptiScaler scans the fatbin containers inside `nvngx_dlssg.dll`, rewrites `.target sm_120` PTX directives to `.target sm_89`, and swaps the container architecture tag from Blackwell to Ada.
- The NVIDIA display driver's JIT compiler generates optimized Ada machine code from the Blackwell PTX routines, which properly read all three interpolation coordinates and execute much faster per sub-frame.
- This ensures sub-frames are evenly spaced in time, resolving the micro-stutter/judder on 120Hz/144Hz/165Hz+ VRR displays.

### Display Refresh Rates & Pacing Division
- **2X FG (60 FPS output from 30 FPS base)** divides evenly into 60 Hz and 120 Hz displays (1 frame every 16.6ms at 60Hz; 2 refreshes per frame at 120Hz), yielding a smooth, uniform cadence.
- **3X FG (90 FPS output from 30 FPS base)** cannot divide evenly into 60 Hz, 120 Hz, or 144 Hz fixed refresh cycles. Without Variable Refresh Rate (G-Sync/FreeSync), the display alternates between 1-refresh and 2-refresh frame durations (3:2 pulldown judder).
- **Best Practice for 3X+ MFG**:
  1. Use a G-Sync Compatible or FreeSync VRR display with G-Sync enabled.
  2. Enable Vertical Sync in the NVIDIA Control Panel (or game menu) so Streamline can synchronize presentation intervals to V-Blanks.
  3. Avoid using tight in-game 30 FPS limiters that put the CPU thread to sleep, as sleep timer jitter (±1–3 ms) disrupts Reflex queue pacing. If an FPS cap is needed, use NVIDIA Control Panel's Max Frame Rate or RTSS set to your monitor's refresh rate.

### DirectX 12 Requirement vs DirectX 11
- Native NVIDIA DLSS Frame Generation (`sl.dlss_g` / `nvngx_dlssg.dll`) is **strictly a DirectX 12 (D3D12)** technology. It cannot attach to or hook DirectX 11 pipelines.
- In DirectX 11 games (e.g. *Grand Theft Auto V*), native DLSS-G will not run. To use Frame Generation in DX11 titles, configure OptiScaler with **AMD FSR 3.1 Frame Generation** (`FrameGen.FGOutput = FSRFG`) via the built-in DX11-with-DX12 interposer bridge.

## Validation and source

`tests/mfg_unlock/run.ps1` compiles the production patcher/scanner against controlled PE images. Cases cover both gate layouts, kernel retargeting, repeat calls, unsupported GPUs, missing/ambiguous gates, malformed kernels and restart semantics. `-Runtime <nvngx_dlssg.dll>` additionally patches an image mapped without DLL initialization, under simulated Ada identity; it checks that the disk file is unchanged. Neither test proves real RTX 40 interpolation works.

The branch changes are the patcher, DLSSG/Streamline/load hooks, the toggle and INI handling, project registrations, tests and package documentation. The [NR upstream inventory](NR-UPSTREAM-DIFF-INVENTORY.md) describes the v0.8.0 base.

Adapted from [y4my4my4m's work](https://github.com/y4my4my4m/OptiScaler_DLSSNR_Multipass_MFG/commit/7b7220bb) and the earlier fork's Ada kernel retargeting, under GPL-3.0. This is the built-in implementation, not Dashdogy's separate unlocker.
