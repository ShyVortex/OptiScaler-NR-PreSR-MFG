#pragma once

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <menu/MenuLayout.h>

// Menu width diagnostic (DLSS Unlocked #42, FG-MENU-WIDTH-RATCHET.md round 3). The main table is SizingStretchSame
// without Resizable, so ImGui sizes it as twice its widest column: one section wider than its column widens the whole
// menu. Each section's own extent is measured by resetting the column's CursorMaxPos to the column start before it and
// restoring the maximum after, so the layout itself is unchanged.
namespace MenuWidthProbe
{
struct ColumnWidthProbe
{
    float start = 0.0f;
    float visibleWidth = 0.0f;
    float maxRight = 0.0f;
    MenuLayout::SectionWidths sections;
};

struct MenuColumns
{
    ColumnWidthProbe left;
    ColumnWidthProbe right;
    ColumnWidthProbe* active = nullptr;
};

inline MenuColumns& Columns()
{
    static MenuColumns columns;
    return columns;
}

// The right edge the current window's content asked for so far (what auto-resize uses).
inline float MenuContentRight()
{
    const auto* window = ImGui::GetCurrentWindow();
    return ImMax(window->DC.CursorMaxPos.x, window->DC.IdealMaxPos.x);
}

// Call right after TableNextColumn.
inline void BeginMenuColumn(ColumnWidthProbe& column)
{
    auto* window = ImGui::GetCurrentWindow();
    column.start = window->DC.CursorPos.x;
    column.visibleWidth = window->WorkRect.Max.x - column.start;
    column.maxRight = window->DC.CursorMaxPos.x;
    column.sections.Clear();
    window->DC.CursorMaxPos.x = column.start;
    Columns().active = &column;
}

// Records how far the content since the previous mark reached. Only at the top level of a column's code: never inside
// a ScopedCollapsingHeader or a nested table.
inline void MarkMenuSection(const char* section)
{
    auto* column = Columns().active;
    if (column == nullptr)
        return;

    auto* window = ImGui::GetCurrentWindow();
    const float right = window->DC.CursorMaxPos.x;
    column->sections.Add(section, right - column->start);
    column->maxRight = ImMax(column->maxRight, right);
    window->DC.CursorMaxPos.x = column->start;
}

// Call before the next TableNextColumn / EndTable: restores the column's measured width.
inline void EndMenuColumn()
{
    auto* column = Columns().active;
    if (column == nullptr)
        return;

    auto* window = ImGui::GetCurrentWindow();
    window->DC.CursorMaxPos.x = ImMax(window->DC.CursorMaxPos.x, column->maxRight);
    Columns().active = nullptr;
}
} // namespace MenuWidthProbe
