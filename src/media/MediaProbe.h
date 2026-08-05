#pragma once

#include <QMetaType>
#include <QString>

#include <cstdint>

namespace dfu {

struct MediaInfo
{
    int width = 0;
    int height = 0;

    // Frame rate stays rational all the way through. 30000/1001 is not 29.97,
    // and rounding it produces audio drift that only shows up twenty minutes
    // into a render.
    int fpsNum = 0;
    int fpsDen = 1;

    int64_t frameCount = 0;
    bool frameCountEstimated = false;

    double durationSeconds = 0.0;

    QString videoCodec;
    QString pixelFormat;
    QString colorSpace;
    QString colorTransfer;
    QString colorPrimaries;

    bool hasAudio = false;
    bool hasSubtitles = false;
    bool isVariableFrameRate = false;
    bool isInterlaced = false;

    bool valid = false;
    QString error;

    double fps() const
    {
        return fpsDen > 0 ? static_cast<double>(fpsNum) / static_cast<double>(fpsDen) : 0.0;
    }

    QString resolutionString() const;
    QString fpsString() const;
};

// Runs ffprobe and parses the result. Blocking: never call it from the UI
// thread.
MediaInfo probeMedia(const QString& ffprobePath, const QString& inputPath,
                     int timeoutMs = 20000);

} // namespace dfu

Q_DECLARE_METATYPE(dfu::MediaInfo)
