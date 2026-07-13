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

    auto *layout = new QVBoxLayout(this);

    auto *header = new QWidget;
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(4, 4, 4, 4);
    fileNameLabel_ = new QLabel(tr("%1: (drop a .zip file here)").arg(roleLabel_));
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
