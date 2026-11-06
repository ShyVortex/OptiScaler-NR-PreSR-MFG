#pragma once

// Pure decisions behind MenuOverlayVk, kept free of Vulkan and Windows headers so unit tests can include them.
// See docs/issues/FG/FG-LINUX-VK-OVERLAY-RESIZE-FREEZE.md.

#include <cstdint>

namespace MenuOverlayVkPolicy
{
// CreateVulkanObjects reads the swapchain images into a fixed array of this size.
constexpr uint32_t MaxImages = 8;

constexpr bool ImageCountSupported(uint32_t imageCount) { return imageCount > 0 && imageCount <= MaxImages; }

// The per-image frame and semaphore arrays were allocated once, for the first swapchain's image count. A later
// swapchain with more images must get larger arrays.
constexpr bool NeedsReallocation(uint32_t allocatedImages, uint32_t imageCount) { return imageCount > allocatedImages; }

// Draw only on the swapchain the overlay built its framebuffers for, with an image index it has a framebuffer for.
// With the Dx11wDx12 bridge on Linux there are swapchains on two devices (DXVK and vkd3d-proton); a present on another
// one must not use these framebuffers, this queue or this image index.
template <typename Handle>
constexpr bool ShouldDraw(Handle presentedSwapchain, Handle overlaySwapchain, uint32_t imageIndex, uint32_t imageCount)
{
    return overlaySwapchain != Handle {} && presentedSwapchain == overlaySwapchain && imageIndex < imageCount;
}

struct Extent
{
    uint32_t width = 0;
    uint32_t height = 0;
};

// Size the menu is rendered at: always the swapchain image size the framebuffers were created with, never the window
// size. After F11 / alt-tab the window changes size before DXVK / vkd3d-proton recreate the swapchain; rendering at
// the window size drew past the image (NVIDIA Xid 13 "CT HEIGHT Violation", device lost, frozen game).
constexpr Extent RenderExtent(Extent imageExtent) { return imageExtent; }
} // namespace MenuOverlayVkPolicy
