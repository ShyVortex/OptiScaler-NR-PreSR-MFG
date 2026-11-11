#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <menu/MenuLayout.h>
#include <menu/MenuWidthProbe.h>
#include <menu/menu_viewport.h>

#ifndef MENU_HEADER_WIDTH_LEGACY
#include <menu/ScopedCollapsingHeader.h>
#else
// The helper before the fix (upstream bd2682a5): the full-width child reports its whole size to the column.
class ScopedCollapsingHeader
{
  public:
    explicit ScopedCollapsingHeader(const char* label, ImGuiTreeNodeFlags flags = 0)
    {
        ImGui::PushID(label);
        ImGui::BeginChild("##CollapsingHeaderChild", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        _headerOpen = ImGui::CollapsingHeader(label, flags);
    }
    bool IsHeaderOpen() const { return _headerOpen; }
    ~ScopedCollapsingHeader()
    {
        ImGui::EndChild();
        ImGui::PopID();
    }

  private:
    bool _headerOpen = false;
};
#endif

#include <cstdio>
#include <cstring>

// DLSS Unlocked #42 (kilyan82, 1920x1080, retest on 0ed77d0): the menu still spanned the display. Every collapsible
// section is a ScopedCollapsingHeader, a child window of width 0 (the column's whole width). EndChild reported that
// width as content, so each column measured exactly as wide as it was and the auto-resizing menu could never shrink
// back (FG-MENU-WIDTH-RATCHET.md, round 2).
//
// Built against the bundled ImGui: tests/run_menu_header_width.ps1 on Windows; on Linux, from the repository root:
//   I=OptiScaler/include/imgui; g++ -std=c++20 -I OptiScaler -I OptiScaler/include $(pkg-config --cflags freetype2)
//     tests/menu_header_width_unit.cpp $I/imgui.cpp $I/imgui_draw.cpp $I/imgui_tables.cpp $I/imgui_widgets.cpp
//     $I/misc/freetype/imgui_freetype.cpp $(pkg-config --libs freetype2) -o menu_header_width && ./menu_header_width
// A model of the main menu: MenuViewport::ConstrainWindow, an AlwaysAutoResize window, the two-column StretchSame
// table with sliders, text and sections in both columns, and the footer's right-aligned button (RightAlignedX).
//
// Validates:
// 1. Scale 1: a long line widens the menu to the display; once it is gone the menu returns to its natural width.
// 2. Scale 0.5: the same, and the natural width is about half of scale 1.
// 3. The section's child still spans the whole column (the bd2682a5 overlap fix is kept).
// 4. The footer button ends inside the window, also while the content is wider than the display (in 0ed77d0 it was
//    placed at the content's edge, off-screen).
// 5. Content inside an open section still widens the menu.
// 6. The width probe (MenuWidthProbe.h) leaves every width unchanged and names the section holding the long line.
// 7. The DMFG checkbox placed with SameLine after the FG selection table (games without DLSS-G before round 3) sits
//    past the column edge and widens the menu; without it the menu keeps its natural width.
// With -DMENU_HEADER_WIDTH_LEGACY (the old helper) tests 1, 2 and 5 fail, and test 6's width comparison with them.

static int failures = 0;
static bool g_probe = false;
static bool g_fgSelection = false;
static bool g_dmfgAfterTable = false;
static float g_lineWidth = 0.0f;

static void Check(bool passed, const char* name)
{
    std::printf("%s: %s\n", passed ? "PASS" : "FAIL", name);
    if (!passed)
        ++failures;
}

static float SectionWidth(const MenuLayout::SectionWidths& sections, const char* name)
{
    for (int i = 0; i < sections.count; ++i)
        if (std::strcmp(sections.names[i], name) == 0)
            return sections.widths[i];
    return -1.0f;
}

struct FrameResult
{
    float width = 0.0f;
    float sectionChildWidth = 0.0f;
    float sectionColumnWidth = 0.0f;
    float footerButtonRight = 0.0f;
};

static FrameResult Frame(float scale, bool longLine, bool openWideSection)
{
    auto& io = ImGui::GetIO();
    io.DisplaySize = { 1920.0f, 1080.0f };
    io.DeltaTime = 1.0f / 60.0f;
    MenuViewport::SetFramebufferScale(io.DisplaySize);
    ImGui::NewFrame();
    ImGui::PushFontSize(16.0f * scale);

    FrameResult result;
    MenuViewport::ConstrainWindow("menu");
    if (ImGui::Begin("menu", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        static float value = 0.4f;
        if (ImGui::BeginTable("main", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();
            if (g_probe)
                MenuWidthProbe::BeginMenuColumn(MenuWidthProbe::Columns().left);
            ImGui::Text("NVIDIA GeForce RTX 3070 Ti");
            ImGui::Text("D3D12 | DLSS 310.9.16 | Input: DLSS | Spoof: Off");
            {
                ScopedCollapsingHeader section("Advanced DLSS Settings");
            }
            {
                ScopedCollapsingHeader section("RTX 20 / 30 (SM75 / SM86) MFG Unlock");
            }
            if (g_probe)
                MenuWidthProbe::MarkMenuSection("Upscalers");
            if (longLine)
                ImGui::Text("NVSmooth30 Status: Pending Restart (Swapchain not bound) (I:\\SteamLibrary\\steamapps\\"
                            "common\\Wuthering Waves\\Client\\Binaries\\Win64\\OptiScaler\\nvsmooth30.dll)");
            if (longLine)
                g_lineWidth = ImGui::GetItemRectSize().x;
            if (g_probe)
                MenuWidthProbe::MarkMenuSection("Status");
            if (g_fgSelection)
            {
                // The FG Input / Output table, then (before the fix, in games without DLSS-G) the DMFG checkbox with
                // SameLine after it: the table's line ends at the column edge.
                ImGui::SeparatorText("Frame Generation");
                if (ImGui::BeginTable("fgSelection", 2, ImGuiTableFlags_SizingStretchSame))
                {
                    static int selected = 0;
                    ImGui::TableNextColumn();
                    ImGui::Combo("FG Input", &selected, "None\0Nukems\0");
                    ImGui::TableNextColumn();
                    ImGui::Combo("FG Output", &selected, "None\0XeFG\0");
                    ImGui::EndTable();
                }
                if (g_dmfgAfterTable)
                {
                    static bool dynamicMfg = false;
                    ImGui::SameLine(0.0f, 16.0f);
                    ImGui::Checkbox("Force Dynamic MFG", &dynamicMfg);
                    ImGui::SameLine();
                    ImGui::TextDisabled("(?)");
                }
                if (g_probe)
                    MenuWidthProbe::MarkMenuSection("FG: selection");
            }
            ImGui::SliderFloat("FPS Limit", &value, 0.0f, 1.0f);
            if (g_probe)
            {
                MenuWidthProbe::MarkMenuSection("Framerate");
                MenuWidthProbe::EndMenuColumn();
            }

            ImGui::TableNextColumn();
            if (g_probe)
                MenuWidthProbe::BeginMenuColumn(MenuWidthProbe::Columns().right);
            ImGui::SliderFloat("Sharpness", &value, 0.0f, 1.0f);
            ImGui::Text("Output Scaling is ENABLED, Target Res: 2880x1620 (3.00)");
            result.sectionColumnWidth = ImGui::GetContentRegionAvail().x;
            {
                ScopedCollapsingHeader section("Advanced Init Flags");
                result.sectionChildWidth = ImGui::GetWindowWidth();
            }
            {
                ScopedCollapsingHeader section("Advanced Settings",
                                               openWideSection ? ImGuiTreeNodeFlags_DefaultOpen : 0);
                if (openWideSection && section.IsHeaderOpen())
                    ImGui::Text("A wide option inside an open section that needs about one thousand pixels of room "
                                "to fit on one line, wider than the closed menu");
            }
            {
                ScopedCollapsingHeader section("Logging");
            }
            if (g_probe)
            {
                MenuWidthProbe::MarkMenuSection("Right sections");
                MenuWidthProbe::EndMenuColumn();
            }
            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::Button("Save Settings");
        ImGui::SameLine();
        const float buttonWidth = ImGui::CalcTextSize("Open Wiki").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        const auto cursor = ImGui::GetCursorScreenPos();
        const float contentRight = ImGui::GetCurrentWindow()->DC.IdealMaxPos.x;
        const float visibleRight = ImGui::GetCurrentWindow()->WorkRect.Max.x;
        ImGui::SetCursorScreenPos(
            { MenuLayout::RightAlignedX(cursor.x, buttonWidth, contentRight, visibleRight), cursor.y });
        ImGui::Button("Open Wiki");
        result.footerButtonRight = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
        result.width = ImGui::GetWindowWidth();
    }
    ImGui::End();
    ImGui::PopFontSize();
    ImGui::Render();
    return result;
}

static FrameResult Run(int frames, float scale, bool longLine, bool openWideSection = false)
{
    FrameResult result;
    for (int i = 0; i < frames; ++i)
        result = Frame(scale, longLine, openWideSection);
    return result;
}

int main()
{
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    // Test 1
    const auto natural = Run(30, 1.0f, false);
    const auto widened = Run(30, 1.0f, true);
    const auto after = Run(60, 1.0f, false);
    std::printf("scale 1: natural %.0f, long line %.0f, line removed %.0f\n", natural.width, widened.width,
                after.width);
    Check(widened.width > natural.width + 500.0f, "scale 1: the long line widens the menu");
    Check(after.width <= natural.width + 1.0f, "scale 1: the menu returns to its natural width");

    // Test 2
    const auto naturalHalf = Run(30, 0.5f, false);
    const auto widenedHalf = Run(30, 0.5f, true);
    const auto afterHalf = Run(60, 0.5f, false);
    std::printf("scale 0.5: natural %.0f, long line %.0f, line removed %.0f\n", naturalHalf.width, widenedHalf.width,
                afterHalf.width);
    Check(afterHalf.width <= naturalHalf.width + 1.0f, "scale 0.5: the menu returns to its natural width");
    Check(naturalHalf.width > natural.width * 0.35f && naturalHalf.width < natural.width * 0.65f,
          "scale 0.5: the natural width is about half of scale 1");

    // Test 3
    Check(widened.sectionChildWidth >= widened.sectionColumnWidth - 1.0f &&
              after.sectionChildWidth >= after.sectionColumnWidth - 1.0f,
          "the section's child spans the whole column");

    // Test 4
    Check(after.footerButtonRight <= after.width && widened.footerButtonRight <= widened.width,
          "the footer button ends inside the window");

    // Test 5
    const auto open = Run(30, 1.0f, false, true);
    const auto gone = Run(60, 1.0f, false, false);
    std::printf("open wide section %.0f, wide option gone %.0f\n", open.width, gone.width);
    Check(open.width > natural.width + 100.0f, "content inside an open section widens the menu");
    Check(gone.width <= natural.width + 1.0f, "the menu shrinks back once the wide option is gone");

    // Test 6: the probe runs on the same model. Widths are compared with tests 1 (same frames, probe off).
    {
        g_probe = true;
        const auto probedNatural = Run(30, 1.0f, false);
        const auto probedWidened = Run(30, 1.0f, true);
        const MenuLayout::SectionWidths left = MenuWidthProbe::Columns().left.sections;
        const MenuLayout::SectionWidths right = MenuWidthProbe::Columns().right.sections;
        const auto probedAfter = Run(60, 1.0f, false);
        g_probe = false;

        for (int i = 0; i < left.count; ++i)
            std::printf("probe left: %s %.0f px\n", left.names[i], left.widths[i]);
        for (int i = 0; i < right.count; ++i)
            std::printf("probe right: %s %.0f px\n", right.names[i], right.widths[i]);
        std::printf("long line %.0f px\n", g_lineWidth);

        const int widest = left.Widest();
        Check(probedNatural.width == natural.width && probedWidened.width == widened.width &&
                  probedAfter.width == after.width,
              "the probe leaves the menu width unchanged");
        Check(widest >= 0 && std::strcmp(left.names[widest], "Status") == 0 &&
                  left.widths[widest] >= g_lineWidth - 1.0f && left.widths[widest] <= g_lineWidth + 1.0f,
              "the probe names the section with the long line and measures it");
        Check(left.count == 3 && right.count == 1 && left.widths[0] > 0.0f && right.widths[0] > 0.0f,
              "both columns are measured");
    }

    // Test 7: the DMFG checkbox placed after the FG selection table (round 3).
    {
        g_fgSelection = true;
        g_probe = true;
        const auto withTable = Run(60, 1.0f, false);
        const float tableOnly = SectionWidth(MenuWidthProbe::Columns().left.sections, "FG: selection");
        g_dmfgAfterTable = true;
        const auto oldPlacement = Run(60, 1.0f, false);
        const auto& left = MenuWidthProbe::Columns().left;
        const float oldSection = SectionWidth(left.sections, "FG: selection");
        const float oldVisible = left.visibleWidth;
        g_dmfgAfterTable = false;
        const auto fixed = Run(60, 1.0f, false);
        g_probe = false;
        g_fgSelection = false;
        std::printf("FG selection: table only %.0f px, menu %.0f; checkbox after the table: section %.0f px in a "
                    "%.0f px column, menu %.0f; without it: menu %.0f\n",
                    tableOnly, withTable.width, oldSection, oldVisible, oldPlacement.width, fixed.width);
        Check(oldSection > oldVisible + 100.0f && oldPlacement.width > withTable.width + 500.0f,
              "the old placement puts the checkbox past the column edge and widens the menu");
        Check(tableOnly > 0.0f && tableOnly < oldVisible && fixed.width == withTable.width && fixed.width < 1300.0f,
              "without the checkbox after the table, the menu keeps its natural width");
    }

    ImGui::DestroyContext();
    if (failures == 0)
        std::puts("All 7 menu section width tests passed.");
    return failures == 0 ? 0 : 1;
}
