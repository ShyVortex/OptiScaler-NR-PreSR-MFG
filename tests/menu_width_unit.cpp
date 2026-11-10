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

    std::puts("All 3 menu width tests passed.");
    return 0;
}
