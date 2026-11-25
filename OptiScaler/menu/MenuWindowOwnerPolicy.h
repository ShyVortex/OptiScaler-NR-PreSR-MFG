#pragma once

// Which window's swapchain the DX overlay menu draws on. Kept free of Windows headers so unit tests can include it.
// See docs/issues/FG/FG-MENU-SECONDARY-WINDOW-WNDPROC-RECURSION.md (DLSS Unlocked #72).
//
// The menu and OptiInput support one window at a time. MSFS 2024 presents a second swapchain on another window for a
// popped-out cockpit panel. Following every present's HWND re-initialised the menu and the input subclass on almost
// every frame, from two threads, until the WndProc chain looped (stack overflow in USER32).

namespace MenuWindowOwnerPolicy
{
// An owner that has not presented for this long has been replaced (launcher or splash -> game window, recreated
// main window), so another window may take the menu over.
constexpr double kOwnerStaleMs = 1000.0;

enum class Decision
{
    Draw,  // the present belongs to the owner window
    Adopt, // no usable owner: the presenting window becomes the owner
    Skip,  // another window while the owner is alive and presenting: no menu, no init, no input change
};

template <typename Handle>
constexpr Decision Decide(Handle owner, Handle candidate, bool ownerIsWindow, double msSinceOwnerPresent)
{
    if (candidate == Handle {})
        return Decision::Skip;

    if (owner == Handle {})
        return Decision::Adopt;

    if (candidate == owner)
        return Decision::Draw;

    if (!ownerIsWindow || msSinceOwnerPresent >= kOwnerStaleMs)
        return Decision::Adopt;

    return Decision::Skip;
}
} // namespace MenuWindowOwnerPolicy
