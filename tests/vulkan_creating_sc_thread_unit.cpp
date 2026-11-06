#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

// vulkanCreatingSC per thread (FG-LINUX-VK-OVERLAY-RESIZE-FREEZE.md).
//
// OptiScaler's Vulkan hooks set State::vulkanCreatingSC around vkQueuePresentKHR and vkCreateSwapchainKHR, so a DXGI
// swapchain created inside them is not wrapped. The flag was process-wide. On Linux DXVK presents from its own thread,
// so while it was inside the present hook the game's thread created its swapchain after F11, saw the flag and got an
// unwrapped swapchain: no Dx11wDx12 bridge, no XeFG, no borderless quirk (Metro 2033 Redux, v0.9.87-pre5).
//
// 1. Process-wide flag: the game's thread sees the present thread's flag (the bug).
// 2. Per-thread flag: it does not; the present thread still sees its own.
// 3. Same-thread nesting (the flag's purpose) still works, and nested scopes restore the previous value.
// 4. Production: State.h declares the flag static thread_local; the hooks still set it through the scope guard.

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

static bool g_sharedFlag = false;
static thread_local bool t_threadFlag = false;

template <bool* Flag> struct SharedScope
{
    bool previous;
    SharedScope() : previous(*Flag) { *Flag = true; }
    ~SharedScope() { *Flag = previous; }
};

struct ThreadScope
{
    bool previous;
    ThreadScope() : previous(t_threadFlag) { t_threadFlag = true; }
    ~ThreadScope() { t_threadFlag = previous; }
};

// The present thread holds the scope while the game's thread checks the flag in its CreateSwapChain hook.
template <typename Scope, typename Read> static bool GameThreadSeesFlag(Read read)
{
    std::atomic<int> stage { 0 };
    bool seen = false;

    std::thread present(
        [&]
        {
            Scope scope;
            stage = 1;
            while (stage.load() != 2)
                std::this_thread::yield();
        });

    while (stage.load() != 1)
        std::this_thread::yield();

    std::thread game([&] { seen = read(); });
    game.join();

    stage = 2;
    present.join();
    return seen;
}

int main()
{
    std::printf("vulkanCreatingSC per-thread tests\n");

    // Test 1
    {
        assert(GameThreadSeesFlag<SharedScope<&g_sharedFlag>>([] { return g_sharedFlag; }));
        assert(!g_sharedFlag);
        std::printf("  [PASS] Test 1: a process-wide flag leaks into the game's thread (the F11 bug).\n");
    }

    // Test 2
    {
        assert(!GameThreadSeesFlag<ThreadScope>([] { return t_threadFlag; }));

        bool presentSaw = false;
        std::thread present(
            [&]
            {
                ThreadScope scope;
                presentSaw = t_threadFlag;
            });
        present.join();
        assert(presentSaw);
        std::printf("  [PASS] Test 2: per thread, the game's swapchain is no longer mistaken for an internal one.\n");
    }

    // Test 3
    {
        assert(!t_threadFlag);
        {
            ThreadScope outer;
            assert(t_threadFlag); // a DXGI swapchain created inside the Vulkan hook is still recognised
            {
                ThreadScope inner;
                assert(t_threadFlag);
            }
            assert(t_threadFlag);
        }
        assert(!t_threadFlag);
        std::printf("  [PASS] Test 3: same-thread nesting works and restores the previous value.\n");
    }

    // Test 4
    {
        const std::string state = ReadFile("OptiScaler/State.h");
        assert(state.find("static inline thread_local bool vulkanCreatingSC = false;") != std::string::npos);
        assert(state.find("    bool vulkanCreatingSC = false;") == std::string::npos);
        assert(state.find("previousState = State::Instance().vulkanCreatingSC;") != std::string::npos);
        assert(state.find("~ScopedVulkanCreatingSC() { State::Instance().vulkanCreatingSC = previousState; }") !=
               std::string::npos);

        const std::string vulkan = ReadFile("OptiScaler/hooks/Vulkan_Hooks.cpp");
        size_t scopes = 0;
        for (size_t pos = vulkan.find("ScopedVulkanCreatingSC scopedVulkanCreatingSC {};"); pos != std::string::npos;
             pos = vulkan.find("ScopedVulkanCreatingSC scopedVulkanCreatingSC {};", pos + 1))
            scopes++;
        assert(scopes == 2);
        std::printf("  [PASS] Test 4: State.h declares the flag per thread; both Vulkan hooks still set it.\n");
    }

    std::printf("All vulkanCreatingSC per-thread tests passed.\n");
    return 0;
}
