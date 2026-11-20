#include "pch.h"
#include "EarlyGpuArch.h"

#include <iterator>
#include <string>

// Runs from the first config load, inside DllMain and before NtdllProxy / KernelBaseProxy are initialized: only plain
// kernel32 and advapi32 calls here, and no library is loaded.

namespace
{

using MfgUnlockerSelect::PciId;

typedef NTSTATUS(APIENTRY* PFN_EarlyEnumAdapters)(D3DKMT_ENUMADAPTERS*);
typedef NTSTATUS(APIENTRY* PFN_EarlyQueryAdapterInfo)(const D3DKMT_QUERYADAPTERINFO*);
typedef NTSTATUS(APIENTRY* PFN_EarlyCloseAdapter)(const D3DKMT_CLOSEADAPTER*);

// False when gdi32 is not loaded yet, D3DKMT is missing, or no adapter answered the device id query.
bool ReadD3dkmt(std::vector<PciId>& adapters)
{
    HMODULE gdi32 = GetModuleHandleW(L"gdi32.dll");
    if (gdi32 == nullptr)
        return false;

    bool answered = false;

    auto enumAdapters = reinterpret_cast<PFN_EarlyEnumAdapters>(GetProcAddress(gdi32, "D3DKMTEnumAdapters"));
    auto queryAdapterInfo =
        reinterpret_cast<PFN_EarlyQueryAdapterInfo>(GetProcAddress(gdi32, "D3DKMTQueryAdapterInfo"));
    auto closeAdapter = reinterpret_cast<PFN_EarlyCloseAdapter>(GetProcAddress(gdi32, "D3DKMTCloseAdapter"));

    D3DKMT_ENUMADAPTERS enumeration = {};

    if (enumAdapters != nullptr && queryAdapterInfo != nullptr && closeAdapter != nullptr &&
        enumAdapters(&enumeration) == 0)
    {
        for (ULONG i = 0; i < enumeration.NumAdapters && i < MAX_ENUM_ADAPTERS; i++)
        {
            D3DKMT_QUERY_DEVICE_IDS ids = {};
            ids.PhysicalAdapterIndex = 0;

            D3DKMT_QUERYADAPTERINFO query = {};
            query.hAdapter = enumeration.Adapters[i].hAdapter;
            query.Type = KMTQAITYPE_PHYSICALADAPTERDEVICEIDS;
            query.pPrivateDriverData = &ids;
            query.PrivateDriverDataSize = sizeof(ids);

            if (queryAdapterInfo(&query) == 0)
            {
                answered = true;
                adapters.push_back(PciId { ids.DeviceIds.VendorID, ids.DeviceIds.DeviceID });
            }

            D3DKMT_CLOSEADAPTER close = {};
            close.hAdapter = enumeration.Adapters[i].hAdapter;
            closeAdapter(&close);
        }
    }

    return answered;
}

std::string Narrow(const wchar_t* text)
{
    std::string result;
    for (; text != nullptr && *text != 0; text++)
        result.push_back(*text < 0x80 ? static_cast<char>(*text) : '?');
    return result;
}

// True when the first instance under a PCI device key has Class "Display".
bool IsDisplayDevice(HKEY deviceKey)
{
    wchar_t instanceName[256] {};
    DWORD instanceSize = static_cast<DWORD>(std::size(instanceName));

    if (RegEnumKeyExW(deviceKey, 0, instanceName, &instanceSize, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
        return false;

    HKEY instanceKey = nullptr;
    if (RegOpenKeyExW(deviceKey, instanceName, 0, KEY_READ, &instanceKey) != ERROR_SUCCESS)
        return false;

    wchar_t className[64] {};
    DWORD classSize = sizeof(className);
    DWORD type = 0;
    const bool display = RegQueryValueExW(instanceKey, L"Class", nullptr, &type, reinterpret_cast<LPBYTE>(className),
                                          &classSize) == ERROR_SUCCESS &&
                         type == REG_SZ && _wcsicmp(className, L"Display") == 0;

    RegCloseKey(instanceKey);
    return display;
}

bool ReadRegistry(std::vector<PciId>& adapters)
{
    HKEY pciKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Enum\\PCI", 0, KEY_READ | KEY_ENUMERATE_SUB_KEYS,
                      &pciKey) != ERROR_SUCCESS)
    {
        return false;
    }

    bool found = false;

    for (DWORD index = 0;; index++)
    {
        wchar_t name[256] {};
        DWORD nameSize = static_cast<DWORD>(std::size(name));

        if (RegEnumKeyExW(pciKey, index, name, &nameSize, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;

        const auto id = MfgUnlockerSelect::ParsePciHardwareId(Narrow(name));
        if (!id.has_value())
            continue;

        HKEY deviceKey = nullptr;
        if (RegOpenKeyExW(pciKey, name, 0, KEY_READ | KEY_ENUMERATE_SUB_KEYS, &deviceKey) != ERROR_SUCCESS)
            continue;

        if (IsDisplayDevice(deviceKey))
        {
            adapters.push_back(*id);
            found = true;
        }

        RegCloseKey(deviceKey);
    }

    RegCloseKey(pciKey);
    return found;
}

EarlyGpuArch::Result Detect()
{
    EarlyGpuArch::Result result;

    if (ReadD3dkmt(result.adapters))
    {
        result.source = "d3dkmt";
    }
    else
    {
        result.adapters.clear();
        if (ReadRegistry(result.adapters))
            result.source = "registry";
    }

    if (result.adapters.empty())
        result.arch = MfgUnlockerSelect::NvArch::Unknown;
    else
        result.arch = MfgUnlockerSelect::CombineAdapters(result.adapters);

    return result;
}

} // namespace

const EarlyGpuArch::Result& EarlyGpuArch::Get()
{
    static const Result result = Detect();
    return result;
}
