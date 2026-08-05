#include "pipeline/JobQueue.h"

#include "engine/VulkanContext.h"
#include "pipeline/JobRunner.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QThread>

#include <spdlog/spdlog.h>

namespace dfu {

// Probing shells out to ffprobe and blocks for as long as that takes, so it
// gets its own thread rather than stalling the UI on a slow or network file.
class ProbeWorker : public QObject
{
    Q_OBJECT

public slots:
    void probe(const QString& jobId, const QString& ffprobePath, const QString& inputPath)
    {
        emit probed(jobId, probeMedia(ffprobePath, inputPath));
    }

signals:
    void probed(const QString& jobId, const dfu::MediaInfo& info);
};

QString jobStatusText(JobStatus status)
{
    switch (status) {
    case JobStatus::Probing:
        return QObject::tr("Reading...");
    case JobStatus::Ready:
        return QObject::tr("Ready");
    case JobStatus::Invalid:
        return QObject::tr("Unsupported");
    case JobStatus::Running:
        return QObject::tr("Running");
    case JobStatus::Paused:
        return QObject::tr("Paused");
    case JobStatus::Completed:
        return QObject::tr("Completed");
    case JobStatus::Failed:
        return QObject::tr("Failed");
    case JobStatus::Cancelled:
        return QObject::tr("Cancelled");
    }
    return QString();
}

JobQueue::JobQueue(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<dfu::MediaInfo>("dfu::MediaInfo");
    qRegisterMetaType<dfu::JobSpec>("dfu::JobSpec");
    qRegisterMetaType<dfu::FfmpegPaths>("dfu::FfmpegPaths");

    m_runnerThread = new QThread(this);
    m_runnerThread->setObjectName(QStringLiteral("JobRunner"));
    m_runner = new JobRunner;
    m_runner->moveToThread(m_runnerThread);
    connect(m_runnerThread, &QThread::finished, m_runner, &QObject::deleteLater);
    connect(m_runner, &JobRunner::started, this, &JobQueue::onRunStarted, Qt::QueuedConnection);
    connect(m_runner, &JobRunner::progress, this, &JobQueue::onRunProgress, Qt::QueuedConnection);
    connect(m_runner, &JobRunner::finished, this, &JobQueue::onRunFinished, Qt::QueuedConnection);
    m_runnerThread->start();

    m_probeThread = new QThread(this);
    m_probeThread->setObjectName(QStringLiteral("MediaProbe"));
    m_probeWorker = new ProbeWorker;
    m_probeWorker->moveToThread(m_probeThread);
    connect(m_probeThread, &QThread::finished, m_probeWorker, &QObject::deleteLater);
    connect(m_probeWorker, &ProbeWorker::probed, this, &JobQueue::onProbed, Qt::QueuedConnection);
    m_probeThread->start();
}

JobQueue::~JobQueue()
{
    if (m_runner) {
        m_runner->cancel();
    }

    m_runnerThread->quit();
    if (!m_runnerThread->wait(8000)) {
        spdlog::warn("Job runner thread did not stop cleanly; terminating");
        m_runnerThread->terminate();
        m_runnerThread->wait(2000);
    }

    m_probeThread->quit();
    m_probeThread->wait(5000);
}

void JobQueue::setFfmpegPaths(const FfmpegPaths& paths)
{
    m_paths = paths;
}

const Job* JobQueue::jobAt(int index) const
{
    if (index < 0 || index >= m_jobs.size()) {
        return nullptr;
    }
    return &m_jobs.at(index);
}

int JobQueue::indexOfId(const QString& id) const
{
    for (int i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

int JobQueue::addFile(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        emit message(tr("Not a file: %1").arg(path));
        return -1;
    }

    for (const Job& existing : m_jobs) {
        if (QFileInfo(existing.spec.inputPath).absoluteFilePath() == info.absoluteFilePath()) {
            emit message(tr("Already in the queue: %1").arg(info.fileName()));
            return -1;
        }
    }

    // Default to the AI upscaler when the machine can actually run it. ncnn
    // caches the driver query, so this is only paid once.
    static const bool vulkanAvailable = VulkanContext::vulkanAvailable();

    Job job;
    job.id = QStringLiteral("job-%1").arg(m_nextId++);
    job.spec.upscaleMethod =
        vulkanAvailable ? UpscaleMethod::RealEsrganNcnn : UpscaleMethod::FfmpegLanczos;
    job.spec.inputPath = info.absoluteFilePath();
    job.spec.outputPath = job.spec.suggestedOutputPath();
    job.status = JobStatus::Probing;

    m_jobs.append(job);
    emit jobsChanged();

    requestProbe(job);
    return static_cast<int>(m_jobs.size()) - 1;
}

void JobQueue::requestProbe(const Job& job)
{
    if (!m_paths.valid) {
        const int index = indexOfId(job.id);
        if (index >= 0) {
            m_jobs[index].status = JobStatus::Invalid;
            m_jobs[index].message = m_paths.error;
            emit jobUpdated(index);
        }
        return;
    }

    QMetaObject::invokeMethod(m_probeWorker, "probe", Qt::QueuedConnection,
                              Q_ARG(QString, job.id), Q_ARG(QString, m_paths.ffprobe),
                              Q_ARG(QString, job.spec.inputPath));
}

void JobQueue::onProbed(const QString& jobId, const MediaInfo& info)
{
    const int index = indexOfId(jobId);
    if (index < 0) {
        return;
    }

    Job& job = m_jobs[index];
    job.info = info;

    if (!info.valid) {
        job.status = JobStatus::Invalid;
        job.message = info.error;
    } else {
        job.status = JobStatus::Ready;
        job.message.clear();
        job.framesTotal = info.frameCount;
        job.outputWidth = info.width * job.spec.upscaleFactor;
        job.outputHeight = info.height * job.spec.upscaleFactor;

        if (info.isVariableFrameRate) {
            emit message(tr("%1 is variable frame rate; it will be normalised to %2 fps.")
                             .arg(QFileInfo(job.spec.inputPath).fileName(), info.fpsString()));
        }
    }

    emit jobUpdated(index);
}

void JobQueue::setSpec(int index, const JobSpec& spec)
{
    if (index < 0 || index >= m_jobs.size()) {
        return;
    }
    if (m_jobs.at(index).status == JobStatus::Running) {
        return;
    }

    Job& job = m_jobs[index];
    job.spec = spec;
    if (job.info.valid) {
        const int factor = spec.upscaleEnabled ? spec.upscaleFactor : 1;
        job.outputWidth = (job.info.width * factor) & ~1;
        job.outputHeight = (job.info.height * factor) & ~1;
    }
    if (job.status == JobStatus::Completed || job.status == JobStatus::Failed
        || job.status == JobStatus::Cancelled) {
        job.status = JobStatus::Ready;
        job.message.clear();
        job.framesDone = 0;
    }
    emit jobUpdated(index);
}

void JobQueue::removeJob(int index)
{
    if (index < 0 || index >= m_jobs.size()) {
        return;
    }
    if (index == m_currentIndex && m_running) {
        emit message(tr("That job is running. Stop it first."));
        return;
    }

    m_jobs.removeAt(index);
    if (m_currentIndex > index) {
        --m_currentIndex;
    }
    emit jobsChanged();
}

void JobQueue::clearFinished()
{
    bool removed = false;
    for (int i = static_cast<int>(m_jobs.size()) - 1; i >= 0; --i) {
        const JobStatus status = m_jobs.at(i).status;
        if (status == JobStatus::Completed || status == JobStatus::Failed
            || status == JobStatus::Cancelled) {
            m_jobs.removeAt(i);
            removed = true;
        }
    }
    if (removed) {
        m_currentIndex = -1;
        emit jobsChanged();
    }
}

bool JobQueue::hasRunnableJob() const
{
    for (const Job& job : m_jobs) {
        if (job.status == JobStatus::Ready) {
            return true;
        }
    }
    return false;
}

void JobQueue::start()
{
    if (m_running) {
        return;
    }
    if (!m_paths.valid) {
        emit message(m_paths.error);
        return;
    }

    m_stopRequested = false;
    startNext();
}

void JobQueue::startNext()
{
    if (m_stopRequested) {
        m_running = false;
        m_currentIndex = -1;
        emit runningChanged(false);
        return;
    }

    for (int i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs.at(i).status != JobStatus::Ready) {
            continue;
        }

        m_currentIndex = i;
        Job& job = m_jobs[i];
        job.status = JobStatus::Running;
        job.framesDone = 0;
        job.message.clear();
        emit jobUpdated(i);

        if (!m_running) {
            m_running = true;
            emit runningChanged(true);
        }

        QMetaObject::invokeMethod(m_runner, "run", Qt::QueuedConnection,
                                  Q_ARG(dfu::JobSpec, job.spec), Q_ARG(dfu::FfmpegPaths, m_paths));
        return;
    }

    // Nothing left to run.
    m_currentIndex = -1;
    if (m_running) {
        m_running = false;
        emit runningChanged(false);
        emit message(tr("Queue finished."));
    }
}

void JobQueue::setPaused(bool paused)
{
    if (!m_running) {
        return;
    }
    m_paused = paused;
    m_runner->setPaused(paused);

    if (m_currentIndex >= 0 && m_currentIndex < m_jobs.size()) {
        m_jobs[m_currentIndex].status = paused ? JobStatus::Paused : JobStatus::Running;
        emit jobUpdated(m_currentIndex);
    }
}

void JobQueue::stop()
{
    if (!m_running) {
        return;
    }
    m_stopRequested = true;
    m_paused = false;
    m_runner->cancel();
}

void JobQueue::onRunStarted(const MediaInfo& info, int outputWidth, int outputHeight)
{
    if (m_currentIndex < 0 || m_currentIndex >= m_jobs.size()) {
        return;
    }
    Job& job = m_jobs[m_currentIndex];
    job.info = info;
    job.framesTotal = info.frameCount;
    job.outputWidth = outputWidth;
    job.outputHeight = outputHeight;
    emit jobUpdated(m_currentIndex);
}

void JobQueue::onRunProgress(qint64 framesDone, qint64 framesTotal, double fps, qint64 etaSeconds)
{
    if (m_currentIndex < 0 || m_currentIndex >= m_jobs.size()) {
        return;
    }
    Job& job = m_jobs[m_currentIndex];
    job.framesDone = framesDone;
    if (framesTotal > 0) {
        job.framesTotal = framesTotal;
    }
    job.fps = fps;
    job.etaSeconds = etaSeconds;
    emit jobUpdated(m_currentIndex);
}

void JobQueue::onRunFinished(bool success, const QString& text, const QString& outputPath)
{
    if (m_currentIndex >= 0 && m_currentIndex < m_jobs.size()) {
        Job& job = m_jobs[m_currentIndex];
        job.message = text;
        job.fps = 0.0;
        job.etaSeconds = -1;

        if (success) {
            job.status = JobStatus::Completed;
            job.framesDone = job.framesTotal;
            job.spec.outputPath = outputPath;
            emit message(tr("Finished: %1").arg(QFileInfo(outputPath).fileName()));
        } else if (m_stopRequested) {
            job.status = JobStatus::Cancelled;
        } else {
            job.status = JobStatus::Failed;
            spdlog::error("Job failed: {}", text.toStdString());
            emit message(tr("Failed: %1").arg(text.section(QLatin1Char('\n'), 0, 0)));
        }
        emit jobUpdated(m_currentIndex);
    }

    m_paused = false;
    startNext();
}

} // namespace dfu

#include "JobQueue.moc"
