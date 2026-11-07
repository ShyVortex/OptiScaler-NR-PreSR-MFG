#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "../OptiScaler/with_dx12/Dx11wDx12PresentPolicy.h"

// Hidden D3D11 present of the Dx11wDx12 bridge under DXVK (FG-LINUX-VK-OVERLAY-RESIZE-FREEZE.md, section 4.2).
//
// Dx11wDx12SC::Present presented the game's hidden D3D11 swapchain and XeFG's D3D12 swapchain to the same window every
// frame. Under DXVK / vkd3d-proton those are two Vulkan swapchains on one window; after F11 both went out of date and
// vkd3d-proton's present never returned (Metro 2033 Redux on Linux, v0.9.87-pre6). The hidden present is skipped
// under DXVK, which is safe only because the game never depends on it while XeFG's swapchain exists.
//
// 1. Policy: present the hidden swapchain only without DXVK.
// 2. Dx11wDx12SC::Present uses the policy, logs the skip once, and always presents XeFG's swapchain.
// 3. DXGI_PRESENT_TEST still goes to the D3D11 swapchain, before any of this.
// 4. Everything the game can wait on or read back goes to XeFG's swapchain first.
// 5. The bridge copies the D3D11 buffer at the reported index on every present, so a non-advancing index still
//    delivers the frame the game rendered.

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

static std::string Method(const std::string& source, const std::string& name)
{
    return Between(source, "Dx11wDx12SC::" + name + "(", "\n}\n");
}

int main()
{
    std::printf("Dx11wDx12 hidden present tests\n");

    const std::string sc = ReadFile("OptiScaler/with_dx12/dx11_with_dx12_sc.cpp");

    // Test 1
    {
        static_assert(Dx11wDx12PresentPolicy::PresentHiddenDx11(false));
        static_assert(!Dx11wDx12PresentPolicy::PresentHiddenDx11(true));
        std::printf("  [PASS] Test 1: the hidden D3D11 swapchain is presented only without DXVK.\n");
    }

    // Test 2
    {
        const std::string present = Between(sc, "HRESULT STDMETHODCALLTYPE Dx11wDx12SC::Present(UINT", "\n}\n");

        const size_t gate =
            present.find("Dx11wDx12PresentPolicy::PresentHiddenDx11(IdentifyGpu::getPrimaryGpu().usesDxvk)");
        const size_t hidden = present.find("_real->Present(0, realFlags)");
        const size_t fg = present.find("auto result = _fgSwapChain->Present(SyncInterval, Flags);");
        assert(gate != std::string::npos && hidden != std::string::npos && fg != std::string::npos);
        assert(gate < hidden && hidden < fg);

        // The skip is logged once, and the FG present is outside the hidden-present branch.
        assert(present.find("_hiddenPresentSkipLogged = true;") != std::string::npos);
        assert(present.find("not presenting the hidden DX11 swapchain") != std::string::npos);

        const std::string header = ReadFile("OptiScaler/with_dx12/dx11_with_dx12_sc.h");
        assert(header.find("bool _hiddenPresentSkipLogged = false;") != std::string::npos);
        std::printf("  [PASS] Test 2: Present skips the hidden present under DXVK and always presents FG's.\n");
    }

    // Test 3
    {
        const std::string present = Between(sc, "HRESULT STDMETHODCALLTYPE Dx11wDx12SC::Present(UINT", "\n}\n");
        const size_t test =
            present.find("if ((Flags & DXGI_PRESENT_TEST) != 0)\n        return _real->Present(SyncInterval, Flags);");
        assert(test != std::string::npos);
        assert(test < present.find("Dx11wDx12PresentPolicy::PresentHiddenDx11("));
        std::printf("  [PASS] Test 3: DXGI_PRESENT_TEST is still answered by the D3D11 swapchain.\n");
    }

    // Test 4
    {
        const char* forwarded[] = { "GetFrameLatencyWaitableObject",
                                    "SetMaximumFrameLatency",
                                    "GetMaximumFrameLatency",
                                    "GetFrameStatistics",
                                    "GetLastPresentCount",
                                    "SetFullscreenState",
                                    "GetFullscreenState",
                                    "GetFullscreenDesc",
                                    "ResizeTarget",
                                    "GetContainingOutput" };

        for (const char* name : forwarded)
        {
            const std::string body = Method(sc, name);
            const size_t fgCall = body.find(std::string("_fgSwapChain->") + name + "(");
            assert(fgCall != std::string::npos);

            const size_t realCall = body.find("_real");
            assert(realCall == std::string::npos || fgCall < realCall);
        }

        std::printf("  [PASS] Test 4: frame-pacing, statistics and fullscreen queries go to XeFG's swapchain first.\n");
    }

    // Test 5
    {
        const std::string present = Between(sc, "HRESULT STDMETHODCALLTYPE Dx11wDx12SC::Present(UINT", "\n}\n");
        const size_t index = present.find("auto dx11Index = _GetDx11BackBufferIndexForPresent();");
        const size_t copy = present.find("_CopyDx11BackBufferToShared(dx11Index)");
        const size_t fg = present.find("_fgSwapChain->Present(SyncInterval, Flags)");
        assert(index != std::string::npos && copy != std::string::npos);
        assert(index < copy && copy < fg);

        const std::string indexFn = Between(sc, "UINT Dx11wDx12SC::_GetDx11BackBufferIndexForPresent()", "\n}\n");
        assert(indexFn.find("_real3->GetCurrentBackBufferIndex()") != std::string::npos);
        std::printf("  [PASS] Test 5: the bridge copies the buffer at the reported index before every FG present.\n");
    }

    std::printf("All Dx11wDx12 hidden present tests passed.\n");
    return 0;
}
