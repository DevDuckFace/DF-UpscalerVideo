#pragma once

#include "core/FrameBuffer.h"

#include <string>

namespace dfu {

struct ProcessorConfig
{
    std::string modelDir;
    std::string modelName;
    int scale = 4;
    int tileSize = 192; // 0 = auto-solve from the VRAM budget
    int tileOverlap = 16;
    bool useFp16 = true;
    int gpuIndex = 0;
};

class IFrameProcessor
{
public:
    virtual ~IFrameProcessor() = default;

    virtual bool init(const ProcessorConfig& cfg, std::string& errorOut) = 0;
    virtual bool process(const FrameBuffer& in, FrameBuffer& out) = 0;
    virtual int scaleFactor() const = 0;
    virtual std::string name() const = 0;
};

class IFrameInterpolator
{
public:
    virtual ~IFrameInterpolator() = default;

    virtual bool init(const ProcessorConfig& cfg, std::string& errorOut) = 0;
    // t in (0,1). t=0.5 for 2x. Called n-1 times for n-x interpolation.
    virtual bool interpolate(const FrameBuffer& a, const FrameBuffer& b, float t,
                             FrameBuffer& out) = 0;
    virtual std::string name() const = 0;
};

} // namespace dfu
