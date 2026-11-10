#pragma once

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

// A collapsible menu section. The header and its contents sit in a child window as wide as the column, so the
// header bar spans the column without overlapping the next one (upstream bd2682a5).
//
// A child of width 0 reports the whole column width as its size, so every section held its column at its current
// width and the auto-resizing menu could never shrink back once something widened it (DLSS Unlocked #42,
// FG-MENU-WIDTH-RATCHET.md). After EndChild the parent is told only what the child's content reached, the same
// used-vs-ideal split ImGui's EndTable does.
class ScopedCollapsingHeader
{
  public:
    explicit ScopedCollapsingHeader(const char* label, ImGuiTreeNodeFlags flags = 0)
    {
        ImGui::PushID(label);

        _parentMaxX = ImGui::GetCurrentWindow()->DC.CursorMaxPos.x;
        ImGui::BeginChild("##CollapsingHeaderChild", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        _headerOpen = ImGui::CollapsingHeader(label, flags);
        _active = true;
    }

    bool IsHeaderOpen() const { return _headerOpen; }

    ~ScopedCollapsingHeader()
    {
        if (_active)
        {
            const ImGuiWindow* child = ImGui::GetCurrentWindow();
            const float contentRight =
                ImMax(child->DC.CursorMaxPos.x, child->DC.IdealMaxPos.x) + child->WindowPadding.x;

            ImGui::EndChild();

            ImGuiWindow* parent = ImGui::GetCurrentWindow();
            parent->DC.CursorMaxPos.x = ImMax(_parentMaxX, contentRight);

            ImGui::PopID();
        }
    }

    ScopedCollapsingHeader(const ScopedCollapsingHeader&) = delete;
    ScopedCollapsingHeader& operator=(const ScopedCollapsingHeader&) = delete;

  private:
    bool _active = false;
    bool _headerOpen = false;
    float _parentMaxX = 0.0f;
};
