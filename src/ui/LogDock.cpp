#include "ui/LogDock.h"

#include "core/Logging.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStyleHints>
#include <QVBoxLayout>
#include <QWidget>

#include <spdlog/spdlog.h>

namespace dfu {

LogDock::LogDock(const std::shared_ptr<LogBridge>& bridge, QWidget* parent)
    : QDockWidget(tr("Log"), parent)
{
    setObjectName(QStringLiteral("LogDock"));
    setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* controls = new QHBoxLayout;
    controls->setSpacing(6);

    controls->addWidget(new QLabel(tr("Level:"), container));

    m_levelBox = new QComboBox(container);
    m_levelBox->addItem(tr("Trace"), static_cast<int>(spdlog::level::trace));
    m_levelBox->addItem(tr("Debug"), static_cast<int>(spdlog::level::debug));
    m_levelBox->addItem(tr("Info"), static_cast<int>(spdlog::level::info));
    m_levelBox->addItem(tr("Warning"), static_cast<int>(spdlog::level::warn));
    m_levelBox->addItem(tr("Error"), static_cast<int>(spdlog::level::err));
    m_levelBox->setCurrentIndex(1);
    m_minLevel = static_cast<int>(spdlog::level::debug);
    controls->addWidget(m_levelBox);

    m_autoScroll = new QCheckBox(tr("Auto-scroll"), container);
    m_autoScroll->setChecked(true);
    controls->addWidget(m_autoScroll);

    controls->addStretch(1);

    auto* copyButton = new QPushButton(tr("Copy all"), container);
    copyButton->setToolTip(tr("Copy every visible line to the clipboard"));
    controls->addWidget(copyButton);

    auto* clearButton = new QPushButton(tr("Clear"), container);
    controls->addWidget(clearButton);

    layout->addLayout(controls);

    m_view = new QPlainTextEdit(container);
    m_view->setReadOnly(true);
    m_view->setUndoRedoEnabled(false);
    m_view->setMaximumBlockCount(static_cast<int>(kMaxEntries));
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_view, 1);

    setWidget(container);

    connect(m_levelBox, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_minLevel = m_levelBox->itemData(index).toInt();
        rebuild();
    });
    connect(copyButton, &QPushButton::clicked, this, &LogDock::copyAllToClipboard);
    connect(clearButton, &QPushButton::clicked, this, &LogDock::clearLog);

    // Level colours are palette-relative, so they have to be recomputed when
    // the user flips the system theme while the app is running.
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) { rebuild(); });

    if (bridge) {
        connect(bridge.get(), &LogBridge::logged, this, &LogDock::appendMessage,
                Qt::QueuedConnection);
    }
}

void LogDock::appendMessage(int level, const QString& text)
{
    if (m_entries.size() >= kMaxEntries) {
        m_entries.pop_front();
    }
    const Entry entry{level, text};
    m_entries.push_back(entry);

    if (!passesFilter(level)) {
        return;
    }
    render(entry);
}

void LogDock::render(const Entry& entry)
{
    const bool atBottom =
        m_view->verticalScrollBar()->value() >= m_view->verticalScrollBar()->maximum() - 4;

    m_view->appendHtml(QStringLiteral("<span style=\"color:%1;\">%2</span>")
                           .arg(colorForLevel(entry.level), entry.text.toHtmlEscaped()));

    if (m_autoScroll->isChecked() || atBottom) {
        m_view->verticalScrollBar()->setValue(m_view->verticalScrollBar()->maximum());
    }
}

void LogDock::rebuild()
{
    m_view->clear();
    for (const Entry& entry : m_entries) {
        if (passesFilter(entry.level)) {
            render(entry);
        }
    }
}

bool LogDock::passesFilter(int level) const
{
    return level >= m_minLevel;
}

QString LogDock::colorForLevel(int level) const
{
    const bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;

    switch (static_cast<spdlog::level::level_enum>(level)) {
    case spdlog::level::trace:
    case spdlog::level::debug:
        return dark ? QStringLiteral("#8a94a6") : QStringLiteral("#6b7280");
    case spdlog::level::warn:
        return dark ? QStringLiteral("#ecc94b") : QStringLiteral("#a16207");
    case spdlog::level::err:
    case spdlog::level::critical:
        return dark ? QStringLiteral("#fc8181") : QStringLiteral("#c53030");
    default:
        break;
    }
    return palette().color(QPalette::Text).name();
}

void LogDock::clearLog()
{
    m_entries.clear();
    m_view->clear();
}

void LogDock::copyAllToClipboard()
{
    QStringList lines;
    lines.reserve(static_cast<qsizetype>(m_entries.size()));
    for (const Entry& entry : m_entries) {
        if (passesFilter(entry.level)) {
            lines.append(entry.text);
        }
    }
    QGuiApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    spdlog::info("Copied {} log line(s) to the clipboard", lines.size());
}

} // namespace dfu
