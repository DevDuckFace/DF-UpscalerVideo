#pragma once

#include "engine/IFrameProcessor.h"
#include "engine/VulkanContext.h"

#include <memory>
#include <string>

namespace ncnn {
class Net;
}

namespace dfu {

// RIFE v4.x frame interpolation, in-process on the GPU.
//
// Borrows the Vulkan device from the same VulkanContext the upscaler uses, so
// one job holds exactly one device on exactly one thread.
//
// The v4 networks take three inputs: the two frames and a timestep plane. They
// require both dimensions to be a multiple of 32, so frames are padded with
// replicated edges and the result is cropped back.
class RifeNcnn final : public IFrameInterpolator
{
public:
    explicit RifeNcnn(VulkanContext& context);
    ~RifeNcnn() override;

    RifeNcnn(const RifeNcnn&) = delete;
    RifeNcnn& operator=(const RifeNcnn&) = delete;

    bool init(const ProcessorConfig& cfg, std::string& errorOut) override;
    bool interpolate(const FrameBuffer& a, const FrameBuffer& b, float t,
                     FrameBuffer& out) override;
    std::string name() const override { return m_name; }

private:
    VulkanContext& m_context;
    std::unique_ptr<ncnn::Net> m_net;
    std::string m_name = "rife-ncnn";
    bool m_useFp16 = true;
};

} // namespace dfu
