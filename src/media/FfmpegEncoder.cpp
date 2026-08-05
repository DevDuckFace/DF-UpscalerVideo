#include "media/FfmpegEncoder.h"

#include <QObject>
#include <QStringList>

#include <algorithm>

namespace dfu {

QStringList availableEncoders()
{
    return {
        QStringLiteral("hevc_nvenc"),
        QStringLiteral("h264_nvenc"),
        QStringLiteral("libx264"),
        QStringLiteral("libx265"),
    };
}

QString encoderDisplayName(const QString& encoder)
{
    if (encoder == QLatin1String("hevc_nvenc")) {
        return QObject::tr("HEVC (NVENC, GPU) - recommended");
    }
    if (encoder == QLatin1String("h264_nvenc")) {
        return QObject::tr("H.264 (NVENC, GPU) - most compatible");
    }
    if (encoder == QLatin1String("libx264")) {
        return QObject::tr("H.264 (libx264, CPU) - slow, higher quality");
    }
    if (encoder == QLatin1String("libx265")) {
        return QObject::tr("HEVC (libx265, CPU) - very slow");
    }
    return encoder;
}

QString buildPostFilterChain(const JobSpec& spec)
{
    QStringList filters;

    if (!spec.sharpenFilter.isEmpty()) {
        const int s = std::clamp(spec.sharpenStrength, 1, 5);
        if (spec.sharpenFilter == QLatin1String("cas")) {
            // Contrast Adaptive Sharpening: sharpens flat detail without the
            // halos unsharp masking leaves on high-contrast edges.
            filters << QStringLiteral("cas=strength=%1")
                           .arg(QString::number(0.15 * s, 'f', 2));
        } else if (spec.sharpenFilter == QLatin1String("unsharp")) {
            filters << QStringLiteral("unsharp=5:5:%1:5:5:0.0")
                           .arg(QString::number(0.3 * s, 'f', 2));
        }
    }

    if (spec.brightness != 0 || spec.contrast != 0 || spec.saturation != 0) {
        // eq takes brightness -1..1, contrast and saturation around 1.0.
        filters << QStringLiteral("eq=brightness=%1:contrast=%2:saturation=%3")
                       .arg(QString::number(spec.brightness / 100.0, 'f', 3),
                            QString::number(1.0 + spec.contrast / 100.0, 'f', 3),
                            QString::number(1.0 + spec.saturation / 100.0, 'f', 3));
    }

    return filters.join(QLatin1Char(','));
}

EncodePlan buildEncodePlan(const JobSpec& spec, const MediaInfo& info, int width, int height,
                           const QString& outputPath)
{
    EncodePlan plan;

    plan.arguments << QStringLiteral("-hide_banner") << QStringLiteral("-loglevel")
                   << QStringLiteral("error") << QStringLiteral("-y");

    // Input 0: our raw frames. Interpolation multiplies the numerator so the
    // rate stays rational -- 30000/1001 at 2x becomes 60000/1001, not 59.94.
    const int multiplier =
        spec.interpolationEnabled ? std::clamp(spec.fpsMultiplier, 1, 4) : 1;

    plan.arguments << QStringLiteral("-f") << QStringLiteral("rawvideo")
                   << QStringLiteral("-pix_fmt") << QStringLiteral("rgb24")
                   << QStringLiteral("-s") << QStringLiteral("%1x%2").arg(width).arg(height)
                   << QStringLiteral("-r")
                   << QStringLiteral("%1/%2").arg(info.fpsNum * multiplier).arg(info.fpsDen)
                   << QStringLiteral("-i") << QStringLiteral("-");

    // Input 1: the original, for audio/subtitles.
    plan.arguments << QStringLiteral("-i") << spec.inputPath;

    plan.arguments << QStringLiteral("-map") << QStringLiteral("0:v:0");

    // The trailing '?' makes the stream optional. Without it a silent input
    // file fails the whole render.
    if (info.hasAudio) {
        plan.arguments << QStringLiteral("-map") << QStringLiteral("1:a?");
    }
    const bool mkv = spec.container.compare(QLatin1String("mkv"), Qt::CaseInsensitive) == 0;
    if (mkv && info.hasSubtitles) {
        plan.arguments << QStringLiteral("-map") << QStringLiteral("1:s?");
    }

    const QString postFilters = buildPostFilterChain(spec);
    if (!postFilters.isEmpty()) {
        plan.arguments << QStringLiteral("-vf") << postFilters;
    }

    plan.arguments << QStringLiteral("-c:v") << spec.encoder;

    const bool nvenc = spec.encoder.endsWith(QLatin1String("_nvenc"));
    if (nvenc) {
        plan.arguments << QStringLiteral("-preset") << QStringLiteral("p5")
                       << QStringLiteral("-rc") << QStringLiteral("vbr")
                       << QStringLiteral("-cq") << QString::number(spec.quality)
                       << QStringLiteral("-b:v") << QStringLiteral("0");
    } else {
        plan.arguments << QStringLiteral("-preset") << QStringLiteral("medium")
                       << QStringLiteral("-crf") << QString::number(spec.quality);
    }

    if (info.hasAudio) {
        plan.arguments << QStringLiteral("-c:a") << QStringLiteral("copy");
    }
    if (mkv && info.hasSubtitles) {
        plan.arguments << QStringLiteral("-c:s") << QStringLiteral("copy");
    }

    plan.arguments << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");

    // Carry colour metadata across explicitly. Losing it turns correct footage
    // washed out, and it gets reported as "your upscaler ruined my colours".
    if (!info.colorSpace.isEmpty() && info.colorSpace != QLatin1String("unknown")) {
        plan.arguments << QStringLiteral("-colorspace") << info.colorSpace;
    }
    if (!info.colorPrimaries.isEmpty() && info.colorPrimaries != QLatin1String("unknown")) {
        plan.arguments << QStringLiteral("-color_primaries") << info.colorPrimaries;
    }
    if (!info.colorTransfer.isEmpty() && info.colorTransfer != QLatin1String("unknown")) {
        plan.arguments << QStringLiteral("-color_trc") << info.colorTransfer;
    }

    if (!mkv) {
        plan.arguments << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    }

    plan.arguments << outputPath;

    return plan;
}

} // namespace dfu
