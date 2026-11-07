#pragma once

// Pure decisions behind Dx11wDx12SC::Present, kept free of Windows headers so unit tests can include them.
// See docs/issues/FG/FG-LINUX-VK-OVERLAY-RESIZE-FREEZE.md, section 4.2.

namespace Dx11wDx12PresentPolicy
{
// Whether the bridge still presents the game's hidden D3D11 swapchain next to XeFG's D3D12 swapchain.
//
// While XeFG's swapchain exists, every frame-pacing query the game can wait on (latency waitable object, frame
// statistics, present count, fullscreen state) already goes to it, and the bridge copies the D3D11 buffer at the
// reported index before each present. So the hidden present matters to no game state.
//
// Under DXVK it does harm: the D3D11 swapchain is a Vulkan swapchain on the same window as vkd3d-proton's D3D12 one.
// Two Vulkan swapchains on one window went out of date together after F11 on Linux, and vkd3d-proton's present never
// returned (Metro 2033 Redux, v0.9.87-pre6). Without DXVK, DXGI handles both swapchains and nothing changes.
constexpr bool PresentHiddenDx11(bool usesDxvk) { return !usesDxvk; }
} // namespace Dx11wDx12PresentPolicy
