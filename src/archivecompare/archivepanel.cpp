#include "archivepanel.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QLabel>
#include <QStackedLayout>
#include <QTreeView>

#include "archivedroputil.h"
#include "archivetreedelegate.h"
#include "archivetreemodel.h"
#ifdef PCB_TIMING // TIMING-DIAG
#include <functional>

#include <QElapsedTimer>

#include "stagetiming.h"
#endif

ArchivePanel::ArchivePanel(QWidget *parent)
    : QWidget(parent)
{
    setAcceptDrops(true);
    setAttribute(Qt::WA_StyledBackground, true);

    treeView_ = new QTreeView;
    treeView_->setHeaderHidden(true);
    // Without this, the single column stretches to fill the viewport and a
    // long file/folder name just gets clipped in place - there's nothing to
    // scroll to because Qt never considers the column "wider than the
    // viewport". Sizing the column to its actual content instead makes it
    // genuinely wider than the panel when names are long, which is what
    // makes the horizontal scrollbar appear and actually do something.
    treeView_->header()->setStretchLastSection(false);
    model_ = new ArchiveTreeModel(this);
    treeView_->setModel(model_);
    treeView_->setItemDelegate(new ArchiveTreeDelegate(treeView_));

    emptyLabel_ = new QLabel;
    emptyLabel_->setObjectName("detailLabel");
    emptyLabel_->setAlignment(Qt::AlignCenter);
    emptyLabel_->setWordWrap(true);

    // A stack rather than hiding/showing widgets in a box layout: the tree
    // and the message occupy the same space, so the panel never changes size
    // when one replaces the other.
    stack_ = new QStackedLayout(this);
    stack_->setContentsMargins(0, 0, 0, 0);
    stack_->addWidget(treeView_);  // index 0
    stack_->addWidget(emptyLabel_); // index 1
    stack_->setCurrentIndex(0);
}

void ArchivePanel::showEntries(const QVector<ArchiveEntry> &entries)
{
#ifdef PCB_TIMING // TIMING-DIAG
    QElapsedTimer timer;
    timer.start();
#endif
    model_->buildFrom(entries);
#ifdef PCB_TIMING // TIMING-DIAG
    addBuildTime(timer);
#endif
    treeView_->expandAll();
#ifdef PCB_TIMING // TIMING-DIAG
    StageTiming::treeTimes().expandMs += timer.restart();
#endif
    // Column must be re-sized to the new content every time - a fresh
    // archive's names are a different width than the last one's.
    treeView_->resizeColumnToContents(0);
#ifdef PCB_TIMING // TIMING-DIAG
    StageTiming::treeTimes().widthMs += timer.elapsed();
#endif
}

void ArchivePanel::setFilter(const TreeFilter &filter)
{
#ifdef PCB_TIMING // TIMING-DIAG
    QElapsedTimer timer;
    timer.start();
#endif
    model_->setFilter(filter);
#ifdef PCB_TIMING // TIMING-DIAG
    addBuildTime(timer);
#endif
    treeView_->expandAll();
#ifdef PCB_TIMING // TIMING-DIAG
    StageTiming::treeTimes().expandMs += timer.restart();
#endif
    treeView_->resizeColumnToContents(0);
#ifdef PCB_TIMING // TIMING-DIAG
    StageTiming::treeTimes().widthMs += timer.elapsed();
#endif
}

#ifdef PCB_TIMING // TIMING-DIAG
void ArchivePanel::addBuildTime(QElapsedTimer &timer)
{
    StageTiming::treeTimes().buildMs += timer.restart();
    ++StageTiming::treeTimes().builds;
}

int ArchivePanel::shownRowCount() const
{
    // Every row the view holds once expanded, folders included - what
    // expandAll and resizeColumnToContents have to walk.
    int count = 0;
    const std::function<void(const QModelIndex &)> walk = [&](const QModelIndex &parent) {
        const int rows = model_->rowCount(parent);
        count += rows;
        for (int row = 0; row < rows; ++row) {
            walk(model_->index(row, 0, parent));
        }
    };
    walk(QModelIndex());
    return count;
}

QWidget *ArchivePanel::treeViewport() const
{
    return treeView_->viewport();
}
#endif

bool ArchivePanel::isEmptyAfterFilter() const
{
    return model_->isEmptyAfterFilter();
}

void ArchivePanel::showEmptyMessage(const QString &message)
{
    if (message.isEmpty()) {
        stack_->setCurrentIndex(0);
        return;
    }
    emptyLabel_->setText(message);
    stack_->setCurrentIndex(1);
}

void ArchivePanel::dragEnterEvent(QDragEnterEvent *event)
{
    if (!singleDroppedZipPath(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
    }
}

void ArchivePanel::dropEvent(QDropEvent *event)
{
    const QString path = singleDroppedZipPath(event->mimeData());
    if (path.isEmpty()) {
        return;
    }
    emit archiveDropped(path);
}
