# Overlay Menu: Crash When a Game Presents a Second Window (MSFS 2024 Panel Pop-Out)

## 1. Overview & Environment

- **Report**: Noobzik, [DLSS Unlocked #72](https://github.com/ShyVortex/DLSS-Unlocked/issues/72) (2026-11-22).
- **Reporter environment**:
  - Game: Microsoft Flight Simulator 2024, Microsoft Store, package `Microsoft.Limitless_1.9.24.0`.
  - OS and GPU: Windows 11 (build 26300), NVIDIA RTX 3080, driver 621.01.
  - DLSS Unlocked v0.9.95-final (OptiScaler `1c8d76b1`), RTX 20/30 unlocker in External mode, LogLevel 0.
- **Analysis environment**: log and crash-report analysis only, no MSFS available locally. CachyOS (rolling), kernel
  7.3.5-2-cachyos, NVIDIA GeForce RTX 3080 Ti Laptop GPU, driver 580.196.07.
- **Not related**: [DLSS Unlocked #65](https://github.com/ShyVortex/DLSS-Unlocked/issues/65), a crash at flight start
  in the same game from another reporter. Nothing ties it to this cause yet.

## 2. Reported Symptoms

- "Popping the panel out from the cockpit of MSFS 2024 leads to crash to desktop." Right Alt + click on a cockpit
  display reproduces it every time.
- The game otherwise runs fine. ReShade was ruled out by the reporter.
- The Windows crash report gives:
  - `MoAppCrash`, exception `c00000fd` (stack overflow);
  - fault module `USER32.dll` 10.0.26100.9818, offset `0x14fc1`.

## 3. Issue Validity Assessment

This is a real OptiScaler defect, in the overlay menu and its input subclassing (`OptiInput`), not frame generation.
The input code comes from upstream (`menu/input/`, "New input system" `a06cdfc8` and later). Upstream had not changed
this logic as of `e797906f`.

### 3.1 Timeline from `OptiScaler.log`

| Time | Event |
|---|---|
| 15:07:08 | Sim swapchain 1920x1080 created on window `0x1E0C8C`. |
| 15:07:10.398 | `OptiInput::InstallWindowSubclass` on `0x1E0C8C`: original A = `0x7ffcaf8d0f80`. |
| 15:12:08.418 | `ValidateWindowSubclassLocked`: "subclass lost", current B = `0x7ffd84bd1000`. A is kept, so the chain is B → Opti → A. |
| 15:12:08.695 | Pop-out swapchain 672x672 created on window `0x4F0968`. |
| 15:12:08.80–.88 | `MenuOverlayDx::Present` logs "Handle changed" 1E0C8C ↔ 4F0968 on almost every present. `MenuCommon::Init` runs from threads 38208 and 9960 at the same time. |
| 15:12:08.837 | Input moves to `0x4F0968`: "RemoveWindowSubclass clearing stale state input:0x1e0c8c original:…0f80 subclassed:0". A is dropped. |
| 15:12:08.880 | Input moves back to `0x1E0C8C`: "subclass installed hwnd:0x1e0c8c previousWndProc:…1000". B is stored as the original. |
| — | The log ends. Crash. |

Which component installed B is not known: it lies in a system-DLL address range, and the session has many sim
add-ons loaded. That doesn't matter for the fix. Any WndProc installed on top of OptiInput's leads to the same
result.

### 3.2 Root cause

1. **The menu followed every swapchain.** `MenuOverlayDx::Present` compared each present's HWND with
   `MenuOverlayBase::Handle()`. On a mismatch it shut the menu down and initialised it on the new window, including
   `OptiInput::Initialize` → `SetInputWindow`. With two windows presenting, this happened almost every frame, from
   two threads.
2. **Leaving a lost chain dropped its original.** `SetInputWindow` calls `RemoveWindowSubclass()` with
   `preserveLostChain = false`. For a window whose subclass was already lost (B on top), that only cleared the state.
   OptiInputWndProc was still in that window's chain.
3. **`OptiInputWndProc` forwarded to a single original.** It used `_state.OriginalWndProc` whatever the window. So
   after step 2, the sim window's messages went to the pop-out window's procedure.
4. **Re-subclassing over the lost chain.** On the way back, `InstallWindowSubclass` saw B as the current procedure,
   replaced it, and stored B as our original. B calls Opti, and Opti now calls B: an endless loop that overflows the
   stack inside `CallWindowProcW` (USER32).

The comment in `ValidateWindowSubclassLocked` already describes this loop. Its guard only held while the input window
never changed.

## 4. Applied Solution

### 4.1 The menu stays on one window (`5e3a5a3d`)

- `OptiScaler/menu/MenuWindowOwnerPolicy.h`, `Decide(owner, candidate, ownerIsWindow, msSinceOwnerPresent)`:
  - `Draw`: the present is the owner's.
  - `Adopt`: no owner yet, the owner window is destroyed, or it hasn't presented for `kOwnerStaleMs` (1000 ms).
  - `Skip`: another window, or a null HWND, while the owner is alive and presenting.
- `ClaimMenuWindow` in `menu_overlay_dx.cpp` applies it under a mutex at the top of `MenuOverlayDx::Present`, right
  after the `OverlayMenu` check:
  - on `Skip` it returns before device capture, `Init`, `Shutdown` and render;
  - the first skipped present per window is logged ("Menu stays on window … skipping swapchain present of window …"),
    and owner changes are logged too.
- The skip runs before device capture, so a pop-out present no longer overwrites `currentSCCommandQueue`, which the
  old code did on every switch.
- Real hand-overs still work through the timeout: a launcher or splash window followed by the game window, or a
  recreated main window.
- The Vulkan overlay is not affected; rule §9 already limits it to one swapchain.

### 4.2 OptiInput never re-subclasses a lost chain (`66d9ff2d`)

- `OptiScaler/menu/input/WndProcChainPolicy.h`:
  - `ForwardTarget(hwnd, inputHwnd, inputOriginal, lost…)` returns the input window's original, the lost chain's
    original for that window, or the default. It never returns another window's procedure.
  - `DecideInstall(current, opti, hasLostChain, lostOriginal)` returns one of:
    - `AlreadyOurs`;
    - `ReuseLostChain`: Opti is still in the chain, so there is no install;
    - `Install`: the window is back at its original.
- `InputState::LostWndProcChains` (HWND → original):
  - `RemoveWindowSubclass` records an entry when the subclass was already lost, or when the current procedure isn't
    ours and so can't be restored. It skips destroyed windows (`IsWindow`), so a reused HWND value is never caught.
  - `OptiInputWndProc` removes the entry on `WM_NCDESTROY`, after forwarding the message.
  - The table survives `ResetStateAfterShutdown`: those chains keep calling us.
- `OptiInputWndProc` forwards through `ForwardWndProcLocked(hwnd)`, in both the `WM_NULL` path and the normal path.
- `InstallWindowSubclass`:
  - `ReuseLostChain`: takes the stored original back and doesn't call `SetWindowLongPtrW`.
  - `AlreadyOurs`: also takes a lost chain's original back, when the procedure on top removed itself.
  - `Install`: drops a stale entry first.
- Shutdown is unchanged: a current lost chain still makes `RemoveWindowSubclass(true)` refuse, and OptiInput keeps
  its state.

Either fix alone prevents the #72 loop. Fix A removes the window switching that MSFS triggers. Fix B makes any
legitimate input-window change safe when another WndProc sits on top.

### 4.3 Not changed

- `MenuOverlayDx::CleanupRenderTarget` still runs when any wrapped swapchain is released or resized, including a
  pop-out one. The menu's render targets are then rebuilt on the owner's next present, as before.
- No menu is drawn on pop-out windows. OptiInput supports one window per session.

### 4.4 Upstream merges

Both fixes change upstream files: `menu_overlay_dx.cpp`, `menu/input/input_system_window.cpp`,
`input_system_messages.cpp` and `input_system_internal.h`. Port upstream changes to them hunk by hunk, and keep
`ClaimMenuWindow`, `ForwardWndProcLocked`, `LostWndProcChains` and the `DecideInstall` switch.

## 5. Verification

### 5.1 Unit tests

- `tests/menu_window_owner_unit.cpp` (7/7):
  - policy cases: no owner, the pop-out skip, destroyed owner, the 1000 ms boundary, owner draw, null HWND;
  - a replay of the #72 present interleaving: the owner never leaves the sim window, and a real hand-over still
    works;
  - source checks: the claim happens before capture, `Init`, `Shutdown` and render.
- `tests/wndproc_lost_chain_unit.cpp` (5/5):
  - `ForwardTarget` and `DecideInstall`;
  - a model WndProc chain replaying #72 with the policy: no procedure is re-entered, and each window reaches its own
    original;
  - the same sequence with the old logic loops: B is stored as the original, as in the log;
  - lost-chain resolution;
  - source checks of the production wiring. These fail on the unfixed sources.
- Related suites: `menu_width_unit`, `vk_overlay_resize_unit`.
- `menu_viewport_unit` needs the bundled ImGui (`tests/run_menu_viewport.ps1`) and was not run.
- Not compiled locally (no MSVC). The CI build is the compile check.

```bash
g++ -std=c++20 -o menu_window_owner_unit tests/menu_window_owner_unit.cpp && ./menu_window_owner_unit
g++ -std=c++20 -o wndproc_lost_chain_unit tests/wndproc_lost_chain_unit.cpp && ./wndproc_lost_chain_unit
```

### 5.2 In-game confirmation

- The interim workaround `[Menu] OverlayMenu=false` stopped the crash (reporter, 2026-11-22). That confirms the menu
  path as the cause.
- Branch build `OptiScaler_v0.9.96-pre1_20261122.7z` (CI run 45042304489), with `OverlayMenu=auto`: the reporter
  confirmed on 2026-11-24 that popping a panel out no longer crashes.
- No log came with the confirmation, so the skip line and the "keeping lost WndProc chain" warning were not checked.
