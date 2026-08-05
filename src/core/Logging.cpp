#include "core/Logging.h"

#include "core/AppInfo.h"

#include <QDir>
#include <QStandardPaths>
#include <QtGlobal>

#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#ifdef Q_OS_WIN
#    include <spdlog/sinks/msvc_sink.h>
#endif

#include <chrono>
#include <cstdio>
#include <mutex>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kMaxLogBytes = 5u * 1024u * 1024u;
constexpr std::size_t kMaxLogFiles = 3u;

// Sink that forwards formatted lines to a LogBridge. Emission happens on the
// calling thread; the queued connection on the receiving side does the hop to
// the UI thread.
template <typename Mutex>
class BridgeSink final : public spdlog::sinks::base_sink<Mutex>
{
public:
    explicit BridgeSink(std::weak_ptr<dfu::LogBridge> bridge)
        : m_bridge(std::move(bridge))
    {
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        const auto bridge = m_bridge.lock();
        if (!bridge) {
            return;
        }

        spdlog::memory_buf_t formatted;
        this->formatter_->format(msg, formatted);

        QString text = QString::fromUtf8(formatted.data(),
                                         static_cast<qsizetype>(formatted.size()));
        while (text.endsWith(QLatin1Char('\n')) || text.endsWith(QLatin1Char('\r'))) {
            text.chop(1);
        }

        bridge->post(static_cast<int>(msg.level), text);
    }

    void flush_() override {}

private:
    std::weak_ptr<dfu::LogBridge> m_bridge;
};

using BridgeSinkMt = BridgeSink<std::mutex>;

// qInstallMessageHandler only accepts a plain function pointer, so the
// previous handler has to live in a file-static. This is the one piece of
// mutable state in the module and it is written exactly twice: once at
// install, once at restore.
QtMessageHandler g_previousQtHandler = nullptr;

spdlog::level::level_enum toSpdlogLevel(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return spdlog::level::debug;
    case QtInfoMsg:
        return spdlog::level::info;
    case QtWarningMsg:
        return spdlog::level::warn;
    case QtCriticalMsg:
        return spdlog::level::err;
    case QtFatalMsg:
        return spdlog::level::critical;
    }
    return spdlog::level::info;
}

void qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const QByteArray utf8 = message.toUtf8();
    if (context.category && qstrcmp(context.category, "default") != 0) {
        spdlog::log(toSpdlogLevel(type), "[qt/{}] {}", context.category, utf8.constData());
    } else {
        spdlog::log(toSpdlogLevel(type), "[qt] {}", utf8.constData());
    }

    if (g_previousQtHandler) {
        g_previousQtHandler(type, context, message);
    }
}

} // namespace

namespace dfu {

LogBridge::LogBridge(QObject* parent)
    : QObject(parent)
{
}

void LogBridge::post(int level, const QString& text)
{
    emit logged(level, text);
}

LogContext initLogging(const LogOptions& opts)
{
    LogContext ctx;
    ctx.bridge = std::make_shared<LogBridge>();

    ctx.logDirPath = opts.logDir;
    if (ctx.logDirPath.isEmpty()) {
        ctx.logDirPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + QStringLiteral("/logs");
    }
    QDir().mkpath(ctx.logDirPath);
    ctx.logFilePath =
        ctx.logDirPath + QStringLiteral("/") + QString::fromLatin1(kLoggerName) + QStringLiteral(".log");

    std::vector<spdlog::sink_ptr> sinks;
    QString fileSinkError;

    // spdlog here is built without SPDLOG_WCHAR_FILENAMES, so the path goes
    // through fopen(). toLocal8Bit() is the encoding that reaches, rather than
    // mangles, a non-ASCII user name in the profile path.
    try {
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            ctx.logFilePath.toLocal8Bit().toStdString(), kMaxLogBytes, kMaxLogFiles);
        fileSink->set_level(opts.fileLevel);
        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%-8l] [t:%t] %v");
        sinks.push_back(std::move(fileSink));
    } catch (const spdlog::spdlog_ex& ex) {
        // Not swallowed: recorded here and re-reported below through the sinks
        // that did come up, so the user still sees why there is no log file.
        fileSinkError = QString::fromUtf8(ex.what());
        ctx.logFilePath.clear();
    }

    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_level(opts.consoleLevel);
    consoleSink->set_pattern("[%H:%M:%S.%e] [%^%-8l%$] %v");
    sinks.push_back(std::move(consoleSink));

    auto uiSink = std::make_shared<BridgeSinkMt>(ctx.bridge);
    uiSink->set_level(opts.uiLevel);
    uiSink->set_pattern("[%H:%M:%S.%e] [t:%t] %v");
    sinks.push_back(std::move(uiSink));

#if defined(Q_OS_WIN) && !defined(NDEBUG)
    auto debuggerSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
    debuggerSink->set_level(spdlog::level::trace);
    sinks.push_back(std::move(debuggerSink));
#endif

    auto logger = std::make_shared<spdlog::logger>(kLoggerName, sinks.begin(), sinks.end());
    // Filtering is per-sink; the logger itself must not gate anything.
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::warn);

    spdlog::set_default_logger(std::move(logger));
    spdlog::flush_every(std::chrono::seconds(2));

    if (!fileSinkError.isEmpty()) {
        spdlog::error("Log file could not be opened, continuing without one: {}",
                      fileSinkError.toStdString());
    }

    if (opts.captureQtMessages) {
        g_previousQtHandler = qInstallMessageHandler(qtMessageHandler);
    }

    return ctx;
}

void shutdownLogging()
{
    qInstallMessageHandler(g_previousQtHandler);
    g_previousQtHandler = nullptr;

    spdlog::default_logger()->flush();
    spdlog::shutdown();
}

QString levelName(int level)
{
    switch (static_cast<spdlog::level::level_enum>(level)) {
    case spdlog::level::trace:
        return QStringLiteral("trace");
    case spdlog::level::debug:
        return QStringLiteral("debug");
    case spdlog::level::info:
        return QStringLiteral("info");
    case spdlog::level::warn:
        return QStringLiteral("warning");
    case spdlog::level::err:
        return QStringLiteral("error");
    case spdlog::level::critical:
        return QStringLiteral("critical");
    default:
        break;
    }
    return QStringLiteral("off");
}

} // namespace dfu
