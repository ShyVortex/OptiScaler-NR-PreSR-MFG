#include "../OptiScaler/framegen/smoothmotion/NvPresentAttach.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// DLSS Unlocked #42: in johnCXVII's v0.9.31 logs NvPresent loaded all its Smooth Motion CUDA kernels, yet OptiScaler
// logged "Swapchain attachment verified = false" and the menu kept "Pending Restart". The check ran once in the
// swapchain wrapper's constructor (before NvPresent attaches during the first presents) and looked at less than
// NVSmooth30 v0.1.0 does (FG-NVSMOOTH30-INTEGRATION.md).
//
// Validates:
// 1. The probe finds NvPresent on the swapchain's own vtable through any of slots 0/7/8/22.
// 2. The probe finds a wrapper at +0x18 and at other offsets up to 0x80, and reports the offset.
// 3. The probe rejects: no NvPresent pointers, a self-pointer, null, and unreadable memory (no read past it).
// 4. The probe schedule: every present up to 120, then every 120th up to 3600; the window closes at 3600.
// 5. The wrapper no longer probes in its constructor; it starts a per-swapchain probe and probes from Present and
//    Present1 only.
// 6. Loader state is per swapchain: a stale swapchain can't set or close the current one's state.
// 7. The menu shows "Pending Restart" only when nvsmooth30.dll was loaded after the swapchain, and a heuristic
//    "Not detected" (with how to verify and report) after the probe window closes.

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

static std::string Between(const std::string& text, const std::string& from, const std::string& to)
{
    const auto start = text.find(from);
    assert(start != std::string::npos);
    const auto end = text.find(to, start + from.size());
    assert(end != std::string::npos);
    return text.substr(start, end - start);
}

namespace
{
// A fake NvPresent image: any address inside it counts as NvPresent code.
unsigned char g_nvp[4096];
unsigned char g_other[4096];

struct Region
{
    const void* start;
    size_t size;
};

std::vector<Region> g_readable;

bool Readable(const void* address, size_t size)
{
    const auto a = reinterpret_cast<uintptr_t>(address);
    for (const auto& r : g_readable)
    {
        const auto s = reinterpret_cast<uintptr_t>(r.start);
        if (a >= s && a + size <= s + r.size)
            return true;
    }
    return false;
}

bool InNvp(const void* address)
{
    const auto a = reinterpret_cast<uintptr_t>(address);
    const auto s = reinterpret_cast<uintptr_t>(g_nvp);
    return a >= s && a < s + sizeof(g_nvp);
}

struct Vtable
{
    void* slots[32];

    explicit Vtable(bool nvpAt = false, int slot = -1)
    {
        for (int i = 0; i < 32; i++)
            slots[i] = g_other + i;
        if (nvpAt)
            slots[slot] = g_nvp + slot;
    }
};

// A fake COM object: the first pointer is its vtable, the rest are fields.
struct Object
{
    void* fields[32] {};
};

NvPresentAttach::Result Probe(const void* object) { return NvPresentAttach::Probe(object, Readable, InNvp); }
} // namespace

int main()
{
    // Test 1
    for (int slot : { 0, 7, 8, 22 })
    {
        Vtable table(true, slot);
        Object swapchain;
        swapchain.fields[0] = &table;
        g_readable = { { &swapchain, sizeof(swapchain) }, { &table, sizeof(table) } };
        const auto result = Probe(&swapchain);
        assert(result.attached && result.offset == 0);
    }
    {
        Vtable table(true, 5); // not an identity or Present slot
        Object swapchain;
        swapchain.fields[0] = &table;
        g_readable = { { &swapchain, sizeof(swapchain) }, { &table, sizeof(table) } };
        assert(!Probe(&swapchain).attached);
    }
    std::puts("Test 1 PASS: NvPresent on the swapchain's own vtable (slots 0/7/8/22)");

    // Test 2
    for (size_t offset : { size_t(0x18), size_t(0x8), size_t(0x40), size_t(0x80) })
    {
        Vtable plain;
        Vtable nvpTable(true, 8);
        Object wrapper;
        wrapper.fields[0] = &nvpTable;
        Object swapchain;
        swapchain.fields[0] = &plain;
        swapchain.fields[offset / sizeof(void*)] = &wrapper;
        g_readable = { { &swapchain, sizeof(swapchain) },
                       { &plain, sizeof(plain) },
                       { &wrapper, sizeof(wrapper) },
                       { &nvpTable, sizeof(nvpTable) } };
        const auto result = Probe(&swapchain);
        assert(result.attached && result.offset == offset);
    }
    {
        // Past 0x80 is out of range, as in NVSmooth30.
        Vtable plain;
        Vtable nvpTable(true, 8);
        Object wrapper;
        wrapper.fields[0] = &nvpTable;
        Object swapchain;
        swapchain.fields[0] = &plain;
        swapchain.fields[0x88 / sizeof(void*)] = &wrapper;
        g_readable = { { &swapchain, sizeof(swapchain) },
                       { &plain, sizeof(plain) },
                       { &wrapper, sizeof(wrapper) },
                       { &nvpTable, sizeof(nvpTable) } };
        assert(!Probe(&swapchain).attached);
    }
    std::puts("Test 2 PASS: a wrapper at +0x18 or any offset up to 0x80, with the offset reported");

    // Test 3
    {
        Vtable plain;
        Object swapchain;
        swapchain.fields[0] = &plain;
        swapchain.fields[3] = &swapchain; // self-pointer at +0x18
        g_readable = { { &swapchain, sizeof(swapchain) }, { &plain, sizeof(plain) } };
        assert(!Probe(&swapchain).attached);
        assert(!Probe(nullptr).attached);

        // Only the first 0x20 bytes are readable: the scan must stop there, even with a valid wrapper further on.
        Vtable nvpTable(true, 8);
        Object wrapper;
        wrapper.fields[0] = &nvpTable;
        swapchain.fields[3] = nullptr;
        swapchain.fields[8] = &wrapper; // +0x40
        g_readable = { { &swapchain, 0x20 },
                       { &plain, sizeof(plain) },
                       { &wrapper, sizeof(wrapper) },
                       { &nvpTable, sizeof(nvpTable) } };
        assert(!Probe(&swapchain).attached);

        // The candidate's vtable is unreadable.
        swapchain.fields[3] = &wrapper;
        g_readable = { { &swapchain, sizeof(swapchain) }, { &plain, sizeof(plain) }, { &wrapper, sizeof(wrapper) } };
        assert(!Probe(&swapchain).attached);

        // The swapchain itself is unreadable.
        g_readable = {};
        assert(!Probe(&swapchain).attached);
    }
    std::puts("Test 3 PASS: rejects no NvPresent, self-pointers, null and unreadable memory");

    // Test 4
    using namespace NvPresentAttach;
    static_assert(!ShouldProbe(0));
    static_assert(ShouldProbe(1) && ShouldProbe(60) && ShouldProbe(120));
    static_assert(!ShouldProbe(121) && !ShouldProbe(239) && ShouldProbe(240));
    static_assert(ShouldProbe(3600) && !ShouldProbe(3601) && !ShouldProbe(3720));
    static_assert(!WindowClosed(3599) && WindowClosed(3600));
    std::puts("Test 4 PASS: probe every present up to 120, then every 120th up to 3600");

    // Test 5
    {
        const auto source = ReadFile("OptiScaler/wrapped/wrapped_swapchain.cpp");
        const auto squashed = Squash(source);
        const auto ctor = Between(source, "WrappedIDXGISwapChain4::WrappedIDXGISwapChain4(IDXGISwapChain* real",
                                  "WrappedIDXGISwapChain4::~WrappedIDXGISwapChain4()");
        assert(ctor.find("NvPresent64.dll") == std::string::npos);
        assert(ctor.find("attachment verified") == std::string::npos);
        assert(Squash(ctor).find("NVSmooth30Loader::BeginSwapchain(_id,NVSmooth30Loader::LastStatus().DllLoaded);") !=
               std::string::npos);

        const auto probe = Squash(Between(source, "void WrappedIDXGISwapChain4::ProbeNvPresent()",
                                          "WrappedIDXGISwapChain4::WrappedIDXGISwapChain4("));
        assert(probe.find("NvPresentAttach::ShouldProbe(present)") != std::string::npos);
        assert(probe.find("NvPresentAttach::Probe(_real,readable,inNvPresent)") != std::string::npos);
        assert(probe.find("VirtualQuery(") != std::string::npos);
        assert(probe.find("NVSmooth30Loader::SetSwapchainAttached(_id)") != std::string::npos);
        assert(probe.find("NVSmooth30Loader::SetProbeDone(_id)") != std::string::npos);
        assert(probe.find("status.SwapchainId!=_id") != std::string::npos);

        assert(squashed.find("result=LocalPresent(_real,SyncInterval,Flags,nullptr,_device,_handle,_uwp);"
                             "ProbeNvPresent();") != std::string::npos);
        assert(squashed.find("result=LocalPresent(_real1,SyncInterval,Flags,pPresentParameters,_device,_handle,_uwp);"
                             "ProbeNvPresent();") != std::string::npos);
    }
    std::puts("Test 5 PASS: no constructor probe; per-swapchain probe from Present and Present1");

    // Test 6
    {
        const auto loader = Squash(ReadFile("OptiScaler/framegen/smoothmotion/NVSmooth30Loader.cpp"));
        assert(loader.find("voidBeginSwapchain(intswapchainId,boolloadedBefore){std::lock_guardlock(s_mutex);"
                           "s_status.SwapchainId=swapchainId;s_status.SwapchainAttached=false;"
                           "s_status.ProbeDone=false;s_status.LoadedBeforeSwapchain=loadedBefore;}") !=
               std::string::npos);
        assert(loader.find("voidSetSwapchainAttached(intswapchainId){std::lock_guardlock(s_mutex);"
                           "if(s_status.SwapchainId==swapchainId)s_status.SwapchainAttached=true;}") !=
               std::string::npos);
        assert(loader.find("voidSetProbeDone(intswapchainId){std::lock_guardlock(s_mutex);"
                           "if(s_status.SwapchainId==swapchainId)s_status.ProbeDone=true;}") != std::string::npos);
    }
    std::puts("Test 6 PASS: loader state is per swapchain");

    // Test 7
    {
        const auto menu = Squash(ReadFile("OptiScaler/menu/menu_common.cpp"));
        assert(menu.find("elseif(nvSmoothStatus.DllLoaded&&!nvSmoothStatus.LoadedBeforeSwapchain){ImGui::TextColored("
                         "ImVec4(0.9f,0.7f,0.2f,1.0f),\"[PendingRestart:Activeonnextgamelaunch]\");}") !=
               std::string::npos);
        assert(menu.find("elseif(nvSmoothStatus.DllLoaded&&!nvSmoothStatus.ProbeDone){ImGui::TextDisabled(\"["
                         "CheckingSmoothMotion...]\");}") != std::string::npos);
        assert(menu.find("\"[SmoothMotionNotDetected]\"") != std::string::npos);
        assert(menu.find("elseif(!nvSmoothStatus.LoadedBeforeSwapchain){ImGui::TextColored(ImVec4(0.9f,0.7f,0.2f,1.0f),"
                         "\"PendingRestart(loadedaftertheswapchain)\");}") != std::string::npos);
        assert(menu.find("\"Notdetectedontheswapchain\"") != std::string::npos);
        assert(menu.find("Thedetectionisaheuristic") != std::string::npos);
        assert(menu.find("OptiScaler.logandnvsmooth30.log") != std::string::npos);
        assert(menu.find("PendingRestart(Swapchainnotbound)") == std::string::npos);
    }
    std::puts("Test 7 PASS: \"Pending Restart\" only when loaded after the swapchain; heuristic \"Not detected\"");

    std::puts("All 7 NVSmooth30 attachment probe tests passed.");
    return 0;
}
