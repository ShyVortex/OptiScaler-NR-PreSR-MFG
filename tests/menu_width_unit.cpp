#include "../OptiScaler/menu/MenuLayout.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

// DLSS Unlocked #42 (kilyan82, 1920x1080): the main menu stayed full width even at Menu Scale 0.5. The window is
// auto-resizing; the footer's "Open Wiki" button was right-aligned to the window's current right edge
// (GetContentRegionAvail), so the content was always as wide as the window and the window could grow but never
// shrink (FG-MENU-WIDTH-RATCHET.md).
//
// Validates:
// 1. RightAlignedX ends the item at the content's right edge, or follows the cursor when that is further right.
// 2. Iterating the old and the new placement over frames: the old one keeps the window width; the new one lets it
//    fall back to the content width.
// 3. The footer uses RightAlignedX with the window's IdealMaxPos and no longer GetContentRegionAvail.
// 4. StatusWrapPosX wraps at StatusWrapWidth (scaled by Menu Scale) from the cursor, or earlier at the column edge.
// 5. RenderFrameGenerationSelection: no TextWrapped left (it wraps at the column edge); status lines use
//    StatusTextWrapped, the XeMFG and NVSmooth30 error lines are wrapped by ScopedStatusWrap, and the NVSmooth30 DLL
//    shows its file name with the full path in a tooltip.

static std::string ReadFile(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        std::fprintf(stderr, "cannot open %s (run from the repository root)\n", path);
        std::exit(1);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static std::string Squash(const std::string& text)
{
    std::string out;
    for (char c : text)
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t')
            out += c;
    return out;
}

int main()
{
    using MenuLayout::RightAlignedX;

    // Test 1
    static_assert(RightAlignedX(100.0f, 50.0f, 400.0f) == 350.0f);
    static_assert(RightAlignedX(380.0f, 50.0f, 400.0f) == 380.0f);
    static_assert(RightAlignedX(100.0f, 50.0f, 150.0f) == 100.0f);
    std::puts("Test 1 PASS: the item ends at the content's right edge, or follows the cursor");

    // Test 2: a window that was widened to 1900 by a line that is gone now; the rest of the content is 900 wide,
    // the footer's other items end at x = 300 and the button is 80 wide.
    {
        const float content = 900.0f;
        const float footerCursor = 300.0f;
        const float button = 80.0f;

        float oldWidth = 1900.0f;
        for (int frame = 0; frame < 10; frame++)
        {
            // Old: aligned to the window edge, so the button's right edge is the window width.
            const float buttonRight = footerCursor + (oldWidth - footerCursor - button) + button;
            oldWidth = content > buttonRight ? content : buttonRight;
        }
        assert(oldWidth == 1900.0f);

        float newWidth = 1900.0f;
        for (int frame = 0; frame < 10; frame++)
        {
            const float buttonRight = RightAlignedX(footerCursor, button, content) + button;
            newWidth = content > buttonRight ? content : buttonRight;
        }
        assert(newWidth == content);
    }
    std::puts("Test 2 PASS: the old placement keeps the window wide, the new one lets it shrink");

    // Test 3
    {
        const auto menu = Squash(ReadFile("OptiScaler/menu/menu_common.cpp"));
        const auto start = menu.find("voidMenuCommon::RenderMainMenuBottomBar(");
        assert(start != std::string::npos);
        const auto end = menu.find("ImGui::Button(\"OpenWiki\")", start);
        assert(end != std::string::npos);
        const auto footer = menu.substr(start, end - start);
        assert(footer.find("constfloatcontentRight=ImGui::GetCurrentWindow()->DC.IdealMaxPos.x;") != std::string::npos);
        assert(footer.find("ImGui::SetCursorScreenPos({MenuLayout::RightAlignedX(cursor.x,textSize.x,contentRight),"
                           "cursor.y});") != std::string::npos);
        assert(footer.find("GetContentRegionAvail") == std::string::npos);
    }
    std::puts("Test 3 PASS: the footer aligns to the content's ideal right edge");

    // Test 4
    {
        using MenuLayout::StatusWrapPosX;
        using MenuLayout::StatusWrapWidth;
        static_assert(StatusWrapWidth(1.0f) == 420.0f && StatusWrapWidth(0.5f) == 210.0f);
        static_assert(StatusWrapPosX(10.0f, 900.0f, 1.0f) == 430.0f); // wide column: bounded width
        static_assert(StatusWrapPosX(10.0f, 900.0f, 0.5f) == 220.0f); // halved with Menu Scale
        static_assert(StatusWrapPosX(10.0f, 300.0f, 1.0f) == 310.0f); // narrow column: its edge
        static_assert(StatusWrapPosX(10.0f, 0.0f, 1.0f) == 430.0f);   // no room reported: bounded width
        static_assert(StatusWrapPosX(10.0f, -5.0f, 1.0f) == 430.0f);
    }
    std::puts("Test 4 PASS: status lines wrap at a bounded, scaled width or the narrower column edge");

    // Test 5
    {
        const auto menu = Squash(ReadFile("OptiScaler/menu/menu_common.cpp"));
        const auto start = menu.find("voidMenuCommon::RenderFrameGenerationSelection(RenderMenuContext&ctx){");
        assert(start != std::string::npos);
        const auto end = menu.find("voidMenuCommon::", start + 10);
        assert(end != std::string::npos);
        const auto fg = menu.substr(start, end - start);
        assert(fg.find("ImGui::TextWrapped(") == std::string::npos);
        assert(fg.find("constfloatmenuScale=ctx.menuResScale;") != std::string::npos);
        assert(fg.find("StatusTextWrapped(menuScale,") != std::string::npos);
        assert(fg.find("ScopedStatusWrapwrap{menuScale};ImGui::TextColored(toneMapColor(ImVec4(1.f,0.4f,0.4f,1.f)),"
                       "\"Error:%s\",status.ErrorMessage.c_str());") != std::string::npos);
        assert(fg.find("ScopedStatusWrapwrap{menuScale};ImGui::TextColored(ImVec4(0.9f,0.2f,0.2f,1.0f),\"%s\","
                       "nvSmoothStatus.ErrorMessage.c_str());") != std::string::npos);
        assert(fg.find("ImGui::TextDisabled(\"(%s)\",wstring_to_string(std::filesystem::path(nvSmoothStatus."
                       "LoadedDllPath).filename().wstring()).c_str());if(ImGui::IsItemHovered())ImGui::SetTooltip("
                       "\"%s\",wstring_to_string(nvSmoothStatus.LoadedDllPath).c_str());") != std::string::npos);

        const auto helper = menu.find("voidStatusTextWrapped(floatmenuScale,constchar*fmt,...){"
                                      "ScopedStatusWrapwrap{menuScale};");
        assert(helper != std::string::npos && helper < start);
        assert(menu.find("ImGui::PushTextWrapPos(MenuLayout::StatusWrapPosX(ImGui::GetCursorPosX(),"
                         "ImGui::GetContentRegionAvail().x,menuScale));") != std::string::npos);
    }
    std::puts("Test 5 PASS: frame generation status lines wrap at the bounded width; DLL path in a tooltip");

    std::puts("All 5 menu width tests passed.");
    return 0;
}
