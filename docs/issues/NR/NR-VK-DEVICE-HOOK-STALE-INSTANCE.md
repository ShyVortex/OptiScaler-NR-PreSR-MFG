# DLSS-NR: Startup Crash on Linux When a Vulkan Device Comes From an Unseen Instance (GTA V Enhanced)

## 1. Overview & Environment

- **Report**: YuriManciniDev, [DLSS Unlocked #73](https://github.com/ShyVortex/DLSS-Unlocked/issues/73) (2026-11-22).
- **Reporter environment**:
  - Game: Grand Theft Auto V Enhanced (Steam 3240220), Story Mode with `-nobattleye`.
  - OS: Linux, Pop!_OS 24.04, kernel 7.2.15, COSMIC (Wayland).
  - GPU: RTX 5060 Ti, NVIDIA driver 595.104.02.
  - Proton Experimental 11.0-20261112, DXVK 3.1.1-209-g1039f682, vkd3d-proton 3.2.0.
  - DLSS Unlocked NR-v0.9.95 (OptiScaler 0.9.95-final `1c8d76b1`) as the `dxgi` proxy.
  - Stock `OptiScaler.ini` with only `LogLevel=0` and `[DlssNr] Enabled=true` changed.
- **Analysis environment**: log and source analysis only, no GTA V Enhanced available locally. CachyOS (rolling),
  kernel 7.3.5-2-cachyos, NVIDIA GeForce RTX 3080 Ti Laptop GPU, driver 580.196.07.

## 2. Reported Symptoms

- With the stock INI the game runs, and DLSS works through OptiScaler (1706x720 → 2560x1080, DLSS 310.4).
- With `[DlssNr] Enabled=true` it closes about 9 s after launch, every time.
- Proton log (`PROTON_LOG=1`):

```text
[Vulkan Loader] ERROR:          vkGetInstanceProcAddr: Invalid instance [VUID-vkGetInstanceProcAddr-instance-parameter]
1303.684:09c0:09cc:warn:seh:dispatch_exception backtrace: --- Exception 0x80000101 at 0x71963813269c: /lib/x86_64-linux-gnu/libc.so.6 + 0x9469c.
1303.684:09c0:09cc:trace:seh:dispatch_exception code=80000101 (EXCEPTION_WINE_ASSERTION) flags=1 addr=000071963813269C
```

- The reporter's analysis named the cause correctly: the NR part of `hkvkCreateDevice` uses the last captured
  `VkInstance`.
- Side note from the report, a separate issue: under Proton, the Insert hotkey stops opening the menu once in
  gameplay.

## 3. Issue Validity Assessment

This is a real OptiScaler defect in the DLSS-NR block of `hkvkCreateDevice` (`OptiScaler/hooks/Vulkan_Hooks.cpp`).
It has been there since that block was added in `5efe527e`.

### 3.1 Timeline from `OptiScaler.log`

| Time | Event |
|---|---|
| 14:16:19–24 | vkd3d-proton creates several instances and devices. Each `hkvkCreateInstance` stores its instance in `State::Instance().VulkanInstance`. The NR block runs for each device: "device offers 265 extensions … vkCreateDevice returned 0". |
| 14:16:27.040 | Last capture: `VulkanInstance captured: 4A06C5C0`. Its device `52B13B70` is created at 27.25. |
| 14:16:27.47–27.80 | That device's command pools are destroyed: the device and its instance are being torn down. |
| 14:16:27.911–27.925 | `socialclub.dll` loads `dxgi.dll` and `d3d11.dll` and calls `D3D11CreateDeviceAndSwapChain`. DXVK creates its `VkInstance`, and no `hkvkCreateInstance` line is logged for it. |
| 14:16:27.930 | `hkvkCreateDevice` for DXVK's device. `VulkanSpoofing::hkvkCreateDevice` completes: "Final extension count: 65", extension list. |
| — | The log ends before "DLSS-NR Vulkan: device offers …". The loader reports "Invalid instance", and Wine aborts. |

### 3.2 Root cause

- The NR block called
  `DlssNr::VkExt::SupportedDeviceExtensions(o_vkGetInstanceProcAddr, State::Instance().VulkanInstance, physicalDevice)`.
- That function looked up `vkEnumerateDeviceExtensionProperties` with
  `vkGetInstanceProcAddr(State::Instance().VulkanInstance, …)`.
- `State::Instance().VulkanInstance` is only "the last instance `hkvkCreateInstance` saw". Here that instance was
  being destroyed, and the device being created belonged to another instance. The Khronos loader validates the
  instance handle in `vkGetInstanceProcAddr`; Wine turns the loader's abort into `EXCEPTION_WINE_ASSERTION`.
- `vkEnumerateDeviceExtensionProperties` is a physical-device function. The loader's exported trampoline dispatches
  through the `VkPhysicalDevice`, so no instance is needed. `VulkanSpoofing::hkvkCreateDevice` already enumerates
  this way (through `o_vkEnumerateDeviceExtensionProperties` or the `vulkan-1.dll` export), which is why it finished
  for the same device.
- With NR off the block is skipped, which is why the stock INI works.

### 3.3 Prior-attempt audit

- No document in `docs/issues/` covers Vulkan instance capture or extension enumeration.
- Rule §6 (NR) and §9 (Linux Vulkan overlay) don't touch this code.

## 4. Applied Solution (`ea4f2f97`)

- `OptiScaler/dlssnr/DlssNr_VkExtensions.h`:
  - `SupportedDeviceExtensions(PFN_vkEnumerateDeviceExtensionProperties enumerate, VkPhysicalDevice physicalDevice)`
    replaces the overload that took `vkGetInstanceProcAddr` and an instance. No caller can pass an instance any more.
  - The list is trimmed to the count of the second call. A shorter answer no longer leaves empty names.
- `VulkanSpoofing::RealEnumerateDeviceExtensionProperties()` (`OptiScaler/spoofing/Vulkan_Spoofing.h/.cpp`):
  - It returns the detour trampoline `o_vkEnumerateDeviceExtensionProperties` when extension spoofing hooked the
    export, otherwise the `vulkan-1.dll` export.
  - It never returns the spoofing hook: with `VulkanExtensionSpoofing` on, the hook adds `VK_NVX_*` and other entries
    the driver may not have. NR would then request them, and `vkCreateDevice` would fail.
- The NR block in `hkvkCreateDevice` calls
  `SupportedDeviceExtensions(VulkanSpoofing::RealEnumerateDeviceExtensionProperties(), physicalDevice)`.
  - Without an enumerate function the list is empty. The block then logs the NR extensions as NOT AVAILABLE and adds
    nothing, so device creation is never put at risk.

### 4.1 Not changed: other uses of the captured instance

`State::Instance().VulkanInstance` is still captured and still passed to FFX context creation:
`OptiScaler/inputs/FSR2_Vk.cpp:345` and `OptiScaler/inputs/FfxApi_Vk.cpp:404`. The same staleness applies there when
a game creates more than one instance. Those paths were not involved in this crash, and they are left open until a
report shows them.

## 5. Verification

### 5.1 Unit tests

- `tests/nr_vk_extension_enumerate_unit.cpp` (6/6). It includes the real `DlssNr_VkExtensions.h`:
  - a fake enumerate function receives only the physical device;
  - null inputs, a failing count call, a zero count, a failing second call, and a shorter second answer;
  - source checks:
    - the NR block uses `RealEnumerateDeviceExtensionProperties()` and never `VulkanInstance` or
      `o_vkGetInstanceProcAddr`;
    - the accessor returns the unhooked original first, then the loader export, and never the hook;
    - the header takes no instance.
  - The source checks fail on the unfixed sources.
- Related suites: `nr_game_native_unit` (13/13), `nr_driver_gate_unit` (14/14), `vulkan_creating_sc_thread_unit`.
- Not compiled with MSVC locally. The CI build is the compile check.

```bash
git submodule update --init external/vulkan
g++ -std=c++20 -Iexternal/vulkan/include -o nr_vk_extension_enumerate_unit tests/nr_vk_extension_enumerate_unit.cpp
./nr_vk_extension_enumerate_unit
```

### 5.2 In-game confirmation (pending)

The reporter tests with `[DlssNr] Enabled=true` and LogLevel 0. Expected:
- the game starts;
- `OptiScaler.log` shows "DLSS-NR Vulkan: device offers …" and "vkCreateDevice returned 0" for the Social Club D3D11
  device too;
- the Proton log has no "Invalid instance" line.

Whether NR then runs in GTA V Enhanced on Linux is a separate check. This fix only removes the startup crash.
