#pragma once

#include "engine/IFrameProcessor.h"
#include "engine/VulkanContext.h"

#include <memory>
#include <string>

namespace ncnn {
class Net;
}

namespace dfu {

// Real-ESRGAN inference, in-process, on the GPU.
//
// Owns its own ncnn::Net and borrows the Vulkan device from the VulkanContext
// that the processing thread holds. Tiling with overlap keeps peak VRAM under
// control on a 6 GB card; on an allocation failure the tile size halves and
// the frame is retried rather than aborting a forty-minute render.
class RealEsrganNcnn final : public IFrameProcessor
{
public:
    explicit RealEsrganNcnn(VulkanContext& context);
    ~RealEsrganNcnn() override;

    RealEsrganNcnn(const RealEsrganNcnn&) = delete;
    RealEsrganNcnn& operator=(const RealEsrganNcnn&) = delete;

    bool init(const ProcessorConfig& cfg, std::string& errorOut) override;
    bool process(const FrameBuffer& in, FrameBuffer& out) override;
    int scaleFactor() const override { return m_scale; }
    std::string name() const override { return m_name; }

    // Resolves the model file pair for a name and scale, e.g.
    // "realesr-animevideov3" + 2 -> "realesr-animevideov3-x2".
    static std::string resolveModelStem(const std::string& modelName, int scale);

private:
    bool runTiled(const FrameBuffer& in, FrameBuffer& out, int tileSize);

    VulkanContext& m_context;
    std::unique_ptr<ncnn::Net> m_net;

    std::string m_name = "realesrgan-ncnn";
    std::string m_inputBlob;
    std::string m_outputBlob;

    int m_scale = 4;
    int m_tileSize = 192;
    int m_tileOverlap = 16;
    bool m_useFp16 = true;
};

} // namespace dfu
