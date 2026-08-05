#include "media/MediaProbe.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStringList>

#include <spdlog/spdlog.h>

#include <cmath>

namespace {

// "30000/1001" -> {30000, 1001}. Returns false for "0/0" and malformed input.
bool parseRational(const QString& text, int& num, int& den)
{
    const qsizetype slash = text.indexOf(QLatin1Char('/'));
    if (slash <= 0) {
        bool ok = false;
        const double value = text.toDouble(&ok);
        if (!ok || value <= 0.0) {
            return false;
        }
        num = static_cast<int>(std::lround(value * 1000.0));
        den = 1000;
        return true;
    }

    bool okNum = false;
    bool okDen = false;
    const int n = text.left(slash).toInt(&okNum);
    const int d = text.mid(slash + 1).toInt(&okDen);
    if (!okNum || !okDen || n <= 0 || d <= 0) {
        return false;
    }
    num = n;
    den = d;
    return true;
}

} // namespace

namespace dfu {

QString MediaInfo::resolutionString() const
{
    return QStringLiteral("%1x%2").arg(width).arg(height);
}

QString MediaInfo::fpsString() const
{
    if (fpsDen <= 0) {
        return QStringLiteral("-");
    }
    const double value = fps();
    // Show the exact rational for the NTSC rates people actually care about.
    if (fpsDen != 1) {
        return QStringLiteral("%1 (%2/%3)")
            .arg(QString::number(value, 'f', 3), QString::number(fpsNum), QString::number(fpsDen));
    }
    return QString::number(value, 'g', 6);
}

MediaInfo probeMedia(const QString& ffprobePath, const QString& inputPath, int timeoutMs)
{
    MediaInfo info;

    QProcess process;
    const QStringList args{
        QStringLiteral("-v"),           QStringLiteral("quiet"),
        QStringLiteral("-print_format"), QStringLiteral("json"),
        QStringLiteral("-show_streams"), QStringLiteral("-show_format"),
        inputPath};

    process.start(ffprobePath, args);
    if (!process.waitForStarted(5000)) {
        info.error = QObject::tr("Could not start ffprobe: %1").arg(process.errorString());
        return info;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
        info.error = QObject::tr("ffprobe timed out while reading the file.");
        return info;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        info.error = QObject::tr("ffprobe failed (exit code %1).").arg(process.exitCode());
        return info;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(process.readAllStandardOutput(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        info.error = QObject::tr("ffprobe output could not be parsed: %1")
                         .arg(parseError.errorString());
        return info;
    }

    const QJsonObject root = document.object();
    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();

    bool videoFound = false;
    for (const QJsonValue& value : streams) {
        const QJsonObject stream = value.toObject();
        const QString type = stream.value(QStringLiteral("codec_type")).toString();

        if (type == QLatin1String("audio")) {
            info.hasAudio = true;
            continue;
        }
        if (type == QLatin1String("subtitle")) {
            info.hasSubtitles = true;
            continue;
        }
        if (type != QLatin1String("video") || videoFound) {
            continue;
        }

        videoFound = true;
        info.width = stream.value(QStringLiteral("width")).toInt();
        info.height = stream.value(QStringLiteral("height")).toInt();
        info.videoCodec = stream.value(QStringLiteral("codec_name")).toString();
        info.pixelFormat = stream.value(QStringLiteral("pix_fmt")).toString();
        info.colorSpace = stream.value(QStringLiteral("color_space")).toString();
        info.colorTransfer = stream.value(QStringLiteral("color_transfer")).toString();
        info.colorPrimaries = stream.value(QStringLiteral("color_primaries")).toString();

        const QString fieldOrder = stream.value(QStringLiteral("field_order")).toString();
        info.isInterlaced = !fieldOrder.isEmpty() && fieldOrder != QLatin1String("progressive");

        // r_frame_rate is the container's nominal rate; avg_frame_rate is what
        // the frames actually average. A meaningful gap between them means the
        // source is variable frame rate and needs CFR normalisation before any
        // frame-level processing.
        int rNum = 0;
        int rDen = 1;
        int aNum = 0;
        int aDen = 1;
        const bool haveR =
            parseRational(stream.value(QStringLiteral("r_frame_rate")).toString(), rNum, rDen);
        const bool haveA =
            parseRational(stream.value(QStringLiteral("avg_frame_rate")).toString(), aNum, aDen);

        if (haveR) {
            info.fpsNum = rNum;
            info.fpsDen = rDen;
        } else if (haveA) {
            info.fpsNum = aNum;
            info.fpsDen = aDen;
        }

        if (haveR && haveA) {
            const double r = static_cast<double>(rNum) / rDen;
            const double a = static_cast<double>(aNum) / aDen;
            if (a > 0.0 && std::abs(r - a) / std::max(r, a) > 0.01) {
                info.isVariableFrameRate = true;
                // Render at the average rate: it is the honest one.
                info.fpsNum = aNum;
                info.fpsDen = aDen;
            }
        }

        const QString frames = stream.value(QStringLiteral("nb_frames")).toString();
        bool okFrames = false;
        const int64_t parsed = frames.toLongLong(&okFrames);
        if (okFrames && parsed > 0) {
            info.frameCount = parsed;
        }
    }

    if (!videoFound) {
        info.error = QObject::tr("The file contains no video stream.");
        return info;
    }

    const QJsonObject format = root.value(QStringLiteral("format")).toObject();
    bool okDuration = false;
    const double duration = format.value(QStringLiteral("duration")).toString().toDouble(&okDuration);
    if (okDuration) {
        info.durationSeconds = duration;
    }

    if (info.frameCount <= 0 && info.durationSeconds > 0.0 && info.fps() > 0.0) {
        info.frameCount = static_cast<int64_t>(std::llround(info.durationSeconds * info.fps()));
        info.frameCountEstimated = true;
    }

    if (info.width <= 0 || info.height <= 0) {
        info.error = QObject::tr("The video stream reports no usable dimensions.");
        return info;
    }

    info.valid = true;

    spdlog::info("Probe: {}x{} {} fps={}/{} frames={}{} audio={} vfr={} interlaced={}", info.width,
                 info.height, info.videoCodec.toStdString(), info.fpsNum, info.fpsDen,
                 info.frameCount, info.frameCountEstimated ? " (estimated)" : "", info.hasAudio,
                 info.isVariableFrameRate, info.isInterlaced);

    return info;
}

} // namespace dfu
