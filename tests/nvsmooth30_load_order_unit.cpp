#include "../OptiScaler/framegen/smoothmotion/NVSmooth30Loader.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

// DLSS Unlocked #42 (kilyan82, v0.9.86, OptiScaler as winmm.dll): getGpuInfo loaded nvsmooth30.dll about 1.5-2 s before
// OptiScaler hooked the DXGI factories. Its 16x16 dummy swapchain was not rejected, NVSmooth30 patched the shared
// Present slots, and its hook recursed with the Steam overlay's until the stack overflowed
// (FG-NVSMOOTH30-INTEGRATION.md 6.9).
//
// Validates:
// 1. DecideLoadGate: load only once the DXGI factory hooks are installed.
// 2. TrySetup checks the gate after the platform/GPU guards and before resolving or loading the DLL, and records the
//    wait (status flag + pending flag).
// 3. RunPendingSetup claims the pending flag before calling TrySetup (no re-entry from a factory created meanwhile).
// 4. All three CreateDXGIFactory* hooks run the pending setup, and FactoryHooksInstalled requires all three.
// 5. DxgiHooks::Hook keeps the factory hooks whenever Smooth Motion with NVSmooth30 is requested.
// 6. The menu shows the wait.

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

static size_t At(const std::string& text, const std::string& needle)
{
    const auto at = text.find(needle);
    assert(at != std::string::npos);
    return at;
}

int main()
{
    using namespace NVSmooth30Loader;

    // Test 1
    static_assert(DecideLoadGate(true) == LoadGate::Load);
    static_assert(DecideLoadGate(false) == LoadGate::WaitForDxgiHooks);
    std::puts("Test 1 PASS: nvsmooth30.dll loads only after the DXGI factory hooks");

    const auto loader = Squash(ReadFile("OptiScaler/framegen/smoothmotion/NVSmooth30Loader.cpp"));
    const auto trySetup = Between(loader, "voidTrySetup(){", "voidSanitizeEnvironment()");

    // Test 2
    {
        const auto gate =
            At(trySetup, "if(DecideLoadGate(DxgiHooks::FactoryHooksInstalled())==LoadGate::WaitForDxgiHooks)");
        assert(At(trySetup, "if(!isAmpere)") < gate);
        assert(gate < At(trySetup, "ResolveCandidatePath("));
        assert(gate < At(trySetup, "LoadLibraryExW_Ldr("));
        assert(gate < At(trySetup, "ApplySmoothMotionDrs(true)"));
        assert(trySetup.find("s_status.WaitingForDxgiHooks=true;return;") != std::string::npos);
        assert(trySetup.find("s_pendingDxgiHooks.exchange(true)") != std::string::npos);
    }
    std::puts("Test 2 PASS: TrySetup waits for the hooks before resolving or loading the DLL");

    // Test 3
    {
        const auto run = Between(loader, "voidRunPendingSetup(){", "voidSanitizeEnvironment()");
        assert(At(run, "if(!s_pendingDxgiHooks.exchange(false))return;") < At(run, "TrySetup();"));
        assert(run.find("DxgiHooks::FactoryHooksInstalled()") != std::string::npos);
    }
    std::puts("Test 3 PASS: RunPendingSetup claims the pending setup before running it");

    const auto dxgi = Squash(ReadFile("OptiScaler/hooks/Dxgi_Hooks.cpp"));

    // Test 4
    for (const char* hook :
         { "inlinestaticHRESULThkCreateDXGIFactory(REFIIDriid", "inlinestaticHRESULThkCreateDXGIFactory1(REFIIDriid",
           "inlinestaticHRESULThkCreateDXGIFactory2(UINTFlags" })
    {
        const auto start = At(dxgi, hook);
        const auto body = dxgi.substr(start, 400);
        assert(body.find("NVSmooth30Loader::RunPendingSetup();") != std::string::npos);
    }
    assert(dxgi.find("returno_CreateDXGIFactory!=nullptr&&o_CreateDXGIFactory1!=nullptr&&o_CreateDXGIFactory2!="
                     "nullptr;") != std::string::npos);
    std::puts("Test 4 PASS: every CreateDXGIFactory* hook runs the pending setup");

    // Test 5
    {
        const auto hook = Between(dxgi, "voidDxgiHooks::Hook(){", "boolDxgiHooks::FactoryHooksInstalled()");
        assert(hook.find("constboolnvSmooth30=Config::Instance()->FGDLSSGSmoothMotion.value_or_default()&&Config::"
                         "Instance()->SmoothMotionNVSmooth30.value_or_default();") != std::string::npos);
        assert(hook.find("if(!nvSmooth30&&!Config::Instance()->OverlayMenu.value_or_default()&&") != std::string::npos);
    }
    std::puts("Test 5 PASS: DxgiHooks::Hook keeps the factory hooks for NVSmooth30");

    // Test 6
    {
        const auto menu = Squash(ReadFile("OptiScaler/menu/menu_common.cpp"));
        assert(menu.find("elseif(nvSmoothStatus.WaitingForDxgiHooks){ImGui::TextDisabled(\"WaitingforOptiScaler'"
                         "sDXGIhooks\");}") != std::string::npos);
    }
    std::puts("Test 6 PASS: the menu shows the wait");

    std::puts("All 6 NVSmooth30 load-order tests passed.");
    return 0;
}
