# Frame Generation: sdli1995 dlssg_for_sm86 v0.4.2 Integration

## 1. Overview & Environment
- **Trigger**: sdli1995 released `dlssg_for_sm86` 0.4.2 (tag `0.4.2`, `e2c2fafa`, 2026-11-14; manifest
  `created_at` 2026-11-14T02:18:06Z).
  - The release files were placed in `dlssg_for_sm86/sdli1995/`.
  - All 29 files match the release `manifest.json` SHA-256 hashes.
- **Work branch**: `dlssg-sm86`, fast-forwarded to `main` (`46589052`) first, per the permanent-branch workflow (Case A:
  mod update).
- **Analysis environment**: CachyOS rolling, kernel 7.3.4-1-cachyos, NVIDIA GeForce RTX 3080 Ti Laptop GPU (compute
  capability 8.6), driver 580.196.07.
- **0.4.1 baseline**: tag `0.4.1` (`44f40064`). It matches the 0.4.1 install in the Metro 2033 Redux folder (root
  proxy SHA-256 `275ffa78…`).

## 2. What Changed in 0.4.2
From the release changelog (`README.en.md` "0.4.2"), the factory INI, `docs/INSTALL.en.md` and the binaries:

| Component | Change |
|---|---|
| `version.dll` (proxy) | Only the version string (`0.4.1` → `0.4.2`) and the signing timestamp. Same size (815,240 bytes), same 19 exports. |
| `dlssg_sm86/runtime/nvngx_dlssg.dll` | Unchanged: 310.9.16.0, `cadee1aa…`. |
| `dlssg_sm86/kernels/fg_kernels.dll` | Re-signed only (the differing bytes are the signature); same kernel strings. |
| `dlssg_sm86/backend/sm86_backend.dll` | 2,312,840 → 2,328,200 bytes, same 17 exports. The new strings are XeSS-FG only: `.bw` kernel images for Blackwell, `jt_` jitter kernels, `route_profile`, `blackwell_images`. |
| `dlssg_sm86/xefg/xefg_kernels.dll` | XeSS-FG model pack: Blackwell images, jittered motion vectors at 3X/4X, 4K scatter optimisation. |
| `dlssg_sm86/sr/sr_kernels.dll` | Rebuilt; `sr/nvngx_dlss.dll` unchanged (`858ccff9…`). |
| `dlssg_sm86.ini` | Only the `[XeSSFG] UseHudless` comment: `Auto` now behaves as `0` (the whole image, UI included, is interpolated). |
| `dlssg_sm86ini_Configurator.html` | New: a single-file editor for the mod's INI. |
| `LICENSE.md` | New in the release zip (not in the git tag): the project source moves from GPLv3 to **MIT**, Copyright (c) 2026 sdli1995. Kernel images stay binary-only and outside the MIT licence. |
| `THIRD_PARTY_NOTICES.txt`, README credits | dashdogy's RTX40MFG-Unlock v1.4.2 credit removed; MFG-Unlocker 1.6.0 is credited for the model-15 port and the frame-count unlock. |

## 3. Validity Assessment
- **The loader, INI generator and menu need no change.**
  - OptiScaler writes `[FrameGeneration] Engine=DLSS` and never writes `[XeSSFG]`. Packaging prunes `xefg/` and `sr/`
    (rule §4). None of the XeSS-FG or SR work reaches our path.
  - The DLSS-G path is byte-identical in function: the same runtime file, the same kernel pack contents, and a proxy
    whose only changes are its version and signature.
  - The INI rules are the same as in 0.4.1. The old places of `Optimized` / `Preset` give only a warning when written
    beside `[DLSSG]`, which is what `FormatIniContent030` does.
  - The mod's `foreign_fg_mod` scan (it names OptiScaler) was already in 0.4.1.
- **Detection checked against the real binaries** with OptiScaler's own `AmpereMfgLoader.h` functions (probe built
  with g++ in the session scratchpad):

  | Binary | `DetectModVariant` | Version read | SM75 family | 310.1 | DMFG | ASI |
  |---|---|---|---|---|---|---|
  | 0.4.1 `version.dll` | sdli1995 | `0.4.1` | yes | no | no | no |
  | 0.4.2 `version.dll` | sdli1995 | `0.4.2` | yes | no | no | no |
  | 0.4.1 / 0.4.2 `sm86_backend.dll` | sdli1995 | none | yes (markers) | no | no | no |

  0.4.2 is shown as `sdli1995 v0.4.2` with no hard-coded version. `IsSdli1995AtLeast041` holds, so the
  Hardware Bilinear lock, the "Auto (runs as A in this build)" preset label, the generation-A warning
  ([DLSS Unlocked #61](https://github.com/ShyVortex/DLSS-Unlocked/issues/61)) and the R580 driver floor apply as on
  0.4.1.
- **Genuine gaps:**
  1. Both release paths still fetched `tags/0.4.1` (`package_release.ps1`, `.github/workflows/package_release.yml`).
  2. MIT requires its notice to ship with copies. The package carried the mod's DLLs without it, and the git tag CI
     fetches has no `LICENSE.md`.
- **Not shipped on purpose:** `dlssg_sm86ini_Configurator.html`. `AmpereMfgLoader::TrySetup` rewrites
  `dlssg_sm86.ini` on every start (and `WriteCompanionIni` on menu changes), so edits made with it would be lost.

## 4. Solution Applied (`dlssg-sm86`)
- **`145f9b18`**: both release paths pin `tags/0.4.2`; their messages name 0.4.2.
- **`cb2262d2`**: `package_release.ps1` stages `OptiScaler/dlssg_sm86/LICENSE.md`.
  - It prefers the source folder's `LICENSE.md` (present in the release zip).
  - It falls back to the vendored `Licenses/dlssg_for_sm86_LICENSE.md`, the 0.4.2 file verbatim (`7537220f…`, as in
    the manifest).
  - Packaging stops if neither exists.

## 5. Verification
- `tests/sm86_packaging_pin_unit.cpp` (4/4 PASS):
  - Test 1 now expects `0.4.2` and failed against the old pin (`pinned == "0.4.2"`).
  - Test 4 (new) checks the licence staging order and the vendored file. It failed before the staging existed.
- `tests/nvsmooth30_packaging_source_unit.cpp` (6/6 PASS): unaffected.
- clang-format 23.1.5 clean and UTF-8 BOM present on the modified test.
- PowerShell is not available on the analysis machine: the staging code runs only in the Package Release workflow.
- **In game (2026-11-17, maintainer):** The Last of Us Part II on Windows with the 0.4.2 package: frame generation
  works through the RTX 20/30 unlocker, and the menu shows the mod as `v0.4.2`, read from the binary.
  `dlssg-sm86` was then merged into `main`.
