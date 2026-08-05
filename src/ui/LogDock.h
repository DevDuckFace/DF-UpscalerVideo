#pragma once

#include <QDockWidget>
#include <QString>

#include <deque>
#include <memory>

class QCheckBox;
class QComboBox;
class QPlainTextEdit;

namespace dfu {

class LogBridge;

// Bottom dock: the live log view. Keeps a bounded backlog of raw entries so
// the level filter can re-render without having to replay spdlog.
class LogDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit LogDock(const std::shared_ptr<LogBridge>& bridge, QWidget* parent = nullptr);

public slots:
    void appendMessage(int level, const QString& text);
    void clearLog();
    void copyAllToClipboard();

private:
    struct Entry
    {
        int level = 0;
        QString text;
    };

    void rebuild();
    void render(const Entry& entry);
    QString colorForLevel(int level) const;
    bool passesFilter(int level) const;

    static constexpr std::size_t kMaxEntries = 5000;

    QPlainTextEdit* m_view = nullptr;
    QComboBox* m_levelBox = nullptr;
    QCheckBox* m_autoScroll = nullptr;

    std::deque<Entry> m_entries;
    int m_minLevel = 0;
};

} // namespace dfu
