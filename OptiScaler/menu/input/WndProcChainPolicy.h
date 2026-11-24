#pragma once

// Pure decisions behind OptiInput's window subclass, kept free of Windows headers so unit tests can include them.
// See docs/issues/FG/FG-MENU-SECONDARY-WINDOW-WNDPROC-RECURSION.md (DLSS Unlocked #72).
//
// When another component installs its WndProc on top of OptiInputWndProc, that component calls OptiInputWndProc as
// its previous procedure. OptiInputWndProc stays in that window's chain even after OptiInput moves to another window,
// so it must keep forwarding to the procedure it wrapped for that window ("lost chain"). Installing OptiInput again
// on such a window would store the top procedure as our original and loop: Opti -> other -> Opti -> ...

#include <cstddef>

namespace WndProcChainPolicy
{
template <typename Hwnd, typename Proc> struct LostChain
{
    Hwnd hwnd {};
    Proc original {};
};

// The procedure a message for `hwnd` is forwarded to: the input window's original, or the lost chain's original for
// that window. Never another window's procedure; an unknown window gets the default (Proc {} -> DefWindowProc).
template <typename Hwnd, typename Proc>
constexpr Proc ForwardTarget(Hwnd hwnd, Hwnd inputHwnd, Proc inputOriginal, const LostChain<Hwnd, Proc>* lost,
                             std::size_t lostCount)
{
    if (hwnd != Hwnd {} && hwnd == inputHwnd && inputOriginal != Proc {})
        return inputOriginal;

    for (std::size_t i = 0; i < lostCount; i++)
    {
        if (lost[i].hwnd == hwnd)
            return lost[i].original;
    }

    return Proc {};
}

enum class InstallAction
{
    AlreadyOurs,    // OptiInputWndProc is the window's current procedure
    ReuseLostChain, // OptiInputWndProc is still inside the window's chain: take the stored original back, no install
    Install,        // safe to replace the window's procedure
};

// `hasLostChain` / `lostOriginal`: the window's lost-chain entry, if any. When the window's procedure is back at the
// original we wrapped, the component that sat on top has removed itself and OptiInput is no longer in the chain.
template <typename Proc>
constexpr InstallAction DecideInstall(Proc current, Proc opti, bool hasLostChain, Proc lostOriginal)
{
    if (current == opti)
        return InstallAction::AlreadyOurs;

    if (hasLostChain && current != lostOriginal)
        return InstallAction::ReuseLostChain;

    return InstallAction::Install;
}

} // namespace WndProcChainPolicy
