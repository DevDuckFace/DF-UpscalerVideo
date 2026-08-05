#pragma once

#include <ncnn/layer.h>

namespace dfu {

// The RIFE networks use a custom layer type, "rife.Warp", which ncnn does not
// know about. Without registering it, load_param fails outright.
//
// It performs backward warping: for every output pixel it samples the input
// image at a position displaced by the optical flow field, bilinearly.
//
// bottom_blobs[0] = image (C channels)
// bottom_blobs[1] = flow  (2 channels: dx, dy)
// top_blobs[0]    = warped image
class RifeWarp final : public ncnn::Layer
{
public:
    RifeWarp();

    int forward(const std::vector<ncnn::Mat>& bottom_blobs, std::vector<ncnn::Mat>& top_blobs,
                const ncnn::Option& opt) const override;
};

// Passed to ncnn::Net::register_custom_layer.
ncnn::Layer* createRifeWarp(void* userdata);

} // namespace dfu
