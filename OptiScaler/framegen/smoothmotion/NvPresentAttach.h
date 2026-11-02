#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Detects whether NVIDIA's NvPresent64.dll (driver Smooth Motion) has attached to a DXGI swapchain.
// Mirrors the order NVSmooth30 v0.1.0 uses (nvpresent.cpp nvp_vtable / wrapper_active):
//   1. the swapchain object's own vtable: identity slots 0/7 and Present slots 8/22;
//   2. a wrapper object behind the swapchain at +0x18, where it has historically lived;
//   3. every other pointer-sized offset from 0x8 to 0x80.
// Every read is range-checked first through `readable`, and no candidate is ever called into.
// The predicates keep the logic testable without Windows:
//   readable(const void* address, size_t size) -> bool
//   inNvPresent(const void* address)           -> bool
namespace NvPresentAttach
{
struct Result
{
    bool attached = false;
    std::size_t offset = 0; // 0: the swapchain object itself; otherwise the wrapper's offset
};

constexpr std::size_t PreferredOffset = 0x18;
constexpr std::size_t MaxOffset = 0x80;

template <typename Readable, typename InNvPresent>
bool VtableInNvPresent(const void* object, Readable&& readable, InNvPresent&& inNvPresent)
{
    if (object == nullptr || !readable(object, sizeof(void*)))
        return false;

    void* const* table = nullptr;
    std::memcpy(&table, object, sizeof(table));
    if (table == nullptr)
        return false;

    if (readable(table, 23 * sizeof(void*)))
        return inNvPresent(table[0]) || inNvPresent(table[7]) || inNvPresent(table[8]) || inNvPresent(table[22]);

    if (readable(table, 9 * sizeof(void*)))
        return inNvPresent(table[0]) || inNvPresent(table[7]) || inNvPresent(table[8]);

    return false;
}

template <typename Readable, typename InNvPresent>
bool CandidateAt(const void* swapchain, std::size_t offset, bool& readOk, Readable&& readable,
                 InNvPresent&& inNvPresent)
{
    const auto* field = static_cast<const std::byte*>(swapchain) + offset;
    readOk = readable(field, sizeof(void*));
    if (!readOk)
        return false;

    void* candidate = nullptr;
    std::memcpy(&candidate, field, sizeof(candidate));
    return candidate != nullptr && candidate != swapchain && VtableInNvPresent(candidate, readable, inNvPresent);
}

template <typename Readable, typename InNvPresent>
Result Probe(const void* swapchain, Readable&& readable, InNvPresent&& inNvPresent)
{
    if (swapchain == nullptr)
        return {};

    if (VtableInNvPresent(swapchain, readable, inNvPresent))
        return { true, 0 };

    bool readOk = false;
    if (CandidateAt(swapchain, PreferredOffset, readOk, readable, inNvPresent))
        return { true, PreferredOffset };

    for (std::size_t offset = sizeof(void*); offset <= MaxOffset; offset += sizeof(void*))
    {
        if (offset == PreferredOffset)
            continue;

        if (CandidateAt(swapchain, offset, readOk, readable, inNvPresent))
            return { true, offset };

        if (!readOk)
            break;
    }

    return {};
}

// Presents after a swapchain is created on which the probe runs: every present up to DensePresents (NvPresent
// attaches during the first presents), then every SparseInterval-th present up to LastPresent.
constexpr uint64_t DensePresents = 120;
constexpr uint64_t SparseInterval = 120;
constexpr uint64_t LastPresent = 3600;

constexpr bool ShouldProbe(uint64_t present)
{
    if (present == 0 || present > LastPresent)
        return false;

    return present <= DensePresents || present % SparseInterval == 0;
}

constexpr bool WindowClosed(uint64_t present) { return present >= LastPresent; }
} // namespace NvPresentAttach
