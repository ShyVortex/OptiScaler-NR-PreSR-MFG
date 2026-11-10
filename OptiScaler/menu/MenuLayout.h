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
constexpr float RightAlignedX(float cursorX, float itemWidth, float contentRightX)
{
    const float aligned = contentRightX - itemWidth;
    return aligned > cursorX ? aligned : cursorX;
}
} // namespace MenuLayout
