#include "engine/RealEsrganNcnn.h"

#include "core/FrameBuffer.h"

#include <spdlog/spdlog.h>

#include <ncnn/gpu.h>
#include <ncnn/mat.h>
#include <ncnn/net.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace dfu {
namespace {

// The blob names differ between converted models, so they are read out of the
// .param file rather than hardcoded. The first Input layer names the input;
// the last layer's first output names the result.
bool resolveBlobNames(const std::filesystem::path& paramPath, std::string& inputBlob,
                      std::string& outputBlob)
{
    std::ifstream file(paramPath);
    if (!file) {
        return false;
    }

    std::string magic;
    std::string line;
    int layerCount = 0;
    int blobCount = 0;
    if (!(file >> magic >> layerCount >> blobCount)) {
        return false;
    }
    std::getline(file, line);

    std::string lastOutput;
    for (int i = 0; i < layerCount && std::getline(file, line); ++i) {
        if (line.empty()) {
            --i;
            continue;
        }

        std::istringstream stream(line);
        std::string type;
        std::string name;
        int bottomCount = 0;
        int topCount = 0;
        if (!(stream >> type >> name >> bottomCount >> topCount)) {
            continue;
        }

        for (int b = 0; b < bottomCount; ++b) {
            std::string ignored;
            stream >> ignored;
        }

        std::string firstTop;
        for (int t = 0; t < topCount; ++t) {
            std::string top;
            if (stream >> top && t == 0) {
                firstTop = top;
            }
        }

        if (type == "Input" && inputBlob.empty()) {
            inputBlob = firstTop;
        }
        if (!firstTop.empty()) {
            lastOutput = firstTop;
        }
    }

    outputBlob = lastOutput;
    return !inputBlob.empty() && !outputBlob.empty();
}

} // namespace

RealEsrganNcnn::RealEsrganNcnn(VulkanContext& context)
    : m_context(context)
{
}

RealEsrganNcnn::~RealEsrganNcnn() = default;

std::string RealEsrganNcnn::resolveModelStem(const std::string& modelName, int scale)
{
    // Only the animevideov3 family ships a separate file per scale factor.
    if (modelName.find("animevideov3") != std::string::npos) {
        return modelName + "-x" + std::to_string(scale);
    }
    return modelName;
}

bool RealEsrganNcnn::init(const ProcessorConfig& cfg, std::string& errorOut)
{
    errorOut.clear();

    if (!m_context.valid()) {
        errorOut = "The Vulkan context is not initialised.";
        return false;
    }

    m_scale = cfg.scale > 0 ? cfg.scale : 4;
    m_tileOverlap = cfg.tileOverlap > 0 ? cfg.tileOverlap : 16;
    m_useFp16 = cfg.useFp16 && m_context.info().supportsFp16Storage;

    const std::string stem = resolveModelStem(cfg.modelName, m_scale);
    const std::filesystem::path dir(cfg.modelDir);
    const std::filesystem::path paramPath = dir / (stem + ".param");
    const std::filesystem::path binPath = dir / (stem + ".bin");

    if (!std::filesystem::exists(paramPath) || !std::filesystem::exists(binPath)) {
        errorOut = "Model files were not found: " + paramPath.string();
        return false;
    }

    if (!resolveBlobNames(paramPath, m_inputBlob, m_outputBlob)) {
        errorOut = "Could not determine the input/output blob names from " + paramPath.string();
        return false;
    }

    m_net = std::make_unique<ncnn::Net>();
    m_net->opt.use_vulkan_compute = true;
    m_net->opt.use_fp16_packed = m_useFp16;
    m_net->opt.use_fp16_storage = m_useFp16;
    m_net->opt.use_fp16_arithmetic = m_useFp16 && m_context.info().supportsFp16Arithmetic;
    m_net->opt.use_int8_storage = false;
    m_net->set_vulkan_device(m_context.device());

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

    // Tile size: honour an explicit choice, otherwise start from the value the
    // VRAM budget suggests, clamped and rounded as specified.
    int tile = cfg.tileSize;
    if (tile <= 0) {
        const uint64_t budget = m_context.usableVramBytes();
        // Peak activation cost grows with tile area and the scale factor; the
        // constant is empirical and deliberately conservative.
        const double perPixelBytes = 220.0 * static_cast<double>(m_scale);
        tile = static_cast<int>(std::sqrt(static_cast<double>(budget) / perPixelBytes));
    }
    tile = std::clamp(tile, 64, 512);
    tile = (tile / 32) * 32;
    m_tileSize = std::max(64, tile);

    m_name = "realesrgan-ncnn:" + stem;

    spdlog::info("Real-ESRGAN ready: {} scale=x{} tile={} overlap={} fp16={} blobs={}/{}", stem,
                 m_scale, m_tileSize, m_tileOverlap, m_useFp16, m_inputBlob, m_outputBlob);

    return true;
}

bool RealEsrganNcnn::process(const FrameBuffer& in, FrameBuffer& out)
{
    if (!m_net || !in.valid() || !out.valid()) {
        return false;
    }

    int tile = m_tileSize;
    // Halve and retry rather than crash: an out-of-memory abort partway
    // through a long render is the worst possible outcome.
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (runTiled(in, out, tile)) {
            if (tile != m_tileSize) {
                spdlog::warn("Tile size reduced to {} for the rest of this job", tile);
                m_tileSize = tile;
            }
            return true;
        }

        if (tile <= 64) {
            break;
        }
        tile = std::max(64, (tile / 2 / 32) * 32);
        spdlog::warn("Inference failed; retrying with tile size {}", tile);
    }

    return false;
}

bool RealEsrganNcnn::runTiled(const FrameBuffer& in, FrameBuffer& out, int tileSize)
{
    const int width = in.width();
    const int height = in.height();
    const int scale = m_scale;

    if (out.width() != width * scale || out.height() != height * scale) {
        spdlog::error("Output frame is {}x{}, expected {}x{}", out.width(), out.height(),
                      width * scale, height * scale);
        return false;
    }

    const int overlap = m_tileOverlap;
    const int step = std::max(1, tileSize - overlap * 2);

    for (int y = 0; y < height; y += step) {
        for (int x = 0; x < width; x += step) {
            // Expand the tile by the overlap so the network sees context past
            // the seam, then keep only the interior when writing back.
            const int x0 = std::max(0, x - overlap);
            const int y0 = std::max(0, y - overlap);
            const int x1 = std::min(width, x + step + overlap);
            const int y1 = std::min(height, y + step + overlap);
            const int tileW = x1 - x0;
            const int tileH = y1 - y0;
            if (tileW <= 0 || tileH <= 0) {
                continue;
            }

            ncnn::Mat inTile =
                ncnn::Mat::from_pixels_roi(in.data(), ncnn::Mat::PIXEL_RGB, width, height, x0, y0,
                                           tileW, tileH);

            const float normMean[3] = {0.0f, 0.0f, 0.0f};
            const float normScale[3] = {1 / 255.0f, 1 / 255.0f, 1 / 255.0f};
            inTile.substract_mean_normalize(normMean, normScale);

            ncnn::Mat outTile;
            {
                // Vulkan compute is enabled through the Net's options and its
                // bound device; current ncnn has no per-extractor switch.
                ncnn::Extractor extractor = m_net->create_extractor();
                if (extractor.input(m_inputBlob.c_str(), inTile) != 0) {
                    return false;
                }
                if (extractor.extract(m_outputBlob.c_str(), outTile) != 0) {
                    return false;
                }
            }

            if (outTile.empty()) {
                return false;
            }

            spdlog::trace("tile src=({},{}) {}x{} -> net {}x{}", x0, y0, tileW, tileH, outTile.w,
                          outTile.h);

            const float denormMean[3] = {0.0f, 0.0f, 0.0f};
            const float denormScale[3] = {255.0f, 255.0f, 255.0f};
            outTile.substract_mean_normalize(denormMean, denormScale);

            // Interior of the tile in source coordinates, clipped to the frame.
            const int keepX0 = x;
            const int keepY0 = y;
            const int keepX1 = std::min(width, x + step);
            const int keepY1 = std::min(height, y + step);
            const int keepW = keepX1 - keepX0;
            const int keepH = keepY1 - keepY0;
            if (keepW <= 0 || keepH <= 0) {
                continue;
            }

            ncnn::Mat region;
            ncnn::copy_cut_border(outTile, region, (keepY0 - y0) * scale, (y1 - keepY1) * scale,
                                  (keepX0 - x0) * scale, (x1 - keepX1) * scale);

            // Resize only if the crop did not already land on the target size,
            // then write with an explicit destination stride.
            //
            // to_pixels_resize's target_stride argument is NOT honoured on the
            // fast path where the mat already matches the target size: it
            // forwards to the two-argument to_pixels and packs rows at
            // width*3. Writing a 960-wide tile into a 1280-wide frame that way
            // fills exactly 75% of the buffer and skews every row. The
            // three-argument to_pixels does respect the stride.
            if (region.w != keepW * scale || region.h != keepH * scale) {
                ncnn::Mat resized;
                ncnn::resize_bilinear(region, resized, keepW * scale, keepH * scale);
                region = resized;
            }

            region.to_pixels(out.data()
                                 + static_cast<std::size_t>(keepY0 * scale) * out.stride()
                                 + static_cast<std::size_t>(keepX0 * scale) * 3,
                             ncnn::Mat::PIXEL_RGB, out.stride());
        }
    }

    out.setFrameIndex(in.frameIndex());
    out.setPtsSeconds(in.ptsSeconds());
    out.setSynthetic(in.isSynthetic());
    return true;
}

} // namespace dfu
