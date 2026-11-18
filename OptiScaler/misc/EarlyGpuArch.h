#pragma once

// The NVIDIA GPU family, found before IdentifyGpu can run (DllMain, config load). It reads PCI ids only:
//   1. D3DKMT (gdi32, only when the process already loaded it): KMTQAITYPE_PHYSICALADAPTERDEVICEIDS of every
//      present adapter.
//   2. If D3DKMT gives nothing (gdi32 not loaded yet, some Wine builds): the display entries of
//      HKLM\SYSTEM\CurrentControlSet\Enum\PCI. Wine writes them for the GPUs it sees; on Windows they also keep
//      removed GPUs, so this is only a fallback.
// No library is loaded (DllMain, before the OptiScaler proxies exist). Computed once and cached.

#include <framegen/dlssg/MfgUnlockerSelect.h>

#include <vector>

namespace EarlyGpuArch
{

struct Result
{
    MfgUnlockerSelect::NvArch arch = MfgUnlockerSelect::NvArch::Unknown;
    std::vector<MfgUnlockerSelect::PciId> adapters;
    const char* source = "none"; // "d3dkmt", "registry" or "none"
};

const Result& Get();

} // namespace EarlyGpuArch
