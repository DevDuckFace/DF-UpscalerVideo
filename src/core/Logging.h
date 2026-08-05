#pragma once

#include <QObject>
#include <QString>

#include <spdlog/spdlog.h>

#include <memory>

namespace dfu {

// Fan-out point between spdlog (called from any thread) and Qt (UI thread).
//
// Not a singleton: main() owns the instance and hands it to whoever needs it.
// The sink holds only a weak reference, so a log call racing with shutdown
// finds an expired handle and drops the line instead of touching freed memory.
class LogBridge : public QObject
{
    Q_OBJECT

public:
    explicit LogBridge(QObject* parent = nullptr);

    // Callable from any thread. `level` is a spdlog::level::level_enum value.
    void post(int level, const QString& text);

signals:
    void logged(int level, const QString& text);
};

struct LogOptions
{
    // Empty means AppDataLocation/logs.
    QString logDir;
    spdlog::level::level_enum consoleLevel = spdlog::level::info;
    spdlog::level::level_enum fileLevel = spdlog::level::debug;
    spdlog::level::level_enum uiLevel = spdlog::level::debug;
    // Route qDebug/qWarning/qCritical through spdlog as well.
    bool captureQtMessages = true;
};

struct LogContext
{
    std::shared_ptr<LogBridge> bridge;
    QString logFilePath;
    QString logDirPath;
};

// Installs the default spdlog logger. Safe to call exactly once, before any
// logging happens. Never throws: if the file sink cannot be opened the
// failure is reported through the remaining sinks and logging continues.
LogContext initLogging(const LogOptions& opts);

// Restores the previous Qt message handler and flushes every sink.
void shutdownLogging();

// Human-readable name for a spdlog level value, for UI display.
QString levelName(int level);

} // namespace dfu
