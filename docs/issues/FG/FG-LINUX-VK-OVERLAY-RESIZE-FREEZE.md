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
- **Status**: two defects fixed on `fix/linux-vk-overlay-resize-freeze` (section 5) and confirmed in game (section 6).
  - A third cause still makes F11 hang the game after a few switches (4.2); it is open, to be handled separately.
  - The overlay fix is verified in game: no more GPU fault on F11.
  - The per-thread flag (4.1) still needs a retest.

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

### 4.1 After F11 the game's swapchain is no longer wrapped (found testing v0.9.87-pre5)

With the overlay fix, F11 no longer froze the game, but it could not leave fullscreen, and after several switches it
hung. Log of v0.9.87-pre5, Metro 2033 Redux, LogLevel 0:

- The game's swapchain was wrapped only at launch (20:47:55): `WrappedIDXGISwapChain4`, `Dx11wDx12SC`, XeFG swapchain.
- Every swapchain the game created after F11 took `DxgiFactoryHooks::CreateSwapChain Vulkan is creating swapchain!`,
  which calls the original function and wraps nothing. That happened at 20:48:09, 20:48:49, 20:48:59, 20:49:53 and
  20:51:59, every time with `Windowed: 0`.
- From then on the game ran on a plain DXVK swapchain:
  - no D3D12 bridge and no XeFG swapchain. XeFG never generated a frame in this session: every pacing report is `0X`;
  - the borderless quirk was bypassed, so the game got the exclusive fullscreen it asked for;
  - the overlay attached to DXVK's swapchains (device `1A8C150`).
- After the 20:51:59 recreation OptiScaler logged nothing for 44 s, in the middle of a frame (DLSS evaluate of frame
  3080). After one more DXVK swapchain at 20:52:43, the process stayed hung at about 145 % CPU.
- The log is complete: logging is synchronous and flushed after every line (`flush_on(trace)`, `LogAsync` off).
- Thread stacks were not captured: `ptrace_scope` is 1.

Cause: `State::vulkanCreatingSC` was a process-wide flag.
- `ScopedVulkanCreatingSC` sets it for the whole `vkQueuePresentKHR` and `vkCreateSwapchainKHR` hooks
  (`Vulkan_Hooks.cpp`). Its purpose is that a DXGI swapchain created inside them, on the same thread, is not wrapped.
- On Linux DXVK presents from its own thread, almost continuously. The game's thread, creating its swapchain at that
  moment, saw the flag, and its swapchain was treated as an OptiScaler-internal one.
- On Windows the game's presents do not go through these Vulkan hooks, so the race cannot happen there.
- The flag dates from `03126e4c`; it was not introduced by the overlay fix.
- The 44 s hang is attributed to the same cause, but without thread stacks this is not proven.

### 4.2 Remaining hang after several F11 switches (open)

With both fixes (v0.9.87-pre6) the game's swapchain is wrapped again after F11. After a few quick F11 presses it still
hangs, also with `OverlayMenu=false`, so the Vulkan overlay is not involved. No NVIDIA Xid is logged.

The last frame in `OptiScaler.log` enters `LocalPresent` and never returns from the real `IDXGISwapChain::Present`.
`FGHooks::FGPresent` holds `FG->Mutex` at that point. Just before, a Vulkan present returned
`VK_ERROR_OUT_OF_DATE_KHR`, and no swapchain is recreated afterwards.

Windows-side backtraces, from `winedbg` of the game's own Proton build:

```
WINEPREFIX=~/.local/share/Steam/steamapps/compatdata/286690/pfx \
  "<Proton-CachyOS>/files/bin/wine" winedbg --command "bt all" 0x<metro.exe wpid>
```

(`protontricks` failed with a Wine version mismatch; `eu-stack` cannot unwind the Windows modules.)

| Thread | Stack (innermost last) | Waits on |
|---|---|---|
| game (0154) | `metro` → OptiScaler `dxgi` (+0x136833, +0xa52d0, +0xa60e1) → `libxess_fg+0x20c9ed` → `dxgi+0xa538f` → `libxess_fg+0x20bdef` → `libxess_fg+0x220ca6` | XeFG |
| XeFG present (0378) | `libxess_fg+0x21c496` → `+0x2202ed` (paced present call) → `dxgi+0x14f70d` (pacing detour) → `libxess_fg+0x21f965` (native present) → `dxgi` (+0x24e000, +0x24b811, +0x8775e) → `d3d12core+0x4bb9cf` | vkd3d-proton `Present` |
| vkd3d-proton present worker (0370) | `d3d12core+0x475adb` → … → `d3d12core+0x4c07c8` → `winevulkan+0x1cd1e` | inside a Vulkan call |

Suspected cause, not proven:
- `Dx11wDx12SC::Present` (upstream `3fc6c8c3`) presents two swapchains to the same window every frame: the game's hidden
  D3D11 swapchain (`_real->Present(0, ...)`, on DXVK) and XeFG's D3D12 swapchain (on vkd3d-proton). The log shows both
  Vulkan swapchains presenting in alternation.
- On Windows DXGI copes with that. On Linux they are two Vulkan swapchains on one native window, which Vulkan does not
  allow.
- After F11 both go out of date, and vkd3d-proton's present does not return.
- Changing this needs its own investigation: the hidden present may keep the D3D11 back-buffer index advancing.

## 5. Planned Solution

On `fix/linux-vk-overlay-resize-freeze`:

- Store the swapchain handle, image count and image extent when `CreateVulkanObjects` builds the framebuffers.
- In `QueuePresent`:
  - draw only on that swapchain and with an image index below its image count;
  - pass the stored extent to `RenderMenu(framebufferSize)`, the same path the DX overlay uses;
  - set `renderArea` to the stored extent. The viewport and scissors then stay inside the image whatever size the
    window has.
- The decisions go in a pure header (`menu/MenuOverlayVkPolicy.h`) with a unit test.
- `CreateVulkanObjects` refuses more than 8 swapchain images (fixed-size array). It grows the per-image arrays, zeroed,
  only when a swapchain has more images, instead of allocating them again on every recreation.
- 4.1: `State::vulkanCreatingSC` is `static inline thread_local`. Same-thread nesting keeps working; the game's thread no
  longer sees a present thread's flag. Test: `tests/vulkan_creating_sc_thread_unit.cpp`.
- Unchanged: when the overlay is created and destroyed, the DX overlay, and the `OverlayMenu` defaults.

Related history:
- `5181a07a`: a different alt-tab deadlock (DLSS-G `_localMutex`).
- `395185fa`: menu objects on Linux.
- Neither covers the render area or the swapchain identity.

## 6. Verification

Unit tests: `tests/vk_overlay_resize_unit.cpp` (5/5), `tests/vulkan_creating_sc_thread_unit.cpp` (4/4).

v0.9.87-pre5 (overlay fix only), Metro 2033 Redux, Linux, maintainer:
- F11 no longer freezes the game.
- No new NVIDIA Xid in the kernel log.
- The fullscreen problem and the later hang come from 4.1.

v0.9.87-pre6 (both fixes), same setup:
- After F11 the game's swapchain is wrapped again: `Dx11wDx12SC 2 created`, a new XeFG context and swapchain, no
  `Vulkan is creating swapchain!`.
- No Xid.
- After a few quick F11 presses the game still hangs, with and without the overlay menu. That is 4.2, which remains
  open.
