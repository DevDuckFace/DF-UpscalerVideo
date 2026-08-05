#pragma once

#include "core/JobSpec.h"
#include "media/MediaProbe.h"

#include <QString>
#include <QStringList>

namespace dfu {

// Builds the decode command line. Kept separate from the process plumbing so
// the argument construction can be reasoned about, logged and tested on its
// own.
struct DecodePlan
{
    QStringList arguments;
    int outputWidth = 0;
    int outputHeight = 0;
    QString filterChain;
};

// Restoration and scaling currently run as FFmpeg filters, which is the
// always-available tier from the specification's fallback ladder. When the
// ncnn processor is in play, scaling is dropped from the chain and the frames
// arrive at source resolution instead.
DecodePlan buildDecodePlan(const JobSpec& spec, const MediaInfo& info, bool scaleInFfmpeg);

} // namespace dfu
