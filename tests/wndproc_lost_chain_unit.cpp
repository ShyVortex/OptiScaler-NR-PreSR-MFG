#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../OptiScaler/menu/input/WndProcChainPolicy.h"

// OptiInput lost WndProc chains (FG-MENU-SECONDARY-WINDOW-WNDPROC-RECURSION.md, DLSS Unlocked #72).
//
// MSFS 2024: OptiInput subclassed the sim window (original A); another component then put its WndProc B on top
// (B -> Opti -> A). When the menu moved to the pop-out window, RemoveWindowSubclass dropped A, and OptiInputWndProc
// forwarded every window's messages to the single current original. Moving back, InstallWindowSubclass stored B as
// the original: Opti -> B -> Opti -> ... until the stack overflowed in USER32.
//
// 1. ForwardTarget: by window, never another window's original; unknown windows get the default.
// 2. DecideInstall: a window whose chain still calls Opti is never subclassed again.
// 3. Replay of #72 on a model chain with the policy: no procedure is re-entered; each window reaches its own original.
//    The same replay with the old logic loops (the test tells the two apart).
// 4. A lost chain resolves when OptiInput heads the chain again or the window is back at its original.
// 5. Production wiring in input_system_window.cpp / input_system_messages.cpp / input_system_internal.h.

using namespace WndProcChainPolicy;
using Hwnd = std::uintptr_t;
using Proc = int; // 0 = none (DefWindowProc)

static constexpr Hwnd kSim = 0x1E0C8C;
static constexpr Hwnd kPopOut = 0x4F0968;
static constexpr Proc kSimOriginal = 1;
static constexpr Proc kPopOutOriginal = 2;
static constexpr Proc kOther = 3; // the procedure installed on top of OptiInput's
static constexpr Proc kOpti = 4;

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

static void TestForwardTarget()
{
    const LostChain<Hwnd, Proc> lost[] = { { kSim, kSimOriginal } };

    assert((ForwardTarget<Hwnd, Proc>(kPopOut, kPopOut, kPopOutOriginal, lost, 1) == kPopOutOriginal));
    assert((ForwardTarget<Hwnd, Proc>(kSim, kPopOut, kPopOutOriginal, lost, 1) == kSimOriginal));
    assert((ForwardTarget<Hwnd, Proc>(0x1234, kPopOut, kPopOutOriginal, lost, 1) == 0));
    assert((ForwardTarget<Hwnd, Proc>(kSim, kPopOut, kPopOutOriginal, nullptr, 0) == 0));
    assert((ForwardTarget<Hwnd, Proc>(kSim, kSim, 0, lost, 1) == kSimOriginal));
    std::puts("PASS 1: ForwardTarget picks the window's own original, never another window's");
}

static void TestDecideInstall()
{
    assert(DecideInstall<Proc>(kOpti, kOpti, false, 0) == InstallAction::AlreadyOurs);
    assert(DecideInstall<Proc>(kOther, kOpti, true, kSimOriginal) == InstallAction::ReuseLostChain);
    assert(DecideInstall<Proc>(kSimOriginal, kOpti, true, kSimOriginal) == InstallAction::Install);
    assert(DecideInstall<Proc>(kSimOriginal, kOpti, false, 0) == InstallAction::Install);
    std::puts("PASS 2: a window whose chain still calls OptiInput is never subclassed again");
}

// Model of the subclass logic in input_system_window.cpp (policy = true) or the logic before the fix (false), plus
// a WndProc chain: kOther forwards to the procedure it replaced, kOpti forwards as OptiInputWndProc does.
struct Model
{
    bool policy = true;

    std::map<Hwnd, Proc> windowProc;
    std::map<Hwnd, Proc> otherPrevious;

    Hwnd input = 0;
    Proc original = 0;
    bool subclassed = false;
    std::vector<LostChain<Hwnd, Proc>> lost;

    LostChain<Hwnd, Proc>* FindLost(Hwnd h)
    {
        for (auto& e : lost)
            if (e.hwnd == h)
                return &e;
        return nullptr;
    }

    void Remember(Hwnd h, Proc p)
    {
        if (!policy || h == 0 || p == 0)
            return;
        if (auto* e = FindLost(h))
            e->original = p;
        else
            lost.push_back({ h, p });
    }

    void Install(Hwnd h)
    {
        const Proc current = windowProc[h];
        auto* entry = policy ? FindLost(h) : nullptr;
        const auto action = DecideInstall<Proc>(current, kOpti, entry != nullptr, entry ? entry->original : 0);

        if (action == InstallAction::AlreadyOurs)
        {
            if (entry != nullptr)
            {
                original = entry->original;
                std::erase_if(lost, [h](const auto& e) { return e.hwnd == h; });
            }

            subclassed = original != 0;
            return;
        }

        if (action == InstallAction::ReuseLostChain)
        {
            original = entry->original;
            subclassed = false;
            std::erase_if(lost, [h](const auto& e) { return e.hwnd == h; });
            return;
        }

        std::erase_if(lost, [h](const auto& e) { return e.hwnd == h; });
        original = current;
        windowProc[h] = kOpti;
        subclassed = true;
    }

    void Remove()
    {
        if (!subclassed || input == 0 || original == 0)
        {
            if (!subclassed && input != 0 && original != 0)
                Remember(input, original);
        }
        else if (windowProc[input] == kOpti)
        {
            windowProc[input] = original;
        }
        else
        {
            Remember(input, original);
        }

        subclassed = false;
        original = 0;
    }

    void SetInput(Hwnd h)
    {
        if (input == h)
            return;
        Remove();
        input = h;
        Install(h);
    }

    // ValidateWindowSubclassLocked: notices the subclass was lost and keeps the original.
    void Validate()
    {
        if (subclassed && windowProc[input] != kOpti)
            subclassed = false;
    }

    void InstallOther(Hwnd h)
    {
        otherPrevious[h] = windowProc[h];
        windowProc[h] = kOther;
    }

    Proc OptiForward(Hwnd h)
    {
        if (!policy)
            return original; // old OptiInputWndProc: the single current original, whatever the window
        return ForwardTarget<Hwnd, Proc>(h, input, original, lost.data(), lost.size());
    }

    // Returns the terminal procedure that handled the message, or -1 if the chain re-entered a procedure.
    Proc Dispatch(Hwnd h)
    {
        std::vector<Proc> path;
        Proc proc = windowProc[h];

        while (true)
        {
            for (Proc seen : path)
                if (seen == proc)
                    return -1;
            path.push_back(proc);

            if (proc == kOther)
                proc = otherPrevious[h];
            else if (proc == kOpti)
                proc = OptiForward(h);
            else
                return proc; // a game procedure or DefWindowProc (0)
        }
    }
};

static bool ReplayIssue72(Model& m)
{
    m.windowProc = { { kSim, kSimOriginal }, { kPopOut, kPopOutOriginal } };

    m.SetInput(kSim);     // 15:07:10
    m.InstallOther(kSim); // 15:12:08.418
    m.Validate();

    for (int round = 0; round < 3; round++)
    {
        m.SetInput(kPopOut); // menu follows the pop-out swapchain
        if (m.Dispatch(kSim) != kSimOriginal || m.Dispatch(kPopOut) != kPopOutOriginal)
            return false;

        m.SetInput(kSim); // and back
        if (m.Dispatch(kSim) != kSimOriginal || m.Dispatch(kPopOut) != kPopOutOriginal)
            return false;
    }

    return true;
}

static void TestIssue72Replay()
{
    Model fixed;
    assert(ReplayIssue72(fixed));
    assert(fixed.windowProc[kSim] == kOther); // never subclassed over the other procedure
    assert(fixed.original == kSimOriginal);   // and still forwarding to the sim window's original
    assert(fixed.windowProc[kPopOut] == kPopOutOriginal);

    Model old;
    old.policy = false;
    assert(!ReplayIssue72(old));

    Model oldLoop;
    oldLoop.policy = false;
    oldLoop.windowProc = { { kSim, kSimOriginal }, { kPopOut, kPopOutOriginal } };
    oldLoop.SetInput(kSim);
    oldLoop.InstallOther(kSim);
    oldLoop.Validate();
    oldLoop.SetInput(kPopOut);
    oldLoop.SetInput(kSim);
    assert(oldLoop.original == kOther && oldLoop.Dispatch(kSim) == -1); // the #72 stack overflow
    std::puts("PASS 3: #72 replay never re-enters a procedure; the old logic loops on the same sequence");
}

static void TestLostChainResolves()
{
    Model m;
    m.windowProc = { { kSim, kSimOriginal }, { kPopOut, kPopOutOriginal } };
    m.SetInput(kSim);
    m.InstallOther(kSim);
    m.Validate();
    m.SetInput(kPopOut);
    assert(m.FindLost(kSim) != nullptr);

    // The other component removes itself, restoring the original it found... which was OptiInput's.
    m.windowProc[kSim] = kOpti;
    assert(m.Dispatch(kSim) == kSimOriginal);
    m.SetInput(kSim);
    assert(m.FindLost(kSim) == nullptr && m.subclassed && m.original == kSimOriginal);
    assert(m.Dispatch(kSim) == kSimOriginal);
    m.SetInput(kPopOut); // restores the sim window properly this time
    assert(m.windowProc[kSim] == kSimOriginal && m.FindLost(kSim) == nullptr);

    // ... or the window is reset to its original: OptiInput is out of the chain and may subclass normally.
    m.windowProc[kSim] = kSimOriginal;
    m.SetInput(kSim);
    assert(m.FindLost(kSim) == nullptr && m.subclassed && m.windowProc[kSim] == kOpti);
    assert(m.Dispatch(kSim) == kSimOriginal);
    std::puts("PASS 4: a lost chain resolves once the window's procedure is back at the original");
}

static void TestProductionWiring()
{
    const std::string window = ReadFile("OptiScaler/menu/input/input_system_window.cpp");
    const std::string messages = ReadFile("OptiScaler/menu/input/input_system_messages.cpp");
    const std::string internal = ReadFile("OptiScaler/menu/input/input_system_internal.h");

    assert(internal.find("#include \"WndProcChainPolicy.h\"") != std::string::npos);
    assert(internal.find("std::vector<WndProcChainPolicy::LostChain<HWND, WNDPROC>> LostWndProcChains;") !=
           std::string::npos);

    const std::string remember =
        Between(window, "static void RememberLostWndProcChainLocked(HWND hwnd, WNDPROC original)", "\n}\n");
    assert(remember.find("!IsWindow(hwnd)") != std::string::npos);

    const std::string install = Between(window, "bool InstallWindowSubclass(HWND hwnd)", "\n}\n");
    const size_t decide = install.find("WndProcChainPolicy::DecideInstall<WNDPROC>(");
    assert(decide != std::string::npos);
    assert(decide < install.find("SetWindowLongPtrW(hwnd, GWLP_WNDPROC"));
    const std::string ours = Between(install, "case WndProcChainPolicy::InstallAction::AlreadyOurs:",
                                     "case WndProcChainPolicy::InstallAction::ReuseLostChain:");
    assert(ours.find("_state.OriginalWndProc = lost->original;") != std::string::npos);
    const std::string reuse = Between(install, "case WndProcChainPolicy::InstallAction::ReuseLostChain:",
                                      "case WndProcChainPolicy::InstallAction::Install:");
    assert(reuse.find("_state.OriginalWndProc = lost->original;") != std::string::npos);
    assert(reuse.find("return true;") != std::string::npos);
    assert(reuse.find("SetWindowLongPtrW") == std::string::npos);

    const std::string remove = Between(window, "bool RemoveWindowSubclass(bool preserveLostChain)", "\n}\n");
    size_t count = 0;
    for (size_t at = remove.find("RememberLostWndProcChainLocked(_state.InputHwnd, _state.OriginalWndProc);");
         at != std::string::npos;
         at = remove.find("RememberLostWndProcChainLocked(_state.InputHwnd, _state.OriginalWndProc);", at + 1))
        count++;
    assert(count == 2);

    const std::string wndProc = Between(messages, "LRESULT CALLBACK OptiInputWndProc(", "\n}\n");
    assert(wndProc.find("originalWndProc = _state.OriginalWndProc;") == std::string::npos);
    size_t forwards = 0;
    for (size_t at = wndProc.find("originalWndProc = ForwardWndProcLocked(hwnd);"); at != std::string::npos;
         at = wndProc.find("originalWndProc = ForwardWndProcLocked(hwnd);", at + 1))
        forwards++;
    assert(forwards == 2);
    const std::string ncDestroy = Between(wndProc, "if (msg == WM_NCDESTROY)", "return result;");
    assert(ncDestroy.find("CallWindowProcW(originalWndProc") < ncDestroy.find("ForgetLostWndProcChainLocked(hwnd);"));
    std::puts("PASS 5: production wiring forwards by window and keeps lost chains");
}

int main()
{
    TestForwardTarget();
    TestDecideInstall();
    TestIssue72Replay();
    TestLostChainResolves();
    TestProductionWiring();
    std::puts("wndproc_lost_chain_unit: all tests passed");
    return 0;
}
