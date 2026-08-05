#pragma once

#include "engine/IFrameProcessor.h"

namespace dfu {

// Copies its input unchanged.
//
// This is what runs when restoration and scaling are handled by the FFmpeg
// filter chain, and it is also the processor the end-to-end passthrough test
// uses: if a clip does not survive this path byte for byte, nothing further
// down the pipeline is trustworthy.
class PassthroughProcessor final : public IFrameProcessor
{
public:
    bool init(const ProcessorConfig& cfg, std::string& errorOut) override;
    bool process(const FrameBuffer& in, FrameBuffer& out) override;
    int scaleFactor() const override { return 1; }
    std::string name() const override { return "passthrough"; }
};

} // namespace dfu
