#pragma once

#include <QTableWidget>

namespace dfu {

class JobQueue;

// Central widget: one row per job. Accepts dropped files.
class JobQueueWidget : public QTableWidget
{
    Q_OBJECT

public:
    explicit JobQueueWidget(JobQueue* queue, QWidget* parent = nullptr);

    int selectedJobIndex() const;

signals:
    void filesDropped(const QStringList& paths);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void rebuild();
    void updateRow(int index);

private:
    enum Column
    {
        ColFile = 0,
        ColInput,
        ColOutput,
        ColFps,
        ColStatus,
        ColProgress,
        ColEta,
        ColumnCount
    };

    JobQueue* m_queue = nullptr;
};

} // namespace dfu
