#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "../OptiScaler/menu/MenuWindowOwnerPolicy.h"

// Overlay menu window owner (FG-MENU-SECONDARY-WINDOW-WNDPROC-RECURSION.md, DLSS Unlocked #72).
//
// MSFS 2024 presents a second swapchain on another window for a popped-out cockpit panel. MenuOverlayDx::Present
// followed every present's HWND, so the menu and OptiInput moved between the two windows on almost every frame, from
// two threads, until OptiInput's WndProc chain looped (stack overflow in USER32).
//
// 1. No owner -> Adopt.
// 2. Owner alive and presenting, other window -> Skip (the pop-out).
// 3. Owner window destroyed -> Adopt.
// 4. Owner silent for kOwnerStaleMs -> Adopt; just under -> Skip.
// 5. Owner's own present -> Draw; null HWND -> Skip.
// 6. Replay of the #72 present sequence: the owner never leaves the sim window.
// 7. Production wiring in menu_overlay_dx.cpp.

using namespace MenuWindowOwnerPolicy;
using Hwnd = std::uintptr_t;

static constexpr Hwnd kSim = 0x1E0C8C;
static constexpr Hwnd kPopOut = 0x4F0968;

static std::string ReadFile(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        std::fprintf(stderr, "cannot open %s\n", path);
        std::exit(1);
    }

    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static std::string Between(const std::string& text, const std::string& from, const std::string& to)
{
    const size_t start = text.find(from);
    assert(start != std::string::npos);
    const size_t end = text.find(to, start + from.size());
    assert(end != std::string::npos);
    return text.substr(start, end - start);
}

static void TestNoOwnerAdopts()
{
    assert(Decide<Hwnd>(0, kSim, false, 0.0) == Decision::Adopt);
    std::puts("PASS 1: no owner adopts the presenting window");
}

static void TestPopOutSkipped()
{
    assert(Decide<Hwnd>(kSim, kPopOut, true, 0.0) == Decision::Skip);
    assert(Decide<Hwnd>(kSim, kPopOut, true, 16.7) == Decision::Skip);
    std::puts("PASS 2: another window is skipped while the owner is alive and presenting");
}

static void TestDestroyedOwnerReplaced()
{
    assert(Decide<Hwnd>(kSim, kPopOut, false, 0.0) == Decision::Adopt);
    std::puts("PASS 3: a destroyed owner window is replaced");
}

static void TestSilentOwnerReplaced()
{
    assert(Decide<Hwnd>(kSim, kPopOut, true, kOwnerStaleMs) == Decision::Adopt);
    assert(Decide<Hwnd>(kSim, kPopOut, true, kOwnerStaleMs + 500.0) == Decision::Adopt);
    assert(Decide<Hwnd>(kSim, kPopOut, true, kOwnerStaleMs - 1.0) == Decision::Skip);
    std::puts("PASS 4: an owner silent for kOwnerStaleMs is replaced, not before");
}

static void TestOwnerDrawsAndNullSkipped()
{
    assert(Decide<Hwnd>(kSim, kSim, true, 5000.0) == Decision::Draw);
    assert(Decide<Hwnd>(kSim, 0, true, 0.0) == Decision::Skip);
    assert(Decide<Hwnd>(0, 0, false, 0.0) == Decision::Skip);
    std::puts("PASS 5: the owner draws; a null HWND never takes the menu");
}

// The same bookkeeping as ClaimMenuWindow in menu_overlay_dx.cpp, without the mutex and logging.
struct OwnerModel
{
    Hwnd owner = 0;
    double ownerPresentMs = 0.0;
    int ownerChanges = 0;

    bool Present(Hwnd hwnd, double nowMs, bool ownerIsWindow = true)
    {
        const auto decision = Decide(owner, hwnd, owner != 0 && ownerIsWindow, nowMs - ownerPresentMs);
        if (decision == Decision::Skip)
            return false;

        if (decision == Decision::Adopt && owner != hwnd)
        {
            owner = hwnd;
            ownerChanges++;
        }

        ownerPresentMs = nowMs;
        return true;
    }
};

static void TestIssue72Replay()
{
    OwnerModel model;
    double now = 0.0;

    // 15:07:10 .. 15:12:08: the sim window presents alone.
    for (int i = 0; i < 600; i++, now += 8.0)
        assert(model.Present(kSim, now));

    // 15:12:08.69: the pop-out swapchain appears; both windows present, in any interleaving.
    const Hwnd order[] = { kPopOut, kSim, kPopOut, kPopOut, kSim, kSim, kPopOut, kSim };
    for (int round = 0; round < 200; round++)
    {
        for (Hwnd hwnd : order)
        {
            const bool drawn = model.Present(hwnd, now);
            assert(drawn == (hwnd == kSim));
            now += 2.0;
        }
    }

    assert(model.owner == kSim);
    assert(model.ownerChanges == 1);

    // The sim window stops presenting (e.g. the game recreates it): the pop-out takes the menu after the timeout.
    now += kOwnerStaleMs;
    assert(model.Present(kPopOut, now));
    assert(model.owner == kPopOut && model.ownerChanges == 2);
    std::puts("PASS 6: #72 replay keeps the menu on the sim window; a real hand-over still works");
}

static void TestProductionWiring()
{
    const std::string src = ReadFile("OptiScaler/menu/menu_overlay_dx.cpp");
    assert(src.find("#include \"MenuWindowOwnerPolicy.h\"") != std::string::npos);

    const std::string claim = Between(src, "static bool ClaimMenuWindow(HWND hWnd)", "void MenuOverlayDx::Present(");
    assert(claim.find("std::scoped_lock lock(ownerMutex);") != std::string::npos);
    assert(claim.find("MenuWindowOwnerPolicy::Decide(owner, hWnd, ownerIsWindow, now - ownerPresentMs)") !=
           std::string::npos);
    assert(claim.find("IsWindow(owner)") != std::string::npos);
    assert(claim.find("ownerPresentMs = now;") != std::string::npos);

    const std::string present = Between(src, "void MenuOverlayDx::Present(", "void MenuOverlayDx::ApplyThemeStyle()");
    const size_t claimAt = present.find("if (!ClaimMenuWindow(hWnd))\n        return;");
    assert(claimAt != std::string::npos);
    assert(claimAt < present.find("QueryInterface"));
    assert(claimAt < present.find("MenuOverlayBase::Shutdown();"));
    assert(claimAt < present.find("MenuOverlayBase::Init(hWnd, isUWP);"));
    assert(claimAt < present.find("RenderImGui_DX12(pSwapChain);"));
    std::puts("PASS 7: MenuOverlayDx::Present claims the window before capture, Init, Shutdown and render");
}

int main()
{
    TestNoOwnerAdopts();
    TestPopOutSkipped();
    TestDestroyedOwnerReplaced();
    TestSilentOwnerReplaced();
    TestOwnerDrawsAndNullSkipped();
    TestIssue72Replay();
    TestProductionWiring();
    std::puts("menu_window_owner_unit: all tests passed");
    return 0;
}
