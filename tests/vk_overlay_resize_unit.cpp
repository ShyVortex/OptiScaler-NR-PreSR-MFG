#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "../OptiScaler/menu/MenuOverlayVkPolicy.h"

// Vulkan menu overlay after a window resize (FG-LINUX-VK-OVERLAY-RESIZE-FREEZE.md).
//
// MenuOverlayVk rendered the menu with the window's size (ImGui_ImplWin32_NewFrame sets io.DisplaySize every frame)
// onto framebuffers that keep the swapchain size of the last vkCreateSwapchainKHR. F11 / alt-tab on Linux resizes the
// window long before DXVK / vkd3d-proton recreate the swapchain, so the menu drew past the image: NVIDIA Xid 13
// "3D HEIGHT CT Violation", device lost, frozen game (Metro 2033 Redux, XeMFG). It also drew on whatever swapchain was
// presented, with any image index.
//
// 1. ImageCountSupported: 1..8 images (the fixed image array).
// 2. NeedsReallocation: only when a swapchain has more images than allocated.
// 3. ShouldDraw: only the overlay's swapchain, a valid image index, never without a swapchain.
// 4. Resize model: the old render size exceeds the image after F11; the new one never does, for window sizes larger
//    and smaller than the image, including ImGui's Vulkan backend float maths.
// 5. Production wiring in menu_overlay_vk.cpp.

using namespace MenuOverlayVkPolicy;

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

// MenuViewport::SetFramebufferScale followed by ImGui_ImplVulkan_RenderDrawData's fb_width / fb_height.
static void BackendTarget(float windowW, float windowH, float framebufferW, float framebufferH, int& fbW, int& fbH)
{
    float scaleX = 1.0f;
    float scaleY = 1.0f;

    if (framebufferW > 0.0f && framebufferH > 0.0f && windowW > 0.0f && windowH > 0.0f)
    {
        scaleX = framebufferW / windowW;
        scaleY = framebufferH / windowH;
    }

    fbW = static_cast<int>(windowW * scaleX);
    fbH = static_cast<int>(windowH * scaleY);
}

struct FakeSwapchain
{
    int id;
};

int main()
{
    std::printf("Vulkan menu overlay resize tests\n");

    // Test 1
    {
        static_assert(!ImageCountSupported(0));
        static_assert(ImageCountSupported(1));
        static_assert(ImageCountSupported(3));
        static_assert(ImageCountSupported(MaxImages));
        static_assert(!ImageCountSupported(MaxImages + 1));
        std::printf("  [PASS] Test 1: 1..8 swapchain images supported, more would overflow the image array.\n");
    }

    // Test 2
    {
        static_assert(NeedsReallocation(0, 3));
        static_assert(!NeedsReallocation(3, 3));
        static_assert(!NeedsReallocation(4, 2));
        static_assert(NeedsReallocation(3, 4));
        std::printf("  [PASS] Test 2: per-image arrays grow only when a swapchain has more images.\n");
    }

    // Test 3
    {
        // Non-dispatchable handles are 64-bit values or pointers depending on the platform.
        constexpr uint64_t ours = 0x1B1BAB0, other = 0x1A8C150;
        static_assert(ShouldDraw<uint64_t>(ours, ours, 0, 3));
        static_assert(ShouldDraw<uint64_t>(ours, ours, 2, 3));
        static_assert(!ShouldDraw<uint64_t>(ours, ours, 3, 3));  // index past the framebuffers
        static_assert(!ShouldDraw<uint64_t>(other, ours, 0, 3)); // DXVK's swapchain while ours is vkd3d-proton's
        static_assert(!ShouldDraw<uint64_t>(0, 0, 0, 3));        // destroyed / never created

        FakeSwapchain a { 1 }, b { 2 };
        assert(ShouldDraw<FakeSwapchain*>(&a, &a, 1, 2));
        assert(!ShouldDraw<FakeSwapchain*>(&b, &a, 1, 2));
        assert(!ShouldDraw<FakeSwapchain*>(&a, nullptr, 1, 2));
        std::printf("  [PASS] Test 3: draws only on the overlay's swapchain with a covered image index.\n");
    }

    // Test 4
    {
        // F11 in Metro 2033 Redux: the window grows to 2560x1440, the swapchain is still 1280x720 (recreated 132 s
        // later). Before: render area = window size, past the image.
        const Extent image { 1280, 720 };
        const float windowW = 2560.0f, windowH = 1440.0f;
        assert(static_cast<uint32_t>(windowH) > image.height); // the old renderArea height: Xid 13

        // After: render area and the backend's target are the image size.
        const Extent area = RenderExtent(image);
        assert(area.width == image.width && area.height == image.height);

        int fbW = 0, fbH = 0;
        BackendTarget(windowW, windowH, static_cast<float>(area.width), static_cast<float>(area.height), fbW, fbH);
        assert(fbW <= static_cast<int>(image.width) && fbH <= static_cast<int>(image.height));
        assert(fbW >= static_cast<int>(image.width) - 1 && fbH >= static_cast<int>(image.height) - 1);

        // Every window size from tiny to 4K, for several image sizes: the target never exceeds the image.
        const Extent images[] = { { 1280, 720 }, { 1920, 1080 }, { 2560, 1440 }, { 1366, 768 }, { 3840, 2160 } };
        for (const Extent& img : images)
        {
            for (int w = 1; w <= 3840; w += 37)
            {
                for (int h = 1; h <= 2160; h += 29)
                {
                    BackendTarget(static_cast<float>(w), static_cast<float>(h), static_cast<float>(img.width),
                                  static_cast<float>(img.height), fbW, fbH);
                    assert(fbW <= static_cast<int>(img.width) && fbH <= static_cast<int>(img.height));
                }
            }
        }

        // Window and image the same size (normal case): unchanged, scale 1.
        BackendTarget(1920.0f, 1080.0f, 1920.0f, 1080.0f, fbW, fbH);
        assert(fbW == 1920 && fbH == 1080);
        std::printf("  [PASS] Test 4: the menu never draws past the swapchain image, whatever the window size.\n");
    }

    // Test 5
    {
        const std::string vk = ReadFile("OptiScaler/menu/menu_overlay_vk.cpp");

        const std::string create = Between(vk, "static void CreateVulkanObjects(", "\n}\n");
        assert(create.find("MenuOverlayVkPolicy::ImageCountSupported(_scImageCount)") != std::string::npos);
        assert(create.find("VkImage images[MenuOverlayVkPolicy::MaxImages];") != std::string::npos);
        assert(create.find("MenuOverlayVkPolicy::NeedsReallocation(_allocatedImageCount, _scImageCount)") !=
               std::string::npos);
        assert(create.find("_vkSwapchain = *pSwapchain;") != std::string::npos);
        assert(create.find("_vkExtent = pCreateInfo->imageExtent;") != std::string::npos);

        const std::string destroy = Between(vk, "void MenuOverlayVk::DestroyVulkanObjects(", "\n}\n");
        assert(destroy.find("_vkSwapchain = VK_NULL_HANDLE;") != std::string::npos);

        const std::string present = Between(vk, "bool MenuOverlayVk::QueuePresent(", "\n}\n");
        const size_t gate = present.find("MenuOverlayVkPolicy::ShouldDraw(pPresentInfo->pSwapchains[0], _vkSwapchain");
        const size_t render = present.find("MenuOverlayBase::RenderMenu(ImVec2(");
        assert(gate != std::string::npos && render != std::string::npos && gate < render);
        assert(present.find("info.renderArea.extent.width = extent.width;") != std::string::npos);
        assert(present.find("info.renderArea.extent.height = extent.height;") != std::string::npos);
        assert(present.find("DisplaySize") == std::string::npos);
        assert(present.find("&_ImVulkan_Frames[imageIndex]") != std::string::npos);
        std::printf("  [PASS] Test 5: menu_overlay_vk.cpp uses the policy in create, destroy and present.\n");
    }

    std::printf("All Vulkan menu overlay resize tests passed.\n");
    return 0;
}
