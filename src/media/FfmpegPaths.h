#pragma once

#include <QMetaType>
#include <QString>

namespace dfu {

// Resolves the bundled FFmpeg binaries. Everything is relative to
// QCoreApplication::applicationDirPath() -- no absolute paths are baked in.
struct FfmpegPaths
{
    QString ffmpeg;
    QString ffprobe;
    bool valid = false;
    QString error;
};

// Search order: <appdir>/bin, <appdir>, then PATH.
FfmpegPaths locateFfmpeg();

} // namespace dfu

Q_DECLARE_METATYPE(dfu::FfmpegPaths)
