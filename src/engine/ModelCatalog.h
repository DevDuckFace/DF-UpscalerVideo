#pragma once

#include <QList>
#include <QString>

namespace dfu {

struct UpscaleModelInfo
{
    // Base name as passed to ProcessorConfig::modelName.
    QString name;
    // Scale factors the weights actually exist for. Models that ship a single
    // file report the factor parsed from their name, or 4 when it says nothing.
    QList<int> scales;
    // Recognised names get a friendlier label and a purpose hint.
    QString displayName;
    QString description;
};

// Scans <appdir>/models for ncnn upscaling weights (.param + .bin pairs).
//
// Any ESRGAN-architecture model converted to ncnn works without code changes,
// because the blob names are read out of the .param at load time. Dropping a
// pair into that folder is all it takes to add one.
QList<UpscaleModelInfo> discoverUpscaleModels();

// Scans <appdir>/models for interpolation models: subdirectories holding a
// flownet.param / flownet.bin pair.
QStringList discoverInterpolationModels();

// Absolute path of the models directory, resolved from applicationDirPath().
QString modelsDirectory();

} // namespace dfu
