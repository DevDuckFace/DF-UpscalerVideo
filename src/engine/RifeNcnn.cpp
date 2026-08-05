#include "engine/RifeNcnn.h"

#include "core/FrameBuffer.h"
#include "engine/RifeWarpLayer.h"

#include <spdlog/spdlog.h>

#include <ncnn/gpu.h>
#include <ncnn/mat.h>
#include <ncnn/net.h>

#include <filesystem>

namespace dfu {

RifeNcnn::RifeNcnn(VulkanContext& context)
    : m_context(context)
{
}

RifeNcnn::~RifeNcnn() = default;

bool RifeNcnn::init(const ProcessorConfig& cfg, std::string& errorOut)
{
    errorOut.clear();

    if (!m_context.valid()) {
        errorOut = "The Vulkan context is not initialised.";
        return false;
    }

    // The model name is a directory; the weights inside are always flownet.
    const std::filesystem::path dir = std::filesystem::path(cfg.modelDir) / cfg.modelName;
    const std::filesystem::path paramPath = dir / "flownet.param";
    const std::filesystem::path binPath = dir / "flownet.bin";

    if (!std::filesystem::exists(paramPath) || !std::filesystem::exists(binPath)) {
        errorOut = "Interpolation model was not found: " + paramPath.string();
        return false;
    }

    m_useFp16 = cfg.useFp16 && m_context.info().supportsFp16Storage;

    m_net = std::make_unique<ncnn::Net>();
    m_net->opt.use_vulkan_compute = true;
    m_net->opt.use_fp16_packed = m_useFp16;
    m_net->opt.use_fp16_storage = m_useFp16;
    m_net->opt.use_fp16_arithmetic = m_useFp16 && m_context.info().supportsFp16Arithmetic;
    m_net->set_vulkan_device(m_context.device());

    // Must happen before load_param, or the parser rejects the layer type.
    if (m_net->register_custom_layer("rife.Warp", createRifeWarp) != 0) {
        errorOut = "Could not register the RIFE warp layer.";
        m_net.reset();
        return false;
    }

    if (m_net->load_param(paramPath.string().c_str()) != 0) {
        errorOut = "Failed to load " + paramPath.string();
        m_net.reset();
        return false;
    }
    if (m_net->load_model(binPath.string().c_str()) != 0) {
        errorOut = "Failed to load " + binPath.string();
        m_net.reset();
        return false;
    }

    m_name = "rife-ncnn:" + cfg.modelName;
    spdlog::info("RIFE ready: {} fp16={}", cfg.modelName, m_useFp16);
    return true;
}

bool RifeNcnn::interpolate(const FrameBuffer& a, const FrameBuffer& b, float t, FrameBuffer& out)
{
    if (!m_net || !a.valid() || !b.valid() || !out.valid()) {
        return false;
    }
    if (a.width() != b.width() || a.height() != b.height()) {
        return false;
    }
    if (out.width() != a.width() || out.height() != a.height()) {
        return false;
    }

    const int w = a.width();
    const int h = a.height();

    // v4 networks need both dimensions to be a multiple of 32.
    const int wPad = (w + 31) / 32 * 32;
    const int hPad = (h + 31) / 32 * 32;

    const float mean[3] = {0.0f, 0.0f, 0.0f};
    const float norm[3] = {1 / 255.0f, 1 / 255.0f, 1 / 255.0f};

    ncnn::Mat in0 = ncnn::Mat::from_pixels(a.data(), ncnn::Mat::PIXEL_RGB, w, h);
    ncnn::Mat in1 = ncnn::Mat::from_pixels(b.data(), ncnn::Mat::PIXEL_RGB, w, h);
    in0.substract_mean_normalize(mean, norm);
    in1.substract_mean_normalize(mean, norm);

    if (wPad != w || hPad != h) {
        ncnn::Mat padded0;
        ncnn::Mat padded1;
        ncnn::copy_make_border(in0, padded0, 0, hPad - h, 0, wPad - w, ncnn::BORDER_REPLICATE,
                               0.0f);
        ncnn::copy_make_border(in1, padded1, 0, hPad - h, 0, wPad - w, ncnn::BORDER_REPLICATE,
                               0.0f);
        in0 = padded0;
        in1 = padded1;
    }

    // Timestep plane: one channel filled with t, matching the padded size.
    ncnn::Mat in2(wPad, hPad, 1);
    if (in2.empty()) {
        return false;
    }
    in2.fill(t);

    ncnn::Mat result;
    {
        ncnn::Extractor extractor = m_net->create_extractor();
        if (extractor.input("in0", in0) != 0) {
            return false;
        }
        if (extractor.input("in1", in1) != 0) {
            return false;
        }
        if (extractor.input("in2", in2) != 0) {
            return false;
        }
        if (extractor.extract("out0", result) != 0) {
            return false;
        }
    }

    if (result.empty()) {
        return false;
    }

    if (result.w != w || result.h != h) {
        ncnn::Mat cropped;
        ncnn::copy_cut_border(result, cropped, 0, result.h - h, 0, result.w - w);
        result = cropped;
    }

    const float denormMean[3] = {0.0f, 0.0f, 0.0f};
    const float denormScale[3] = {255.0f, 255.0f, 255.0f};
    result.substract_mean_normalize(denormMean, denormScale);

    // Three-argument to_pixels, for the stride reason documented in
    // RealEsrganNcnn::runTiled.
    result.to_pixels(out.data(), ncnn::Mat::PIXEL_RGB, out.stride());

    out.setPtsSeconds(a.ptsSeconds() + (b.ptsSeconds() - a.ptsSeconds()) * t);
    out.setFrameIndex(a.frameIndex());
    out.setSynthetic(true);
    return true;
}

} // namespace dfu
