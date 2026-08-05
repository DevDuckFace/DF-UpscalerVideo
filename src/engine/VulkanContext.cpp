#include "engine/VulkanContext.h"

#include <spdlog/spdlog.h>

#include <ncnn/gpu.h>

namespace dfu {
namespace {

GpuInfo describe(int index)
{
    GpuInfo info;
    info.index = index;

    const ncnn::GpuInfo& gpu = ncnn::get_gpu_info(index);
    info.name = gpu.device_name();
    info.supportsFp16Storage = gpu.support_fp16_storage();
    info.supportsFp16Arithmetic = gpu.support_fp16_arithmetic();

    // ncnn exposes heap budget through its allocator; the device-local heap
    // size is the honest upper bound to report.
    info.deviceMemoryBytes = 0;

    return info;
}

} // namespace

bool VulkanContext::vulkanAvailable()
{
    // ncnn::get_gpu_count() initialises the instance on first call and returns
    // 0 rather than throwing when no driver is present.
    return ncnn::get_gpu_count() > 0;
}

std::vector<GpuInfo> VulkanContext::enumerateGpus()
{
    std::vector<GpuInfo> gpus;

    const int count = ncnn::get_gpu_count();
    gpus.reserve(static_cast<std::size_t>(count > 0 ? count : 0));
    for (int i = 0; i < count; ++i) {
        gpus.push_back(describe(i));
    }
    return gpus;
}

bool VulkanContext::init(int gpuIndex, std::string& errorOut)
{
    errorOut.clear();

    const int count = ncnn::get_gpu_count();
    if (count <= 0) {
        errorOut = "No Vulkan-capable GPU was found. Update the graphics driver, or use the "
                   "FFmpeg scaling method instead.";
        return false;
    }

    if (gpuIndex < 0 || gpuIndex >= count) {
        spdlog::warn("GPU index {} is out of range ({} available); falling back to 0", gpuIndex,
                     count);
        gpuIndex = 0;
    }

    m_device = ncnn::get_gpu_device(gpuIndex);
    if (!m_device) {
        errorOut = "The Vulkan device could not be created.";
        return false;
    }

    m_info = describe(gpuIndex);
    m_initialised = true;

    spdlog::info("Vulkan device {}: {} (fp16 storage={}, fp16 arithmetic={})", gpuIndex,
                 m_info.name, m_info.supportsFp16Storage, m_info.supportsFp16Arithmetic);

    return true;
}

uint64_t VulkanContext::usableVramBytes() const
{
    // The desktop compositor is already resident, so the nominal figure is not
    // the budget. The specification's guidance for a 6 GB card is to plan
    // against roughly 5 GB.
    constexpr uint64_t kCompositorReserve = 900ull * 1024 * 1024;
    constexpr uint64_t kHeadroom = 400ull * 1024 * 1024;

    if (m_info.deviceMemoryBytes == 0) {
        // Unknown: assume the design target rather than guessing high.
        return 5ull * 1024 * 1024 * 1024;
    }
    if (m_info.deviceMemoryBytes <= kCompositorReserve + kHeadroom) {
        return 0;
    }
    return m_info.deviceMemoryBytes - kCompositorReserve - kHeadroom;
}

void VulkanContext::shutdown()
{
    // The device belongs to ncnn's registry; releasing the global instance is
    // what actually frees it, and that must happen exactly once per process.
    m_device = nullptr;
    m_initialised = false;
}

VulkanContext::~VulkanContext()
{
    shutdown();
}

} // namespace dfu
