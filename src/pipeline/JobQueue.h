#pragma once

#include "core/JobSpec.h"
#include "media/FfmpegPaths.h"
#include "media/MediaProbe.h"

#include <QList>
#include <QObject>
#include <QString>

class QThread;

namespace dfu {

enum class JobStatus
{
    Probing,
    Ready,
    Invalid,
    Running,
    Paused,
    Completed,
    Failed,
    Cancelled
};

class JobRunner;
class ProbeWorker;

QString jobStatusText(JobStatus status);

struct Job
{
    QString id;
    JobSpec spec;
    MediaInfo info;
    JobStatus status = JobStatus::Probing;
    QString message;

    qint64 framesDone = 0;
    qint64 framesTotal = 0;
    double fps = 0.0;
    qint64 etaSeconds = -1;
    int outputWidth = 0;
    int outputHeight = 0;

    int progressPercent() const
    {
        if (framesTotal <= 0) {
            return 0;
        }
        const double ratio = static_cast<double>(framesDone) / static_cast<double>(framesTotal);
        return static_cast<int>(ratio * 100.0 + 0.5);
    }
};

// Probes files and runs jobs one at a time. All heavy work happens on worker
// threads; this object only ever touches its own state on the UI thread.
class JobQueue : public QObject
{
    Q_OBJECT

public:
    explicit JobQueue(QObject* parent = nullptr);
    ~JobQueue() override;

    void setFfmpegPaths(const FfmpegPaths& paths);
    const FfmpegPaths& ffmpegPaths() const { return m_paths; }

    int addFile(const QString& path);
    void removeJob(int index);
    void clearFinished();

    int count() const { return static_cast<int>(m_jobs.size()); }
    const Job* jobAt(int index) const;
    void setSpec(int index, const JobSpec& spec);

    bool isRunning() const { return m_running; }
    bool isPaused() const { return m_paused; }
    bool hasRunnableJob() const;

public slots:
    void start();
    void setPaused(bool paused);
    void stop();

signals:
    void jobsChanged();
    void jobUpdated(int index);
    void runningChanged(bool running);
    void message(const QString& text);

private slots:
    void onProbed(const QString& jobId, const dfu::MediaInfo& info);
    void onRunStarted(const dfu::MediaInfo& info, int outputWidth, int outputHeight);
    void onRunProgress(qint64 framesDone, qint64 framesTotal, double fps, qint64 etaSeconds);
    void onRunFinished(bool success, const QString& message, const QString& outputPath);

private:
    int indexOfId(const QString& id) const;
    void startNext();
    void requestProbe(const Job& job);

    QList<Job> m_jobs;
    FfmpegPaths m_paths;

    QThread* m_runnerThread = nullptr;
    JobRunner* m_runner = nullptr;
    QThread* m_probeThread = nullptr;
    ProbeWorker* m_probeWorker = nullptr;

    int m_currentIndex = -1;
    bool m_running = false;
    bool m_paused = false;
    bool m_stopRequested = false;
    quint64 m_nextId = 1;
};

} // namespace dfu
