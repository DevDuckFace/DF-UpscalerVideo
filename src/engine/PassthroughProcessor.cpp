#include "engine/PassthroughProcessor.h"

#include <cstring>

namespace dfu {

bool PassthroughProcessor::init(const ProcessorConfig& cfg, std::string& errorOut)
{
    (void)cfg;
    errorOut.clear();
    return true;
}

bool PassthroughProcessor::process(const FrameBuffer& in, FrameBuffer& out)
{
    if (!in.valid() || !out.valid()) {
        return false;
    }
    if (in.sizeBytes() != out.sizeBytes()) {
        return false;
    }

    std::memcpy(out.data(), in.data(), in.sizeBytes());
    out.setFrameIndex(in.frameIndex());
    out.setPtsSeconds(in.ptsSeconds());
    out.setSynthetic(in.isSynthetic());
    return true;
}

} // namespace dfu
