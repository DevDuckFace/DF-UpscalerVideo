#pragma once

#include "core/JobSpec.h"

#include <QByteArray>
#include <QMainWindow>
#include <QStringList>

class QAction;
class QDockWidget;
class QLabel;
class QMenu;

namespace dfu {

struct LogContext;
class JobQueue;
class JobQueueWidget;
class LogDock;
class SettingsPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const LogContext& log, QWidget* parent = nullptr);
    ~MainWindow() override;

    // Adds files to the queue. Also the drop and command-line entry point.
    void addPaths(const QStringList& paths);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void addFiles();
    void removeSelectedJobs();
    void openOutputFolder();
    void openLogFolder();
    void resetLayout();
    void showAbout();

    void onSelectionChanged();
    void onSpecEdited(const dfu::JobSpec& spec);
    void onRunningChanged(bool running);
    void onJobUpdated(int index);
    void onQueueMessage(const QString& text);

private:
    void buildDocks(const LogContext& log);
    void buildActions();
    void buildThemeMenu(QMenu* parentMenu);
    void buildStatusBar();
    void restoreLayout();
    void saveLayout() const;
    void updateActionStates();

    QString m_logDirPath;

    JobQueue* m_queue = nullptr;
    JobQueueWidget* m_queueWidget = nullptr;
    SettingsPanel* m_settingsPanel = nullptr;
    LogDock* m_logDock = nullptr;
    QDockWidget* m_settingsDock = nullptr;

    QAction* m_addAction = nullptr;
    QAction* m_startAction = nullptr;
    QAction* m_pauseAction = nullptr;
    QAction* m_stopAction = nullptr;
    QAction* m_removeAction = nullptr;
    QAction* m_openOutputAction = nullptr;

    QLabel* m_statusFps = nullptr;
    QLabel* m_statusEta = nullptr;

    QByteArray m_defaultState;
};

} // namespace dfu
