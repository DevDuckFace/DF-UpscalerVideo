#pragma once

#include <QJsonObject>
#include <QMetaType>
#include <QString>

namespace dfu {

// Upscaling backend. The FFmpeg tier always works and is the fallback the
// specification requires to stay reachable; the ncnn tier is selected once
// Real-ESRGAN is available.
enum class UpscaleMethod
{
    None,
    FfmpegLanczos,
    FfmpegSpline,
    RealEsrganNcnn
};

QString upscaleMethodKey(UpscaleMethod method);
UpscaleMethod upscaleMethodFromKey(const QString& key);

struct JobSpec
{
    QString inputPath;
    QString outputPath;

    // --- Restoration, applied before scaling -------------------------------
    bool deinterlace = false;
    QString denoiseFilter;   // "", "hqdn3d", "nlmeans", "atadenoise", "vaguedenoiser"
    int denoiseStrength = 2; // 1..5, mapped per filter
    bool deblock = false;    // compression block artefacts
    bool deband = false;

    // --- Enhancement, applied after scaling --------------------------------
    // Sharpening belongs after the upscaler: sharpening first would have the
    // network amplify the halos it introduces.
    QString sharpenFilter;  // "", "cas", "unsharp"
    int sharpenStrength = 2; // 1..5

    int brightness = 0; // -50..50
    int contrast = 0;   // -50..50
    int saturation = 0; // -50..50

    // Upscale
    bool upscaleEnabled = true;
    UpscaleMethod upscaleMethod = UpscaleMethod::FfmpegLanczos;
    QString upscaleModel = QStringLiteral("realesr-animevideov3");
    int upscaleFactor = 2; // 2 or 4
    int tileSize = 0;      // 0 = auto

    // --- Frame interpolation ------------------------------------------------
    bool interpolationEnabled = false;
    int fpsMultiplier = 2; // 2, 3 or 4
    QString interpolationModel = QStringLiteral("rife-v4.6");

    enum class StageOrder
    {
        UpscaleFirst,
        InterpolateFirst
    };
    StageOrder order = StageOrder::UpscaleFirst;

    // Encode
    QString encoder = QStringLiteral("hevc_nvenc");
    int quality = 20;
    QString container = QStringLiteral("mp4");

    int chunkSeconds = 60;

    QJsonObject toJson() const;
    static JobSpec fromJson(const QJsonObject& json);

    // Stable across runs; used to invalidate a resume manifest when the job
    // settings changed.
    QString specHash() const;

    // Derives an output path next to the input when none was chosen.
    QString suggestedOutputPath() const;
};

} // namespace dfu

Q_DECLARE_METATYPE(dfu::JobSpec)
