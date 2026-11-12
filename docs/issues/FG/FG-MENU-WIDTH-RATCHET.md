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
  It logged again only when the widest section changed. Replaced in round 3 (section 7.3): it blamed the wrong
  section.

### 6.5 Still open
- What made the content wider than the display in kilyan82's session on `0ed77d0`. Found in round 3 (section 7).

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

## 7. Round 3: A Hidden Checkbox Past the Left Column
### 7.1 Report
- **kilyan82, 2026-11-10:** NTE on v0.9.90-final (`28557e6`). "Open Wiki is there now, but window's still stretched."
- **His log**, one line from `af0ffd38`:
  ```text
  Menu content is 2118 px wide, the display 1920 px (Menu Scale 1.00). Widest section: Graphs. Left column 1055 px, right column 409 px
  ```
- **Maintainer reproduction, 2026-11-11:** Metro 2033 Redux on Linux, `ee43f5b`, display 2560×1440. CachyOS, kernel
  7.3.3-1-cachyos, NVIDIA GeForce RTX 3080 Ti Laptop GPU, driver 580.196.07. Lost Soul Aside on the same machine
  did not reproduce it: that game has DLSS-G (7.3).

### 7.2 Why the menu is twice the widest column
- The main table is `SizingStretchSame` without `ImGuiTableFlags_Resizable`, so its columns are `NoResize`
  (`imgui_tables.cpp:763`).
- For `NoResize` stretch columns ImGui reports the table as wide as needed to give *every* column the widest one's
  content (`auto_fit_width_for_stretched_min`, `:1451`). With two equal columns that's 2 × the widest column.
- kilyan82's numbers: 2 × 1055 + padding ≈ 2118. One section wider than its column widens the whole menu by twice
  the overshoot.
- The `af0ffd38` line blamed "Graphs" wrongly: the main table adds its own width to the window in `EndTable`, after
  the last section had been noted. It also logged a single moment, not the steady state.

### 7.3 Per-section probe (`ee43f5b7`)
- **How it measures:** `OptiScaler/menu/MenuWidthProbe.h`. Before each section, the column's `CursorMaxPos.x` is
  reset to the column start; after it, the section's extent is recorded and the maximum restored. The layout is
  unchanged.
- **Marks:** every section of both columns, plus the frame generation blocks (ownership, RTX 40, RTX 20/30, XeMFG,
  Smooth Motion, then "FG: selection" for the rest).
- **Report:** once the menu has been wider than the display for 120 frames in a row, three lines with every
  section's width. Repeated once per wide episode (`MenuLayout::WideStreak`).
- **Maintainer's Metro log:**

  | Menu Scale | Left column visible | `FG: selection` | Past the edge |
  |---|---|---|---|
  | 1.3 | 1241 px | 1476 px | 235 px = 181 × 1.3 |
  | 2.0 | 1208 px | 1568 px | 360 px = 180 × 2.0 |

  Every other section fits its column. The overshoot is a fixed ~180 px × Menu Scale: an item of fixed size placed
  past the column edge.

### 7.4 Root cause: the DMFG checkbox placed after the FG selection table (fork code)
- **The code:** the DLSS-G override row in `RenderFrameGenerationSelection`.
  - The "Override DLSSG Ratio" combo is drawn only when the game's DLSS-G maximum is known (`state.dlssgMfgMax`).
  - The DMFG block after it always started with `ImGui::SameLine(0.0f, 16.0f)`.
- **What happened in games without DLSS-G (Metro, NTE):**
  - The combo isn't drawn, so `SameLine` attached "Force Dynamic MFG (?)" to the previous item, the FG Input /
    Output table.
  - That table's line ends at the column's right edge, so the checkbox sat past the edge.
  - ImGui clips each column, so it was invisible. Its ~180 px still counted as content.
- **History:** fork `cdef05cd` (2026-09-22, "DLSS-G: add DMFG menu options") widened the block's gate from
  `state.dlssgGameDMFGSupported && !dlssgInputOrOutput` to `!dlssgInputOrOutput`, so DMFG could be forced on
  Streamline 2.11+ even when the game doesn't report DMFG support. It kept the `SameLine`. Upstream (`6125a75b`)
  still has its own gating.
- **Why Lost Soul Aside didn't show it:** the game has DLSS-G, so the ratio combo was on the line and the checkbox
  followed it, inside the column.
- **Audit:** no `docs/issues/` entry covers this block.

### 7.5 Solution (`86054259`)
- The DMFG controls are drawn only when the game's DLSS-G is known: `MenuLayout::ShowsDlssgDmfgControls`
  (`dlssgMfgMax` set, OptiScaler not owning DLSS-G).
- `SameLine` is used only when the ratio combo was drawn on that line (`MenuLayout::ShowsDlssgRatioCombo`).
- cdef05cd's enablement gate is unchanged: `dlssgGameDMFGSupported` or Streamline 2.11+.
- **Visible effect:**
  - Games without DLSS-G look the same, just at their natural width.
  - With DLSS-G nothing changes.
  - If a DLSS-G game reports a maximum of 0 generated frames, the checkbox now starts its own line instead of
    following the table.

### 7.6 Test Verification
- `tests/menu_header_width_unit.cpp` (7/7 PASS, bundled ImGui headless):
  - Test 6: the probe leaves every width unchanged, and names and measures a 1396 px line exactly.
  - Test 7 reproduces this defect: with the checkbox placed after the FG selection table, that section measures
    1159 px in a 929 px column and the menu grows from 1016 to 1882 px (the display cap). Without it the menu stays
    at 1016 px.
- `tests/menu_width_unit.cpp` (7/7 PASS):
  - Test 6: `SectionWidths`, `WideStreak` and the marks.
  - Test 7: both gates, the `SameLine` only after the combo, and cdef05cd's enablement gate kept.
- Also passing: `dmfg_menu_logic_unit`, `streamline_mfg_options_unit`, and the menu suites listed in 6.6.

## 8. In-Game Verification
- **Confirmed by the maintainer, 2026-11-11:** Metro 2033 Redux on Linux, `c1ed00b`, 2560×1440. The issue is fixed:
  the menu was drawn for about 4,950 frames, and the log has no "Menu wider than the display" report. Before the
  fix, the same setup logged it.
- **Confirmed by kilyan82, 2026-11-11:** Neverness to Everness on Windows, `c1ed00b`, 1920×1080, Menu Scale 1.0.
  "No more stretch": the menu is about 960 px wide and "Open Wiki" is visible (Smooth Motion and FG off at that
  moment; with NVSmooth30 active the menu doesn't open at all, section 6.13 of `FG-NVSMOOTH30-INTEGRATION.md`).
- **Maintainer checklist (Linux, Metro 2033 Redux, 2560×1440):**
  1. At Menu Scale 1.3 and 2.0 the menu should be narrower than the display, and the log should have no "Menu wider
     than the display" line.
  2. Open a wide section, e.g. "Advanced Settings", then close it: the menu should shrink back.
  3. Change Menu Scale up and back down: the width should come back each time.
  4. In a game with DLSS-G (Lost Soul Aside), the "Force Dynamic MFG" checkbox should still sit after the ratio
     combo.
- **kilyan82 (Windows, 1080p):**
  - The menu should no longer span the screen; at Menu Scale 0.5 it should be about half as wide.
  - If it's still wide, `OptiScaler.log` should contain the "Menu wider than the display" / "Menu left column" /
    "Menu right column" lines.
