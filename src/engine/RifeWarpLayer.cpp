#include "engine/RifeWarpLayer.h"

#include <algorithm>
#include <cmath>

namespace dfu {

RifeWarp::RifeWarp()
{
    // Two inputs, and the output cannot alias them.
    one_blob_only = false;
    support_inplace = false;
    // No Vulkan implementation: ncnn moves the tensors back to the host around
    // this layer. Correctness first; the flow fields RIFE warps are small
    // relative to the frame, so the cost is bounded.
    support_vulkan = false;
}

int RifeWarp::forward(const std::vector<ncnn::Mat>& bottom_blobs, std::vector<ncnn::Mat>& top_blobs,
                      const ncnn::Option& opt) const
{
    if (bottom_blobs.size() < 2) {
        return -1;
    }

    const ncnn::Mat& image = bottom_blobs[0];
    const ncnn::Mat& flow = bottom_blobs[1];

    const int w = image.w;
    const int h = image.h;
    const int channels = image.c;
    const std::size_t elemsize = image.elemsize;

    if (flow.w != w || flow.h != h || flow.c < 2) {
        return -1;
    }

    ncnn::Mat& top = top_blobs[0];
    top.create(w, h, channels, elemsize, opt.blob_allocator);
    if (top.empty()) {
        return -100;
    }

#pragma omp parallel for num_threads(opt.num_threads)
    for (int q = 0; q < channels; q++) {
        float* outptr = top.channel(q);
        const ncnn::Mat source = image.channel(q);

        for (int y = 0; y < h; y++) {
            const float* flowX = flow.channel(0).row(y);
            const float* flowY = flow.channel(1).row(y);

            for (int x = 0; x < w; x++) {
                const float sampleX = static_cast<float>(x) + flowX[x];
                const float sampleY = static_cast<float>(y) + flowY[x];

                const int x0 = static_cast<int>(std::floor(sampleX));
                const int y0 = static_cast<int>(std::floor(sampleY));
                const float alpha = sampleX - static_cast<float>(x0);
                const float beta = sampleY - static_cast<float>(y0);

                // Clamp to the edge: flow can point outside the frame, and
                // wrapping there would drag content across the image.
                const int cx0 = std::clamp(x0, 0, w - 1);
                const int cx1 = std::clamp(x0 + 1, 0, w - 1);
                const int cy0 = std::clamp(y0, 0, h - 1);
                const int cy1 = std::clamp(y0 + 1, 0, h - 1);

                const float v00 = source.row(cy0)[cx0];
                const float v01 = source.row(cy0)[cx1];
                const float v10 = source.row(cy1)[cx0];
                const float v11 = source.row(cy1)[cx1];

                const float top_ = v00 * (1.0f - alpha) + v01 * alpha;
                const float bottom_ = v10 * (1.0f - alpha) + v11 * alpha;

                outptr[x] = top_ * (1.0f - beta) + bottom_ * beta;
            }

            outptr += w;
        }
    }

    return 0;
}

ncnn::Layer* createRifeWarp(void* /*userdata*/)
{
    return new RifeWarp;
}

} // namespace dfu
