#include "media/FfmpegPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <spdlog/spdlog.h>

namespace {

#ifdef Q_OS_WIN
constexpr char kFfmpegName[] = "ffmpeg.exe";
constexpr char kFfprobeName[] = "ffprobe.exe";
#else
constexpr char kFfmpegName[] = "ffmpeg";
constexpr char kFfprobeName[] = "ffprobe";
#endif

QString findIn(const QString& dir, const QString& fileName)
{
    const QFileInfo info(QDir(dir).filePath(fileName));
    return info.exists() && info.isFile() ? info.absoluteFilePath() : QString();
}

} // namespace

namespace dfu {

FfmpegPaths locateFfmpeg()
{
    FfmpegPaths paths;

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates{QDir(appDir).filePath(QStringLiteral("bin")), appDir};

    const QString ffmpegName = QString::fromLatin1(kFfmpegName);
    const QString ffprobeName = QString::fromLatin1(kFfprobeName);

    for (const QString& dir : candidates) {
        if (paths.ffmpeg.isEmpty()) {
            paths.ffmpeg = findIn(dir, ffmpegName);
        }
        if (paths.ffprobe.isEmpty()) {
            paths.ffprobe = findIn(dir, ffprobeName);
        }
    }

    // Falling back to PATH keeps a developer build usable before anything has
    // been copied into bin/.
    if (paths.ffmpeg.isEmpty()) {
        paths.ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    }
    if (paths.ffprobe.isEmpty()) {
        paths.ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    }

    if (paths.ffmpeg.isEmpty() || paths.ffprobe.isEmpty()) {
        paths.valid = false;
        paths.error = QObject::tr("ffmpeg.exe and ffprobe.exe were not found. Expected them in "
                                  "\"%1\".")
                          .arg(QDir(appDir).filePath(QStringLiteral("bin")));
        spdlog::error("{}", paths.error.toStdString());
        return paths;
    }

    paths.valid = true;
    spdlog::info("ffmpeg : {}", paths.ffmpeg.toStdString());
    spdlog::info("ffprobe: {}", paths.ffprobe.toStdString());
    return paths;
}

} // namespace dfu
