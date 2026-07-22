#include "archivepanel.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QTreeView>
#include <QVBoxLayout>

#include "../common/filepicker.h"
#include "archivetreedelegate.h"
#include "archivetreemodel.h"

ArchivePanel::ArchivePanel(const QString &roleLabel, QWidget *parent)
    : QWidget(parent)
    , roleLabel_(roleLabel)
{
    setAcceptDrops(true);
    setAttribute(Qt::WA_StyledBackground, true);

    auto *layout = new QVBoxLayout(this);

    auto *header = new QWidget;
    header->setObjectName("treeCardHeader");
    header->setAttribute(Qt::WA_StyledBackground, true);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(10, 8, 10, 8);
    fileNameLabel_ = new QLabel(tr("%1: (drop a .zip file here)").arg(roleLabel_));
    fileNameLabel_->setWordWrap(true);
    fileNameLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    // Fixed at exactly 2 lines' height, regardless of the current text, so
    // a long archive name that wraps in one panel doesn't push that
    // panel's tree down relative to the other's - both trees always start
    // at the same Y position because both labels reserve the same height.
    fileNameLabel_->setFixedHeight(fileNameLabel_->fontMetrics().height() * 2);
    changeButton_ = new QPushButton(tr("Change..."));
    headerLayout->addWidget(fileNameLabel_, 1);
    headerLayout->addWidget(changeButton_);

    treeView_ = new QTreeView;
    treeView_->setHeaderHidden(true);
    model_ = new ArchiveTreeModel(this);
    treeView_->setModel(model_);
    treeView_->setItemDelegate(new ArchiveTreeDelegate(treeView_));

    layout->addWidget(header);
    layout->addWidget(treeView_, 1);

    connect(changeButton_, &QPushButton::clicked, this, &ArchivePanel::pickArchive);
}

void ArchivePanel::pickArchive()
{
    const QString path = FilePicker::pickFile(this, tr("Select %1 archive").arg(roleLabel_),
                                               tr("Zip archives (*.zip)"));
    if (path.isEmpty()) {
        return;
    }
    setArchivePath(path);
}

void ArchivePanel::setArchivePath(const QString &path)
{
    archivePath_ = path;
    fileNameLabel_->setText(tr("%1: %2").arg(roleLabel_, QFileInfo(path).fileName()));
    emit archiveChanged(path);
}

void ArchivePanel::showEntries(const QVector<ArchiveEntry> &entries)
{
    model_->buildFrom(entries);
    treeView_->expandAll();
}

void ArchivePanel::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void ArchivePanel::dropEvent(QDropEvent *event)
{
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty()) {
        return;
    }
    const QString path = urls.first().toLocalFile();
    if (path.isEmpty()) {
        return;
    }
    setArchivePath(path);
}
