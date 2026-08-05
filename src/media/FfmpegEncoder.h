#pragma once

#include "core/JobSpec.h"
#include "media/MediaProbe.h"

#include <QStringList>

namespace dfu {

struct EncodePlan
{
    QStringList arguments;
};

// Frames arrive on stdin as raw rgb24 at width x height; the original file is
// opened again as a second input purely to carry audio, subtitles and colour
// metadata across.
EncodePlan buildEncodePlan(const JobSpec& spec, const MediaInfo& info, int width, int height,
                           const QString& outputPath);

// Encoders offered in the UI, most appropriate first.
QStringList availableEncoders();
QString encoderDisplayName(const QString& encoder);

// Enhancement applied after scaling, on the way into the encoder. Empty when
// nothing is enabled.
QString buildPostFilterChain(const JobSpec& spec);

} // namespace dfu
