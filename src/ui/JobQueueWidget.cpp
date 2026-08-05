#include "ui/JobQueueWidget.h"

#include "pipeline/JobQueue.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QHeaderView>
#include <QMimeData>
#include <QProgressBar>
#include <QUrl>

namespace {

QString formatEta(qint64 seconds)
{
    if (seconds < 0) {
        return QStringLiteral("-");
    }
    if (seconds < 60) {
        return QObject::tr("%1s").arg(seconds);
    }
    const qint64 minutes = seconds / 60;
    if (minutes < 60) {
        return QObject::tr("%1m %2s").arg(minutes).arg(seconds % 60);
    }
    return QObject::tr("%1h %2m").arg(minutes / 60).arg(minutes % 60);
}

} // namespace

namespace dfu {

JobQueueWidget::JobQueueWidget(JobQueue* queue, QWidget* parent)
    : QTableWidget(parent)
    , m_queue(queue)
{
    setColumnCount(ColumnCount);
    setHorizontalHeaderLabels({tr("File"), tr("Input"), tr("Output"), tr("FPS"), tr("Status"),
                               tr("Progress"), tr("ETA")});

    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setAlternatingRowColors(true);
    verticalHeader()->setVisible(false);
    horizontalHeader()->setSectionResizeMode(ColFile, QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(ColProgress, QHeaderView::Fixed);
    setColumnWidth(ColProgress, 160);
    setShowGrid(false);

    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DropOnly);

    connect(m_queue, &JobQueue::jobsChanged, this, &JobQueueWidget::rebuild);
    connect(m_queue, &JobQueue::jobUpdated, this, &JobQueueWidget::updateRow);

    rebuild();
}

int JobQueueWidget::selectedJobIndex() const
{
    const QModelIndexList selected = selectionModel()->selectedRows();
    return selected.isEmpty() ? -1 : selected.first().row();
}

void JobQueueWidget::rebuild()
{
    const int previous = selectedJobIndex();

    setRowCount(m_queue->count());
    for (int i = 0; i < m_queue->count(); ++i) {
        for (int column = 0; column < ColumnCount; ++column) {
            if (column == ColProgress) {
                continue;
            }
            if (!item(i, column)) {
                setItem(i, column, new QTableWidgetItem);
            }
        }
        if (!cellWidget(i, ColProgress)) {
            auto* bar = new QProgressBar(this);
            bar->setRange(0, 100);
            bar->setTextVisible(true);
            setCellWidget(i, ColProgress, bar);
        }
        updateRow(i);
    }

    if (previous >= 0 && previous < m_queue->count()) {
        selectRow(previous);
    }
}

void JobQueueWidget::updateRow(int index)
{
    const Job* job = m_queue->jobAt(index);
    if (!job || index >= rowCount()) {
        return;
    }

    item(index, ColFile)->setText(QFileInfo(job->spec.inputPath).fileName());
    item(index, ColFile)->setToolTip(job->spec.inputPath);

    item(index, ColInput)->setText(job->info.valid ? job->info.resolutionString()
                                                   : QStringLiteral("-"));
    item(index, ColOutput)->setText(job->outputWidth > 0
                                        ? QStringLiteral("%1x%2")
                                              .arg(job->outputWidth)
                                              .arg(job->outputHeight)
                                        : QStringLiteral("-"));
    item(index, ColFps)->setText(job->info.valid ? job->info.fpsString() : QStringLiteral("-"));

    QString status = jobStatusText(job->status);
    if (job->status == JobStatus::Running && job->fps > 0.0) {
        status = tr("Running (%1 fps)").arg(QString::number(job->fps, 'f', 1));
    }
    item(index, ColStatus)->setText(status);
    item(index, ColStatus)->setToolTip(job->message);

    if (auto* bar = qobject_cast<QProgressBar*>(cellWidget(index, ColProgress))) {
        bar->setValue(job->progressPercent());
    }

    item(index, ColEta)->setText(job->status == JobStatus::Running ? formatEta(job->etaSeconds)
                                                                  : QStringLiteral("-"));
}

void JobQueueWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        return;
    }
    QTableWidget::dragEnterEvent(event);
}

void JobQueueWidget::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        return;
    }
    QTableWidget::dragMoveEvent(event);
}

void JobQueueWidget::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasUrls()) {
        QTableWidget::dropEvent(event);
        return;
    }

    QStringList paths;
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls) {
        if (url.isLocalFile()) {
            paths << url.toLocalFile();
        }
    }

    if (!paths.isEmpty()) {
        emit filesDropped(paths);
    }
    event->acceptProposedAction();
}

} // namespace dfu
