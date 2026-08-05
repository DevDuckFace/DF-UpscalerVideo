#include "engine/ModelCatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>

#include <spdlog/spdlog.h>

#include <algorithm>

namespace dfu {
namespace {

struct KnownModel
{
    const char* name;
    const char* display;
    const char* description;
};

// Labels for the models shipped with the installer. Anything else is listed
// under its own file name and marked as user-supplied.
const KnownModel kKnownModels[] = {
    {"realesr-animevideov3", "Anime video v3 - fast",
     "Animation and cartoons. The fastest of the bundled models."},
    {"realesrgan-x4plus", "x4plus - general purpose (real footage)",
     "Live action and photographic content."},
    {"realesrgan-x4plus-anime", "x4plus anime - higher quality",
     "Anime stills. Slower than animevideov3, more detail."},
    {"realesr-general-x4v3", "general x4v3 - compact",
     "Small general-purpose model."},
};

const KnownModel* findKnown(const QString& name)
{
    for (const KnownModel& model : kKnownModels) {
        if (name == QLatin1String(model.name)) {
            return &model;
        }
    }
    return nullptr;
}

} // namespace

QString modelsDirectory()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("models"));
}

QList<UpscaleModelInfo> discoverUpscaleModels()
{
    QList<UpscaleModelInfo> models;

    const QDir dir(modelsDirectory());
    if (!dir.exists()) {
        spdlog::warn("Models directory not found: {}", dir.absolutePath().toStdString());
        return models;
    }

    // Families such as realesr-animevideov3 ship one file per scale factor:
    // "<base>-x2", "<base>-x3", "<base>-x4". Collapse those into one entry
    // carrying the scales that are actually present.
    static const QRegularExpression scaleSuffix(QStringLiteral("^(.*)-x([234])$"));

    QMap<QString, QList<int>> grouped;

    const QStringList paramFiles = dir.entryList({QStringLiteral("*.param")}, QDir::Files, QDir::Name);
    for (const QString& paramFile : paramFiles) {
        const QString stem = QFileInfo(paramFile).completeBaseName();

        // A .param without its .bin is unusable; skip rather than offer a
        // model that will fail at load time.
        if (!QFileInfo::exists(dir.filePath(stem + QStringLiteral(".bin")))) {
            spdlog::warn("Ignoring {}: the matching .bin file is missing", stem.toStdString());
            continue;
        }

        const QRegularExpressionMatch match = scaleSuffix.match(stem);
        if (match.hasMatch()) {
            grouped[match.captured(1)].append(match.captured(2).toInt());
            continue;
        }

        // A single-file model. Community models follow one of two naming
        // conventions -- a leading factor ("4x-UltraSharp", "1x-Archiver") or
        // a trailing one ("realesrgan-x4plus"). A leading "1x" means a
        // restoration model that returns the frame at its original size.
        static const QRegularExpression leadingScale(QStringLiteral("^([1234])[xX][-_]"));
        static const QRegularExpression embeddedScale(QStringLiteral("[xX]([1234])"));

        int scale = 4;
        const QRegularExpressionMatch leading = leadingScale.match(stem);
        if (leading.hasMatch()) {
            scale = leading.captured(1).toInt();
        } else {
            const QRegularExpressionMatch embedded = embeddedScale.match(stem);
            if (embedded.hasMatch()) {
                scale = embedded.captured(1).toInt();
            }
        }

        grouped[stem].append(scale);
    }

    for (auto it = grouped.constBegin(); it != grouped.constEnd(); ++it) {
        UpscaleModelInfo info;
        info.name = it.key();
        info.scales = it.value();
        std::sort(info.scales.begin(), info.scales.end());

        if (const KnownModel* known = findKnown(info.name)) {
            info.displayName = QCoreApplication::translate("ModelCatalog", known->display);
            info.description = QCoreApplication::translate("ModelCatalog", known->description);
        } else {
            info.displayName = info.name;
            info.description = QCoreApplication::translate(
                "ModelCatalog", "Custom model found in the models folder.");
        }

        models.append(info);
    }

    spdlog::info("Upscale models available: {}", models.size());
    return models;
}

QStringList discoverInterpolationModels()
{
    QStringList models;

    const QDir dir(modelsDirectory());
    if (!dir.exists()) {
        return models;
    }

    const QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& subdir : subdirs) {
        const QDir modelDir(dir.filePath(subdir));
        if (modelDir.exists(QStringLiteral("flownet.param"))
            && modelDir.exists(QStringLiteral("flownet.bin"))) {
            models.append(subdir);
        }
    }

    return models;
}

} // namespace dfu
