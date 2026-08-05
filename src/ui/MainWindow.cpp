#include "ui/MainWindow.h"

#include "core/AppInfo.h"
#include "core/Logging.h"
#include "media/FfmpegPaths.h"
#include "pipeline/JobQueue.h"
#include "ui/JobQueueWidget.h"
#include "ui/LogDock.h"
#include "ui/SettingsPanel.h"
#include "ui/Theme.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>

#include <spdlog/spdlog.h>

namespace {

QString videoFileFilter()
{
    return QObject::tr("Video files (*.mp4 *.mkv *.mov *.avi *.webm *.m2ts *.ts *.wmv *.flv);;"
                       "All files (*)");
}

} // namespace

namespace dfu {

MainWindow::MainWindow(const LogContext& log, QWidget* parent)
    : QMainWindow(parent)
    , m_logDirPath(log.logDirPath)
{
    setObjectName(QStringLiteral("MainWindow"));
    setWindowTitle(appName());
    resize(1280, 800);
    setAcceptDrops(true);

    m_queue = new JobQueue(this);

    m_queueWidget = new JobQueueWidget(m_queue, this);
    setCentralWidget(m_queueWidget);

    buildDocks(log);
    buildActions();
    buildStatusBar();

    m_defaultState = saveState();
    restoreLayout();

    connect(m_queueWidget, &JobQueueWidget::filesDropped, this, &MainWindow::addPaths);
    connect(m_queueWidget->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &MainWindow::onSelectionChanged);
    connect(m_settingsPanel, &SettingsPanel::specEdited, this, &MainWindow::onSpecEdited);
    connect(m_queue, &JobQueue::runningChanged, this, &MainWindow::onRunningChanged);
    connect(m_queue, &JobQueue::jobUpdated, this, &MainWindow::onJobUpdated);
    connect(m_queue, &JobQueue::jobsChanged, this, &MainWindow::updateActionStates);
    connect(m_queue, &JobQueue::message, this, &MainWindow::onQueueMessage);

    spdlog::info("{} {} started", kAppName, kAppVersion);
    if (!log.logFilePath.isEmpty()) {
        spdlog::info("Log file: {}", log.logFilePath.toStdString());
    }

    // Locating ffmpeg touches the filesystem only; it is fast enough to do
    // here, and a missing binary must be reported before the user queues work.
    const FfmpegPaths paths = locateFfmpeg();
    m_queue->setFfmpegPaths(paths);
    if (!paths.valid) {
        statusBar()->showMessage(tr("FFmpeg not found - processing is unavailable"));
    }

    onSelectionChanged();
    updateActionStates();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildDocks(const LogContext& log)
{
    m_settingsDock = new QDockWidget(tr("Settings"), this);
    m_settingsDock->setObjectName(QStringLiteral("SettingsDock"));
    m_settingsDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    m_settingsPanel = new SettingsPanel(m_settingsDock);
    m_settingsDock->setWidget(m_settingsPanel);
    m_settingsDock->setMinimumWidth(320);
    addDockWidget(Qt::RightDockWidgetArea, m_settingsDock);

    m_logDock = new LogDock(log.bridge, this);
    addDockWidget(Qt::BottomDockWidgetArea, m_logDock);
    resizeDocks({m_logDock}, {200}, Qt::Vertical);
}

void MainWindow::buildActions()
{
    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    m_addAction = fileMenu->addAction(tr("&Add Files..."), QKeySequence::Open, this,
                                      &MainWindow::addFiles);
    m_removeAction = fileMenu->addAction(tr("&Remove Selected"), QKeySequence::Delete, this,
                                         &MainWindow::removeSelectedJobs);
    fileMenu->addSeparator();
    m_openOutputAction =
        fileMenu->addAction(tr("Open &Output Folder"), this, &MainWindow::openOutputFolder);
    fileMenu->addAction(tr("Open &Log Folder"), this, &MainWindow::openLogFolder);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    auto* jobMenu = menuBar()->addMenu(tr("&Job"));
    m_startAction = jobMenu->addAction(tr("&Start"), QKeySequence(Qt::Key_F9), this,
                                       [this]() { m_queue->start(); });
    m_pauseAction = jobMenu->addAction(tr("&Pause"), QKeySequence(Qt::Key_Space), this,
                                       [this]() { m_queue->setPaused(!m_queue->isPaused()); });
    m_pauseAction->setCheckable(true);
    m_stopAction = jobMenu->addAction(tr("Sto&p"), QKeySequence(Qt::Key_Escape), this,
                                      [this]() { m_queue->stop(); });
    jobMenu->addSeparator();
    jobMenu->addAction(tr("&Clear Finished"), this, [this]() { m_queue->clearFinished(); });

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_settingsDock->toggleViewAction());
    viewMenu->addAction(m_logDock->toggleViewAction());
    viewMenu->addSeparator();
    buildThemeMenu(viewMenu);
    viewMenu->addSeparator();
    viewMenu->addAction(tr("&Reset Layout"), this, &MainWindow::resetLayout);

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About %1").arg(appName()), this, &MainWindow::showAbout);
    helpMenu->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);

    auto* toolBar = addToolBar(tr("Main"));
    toolBar->setObjectName(QStringLiteral("MainToolBar"));
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolBar->addAction(m_addAction);
    toolBar->addAction(m_removeAction);
    toolBar->addSeparator();
    toolBar->addAction(m_startAction);
    toolBar->addAction(m_pauseAction);
    toolBar->addAction(m_stopAction);
    toolBar->addSeparator();
    toolBar->addAction(m_openOutputAction);
}

void MainWindow::buildThemeMenu(QMenu* parentMenu)
{
    QMenu* themeMenu = parentMenu->addMenu(tr("&Theme"));

    auto* group = new QActionGroup(this);
    group->setExclusive(true);

    const ThemePreference current = loadThemePreference();

    const struct
    {
        ThemePreference preference;
        QString label;
    } entries[] = {
        {ThemePreference::System, tr("Follow &System")},
        {ThemePreference::Light, tr("&Light")},
        {ThemePreference::Dark, tr("&Dark")},
    };

    for (const auto& entry : entries) {
        QAction* action = themeMenu->addAction(entry.label);
        action->setCheckable(true);
        action->setChecked(entry.preference == current);
        group->addAction(action);

        const ThemePreference preference = entry.preference;
        connect(action, &QAction::triggered, this, [preference]() {
            applyThemePreference(preference);
            saveThemePreference(preference);
        });
    }
}

void MainWindow::buildStatusBar()
{
    statusBar()->showMessage(tr("Ready"));

    m_statusFps = new QLabel(this);
    m_statusEta = new QLabel(this);
    auto* version = new QLabel(tr("v%1").arg(appVersion()), this);
    version->setContentsMargins(0, 0, 8, 0);

    statusBar()->addPermanentWidget(m_statusFps);
    statusBar()->addPermanentWidget(m_statusEta);
    statusBar()->addPermanentWidget(version);
}

void MainWindow::addFiles()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Add video files"), QString(),
                                                            videoFileFilter());
    addPaths(paths);
}

void MainWindow::addPaths(const QStringList& paths)
{
    int added = 0;
    for (const QString& path : paths) {
        if (m_queue->addFile(path) >= 0) {
            ++added;
        }
    }

    if (added > 0) {
        spdlog::info("Added {} file(s) to the queue", added);
        if (m_queueWidget->selectedJobIndex() < 0) {
            m_queueWidget->selectRow(m_queue->count() - 1);
        }
    }
    updateActionStates();
}

void MainWindow::removeSelectedJobs()
{
    const QModelIndexList selected = m_queueWidget->selectionModel()->selectedRows();
    QList<int> rows;
    for (const QModelIndex& index : selected) {
        rows.append(index.row());
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows) {
        m_queue->removeJob(row);
    }
    updateActionStates();
}

void MainWindow::openOutputFolder()
{
    const int index = m_queueWidget->selectedJobIndex();
    const Job* job = m_queue->jobAt(index);

    QString folder;
    if (job && !job->spec.outputPath.isEmpty()) {
        folder = QFileInfo(job->spec.outputPath).absolutePath();
    } else if (job) {
        folder = QFileInfo(job->spec.inputPath).absolutePath();
    }

    if (folder.isEmpty()) {
        statusBar()->showMessage(tr("Select a job first."), 4000);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void MainWindow::openLogFolder()
{
    if (m_logDirPath.isEmpty()) {
        spdlog::warn("No log directory is available to open");
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(m_logDirPath))) {
        spdlog::warn("Could not open the log folder: {}", m_logDirPath.toStdString());
    }
}

void MainWindow::onSelectionChanged()
{
    const int index = m_queueWidget->selectedJobIndex();
    const Job* job = m_queue->jobAt(index);

    if (!job) {
        m_settingsPanel->setEditingEnabled(false);
        m_settingsPanel->setSourceResolution(0, 0);
        updateActionStates();
        return;
    }

    m_settingsPanel->setEditingEnabled(job->status != JobStatus::Running);
    m_settingsPanel->setSpec(job->spec);
    m_settingsPanel->setSourceResolution(job->info.width, job->info.height);
    m_settingsPanel->setSourceFps(job->info.fpsNum, job->info.fpsDen);
    updateActionStates();
}

void MainWindow::onSpecEdited(const JobSpec& spec)
{
    const QModelIndexList selected = m_queueWidget->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        return;
    }

    // Multi-select edits every selected job, but each keeps its own paths.
    for (const QModelIndex& index : selected) {
        const Job* job = m_queue->jobAt(index.row());
        if (!job) {
            continue;
        }

        JobSpec updated = spec;
        updated.inputPath = job->spec.inputPath;
        if (selected.size() > 1) {
            updated.outputPath.clear();
            updated.outputPath = updated.suggestedOutputPath();
        }
        m_queue->setSpec(index.row(), updated);
    }
}

void MainWindow::onJobUpdated(int index)
{
    if (index == m_queueWidget->selectedJobIndex()) {
        const Job* job = m_queue->jobAt(index);
        if (job) {
            m_settingsPanel->setSourceResolution(job->info.width, job->info.height);
            m_settingsPanel->setSourceFps(job->info.fpsNum, job->info.fpsDen);
            m_settingsPanel->setEditingEnabled(job->status != JobStatus::Running);

            if (job->status == JobStatus::Running) {
                m_statusFps->setText(job->fps > 0.0
                                         ? tr("%1 fps").arg(QString::number(job->fps, 'f', 1))
                                         : QString());
                m_statusEta->setText(job->etaSeconds >= 0
                                         ? tr("ETA %1s").arg(job->etaSeconds)
                                         : QString());
            }
        }
    }
    updateActionStates();
}

void MainWindow::onRunningChanged(bool running)
{
    if (!running) {
        m_statusFps->clear();
        m_statusEta->clear();
        m_pauseAction->setChecked(false);
        statusBar()->showMessage(tr("Idle"), 4000);
    } else {
        statusBar()->showMessage(tr("Processing..."));
    }
    updateActionStates();
}

void MainWindow::onQueueMessage(const QString& text)
{
    spdlog::info("{}", text.toStdString());
    statusBar()->showMessage(text, 6000);
}

void MainWindow::updateActionStates()
{
    const bool running = m_queue->isRunning();
    const bool ffmpegOk = m_queue->ffmpegPaths().valid;

    m_startAction->setEnabled(!running && ffmpegOk && m_queue->hasRunnableJob());
    m_pauseAction->setEnabled(running);
    m_stopAction->setEnabled(running);
    m_removeAction->setEnabled(m_queueWidget->selectedJobIndex() >= 0);
    m_openOutputAction->setEnabled(m_queueWidget->selectedJobIndex() >= 0);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasUrls()) {
        return;
    }

    QStringList paths;
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls) {
        if (url.isLocalFile()) {
            paths << url.toLocalFile();
        }
    }
    addPaths(paths);
    event->acceptProposedAction();
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("About %1").arg(appName()),
        tr("<h3>%1 %2</h3>"
           "<p>GPU video upscaling, frame interpolation and restoration.</p>"
           "<p>Built with Qt %3.</p>")
            .arg(appName(), appVersion(), QString::fromLatin1(qVersion())));
}

void MainWindow::resetLayout()
{
    restoreState(m_defaultState);
    m_settingsDock->show();
    m_logDock->show();

    QSettings settings;
    settings.beginGroup(QStringLiteral("MainWindow"));
    settings.remove(QStringLiteral("state"));
    settings.endGroup();

    spdlog::info("Window layout reset to defaults");
    statusBar()->showMessage(tr("Layout reset"), 4000);
}

void MainWindow::restoreLayout()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("MainWindow"));
    const QByteArray geometry = settings.value(QStringLiteral("geometry")).toByteArray();
    const QByteArray state = settings.value(QStringLiteral("state")).toByteArray();
    settings.endGroup();

    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    if (!state.isEmpty()) {
        restoreState(state);
    }
}

void MainWindow::saveLayout() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("MainWindow"));
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("state"), saveState());
    settings.endGroup();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_queue->isRunning()) {
        const auto answer = QMessageBox::question(
            this, tr("Stop processing?"),
            tr("A job is still running. Closing now discards its partial output.\n\nClose "
               "anyway?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_queue->stop();
    }

    saveLayout();
    spdlog::info("Shutting down");
    QMainWindow::closeEvent(event);
}

} // namespace dfu
