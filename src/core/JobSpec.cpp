#include "core/JobSpec.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonDocument>

namespace dfu {

QString upscaleMethodKey(UpscaleMethod method)
{
    switch (method) {
    case UpscaleMethod::None:
        return QStringLiteral("none");
    case UpscaleMethod::FfmpegSpline:
        return QStringLiteral("ffmpeg-spline");
    case UpscaleMethod::RealEsrganNcnn:
        return QStringLiteral("realesrgan-ncnn");
    case UpscaleMethod::FfmpegLanczos:
        break;
    }
    return QStringLiteral("ffmpeg-lanczos");
}

UpscaleMethod upscaleMethodFromKey(const QString& key)
{
    if (key == QLatin1String("none")) {
        return UpscaleMethod::None;
    }
    if (key == QLatin1String("ffmpeg-spline")) {
        return UpscaleMethod::FfmpegSpline;
    }
    if (key == QLatin1String("realesrgan-ncnn")) {
        return UpscaleMethod::RealEsrganNcnn;
    }
    return UpscaleMethod::FfmpegLanczos;
}

QJsonObject JobSpec::toJson() const
{
    QJsonObject json;
    json[QStringLiteral("inputPath")] = inputPath;
    json[QStringLiteral("outputPath")] = outputPath;

    json[QStringLiteral("deinterlace")] = deinterlace;
    json[QStringLiteral("denoiseFilter")] = denoiseFilter;
    json[QStringLiteral("denoiseStrength")] = denoiseStrength;
    json[QStringLiteral("deblock")] = deblock;
    json[QStringLiteral("deband")] = deband;

    json[QStringLiteral("sharpenFilter")] = sharpenFilter;
    json[QStringLiteral("sharpenStrength")] = sharpenStrength;
    json[QStringLiteral("brightness")] = brightness;
    json[QStringLiteral("contrast")] = contrast;
    json[QStringLiteral("saturation")] = saturation;

    json[QStringLiteral("upscaleEnabled")] = upscaleEnabled;
    json[QStringLiteral("upscaleMethod")] = upscaleMethodKey(upscaleMethod);
    json[QStringLiteral("upscaleModel")] = upscaleModel;
    json[QStringLiteral("upscaleFactor")] = upscaleFactor;
    json[QStringLiteral("tileSize")] = tileSize;

    json[QStringLiteral("interpolationEnabled")] = interpolationEnabled;
    json[QStringLiteral("fpsMultiplier")] = fpsMultiplier;
    json[QStringLiteral("interpolationModel")] = interpolationModel;
    json[QStringLiteral("order")] =
        order == StageOrder::UpscaleFirst ? QStringLiteral("upscale-first")
                                          : QStringLiteral("interpolate-first");

    json[QStringLiteral("encoder")] = encoder;
    json[QStringLiteral("quality")] = quality;
    json[QStringLiteral("container")] = container;
    json[QStringLiteral("chunkSeconds")] = chunkSeconds;

    return json;
}

JobSpec JobSpec::fromJson(const QJsonObject& json)
{
    JobSpec spec;
    spec.inputPath = json.value(QStringLiteral("inputPath")).toString();
    spec.outputPath = json.value(QStringLiteral("outputPath")).toString();

    spec.deinterlace = json.value(QStringLiteral("deinterlace")).toBool(false);
    spec.denoiseFilter = json.value(QStringLiteral("denoiseFilter")).toString();
    spec.denoiseStrength = json.value(QStringLiteral("denoiseStrength")).toInt(2);
    spec.deblock = json.value(QStringLiteral("deblock")).toBool(false);
    spec.deband = json.value(QStringLiteral("deband")).toBool(false);

    spec.sharpenFilter = json.value(QStringLiteral("sharpenFilter")).toString();
    spec.sharpenStrength = json.value(QStringLiteral("sharpenStrength")).toInt(2);
    spec.brightness = json.value(QStringLiteral("brightness")).toInt(0);
    spec.contrast = json.value(QStringLiteral("contrast")).toInt(0);
    spec.saturation = json.value(QStringLiteral("saturation")).toInt(0);

    spec.upscaleEnabled = json.value(QStringLiteral("upscaleEnabled")).toBool(true);
    spec.upscaleMethod =
        upscaleMethodFromKey(json.value(QStringLiteral("upscaleMethod")).toString());
    spec.upscaleModel = json.value(QStringLiteral("upscaleModel"))
                            .toString(QStringLiteral("realesr-animevideov3"));
    spec.upscaleFactor = json.value(QStringLiteral("upscaleFactor")).toInt(2);
    spec.tileSize = json.value(QStringLiteral("tileSize")).toInt(0);

    spec.interpolationEnabled = json.value(QStringLiteral("interpolationEnabled")).toBool(false);
    spec.fpsMultiplier = json.value(QStringLiteral("fpsMultiplier")).toInt(2);
    spec.interpolationModel =
        json.value(QStringLiteral("interpolationModel")).toString(QStringLiteral("rife-v4.6"));
    spec.order = json.value(QStringLiteral("order")).toString() == QLatin1String("interpolate-first")
                     ? StageOrder::InterpolateFirst
                     : StageOrder::UpscaleFirst;

    spec.encoder = json.value(QStringLiteral("encoder")).toString(QStringLiteral("hevc_nvenc"));
    spec.quality = json.value(QStringLiteral("quality")).toInt(20);
    spec.container = json.value(QStringLiteral("container")).toString(QStringLiteral("mp4"));
    spec.chunkSeconds = json.value(QStringLiteral("chunkSeconds")).toInt(60);

    return spec;
}

QString JobSpec::specHash() const
{
    // Deliberately excludes outputPath: writing the same job somewhere else is
    // not a reason to discard completed work.
    QJsonObject json = toJson();
    json.remove(QStringLiteral("outputPath"));

    const QByteArray payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(
        QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex().left(16));
}

QString JobSpec::suggestedOutputPath() const
{
    if (!outputPath.isEmpty()) {
        return outputPath;
    }
    if (inputPath.isEmpty()) {
        return QString();
    }

    const QFileInfo info(inputPath);
    QString suffix = QStringLiteral("_upscaled");
    if (upscaleEnabled && upscaleFactor > 1) {
        suffix = QStringLiteral("_x%1").arg(upscaleFactor);
    }

    return info.absolutePath() + QLatin1Char('/') + info.completeBaseName() + suffix
           + QLatin1Char('.') + container;
}

} // namespace dfu
