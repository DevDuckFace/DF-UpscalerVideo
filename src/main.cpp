#include "core/AppInfo.h"
#include "core/JobSpec.h"
#include "core/Logging.h"
#include "engine/ModelCatalog.h"
#include "engine/VulkanContext.h"
#include "media/FfmpegPaths.h"
#include "pipeline/JobRunner.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QIcon>
#include <QString>
#include <QStringList>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <memory>

#if defined(Q_OS_WIN) && !defined(DFU_CONSOLE_BUILD)
#    include <windows.h>
#    define DFU_NEEDS_CONSOLE_ATTACH 1
#endif

namespace {

// Exit codes. Anything the shell sees is defined here and nowhere else.
constexpr int kExitOk = 0;
constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;

// The console companion binary is headless by definition; the GUI binary
// decides from the command line.
#ifdef DFU_CONSOLE_BUILD
constexpr bool kAlwaysHeadless = true;
#else
constexpr bool kAlwaysHeadless = false;
#endif

// The GUI/CLI decision has to be made before the application object exists,
// because QApplication and QCoreApplication are different types. Scanning argv
// directly is the only thing available this early.
bool hasCliFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--cli") == 0) {
            return true;
        }
    }
    return false;
}

#ifdef DFU_NEEDS_CONSOLE_ATTACH
bool standardStreamIsAttached(DWORD stdHandle)
{
    const HANDLE handle = GetStdHandle(stdHandle);
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

// The executable is linked as a GUI subsystem binary so launching it from
// Explorer does not flash a console. In CLI mode that means stdout is not
// connected to anything, so reattach to the parent shell's console.
//
// Streams the caller already redirected are left alone: pointing them at
// CONOUT$ would send output to the console window instead of the pipe or file
// the caller asked for, which breaks `app --cli ... > out.txt`.
void attachParentConsole()
{
    const bool hasStdout = standardStreamIsAttached(STD_OUTPUT_HANDLE);
    const bool hasStderr = standardStreamIsAttached(STD_ERROR_HANDLE);
    if (hasStdout && hasStderr) {
        return;
    }

    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        return;
    }

    FILE* stream = nullptr;
    if (!hasStdout) {
        freopen_s(&stream, "CONOUT$", "w", stdout);
    }
    if (!hasStderr) {
        freopen_s(&stream, "CONOUT$", "w", stderr);
    }
}
#endif

struct CliOptions
{
    QCommandLineOption cli{QStringLiteral("cli"),
                           QCoreApplication::translate("main", "Run without a GUI.")};
    QCommandLineOption input{
        QStringList{QStringLiteral("i"), QStringLiteral("input")},
        QCoreApplication::translate("main", "Source video file."),
        QCoreApplication::translate("main", "path")};
    QCommandLineOption output{
        QStringList{QStringLiteral("o"), QStringLiteral("output")},
        QCoreApplication::translate("main", "Destination video file."),
        QCoreApplication::translate("main", "path")};
    QCommandLineOption logLevel{
        QStringLiteral("log-level"),
        QCoreApplication::translate(
            "main", "Console log level: trace, debug, info, warn, error. Default: info."),
        QCoreApplication::translate("main", "level"),
        QStringLiteral("info")};
    QCommandLineOption scale{
        QStringLiteral("scale"),
        QCoreApplication::translate("main", "Upscale factor: 1, 2, 3 or 4. Default: 2."),
        QCoreApplication::translate("main", "factor"), QStringLiteral("2")};
    QCommandLineOption denoise{
        QStringLiteral("denoise"),
        QCoreApplication::translate(
            "main", "Denoise filter: none, hqdn3d, atadenoise, nlmeans. Default: none."),
        QCoreApplication::translate("main", "filter"), QStringLiteral("none")};
    QCommandLineOption denoiseStrength{
        QStringLiteral("denoise-strength"),
        QCoreApplication::translate("main", "Denoise strength, 1 to 5. Default: 2."),
        QCoreApplication::translate("main", "n"), QStringLiteral("2")};
    QCommandLineOption encoder{
        QStringLiteral("encoder"),
        QCoreApplication::translate(
            "main", "Encoder: hevc_nvenc, h264_nvenc, libx264, libx265. Default: hevc_nvenc."),
        QCoreApplication::translate("main", "name"), QStringLiteral("hevc_nvenc")};
    QCommandLineOption quality{
        QStringLiteral("quality"),
        QCoreApplication::translate("main", "CQ/CRF quality, lower is better. Default: 20."),
        QCoreApplication::translate("main", "n"), QStringLiteral("20")};
    QCommandLineOption deinterlace{
        QStringLiteral("deinterlace"),
        QCoreApplication::translate("main", "Force deinterlacing (bwdif).")};
    QCommandLineOption method{
        QStringLiteral("method"),
        QCoreApplication::translate(
            "main", "Upscale method: ai, lanczos, spline. Default: ai when a Vulkan GPU is "
                    "present, otherwise lanczos."),
        QCoreApplication::translate("main", "name")};
    QCommandLineOption sharpen{
        QStringLiteral("sharpen"),
        QCoreApplication::translate("main", "Sharpen after upscaling: none, cas, unsharp."),
        QCoreApplication::translate("main", "filter"), QStringLiteral("none")};
    QCommandLineOption sharpenStrength{
        QStringLiteral("sharpen-strength"),
        QCoreApplication::translate("main", "Sharpen amount, 1 to 5. Default: 2."),
        QCoreApplication::translate("main", "n"), QStringLiteral("2")};
    QCommandLineOption deblock{
        QStringLiteral("deblock"),
        QCoreApplication::translate("main", "Remove compression block artefacts.")};
    QCommandLineOption deband{QStringLiteral("deband"),
                              QCoreApplication::translate("main", "Smooth banded gradients.")};
    QCommandLineOption fps{
        QStringLiteral("fps-multiplier"),
        QCoreApplication::translate(
            "main", "Frame interpolation with RIFE: 1 (off), 2, 3 or 4. Default: 1."),
        QCoreApplication::translate("main", "n"), QStringLiteral("1")};
    QCommandLineOption model{
        QStringLiteral("model"),
        QCoreApplication::translate(
            "main", "AI model: realesr-animevideov3, realesrgan-x4plus, realesrgan-x4plus-anime."),
        QCoreApplication::translate("main", "name"),
        QStringLiteral("realesr-animevideov3")};
};

spdlog::level::level_enum parseLevel(const QString& name, bool& ok)
{
    ok = true;
    const QString lowered = name.toLower();
    if (lowered == QLatin1String("trace")) {
        return spdlog::level::trace;
    }
    if (lowered == QLatin1String("debug")) {
        return spdlog::level::debug;
    }
    if (lowered == QLatin1String("info")) {
        return spdlog::level::info;
    }
    if (lowered == QLatin1String("warn") || lowered == QLatin1String("warning")) {
        return spdlog::level::warn;
    }
    if (lowered == QLatin1String("error") || lowered == QLatin1String("err")) {
        return spdlog::level::err;
    }
    ok = false;
    return spdlog::level::info;
}

int runCli(const QCommandLineParser& parser, const CliOptions& options)
{
    const QString input = parser.value(options.input);
    const QString output = parser.value(options.output);

    if (input.isEmpty() || output.isEmpty()) {
        spdlog::error("--cli requires both --input and --output");
        return kExitUsage;
    }

    const QFileInfo inputInfo(input);
    if (!inputInfo.exists() || !inputInfo.isFile()) {
        spdlog::error("Input file does not exist: {}", input.toStdString());
        return kExitUsage;
    }

    const dfu::FfmpegPaths paths = dfu::locateFfmpeg();
    if (!paths.valid) {
        spdlog::error("{}", paths.error.toStdString());
        return kExitFailure;
    }

    dfu::JobSpec spec;
    spec.inputPath = inputInfo.absoluteFilePath();
    spec.outputPath = QFileInfo(output).absoluteFilePath();
    spec.container = QFileInfo(output).suffix().toLower();
    if (spec.container.isEmpty()) {
        spec.container = QStringLiteral("mp4");
    }

    const int factor = parser.value(options.scale).toInt();
    spec.upscaleFactor = std::clamp(factor <= 0 ? 2 : factor, 1, 4);

    const QString denoise = parser.value(options.denoise).toLower();
    spec.denoiseFilter = denoise == QLatin1String("none") ? QString() : denoise;
    spec.denoiseStrength = std::clamp(parser.value(options.denoiseStrength).toInt(), 1, 5);

    spec.encoder = parser.value(options.encoder);
    spec.quality = std::clamp(parser.value(options.quality).toInt(), 0, 51);
    spec.deinterlace = parser.isSet(options.deinterlace);
    spec.upscaleModel = parser.value(options.model);

    const QString sharpen = parser.value(options.sharpen).toLower();
    spec.sharpenFilter = sharpen == QLatin1String("none") ? QString() : sharpen;
    spec.sharpenStrength = std::clamp(parser.value(options.sharpenStrength).toInt(), 1, 5);
    spec.deblock = parser.isSet(options.deblock);
    spec.deband = parser.isSet(options.deband);

    const int multiplier = std::clamp(parser.value(options.fps).toInt(), 1, 4);
    spec.interpolationEnabled = multiplier > 1;
    spec.fpsMultiplier = multiplier;

    const QString method = parser.value(options.method).toLower();
    if (method.isEmpty()) {
        spec.upscaleMethod = dfu::VulkanContext::vulkanAvailable()
                                 ? dfu::UpscaleMethod::RealEsrganNcnn
                                 : dfu::UpscaleMethod::FfmpegLanczos;
    } else if (method == QLatin1String("ai") || method == QLatin1String("realesrgan")) {
        spec.upscaleMethod = dfu::UpscaleMethod::RealEsrganNcnn;
    } else if (method == QLatin1String("spline")) {
        spec.upscaleMethod = dfu::UpscaleMethod::FfmpegSpline;
    } else {
        spec.upscaleMethod = dfu::UpscaleMethod::FfmpegLanczos;
    }

    if (spec.upscaleMethod == dfu::UpscaleMethod::RealEsrganNcnn) {
        // Constrain the factor to the scales the chosen model actually ships,
        // rather than guessing from its name. A 1x model is a restoration
        // pass: the AI stage still runs, it just does not change the size.
        QList<int> scales;
        const QList<dfu::UpscaleModelInfo> models = dfu::discoverUpscaleModels();
        for (const dfu::UpscaleModelInfo& info : models) {
            if (info.name == spec.upscaleModel) {
                scales = info.scales;
                break;
            }
        }

        if (scales.isEmpty()) {
            spdlog::error("Model '{}' was not found in {}", spec.upscaleModel.toStdString(),
                          dfu::modelsDirectory().toStdString());
            return kExitUsage;
        }
        if (!scales.contains(spec.upscaleFactor)) {
            spdlog::warn("Model '{}' has no x{} weights; using x{} instead",
                         spec.upscaleModel.toStdString(), spec.upscaleFactor, scales.first());
            spec.upscaleFactor = scales.first();
        }
        spec.upscaleEnabled = true;
    } else {
        spec.upscaleEnabled = spec.upscaleFactor > 1;
    }

    dfu::JobRunner runner;
    int exitCode = kExitOk;

    // Direct connections: run() is synchronous on this very thread, so there
    // is no event loop to deliver queued signals.
    QObject::connect(&runner, &dfu::JobRunner::progress,
                     [](qint64 done, qint64 total, double fps, qint64 eta) {
                         if (total > 0) {
                             std::fprintf(stderr, "\r%lld/%lld frames  %.1f fps  eta %llds   ",
                                          static_cast<long long>(done),
                                          static_cast<long long>(total), fps,
                                          static_cast<long long>(eta));
                         } else {
                             std::fprintf(stderr, "\r%lld frames  %.1f fps   ",
                                          static_cast<long long>(done), fps);
                         }
                         std::fflush(stderr);
                     });

    QObject::connect(&runner, &dfu::JobRunner::finished,
                     [&exitCode](bool success, const QString& message, const QString& outputPath) {
                         std::fprintf(stderr, "\n");
                         if (success) {
                             spdlog::info("Wrote {}", outputPath.toStdString());
                         } else {
                             spdlog::error("{}", message.toStdString());
                             exitCode = kExitFailure;
                         }
                     });

    runner.run(spec, paths);
    return exitCode;
}

} // namespace

int main(int argc, char* argv[])
{
    const bool cliMode = kAlwaysHeadless || hasCliFlag(argc, argv);

#ifdef DFU_NEEDS_CONSOLE_ATTACH
    if (cliMode) {
        attachParentConsole();
    }
#endif

    std::unique_ptr<QCoreApplication> app;
    if (cliMode) {
        app = std::make_unique<QCoreApplication>(argc, argv);
    } else {
        app = std::make_unique<QApplication>(argc, argv);
    }

    QCoreApplication::setApplicationName(dfu::appName());
    QCoreApplication::setApplicationVersion(dfu::appVersion());
    QCoreApplication::setOrganizationName(QString::fromLatin1(dfu::kOrgName));
    QCoreApplication::setOrganizationDomain(QString::fromLatin1(dfu::kOrgDomain));

    CliOptions options;

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::translate("main", "GPU video upscaling, frame interpolation and "
                                            "restoration."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("files"),
        QCoreApplication::translate("main", "Video files to add to the queue."),
        QStringLiteral("[files...]"));
    parser.addOption(options.cli);
    parser.addOption(options.input);
    parser.addOption(options.output);
    parser.addOption(options.logLevel);
    parser.addOption(options.scale);
    parser.addOption(options.denoise);
    parser.addOption(options.denoiseStrength);
    parser.addOption(options.encoder);
    parser.addOption(options.quality);
    parser.addOption(options.deinterlace);
    parser.addOption(options.method);
    parser.addOption(options.model);
    parser.addOption(options.sharpen);
    parser.addOption(options.sharpenStrength);
    parser.addOption(options.deblock);
    parser.addOption(options.deband);
    parser.addOption(options.fps);
    parser.process(*app);

    bool levelOk = false;
    const spdlog::level::level_enum consoleLevel = parseLevel(parser.value(options.logLevel), levelOk);

    dfu::LogOptions logOptions;
    logOptions.consoleLevel = consoleLevel;
    const dfu::LogContext log = dfu::initLogging(logOptions);

    if (!levelOk) {
        spdlog::warn("Unrecognised --log-level '{}', falling back to info",
                     parser.value(options.logLevel).toStdString());
    }

    int result = kExitOk;

    if (cliMode) {
        result = runCli(parser, options);
    } else {
        QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.ico")));
        dfu::applyThemePreference(dfu::loadThemePreference());

        dfu::MainWindow window(log);
        window.show();

        // Files named on the command line, so the executable works as an
        // "Open with" target and accepts a drop onto its icon.
        const QStringList files = parser.positionalArguments();
        if (!files.isEmpty()) {
            window.addPaths(files);
        }

        result = app->exec();
    }

    dfu::shutdownLogging();
    return result;
}
