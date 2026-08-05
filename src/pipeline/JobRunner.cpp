#include "pipeline/JobRunner.h"

#include "core/FrameBuffer.h"
#include "core/FramePool.h"
#include "core/RingBuffer.h"
#include "engine/PassthroughProcessor.h"
#include "engine/RealEsrganNcnn.h"
#include "engine/RifeNcnn.h"
#include "engine/VulkanContext.h"
#include "media/FfmpegDecoder.h"
#include "media/FfmpegEncoder.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>
#include <QThread>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>

namespace {

// Runs a lambda on its own QThread. Each ffmpeg QProcess is created, used and
// destroyed entirely inside one thread, so there is no cross-thread affinity
// problem and no event loop is needed -- the synchronous waitFor* API is
// exactly what a pipe pump wants.
class FunctionThread : public QThread
{
public:
    explicit FunctionThread(std::function<void()> fn)
        : m_fn(std::move(fn))
    {
    }

protected:
    void run() override { m_fn(); }

private:
    std::function<void()> m_fn;
};

// ffmpeg reports the reason for any failure on stderr and nowhere else.
class StderrTail
{
public:
    void append(const QByteArray& chunk)
    {
        m_buffer += QString::fromLocal8Bit(chunk);
        const QStringList lines = m_buffer.split(QLatin1Char('\n'));
        m_buffer = lines.isEmpty() ? QString() : lines.last();

        for (qsizetype i = 0; i + 1 < lines.size(); ++i) {
            const QString line = lines.at(i).trimmed();
            if (line.isEmpty()) {
                continue;
            }
            m_lines.append(line);
            spdlog::debug("ffmpeg: {}", line.toStdString());
            while (m_lines.size() > kMaxLines) {
                m_lines.removeFirst();
            }
        }
    }

    QString text() const { return m_lines.join(QLatin1Char('\n')); }
    bool empty() const { return m_lines.isEmpty(); }

private:
    static constexpr qsizetype kMaxLines = 50;
    QStringList m_lines;
    QString m_buffer;
};

} // namespace

namespace dfu {

JobRunner::JobRunner(QObject* parent)
    : QObject(parent)
{
}

JobRunner::~JobRunner() = default;

void JobRunner::cancel()
{
    m_cancelled.store(true);

    // Release a paused job so it can observe the cancellation.
    m_paused.store(false);
    m_pauseCv.notify_all();

    std::lock_guard<std::mutex> lock(m_processMutex);
    if (m_decodeProcess) {
        m_decodeProcess->kill();
    }
    if (m_encodeProcess) {
        m_encodeProcess->kill();
    }
}

void JobRunner::setPaused(bool paused)
{
    m_paused.store(paused);
    if (!paused) {
        m_pauseCv.notify_all();
    }
}

void JobRunner::waitWhilePaused()
{
    if (!m_paused.load()) {
        return;
    }
    std::unique_lock<std::mutex> lock(m_pauseMutex);
    m_pauseCv.wait(lock, [this] { return !m_paused.load() || m_cancelled.load(); });
}

void JobRunner::run(const JobSpec& spec, const FfmpegPaths& paths)
{
    m_cancelled.store(false);
    m_paused.store(false);

    QString error;
    const bool ok = runInternal(spec, paths, error);

    const QString outputPath = spec.suggestedOutputPath();

    if (m_cancelled.load()) {
        emit finished(false, tr("Cancelled."), outputPath);
    } else if (ok) {
        emit finished(true, tr("Completed."), outputPath);
    } else {
        emit finished(false, error, outputPath);
    }

    {
        std::lock_guard<std::mutex> lock(m_processMutex);
        m_decodeProcess = nullptr;
        m_encodeProcess = nullptr;
    }
}

bool JobRunner::runInternal(const JobSpec& spec, const FfmpegPaths& paths, QString& errorOut)
{
    if (!paths.valid) {
        errorOut = paths.error;
        return false;
    }

    const QFileInfo inputInfo(spec.inputPath);
    if (!inputInfo.exists() || !inputInfo.isFile()) {
        errorOut = tr("Input file not found: %1").arg(spec.inputPath);
        return false;
    }

    const MediaInfo info = probeMedia(paths.ffprobe, spec.inputPath);
    if (!info.valid) {
        errorOut = info.error;
        return false;
    }

    // With the AI upscaler, ffmpeg only restores and hands over frames at
    // source resolution; the network does the scaling. With the FFmpeg tier,
    // the filter chain scales and the processor is a passthrough.
    const bool useNcnn =
        spec.upscaleEnabled && spec.upscaleMethod == UpscaleMethod::RealEsrganNcnn;
    const DecodePlan decodePlan = buildDecodePlan(spec, info, !useNcnn);

    const int decodeWidth = decodePlan.outputWidth;
    const int decodeHeight = decodePlan.outputHeight;
    if (decodeWidth <= 0 || decodeHeight <= 0) {
        errorOut = tr("Could not determine the decode resolution.");
        return false;
    }

    int width = decodeWidth;
    int height = decodeHeight;

    // The processor is built before the encode plan, because with the AI tier
    // it is what determines the output resolution.
    std::unique_ptr<VulkanContext> vulkan;
    std::unique_ptr<IFrameProcessor> processor;

    if (useNcnn) {
        vulkan = std::make_unique<VulkanContext>();
        std::string vulkanError;
        if (!vulkan->init(0, vulkanError)) {
            errorOut = QString::fromStdString(vulkanError);
            return false;
        }

        auto esrgan = std::make_unique<RealEsrganNcnn>(*vulkan);

        ProcessorConfig cfg;
        cfg.modelDir = QDir(QCoreApplication::applicationDirPath())
                           .filePath(QStringLiteral("models"))
                           .toStdString();
        cfg.modelName = spec.upscaleModel.toStdString();
        cfg.scale = spec.upscaleFactor;
        cfg.tileSize = spec.tileSize;
        cfg.tileOverlap = 16;
        cfg.useFp16 = true;
        cfg.gpuIndex = 0;

        std::string processorError;
        if (!esrgan->init(cfg, processorError)) {
            errorOut = QString::fromStdString(processorError);
            return false;
        }

        width = decodeWidth * spec.upscaleFactor;
        height = decodeHeight * spec.upscaleFactor;
        processor = std::move(esrgan);
    } else {
        auto passthrough = std::make_unique<PassthroughProcessor>();
        std::string processorError;
        if (!passthrough->init(ProcessorConfig{}, processorError)) {
            errorOut = QString::fromStdString(processorError);
            return false;
        }
        processor = std::move(passthrough);
    }

    // Interpolation runs after upscaling: doing it first would put twice as
    // many frames through the upscaler, which dominates the runtime.
    const int fpsMultiplier =
        spec.interpolationEnabled ? std::clamp(spec.fpsMultiplier, 2, 4) : 1;
    std::unique_ptr<IFrameInterpolator> interpolator;

    if (fpsMultiplier > 1) {
        if (!vulkan) {
            vulkan = std::make_unique<VulkanContext>();
            std::string vulkanError;
            if (!vulkan->init(0, vulkanError)) {
                errorOut = tr("Frame interpolation needs a Vulkan GPU: %1")
                               .arg(QString::fromStdString(vulkanError));
                return false;
            }
        }

        auto rife = std::make_unique<RifeNcnn>(*vulkan);

        ProcessorConfig cfg;
        cfg.modelDir = QDir(QCoreApplication::applicationDirPath())
                           .filePath(QStringLiteral("models"))
                           .toStdString();
        cfg.modelName = spec.interpolationModel.toStdString();
        cfg.useFp16 = true;

        std::string rifeError;
        if (!rife->init(cfg, rifeError)) {
            errorOut = QString::fromStdString(rifeError);
            return false;
        }
        interpolator = std::move(rife);
    }

    const QString outputPath = spec.suggestedOutputPath();
    if (outputPath.isEmpty()) {
        errorOut = tr("No output path was set.");
        return false;
    }
    if (QFileInfo(outputPath).absoluteFilePath() == inputInfo.absoluteFilePath()) {
        errorOut = tr("The output file would overwrite the input file.");
        return false;
    }
    QDir().mkpath(QFileInfo(outputPath).absolutePath());

    const EncodePlan encodePlan = buildEncodePlan(spec, info, width, height, outputPath);

    spdlog::info("Job: {} -> {}", spec.inputPath.toStdString(), outputPath.toStdString());
    spdlog::info("  {}x{} -> {}x{}", info.width, info.height, width, height);
    spdlog::info("  filters: {}",
                 decodePlan.filterChain.isEmpty() ? "(none)"
                                                  : decodePlan.filterChain.toStdString());
    spdlog::info("  processor: {}", processor->name());
    spdlog::info("  encoder: {} q={}", spec.encoder.toStdString(), spec.quality);

    emit started(info, width, height);

    const std::size_t decodeFrameBytes =
        static_cast<std::size_t>(decodeWidth) * decodeHeight * 3u;
    const std::size_t frameBytes = static_cast<std::size_t>(width) * height * 3u;

    // Four frames in flight per ring keeps memory bounded: at 4K that is
    // roughly 25 MB each, so ~200 MB total across both rings and the pools.
    constexpr std::size_t kRingCapacity = 4;
    FramePool decodePool(decodeWidth, decodeHeight, kRingCapacity * 2);
    FramePool encodePool(width, height, kRingCapacity * 2);
    RingBuffer<FrameBuffer> inputRing(kRingCapacity);
    RingBuffer<FrameBuffer> outputRing(kRingCapacity);

    StderrTail decodeErr;
    StderrTail encodeErr;
    std::atomic_bool decodeFailed{false};
    std::atomic_bool encodeFailed{false};
    std::atomic<qint64> framesDecoded{0};
    std::atomic<qint64> framesEncoded{0};

    // ---------------------------------------------------------------- decode
    FunctionThread decodeThread([&]() {
        QProcess process;
        process.setReadChannel(QProcess::StandardOutput);
        {
            std::lock_guard<std::mutex> lock(m_processMutex);
            m_decodeProcess = &process;
        }

        process.start(paths.ffmpeg, decodePlan.arguments);
        if (!process.waitForStarted(10000)) {
            decodeFailed.store(true);
            inputRing.close();
            return;
        }

        std::size_t filled = 0;
        FrameBuffer frame = decodePool.acquire();
        int64_t index = 0;

        while (!m_cancelled.load()) {
            decodeErr.append(process.readAllStandardError());

            if (process.bytesAvailable() == 0) {
                if (!process.waitForReadyRead(150)) {
                    if (process.state() == QProcess::NotRunning && process.bytesAvailable() == 0) {
                        break;
                    }
                    continue;
                }
            }

            // Partial reads are normal on a pipe: a frame arrives across an
            // arbitrary number of chunks, so accumulate and only emit whole
            // frames.
            const qint64 got = process.read(reinterpret_cast<char*>(frame.data()) + filled,
                                            static_cast<qint64>(decodeFrameBytes - filled));
            if (got < 0) {
                break;
            }
            filled += static_cast<std::size_t>(got);

            if (filled == decodeFrameBytes) {
                frame.setFrameIndex(index);
                frame.setPtsSeconds(info.fps() > 0.0 ? static_cast<double>(index) / info.fps() : 0.0);
                if (!inputRing.push(std::move(frame))) {
                    break;
                }
                ++index;
                framesDecoded.store(index);
                frame = decodePool.acquire();
                filled = 0;
            }
        }

        decodeErr.append(process.readAllStandardError());

        if (process.state() != QProcess::NotRunning) {
            process.closeReadChannel(QProcess::StandardOutput);
            if (!process.waitForFinished(5000)) {
                process.kill();
                process.waitForFinished(2000);
            }
        }

        if (!m_cancelled.load() && process.exitStatus() == QProcess::NormalExit
            && process.exitCode() != 0) {
            decodeFailed.store(true);
        }

        {
            std::lock_guard<std::mutex> lock(m_processMutex);
            m_decodeProcess = nullptr;
        }
        inputRing.close();
    });

    // ---------------------------------------------------------------- encode
    FunctionThread encodeThread([&]() {
        QProcess process;
        {
            std::lock_guard<std::mutex> lock(m_processMutex);
            m_encodeProcess = &process;
        }

        process.start(paths.ffmpeg, encodePlan.arguments);
        if (!process.waitForStarted(10000)) {
            encodeFailed.store(true);
            outputRing.cancel();
            return;
        }

        FrameBuffer frame;
        while (outputRing.pop(frame)) {
            if (m_cancelled.load()) {
                break;
            }

            encodeErr.append(process.readAllStandardError());

            const char* src = reinterpret_cast<const char*>(frame.data());
            qint64 written = 0;
            bool writeOk = true;
            while (written < static_cast<qint64>(frameBytes)) {
                const qint64 n = process.write(src + written,
                                               static_cast<qint64>(frameBytes) - written);
                if (n <= 0) {
                    writeOk = false;
                    break;
                }
                written += n;
                if (!process.waitForBytesWritten(30000)) {
                    writeOk = false;
                    break;
                }
            }

            if (!writeOk) {
                encodeFailed.store(true);
                break;
            }

            framesEncoded.fetch_add(1);
            frame.reset();
        }

        process.closeWriteChannel();
        encodeErr.append(process.readAllStandardError());

        if (!process.waitForFinished(120000)) {
            process.kill();
            process.waitForFinished(5000);
            encodeFailed.store(true);
        }
        encodeErr.append(process.readAllStandardError());

        if (!m_cancelled.load() && process.exitCode() != 0) {
            encodeFailed.store(true);
        }

        {
            std::lock_guard<std::mutex> lock(m_processMutex);
            m_encodeProcess = nullptr;
        }
    });

    decodeThread.start();
    encodeThread.start();

    // ------------------------------------------------------------ processing
    QElapsedTimer timer;
    timer.start();
    qint64 lastEmitMs = 0;
    qint64 processed = 0;

    FrameBuffer in;
    FrameBuffer previous;
    while (inputRing.pop(in)) {
        if (m_cancelled.load()) {
            break;
        }
        waitWhilePaused();
        if (m_cancelled.load()) {
            break;
        }

        FrameBuffer out = encodePool.acquire();
        if (!processor->process(in, out)) {
            errorOut = tr("The frame processor failed on frame %1.").arg(processed);
            m_cancelled.store(true);
            break;
        }
        in.reset();

        // Emit the frames that belong between the previous output and this
        // one, then the frame itself. The result is (n-1)*m + 1 frames, which
        // is within one frame of n*m -- the tolerance the spec allows.
        if (interpolator && previous.valid()) {
            bool interpolationOk = true;
            for (int k = 1; k < fpsMultiplier && interpolationOk; ++k) {
                const float t = static_cast<float>(k) / static_cast<float>(fpsMultiplier);
                FrameBuffer mid = encodePool.acquire();
                if (!interpolator->interpolate(previous, out, t, mid)) {
                    errorOut = tr("Frame interpolation failed near frame %1. The GPU may not "
                                  "have enough memory at this resolution.")
                                   .arg(processed);
                    m_cancelled.store(true);
                    interpolationOk = false;
                    break;
                }
                if (!outputRing.push(std::move(mid))) {
                    interpolationOk = false;
                    break;
                }
            }
            if (!interpolationOk) {
                break;
            }
        }

        if (interpolator) {
            // The next iteration needs this frame after it has been handed to
            // the encoder, so keep a copy.
            FrameBuffer keep = encodePool.acquire();
            std::memcpy(keep.data(), out.data(), out.sizeBytes());
            keep.setFrameIndex(out.frameIndex());
            keep.setPtsSeconds(out.ptsSeconds());
            previous = std::move(keep);
        }

        if (!outputRing.push(std::move(out))) {
            break;
        }
        ++processed;

        // Progress is rate limited to 10 Hz. Emitting per frame floods the
        // event loop and makes the UI stutter.
        const qint64 elapsed = timer.elapsed();
        if (elapsed - lastEmitMs >= 100) {
            lastEmitMs = elapsed;
            const double seconds = static_cast<double>(elapsed) / 1000.0;
            const double fps = seconds > 0.0 ? static_cast<double>(processed) / seconds : 0.0;
            const qint64 total = info.frameCount;
            qint64 eta = -1;
            if (fps > 0.01 && total > processed) {
                eta = static_cast<qint64>(static_cast<double>(total - processed) / fps);
            }
            emit progress(processed, total, fps, eta);
        }
    }

    outputRing.close();
    if (m_cancelled.load()) {
        inputRing.cancel();
        outputRing.cancel();
    }

    decodeThread.wait();
    encodeThread.wait();

    emit progress(processed, info.frameCount, 0.0, 0);

    if (m_cancelled.load()) {
        // A partial file is worse than none: it looks like a successful render
        // until you play it.
        QFile::remove(outputPath);
        return false;
    }

    if (decodeFailed.load()) {
        errorOut = decodeErr.empty() ? tr("Decoding failed.")
                                     : tr("Decoding failed:\n%1").arg(decodeErr.text());
        return false;
    }
    if (encodeFailed.load()) {
        errorOut = encodeErr.empty() ? tr("Encoding failed.")
                                     : tr("Encoding failed:\n%1").arg(encodeErr.text());
        return false;
    }
    if (processed == 0) {
        errorOut = decodeErr.empty() ? tr("No frames were produced.")
                                     : tr("No frames were produced:\n%1").arg(decodeErr.text());
        return false;
    }

    spdlog::info("Job finished: {} frames decoded, {} encoded, {:.2f} s", framesDecoded.load(),
                 framesEncoded.load(), timer.elapsed() / 1000.0);

    return true;
}

} // namespace dfu
