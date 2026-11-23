// DLSS-NR Vulkan device extensions without a VkInstance (NR-VK-DEVICE-HOOK-STALE-INSTANCE.md, DLSS Unlocked #73).
//
// GTA V Enhanced on Linux crashed at start with [DlssNr] Enabled=true. hkvkCreateDevice looked up
// vkEnumerateDeviceExtensionProperties through State::VulkanInstance, the last instance hkvkCreateInstance had seen.
// Social Club's D3D11 device (DXVK) comes from an instance OptiScaler never saw, and the captured one was already
// torn down: the loader reported "vkGetInstanceProcAddr: Invalid instance" and Wine aborted the game.
//
// 1. The enumerate function is called with the physical device only; the result lists its extensions.
// 2. Null enumerate or null physical device -> empty, nothing called.
// 3. Count failure / zero count / second-call failure -> empty; a shrinking second call is trimmed.
// 4. hkvkCreateDevice's NR block uses RealEnumerateDeviceExtensionProperties, never VulkanInstance.
// 5. The accessor prefers the unhooked original, never the spoofing hook.
// 6. DlssNr_VkExtensions.h has no VkInstance parameter left.
//
// g++ -std=c++20 -Iexternal/vulkan/include -o nr_vk_extension_enumerate_unit tests/nr_vk_extension_enumerate_unit.cpp

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../OptiScaler/dlssnr/DlssNr_VkExtensions.h"

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

static VkPhysicalDevice const kDevice = reinterpret_cast<VkPhysicalDevice>(0x4A06C5D8);

struct Fake
{
    std::vector<std::string> extensions;
    VkResult countResult = VK_SUCCESS;
    VkResult fillResult = VK_SUCCESS;
    uint32_t fillLimit = UINT32_MAX;
    int calls = 0;
    bool sawOtherDevice = false;
    bool sawLayer = false;
};

static Fake g_fake;

static VKAPI_ATTR VkResult VKAPI_CALL FakeEnumerate(VkPhysicalDevice physicalDevice, const char* pLayerName,
                                                    uint32_t* pCount, VkExtensionProperties* pProps)
{
    g_fake.calls++;
    g_fake.sawOtherDevice |= physicalDevice != kDevice;
    g_fake.sawLayer |= pLayerName != nullptr;

    if (pProps == nullptr)
    {
        *pCount = (uint32_t) g_fake.extensions.size();
        return g_fake.countResult;
    }

    const uint32_t n = std::min<uint32_t>({ *pCount, (uint32_t) g_fake.extensions.size(), g_fake.fillLimit });
    for (uint32_t i = 0; i < n; i++)
    {
        std::memset(&pProps[i], 0, sizeof(VkExtensionProperties));
        std::strncpy(pProps[i].extensionName, g_fake.extensions[i].c_str(), VK_MAX_EXTENSION_NAME_SIZE - 1);
    }

    *pCount = n;
    return g_fake.fillResult;
}

static void Reset(std::vector<std::string> extensions)
{
    g_fake = {};
    g_fake.extensions = std::move(extensions);
}

static void TestEnumeratesThroughPhysicalDevice()
{
    Reset({ "VK_NVX_binary_import", "VK_NVX_image_view_handle", "VK_KHR_push_descriptor",
            "VK_KHR_buffer_device_address" });

    const auto got = DlssNr::VkExt::SupportedDeviceExtensions(FakeEnumerate, kDevice);
    assert(got.size() == 4);
    assert(DlssNr::VkExt::Contains(got, "VK_KHR_buffer_device_address"));
    assert(g_fake.calls == 2 && !g_fake.sawOtherDevice && !g_fake.sawLayer);
    std::puts("PASS 1: extensions come from the physical device alone");
}

static void TestNullInputs()
{
    Reset({ "VK_KHR_push_descriptor" });
    assert(DlssNr::VkExt::SupportedDeviceExtensions(nullptr, kDevice).empty());
    assert(DlssNr::VkExt::SupportedDeviceExtensions(FakeEnumerate, VK_NULL_HANDLE).empty());
    assert(g_fake.calls == 0);
    std::puts("PASS 2: no enumerate function or no physical device -> empty, nothing called");
}

static void TestFailures()
{
    Reset({ "VK_KHR_push_descriptor" });
    g_fake.countResult = VK_ERROR_INITIALIZATION_FAILED;
    assert(DlssNr::VkExt::SupportedDeviceExtensions(FakeEnumerate, kDevice).empty());

    Reset({});
    assert(DlssNr::VkExt::SupportedDeviceExtensions(FakeEnumerate, kDevice).empty());
    assert(g_fake.calls == 1);

    Reset({ "VK_KHR_push_descriptor", "VK_NVX_binary_import" });
    g_fake.fillResult = VK_INCOMPLETE;
    assert(DlssNr::VkExt::SupportedDeviceExtensions(FakeEnumerate, kDevice).empty());

    Reset({ "VK_KHR_push_descriptor", "VK_NVX_binary_import", "VK_NVX_image_view_handle" });
    g_fake.fillLimit = 2;
    const auto got = DlssNr::VkExt::SupportedDeviceExtensions(FakeEnumerate, kDevice);
    assert(got.size() == 2);
    for (const auto& name : got)
        assert(!name.empty());
    std::puts("PASS 3: failures give an empty list; a shorter second answer is trimmed");
}

static void TestHookWiring()
{
    const std::string hooks = ReadFile("OptiScaler/hooks/Vulkan_Hooks.cpp");
    const std::string device = Between(hooks, "static VkResult hkvkCreateDevice(", "\n}\n");
    const std::string nr =
        Between(device, "if (Config::Instance()->DlssNrEnabled.value_or_default())", "auto result = o_vkCreateDevice(");

    assert(nr.find("DlssNr::VkExt::SupportedDeviceExtensions(") != std::string::npos);
    assert(nr.find("VulkanSpoofing::RealEnumerateDeviceExtensionProperties(), physicalDevice)") != std::string::npos);
    assert(nr.find("VulkanInstance") == std::string::npos);
    assert(nr.find("o_vkGetInstanceProcAddr") == std::string::npos);
    std::puts("PASS 4: hkvkCreateDevice's NR block never goes through a captured VkInstance");
}

static void TestAccessor()
{
    const std::string header = ReadFile("OptiScaler/spoofing/Vulkan_Spoofing.h");
    assert(header.find("static PFN_vkEnumerateDeviceExtensionProperties RealEnumerateDeviceExtensionProperties();") !=
           std::string::npos);

    const std::string src = ReadFile("OptiScaler/spoofing/Vulkan_Spoofing.cpp");
    const std::string body = Between(
        src, "PFN_vkEnumerateDeviceExtensionProperties VulkanSpoofing::RealEnumerateDeviceExtensionProperties()",
        "\n}\n");

    const size_t original = body.find("return o_vkEnumerateDeviceExtensionProperties;");
    const size_t exported = body.find("\"vkEnumerateDeviceExtensionProperties\"");
    assert(original != std::string::npos && exported != std::string::npos && original < exported);
    assert(body.find("hkvkEnumerateDeviceExtensionProperties") == std::string::npos);
    assert(body.find("VulkanInstance") == std::string::npos && body.find("GetInstanceProcAddr") == std::string::npos);
    std::puts("PASS 5: the accessor returns the unhooked original first, then the loader export");
}

static void TestHeaderSignature()
{
    const std::string header = ReadFile("OptiScaler/dlssnr/DlssNr_VkExtensions.h");
    const std::string fn = Between(header, "inline std::vector<std::string> SupportedDeviceExtensions(", "{");
    assert(fn.find("VkInstance") == std::string::npos);
    assert(fn.find("PFN_vkGetInstanceProcAddr") == std::string::npos);
    assert(fn.find("PFN_vkEnumerateDeviceExtensionProperties enumerate") != std::string::npos);
    std::puts("PASS 6: SupportedDeviceExtensions takes no instance");
}

int main()
{
    TestEnumeratesThroughPhysicalDevice();
    TestNullInputs();
    TestFailures();
    TestHookWiring();
    TestAccessor();
    TestHeaderSignature();
    std::puts("nr_vk_extension_enumerate_unit: all tests passed");
    return 0;
}
