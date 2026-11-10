#pragma once

// Layout helpers for the auto-resizing main menu (ImGuiWindowFlags_AlwaysAutoResize).
//
// The window's width follows its content. Anything placed at the window's *current* right edge (for example
// right-aligned with GetContentRegionAvail) makes the content exactly as wide as the window, so the window can grow
// but never shrink back: once a long line widened it, it stayed full width even at Menu Scale 0.5
// (DLSS Unlocked #42, FG-MENU-WIDTH-RATCHET.md).
namespace MenuLayout
{
// X for an item of itemWidth that should end at the content's own right edge (contentRightX: the widest point the
// rest of the content reached, not the window edge). If the cursor is already past that point, the item simply
// follows the cursor, so it never pushes the content further right than it would go anyway.
// visibleRightX is the window's visible right edge: when the content is wider than the window (the window is clamped
// to the display), the item ends there instead of off-screen. That never holds the width: it only applies while the
// content is already wider than the window.
constexpr float RightAlignedX(float cursorX, float itemWidth, float contentRightX, float visibleRightX)
{
    const float right = contentRightX < visibleRightX ? contentRightX : visibleRightX;
    const float aligned = right - itemWidth;
    return aligned > cursorX ? aligned : cursorX;
}

// Width at which status and error lines wrap, scaled with Menu Scale. Long unwrapped lines (a full DLL path, a loader
// error naming the GPU) widened their table column, and the two-column table then widened both columns. Wrap at a
// fixed width, never at the window edge: that would follow the window and keep it wide.
constexpr float StatusWrapWidth(float menuScale) { return 420.0f * menuScale; }

// Local X at which a section's text wraps: StatusWrapWidth from the section's start, but never past the column's
// current width (avail), so text can't run into the next column when the column is narrower.
constexpr float StatusWrapPosX(float cursorX, float avail, float menuScale)
{
    const float width = StatusWrapWidth(menuScale);
    return cursorX + (avail > 0.0f && avail < width ? avail : width);
}

// Diagnostic: remembers which menu section pushed the content furthest right, i.e. the widest one. Note() is called
// after each section with the content's right edge so far; the last section that moved it is the widest.
struct WidestSection
{
    const char* name = nullptr;
    float right = 0.0f;

    constexpr void Reset(float startRight)
    {
        name = nullptr;
        right = startRight;
    }

    constexpr void Note(const char* section, float contentRight)
    {
        if (contentRight > right + 0.5f)
        {
            name = section;
            right = contentRight;
        }
    }
};

// True when the menu's content asks for more width than the display can give it (the window is then clamped).
constexpr bool ExceedsWidth(float contentWidth, float availableWidth) { return contentWidth > availableWidth + 0.5f; }
} // namespace MenuLayout
