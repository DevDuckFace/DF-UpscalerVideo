#pragma once

#include "core/JobSpec.h"
#include "media/FfmpegPaths.h"
#include "media/MediaProbe.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <mutex>

class QProcess;

namespace dfu {

// Runs one job. Lives on a worker thread; every signal reaches the UI through
// a queued connection.
//
// Thread topology, as specified:
//   decoder thread   ffmpeg stdout  -> input ring
//   this thread      input ring -> IFrameProcessor -> output ring
//   encoder thread   output ring -> ffmpeg stdin
//
// The Vulkan device, when there is one, belongs to the processing thread and
// to nothing else.
class JobRunner : public QObject
{
    Q_OBJECT

public:
    explicit JobRunner(QObject* parent = nullptr);
    ~JobRunner() override;

public slots:
    // Blocking. Invoke with a queued connection from the UI thread.
    void run(const dfu::JobSpec& spec, const dfu::FfmpegPaths& paths);

    // Safe to call from any thread while a job is running.
    void cancel();
    void setPaused(bool paused);

signals:
    void started(const dfu::MediaInfo& info, int outputWidth, int outputHeight);
    void progress(qint64 framesDone, qint64 framesTotal, double fps, qint64 etaSeconds);
    void finished(bool success, const QString& message, const QString& outputPath);

private:
    bool runInternal(const JobSpec& spec, const FfmpegPaths& paths, QString& errorOut);
    void waitWhilePaused();

    std::atomic_bool m_cancelled{false};
    std::atomic_bool m_paused{false};
    std::mutex m_pauseMutex;
    std::condition_variable m_pauseCv;

    // Guarded because cancel() reaches in from another thread to kill them.
    std::mutex m_processMutex;
    QProcess* m_decodeProcess = nullptr;
    QProcess* m_encodeProcess = nullptr;
};

} // namespace dfu
