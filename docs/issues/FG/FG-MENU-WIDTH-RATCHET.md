# Overlay Menu: Stays at the Full Display Width, Even at Menu Scale 0.5

## 1. Overview & Environment

- **Report**: kilyan82, [DLSS Unlocked #42](https://github.com/ShyVortex/DLSS-Unlocked/issues/42) (2026-11-09).
  It's a follow-up in the NVSmooth30 thread ([FG-NVSMOOTH30-INTEGRATION.md](FG-NVSMOOTH30-INTEGRATION.md)) and a
  different defect.
- **Reporter environment**: Windows, NVIDIA RTX 30 series (Ampere), 1920×1080 display.
  - Neverness to Everness, v0.9.88-pre4, Menu Scale 0.5.
  - Wuthering Waves, v0.9.85, Menu Scale 1.
- **Analysis environment**: source analysis only, no repro yet. CachyOS, kernel 7.3.3-1-cachyos, NVIDIA GeForce RTX
  3080 Ti Laptop GPU, driver 580.196.07.

## 2. Reported Symptoms
- "even at 0.5 it takes all the screen, i think it should be resizable like a windows window".
- **NTE screenshot**: the menu spans about 1900 px. At scale 0.5 the text is half size, but the two columns and
  their sliders stay full width.
- **Wuthering Waves screenshot**: the same width. The NVSmooth30 status line in the left column runs into the right
  column:
  ```text
  Pending Restart (Swapchain not bound) (I:\SteamLibrary\steamapps\common\Wuthering Waves\Client\Binaries\Win64\O…
  ```

## 3. Issue Validity Assessment
It's a real layout defect, not a configuration problem. It has two parts that add up.

### 3.1 How the main menu is sized
- **The window:** `RenderMainMenuWindow` creates it with `ImGuiWindowFlags_AlwaysAutoResize`, so its width is the
  width its content asked for on the previous frame (`ContentSizeIdeal`, from `DC.IdealMaxPos`).
  `MenuViewport::ConstrainWindow` clamps it to the display.
- **The table:** the two-column table is `SizingStretchSame`, so both columns get the width of the wider one. Sliders
  and plots take a share of their column, so a wide column makes everything wide.
- **What the table reports:** in the bundled ImGui (1.92.0 WIP), a table reports `ColumnsAutoFitWidth` to the window,
  not its stretched width. The table alone can therefore shrink. Only items placed at the window's *current* right
  edge, or wrapped at it, hold the width.

### 3.2 Part 1: the window could grow but never shrink (upstream code)
- `RenderMainMenuBottomBar` right-aligned "Open Wiki" with
  `SetCursorPosX(GetCursorPosX() + GetContentRegionAvail().x - textSize.x)`.
- The button therefore always ended at the window's current right edge, and the content measured exactly as wide as
  the window, so auto-resize kept that width every frame.
- Once anything widened the window (a long line, a scale change), it stayed wide until the scale changed again.
- Upstream `284d7d4d` (cdozdil, 2026-03-29). It's harmless upstream, where nothing widens the menu.

### 3.3 Part 2: long fork status lines widened a column (fork code)
- The NVSmooth30 status printed the full `nvsmooth30.dll` path (`63a0b69a`, 2026-09-24). Game-folder paths are often
  more than 100 characters long. That's the line in the Wuthering Waves screenshot.
- The XeMFG and NVSmooth30 error lines were printed without wrapping, e.g. "NVSmooth30 requires an RTX 30 series
  (Ampere) GPU. Detected arch 0x… (<GPU name>)."
- The other frame generation status lines used `TextWrapped`, which wraps at the column's edge. That's also a
  window-edge position: the line fills whatever width the column has, which holds it there.

### 3.4 Not fully explained yet
The NTE screenshot shows `Disabled in config` at that moment. The width may have come from an earlier line in the
same session, kept by the ratchet in 3.2. A repro on the maintainer's machine is still pending (section 6).

### 3.5 Audit
- No `docs/issues/` entry or commit about the menu width (grep for menu width, `ConstrainWindow`, `menu_viewport`).
- `8c9b4531` (DX12 framebuffer mapping and viewport bounds) only added the display clamp.

### 3.6 The request for a resizable menu
- Not done. A user-resizable window that also scales its content is a larger change, and it would hide this defect
  rather than fix it.
- Once the menu returns to its natural width, the existing Menu Scale covers smaller sizes.
- Revisit if it's still wanted after this fix.

## 4. Solution Applied
### 4.1 The footer follows the content's right edge (`c36e8a3d`)
- New header `OptiScaler/menu/MenuLayout.h`.
  - `MenuLayout::RightAlignedX(cursorX, itemWidth, contentRightX)` ends the item at the content's own right edge,
    `window->DC.IdealMaxPos.x` after the table and the plots.
  - If the cursor is already past that point, the item follows the cursor.
- `RenderMainMenuBottomBar` uses it with `SetCursorScreenPos`, and no longer `GetContentRegionAvail`.
- The window can now fall back to its content width.

### 4.2 Status lines wrap at a bounded width (`60e5607a`)
- **The wrap position:** `MenuLayout::StatusWrapWidth(menuScale)` is 420 px × Menu Scale.
  `MenuLayout::StatusWrapPosX(cursorX, avail, menuScale)` wraps at that width from where the text starts, or at the
  column edge when the column is narrower. A fixed width, not the window edge, so the wrap can't follow the window.
- **In `menu_common.cpp`:**
  - `ScopedStatusWrap` pushes that wrap position and pops it on scope exit.
  - `StatusTextWrapped(menuScale, fmt, ...)` is `TextWrapped` with the bounded wrap.
- **In `RenderFrameGenerationSelection`:**
  - all 17 `TextWrapped` calls became `StatusTextWrapped`;
  - the XeMFG and NVSmooth30 error lines are wrapped with `ScopedStatusWrap`;
  - the NVSmooth30 status shows only the DLL file name, with the full path in a tooltip.

### 4.3 Not changed
- `DlssNr_Menu.cpp` (upstream wilsjo2 code, `944c55e2`) sizes its toggle row from `GetContentRegionAvail()` while
  the "DLSS Neural Rendering" header is open. That may hold the right column at its current width while the header
  is open.
- Check it during the repro. Port any fix hunk by hunk (architecture rule §6).
- Tooltips are separate windows and don't affect the menu width.

## 5. Test Verification
- `tests/menu_width_unit.cpp` (5/5 PASS):
  1. the `RightAlignedX` placement table;
  2. a frame-by-frame simulation: the old placement keeps a 1900 px window at 1900 px, the new one shrinks it back to
     its 900 px content;
  3. the footer uses `IdealMaxPos` and `RightAlignedX`, and no `GetContentRegionAvail`;
  4. the `StatusWrapPosX` / `StatusWrapWidth` table (bounded width, halved at scale 0.5, narrower column edge, no
     room reported);
  5. `RenderFrameGenerationSelection` has no `TextWrapped` left; the error lines are wrapped; the DLL path is in a
     tooltip.
- Also passing: `splash_branding_unit`, `nvsmooth30_unwrapped_swapchain_unit`, `nvsmooth30_loader_unit`,
  `nvsmooth30_attach_probe_unit`, `ampere_smooth_motion_unit`, `xemfg_menu_exclusion_unit`,
  `turing_menu_recognition_unit`, `nr_driver_gate_unit`, `nr_game_native_unit`.
- Windows CI build: passed (run 42785820147, `0ed77d01`).

## 6. Round 2: Still Full Width on `0ed77d0`
### 6.1 Report
- kilyan82, 2026-11-10: Neverness to Everness on v0.9.90-final (`0ed77d0`), Menu Scale 1.0.
- The menu still spans about 1890 px.
- No long line is visible: the NVSmooth30 status reads `Disabled in config`.
- "Open Wiki" is missing from the footer.

### 6.2 Root cause: every collapsible section held its column
- **The helper:** `ScopedCollapsingHeader` (upstream cdozdil `bd2682a5`, 2025-12-22, "Fix collapsing headers
  overlapping with controls on second column") puts every collapsible section ("Advanced DLSS Settings", "Logging",
  …) in `BeginChild(ImVec2(0, 0), ImGuiChildFlags_AutoResizeY)`.
- **Why it holds:** width 0 means the column's whole available width, and `EndChild` reports the child's size to the
  layout (`ItemSize`). Each column therefore measured exactly as wide as it already was.
- **Effect:** once anything widened the menu (a long line, a wide section opened, a scale change), it never came back.
  Both columns have sections, so both were held.
- **What round 1 missed:** section 3.1 named items placed at the window edge, but not child windows. The footer and
  the long lines were real, but they were not the only causes.
- **Audit:** no `docs/issues/` entry or other commit touches `ScopedCollapsingHeader`.

### 6.3 Why "Open Wiki" disappeared (round 1 bug)
- Task 1 ends the button at the content's right edge (`IdealMaxPos.x`).
- When the content is wider than the display, the window is clamped to the display but the content edge is not, so
  the button was placed off-screen.
- The screenshot therefore also means the content in that session was wider than the display. Its source isn't known
  yet (section 6.5).

### 6.4 Solution (`20984022`, `af0ffd38`)
- **Sections (`20984022`):**
  - `ScopedCollapsingHeader` moves to `OptiScaler/menu/ScopedCollapsingHeader.h`. `menu_common.h` includes it, so the
    API is unchanged for its users in `menu_common.cpp` and `DlssNr_Menu.cpp`.
  - The child still spans the column, so the header bar and the `bd2682a5` overlap fix are unchanged.
  - After `EndChild`, the parent's `CursorMaxPos.x` is set to what the child's content reached: `max(child
    CursorMaxPos.x, IdealMaxPos.x) + WindowPadding.x`, never below the parent's previous value. ImGui's own
    `EndTable` makes the same split between the width a table uses and the width it reports.
- **Footer (`20984022`):** `MenuLayout::RightAlignedX` takes the window's visible edge (`WorkRect.Max.x`) and never
  goes past it. This can't hold the width again, because the cap only applies while the content is already wider
  than the window.
- **Diagnostic (`af0ffd38`):** the main menu notes its content's right edge after each section. When the content is
  wider than the display, it logs once, at Info level:
  ```text
  Menu content is N px wide, the display M px (Menu Scale S). Widest section: <name>. Left column L px, right column R px
  ```
  It logs again only when the widest section changes (`MenuLayout::WidestSection`, `ExceedsWidth`).

### 6.5 Still open
- What made the content wider than the display in kilyan82's session on `0ed77d0`.
- The log line above names the section on the next build.

### 6.6 Test Verification
- `tests/menu_header_width_unit.cpp` (5/5 PASS) builds the bundled ImGui (1.92.0 WIP) headless. Windows:
  `tests/run_menu_header_width.ps1`; Linux: the g++ line in the test header.
- It runs a model of the main menu with the real helper, `MenuViewport::ConstrainWindow`, the two-column
  `SizingStretchSame` table and the footer, on a 1920×1080 display.

  | | Natural | With a 2800 px line | Line removed |
  |---|---|---|---|
  | Fixed, scale 1 | 1016 px | 1882 px | 1016 px |
  | Fixed, scale 0.5 | 464 px | 1266 px | 464 px |
  | Old helper (`-DMENU_HEADER_WIDTH_LEGACY`), scale 1 | 1016 px | 1882 px | **1882 px** |

  - The section's child still spans the whole column.
  - The footer button stays inside the window. On `0ed77d0` it ended at 2808 px in an 1882 px window.
  - A wide option inside an open section still widens the menu, and the menu shrinks back once it's gone.
- `tests/menu_width_unit.cpp` (6/6 PASS): `RightAlignedX` with the visible edge (test 1), and the diagnostic
  (test 6).
- The related menu suites still pass: splash, NVSmooth30, NR gate, game-native NR, Turing and XeMFG menu.

## 7. Pending In-Game Verification
- **Maintainer (Linux, RTX 3080 Ti laptop, Ampere, so the NVSmooth30 section shows):**
  1. Open the menu in any game and note its width.
  2. Open a wide section, e.g. "Advanced Settings", then close it: the menu should shrink back.
  3. With `[SmoothMotion]` and NVSmooth30 enabled, restart and open the menu. On Linux the status reads "NVSmooth30
     requires Windows …". The menu should keep its natural width.
  4. Change Menu Scale up and back down: the width should come back each time.
- **kilyan82 (Windows, 1080p):**
  - The menu should no longer span the screen, and "Open Wiki" should be visible.
  - At Menu Scale 0.5 the menu should be about half as wide.
  - If it's still wide, `OptiScaler.log` should contain "Menu content is … Widest section: …".
