# Linux: Game Freezes on F11 / Alt-Tab With Frame Generation and the Overlay Menu

## 1. Overview & Environment

- **Report**: maintainer's own finding. On Linux, alt-tab or F11 while frame generation is active freezes the game
  when `[Menu] OverlayMenu` is on. Until now it was worked around with `OverlayMenu=false`. Not tracked on GitHub.
- **Test environment** (verified by command and from the log):
  - CachyOS, kernel 7.3.1-1-cachyos, NVIDIA GeForce RTX 3080 Ti Laptop GPU 16 GB, driver 580.196.07.
  - Proton-CachyOS Latest (x86_64_v3), Wine 11.0, DXVK + vkd3d-proton, dxvk-nvapi.
  - Metro 2033 Redux (`metro.exe`, D3D11, Steam AppID 286690). It runs through OptiScaler's `Dx11wDx12` bridge, so
    the D3D11 game is on DXVK and XeFG on vkd3d-proton.
  - OptiScaler v0.9.87-pre4 (`6961c56`), LogLevel 0, `OverlayMenu=auto`.
  - `[XeMFG] UnlockMFG=true`, `Source=OptiScaler`, 4X.
- **Status**: root cause found in the code; fix planned on `fix/linux-vk-overlay-resize-freeze` (section 5).

## 2. Reported Symptoms

- Pressing F11 to go fullscreen in Metro 2033 Redux with XeMFG active froze the game window.
- The window stays on screen and accepts no input. The process does not exit.
- With `OverlayMenu=false` the same steps do not freeze.

## 3. Evidence

### 3.1 OptiScaler log

```
20:26:56.550 Vulkan_wDx12::hk_vkCreateCommandPool Command pool ... created          (x4)
20:26:56.550 MenuOverlayVk::QueuePresent rendering menu, swapchain count: 1
20:26:57.094 MenuOverlayVk::QueuePresent vkQueueSubmit error: FFFFFFFC               (VK_ERROR_DEVICE_LOST)
20:26:57.095 hkvkQueuePresentKHR QueuePresent: false!
20:29:09.160 hkvkCreateSwapchainKHR
20:29:09.211 MenuOverlayVk::DestroyVulkanObjects vkDeviceWaitIdle error: FFFFFFFC
20:29:09.211 CreateVulkanObjects vkQueueSubmit error: FFFFFFFC
20:29:09.212 XeFGPacing::ReportStats ... gap 1670.95 avg / 30.55 min / 132691.93 max ms over 81 frames
20:29:09.264 Dx11wDx12SC::SetFullscreenState Dx11wDx12SC SetFullscreenState: 0 ...
```

- The device was lost on the first menu draw after the F11 press.
- The Vulkan swapchain was recreated only 132 s later, which is the 132,692 ms gap in the pacing stats.
- Startup created Vulkan swapchains on two devices. The overlay re-attached to each one in turn
  (`hkvkCreateSwapchainKHR _device captured: 1A8C150`, then `1B1BAB0`).

### 3.2 Kernel log

```
20:26:56 NVRM: Xid (PCI:0000:01:00): 13, Graphics Exception on GPC 0: 3D HEIGHT CT Violation. Coordinates: (0x40, 0x2c0)
         ... the same on GPC 1-5 ...
20:26:56 NVRM: Xid (PCI:0000:01:00): 13, pid=23461, name=metro.exe, Graphics Exception: channel 0x0000003a, Class 0000c797
20:29:09 NVRM: Xid (PCI:0000:01:00): 31, pid=23461, name=metro.exe, ... MMU Fault: ENGINE GRAPHICS ... FAULT_PTE ACCESS_TYPE_VIRT_READ
```

"CT HEIGHT Violation" means a draw wrote past the bottom of its colour target. It happened in the same second as the
menu draw that lost the device.

## 4. Validity Assessment & Root Cause

This is a code defect in `OptiScaler/menu/menu_overlay_vk.cpp`, not a configuration problem. `OverlayMenu=false`
avoids it only because the Vulkan overlay then never draws.

1. `MenuCommon::RenderMenu` calls `ImGui_ImplWin32_NewFrame()`. That sets `io.DisplaySize` to the window's client
   rectangle every frame (`imgui_impl_win32.cpp`).
2. `MenuOverlayVk::QueuePresent` begins its render pass with `renderArea` = `io.DisplaySize`.
   - It calls `MenuOverlayBase::RenderMenu()` with no framebuffer size, so `DisplayFramebufferScale` stays 1.
   - The ImGui Vulkan backend's viewport and scissor rectangles therefore follow the window as well.
3. The overlay's framebuffers are created in `CreateVulkanObjects` with the swapchain's `imageExtent`, as it was at the
   last `vkCreateSwapchainKHR`.
4. F11 or alt-tab resizes the window first. DXVK / vkd3d-proton recreate the swapchain later, here 132 s later.
   - In between, every menu draw covers the window's new size on images of the old size.
   - The first such draw went past the bottom of the image (Xid 13). The device was lost, and the presenting thread
     never recovered.
5. Second gap: `QueuePresent` never checks that the presented swapchain (`pPresentInfo->pSwapchains[0]`) is the one its
   framebuffers belong to, or that the image index is within its image count.
   - With the `Dx11wDx12` bridge there are swapchains on two devices.
   - A present on any swapchain other than the last one created would draw with the wrong framebuffers and submit on
     the wrong queue.

The DX overlay does not have the first problem: it passes the backbuffer size to `RenderMenu(framebufferSize)`, and
`MenuViewport::SetFramebufferScale` maps window coordinates onto the image.

## 5. Planned Solution

On `fix/linux-vk-overlay-resize-freeze`:

- Store the swapchain handle, image count and image extent when `CreateVulkanObjects` builds the framebuffers.
- In `QueuePresent`:
  - draw only on that swapchain and with an image index below its image count;
  - pass the stored extent to `RenderMenu(framebufferSize)`, the same path the DX overlay uses;
  - set `renderArea` to the stored extent. The viewport and scissors then stay inside the image whatever size the
    window has.
- The decisions go in a pure header (`menu/MenuOverlayVkPolicy.h`) with a unit test.
- Unchanged: when the overlay is created and destroyed, the DX overlay, and the `OverlayMenu` defaults.

Related history:
- `5181a07a`: a different alt-tab deadlock (DLSS-G `_localMutex`).
- `395185fa`: menu objects on Linux.
- Neither covers the render area or the swapchain identity.

## 6. Verification

Pending: unit test with the fix, then in game on Linux.
- Metro 2033 Redux with XeMFG and `OverlayMenu=auto`: F11 and alt-tab several times, with the menu open and closed.
- No freeze, and the menu still renders and takes input after each switch.
