#pragma once

#include <string>
#include <vector>

namespace ncnn {
class VulkanDevice;
}

namespace dfu {

struct GpuInfo
{
    int index = 0;
    std::string name;
    uint64_t deviceMemoryBytes = 0;
    bool supportsFp16Storage = false;
    bool supportsFp16Arithmetic = false;
};

// Owns the ncnn Vulkan device for exactly one thread.
//
// Not a singleton and never global: the specification is explicit that the
// device handle must not be global mutable state. The processing thread
// constructs one of these and every ncnn::Net it creates borrows the device
// from it.
class VulkanContext
{
public:
    VulkanContext() = default;
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    bool init(int gpuIndex, std::string& errorOut);
    void shutdown();

    bool valid() const noexcept { return m_device != nullptr; }
    ncnn::VulkanDevice* device() const noexcept { return m_device; }
    const GpuInfo& info() const noexcept { return m_info; }

    // Usable VRAM after subtracting what the desktop compositor already holds.
    uint64_t usableVramBytes() const;

    // Enumerates adapters without keeping a device alive. Safe to call before
    // init; returns an empty list when no Vulkan driver is present.
    static std::vector<GpuInfo> enumerateGpus();

    // True when a Vulkan driver is usable at all. This is the check that
    // decides whether the AI tier can be offered.
    static bool vulkanAvailable();

private:
    // Borrowed from ncnn's internal registry, which owns the lifetime.
    ncnn::VulkanDevice* m_device = nullptr;
    GpuInfo m_info;
    bool m_initialised = false;
};

} // namespace dfu
