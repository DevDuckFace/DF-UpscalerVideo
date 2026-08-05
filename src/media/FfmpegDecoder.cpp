#include "media/FfmpegDecoder.h"

namespace {

// Maps a 1..5 strength dial onto each filter's own parameter space.
QString denoiseExpression(const QString& filter, int strength)
{
    const int s = std::clamp(strength, 1, 5);

    if (filter == QLatin1String("hqdn3d")) {
        // luma_spatial:chroma_spatial:luma_tmp:chroma_tmp
        const double luma = 1.0 * s;
        const double chroma = 0.75 * s;
        return QStringLiteral("hqdn3d=%1:%2:%3:%4")
            .arg(QString::number(luma, 'f', 1), QString::number(chroma, 'f', 1),
                 QString::number(luma * 1.5, 'f', 1), QString::number(chroma * 1.5, 'f', 1));
    }
    if (filter == QLatin1String("nlmeans")) {
        // Very slow on this CPU, but it is the quality option.
        return QStringLiteral("nlmeans=s=%1").arg(QString::number(1.0 * s, 'f', 1));
    }
    if (filter == QLatin1String("atadenoise")) {
        return QStringLiteral("atadenoise=s=%1").arg(std::min(9, 3 + s * 2 / 2));
    }
    if (filter == QLatin1String("vaguedenoiser")) {
        return QStringLiteral("vaguedenoiser=threshold=%1").arg(s * 2);
    }

    return QString();
}

} // namespace

namespace dfu {

DecodePlan buildDecodePlan(const JobSpec& spec, const MediaInfo& info, bool scaleInFfmpeg)
{
    DecodePlan plan;

    QStringList filters;

    if (spec.deinterlace || info.isInterlaced) {
        filters << QStringLiteral("bwdif=mode=send_frame");
    }

    // Deblocking first: removing compression blocks before denoising stops the
    // denoiser from treating block edges as detail worth preserving.
    if (spec.deblock) {
        filters << QStringLiteral("deblock=filter=weak:block=4");
    }

    if (!spec.denoiseFilter.isEmpty()) {
        const QString expression = denoiseExpression(spec.denoiseFilter, spec.denoiseStrength);
        if (!expression.isEmpty()) {
            filters << expression;
        }
    }

    if (spec.deband) {
        filters << QStringLiteral("deband");
    }

    plan.outputWidth = info.width;
    plan.outputHeight = info.height;

    if (spec.upscaleEnabled && spec.upscaleFactor > 1 && scaleInFfmpeg) {
        // Keep both dimensions even: yuv420p needs it, and an odd dimension
        // makes the encoder fail late instead of here.
        plan.outputWidth = (info.width * spec.upscaleFactor) & ~1;
        plan.outputHeight = (info.height * spec.upscaleFactor) & ~1;

        const QString algorithm = spec.upscaleMethod == UpscaleMethod::FfmpegSpline
                                      ? QStringLiteral("spline")
                                      : QStringLiteral("lanczos");
        filters << QStringLiteral("scale=%1:%2:flags=%3")
                       .arg(plan.outputWidth)
                       .arg(plan.outputHeight)
                       .arg(algorithm);
    }

    plan.filterChain = filters.join(QLatin1Char(','));

    plan.arguments << QStringLiteral("-hide_banner") << QStringLiteral("-loglevel")
                   << QStringLiteral("error");

    // A variable frame rate source must be normalised before anything works on
    // individual frames, otherwise output timing is garbage.
    if (info.isVariableFrameRate) {
        plan.arguments << QStringLiteral("-fps_mode") << QStringLiteral("cfr")
                       << QStringLiteral("-r")
                       << QStringLiteral("%1/%2").arg(info.fpsNum).arg(info.fpsDen);
    }

    plan.arguments << QStringLiteral("-i") << spec.inputPath;

    if (!plan.filterChain.isEmpty()) {
        plan.arguments << QStringLiteral("-vf") << plan.filterChain;
    }

    plan.arguments << QStringLiteral("-map") << QStringLiteral("0:v:0")
                   << QStringLiteral("-f") << QStringLiteral("rawvideo")
                   << QStringLiteral("-pix_fmt") << QStringLiteral("rgb24")
                   << QStringLiteral("-");

    return plan;
}

} // namespace dfu
