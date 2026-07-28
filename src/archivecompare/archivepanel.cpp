#include "archivepanel.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
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

    // Hidden until showError() sets it - inline replacement for the modal
    // warning dialog this panel used to be notified via (Q7: errors are
    // data, only display code shows anything).
    errorLabel_ = new QLabel;
    errorLabel_->setObjectName("detailLabel");
    errorLabel_->setWordWrap(true);
    errorLabel_->setVisible(false);

    treeView_ = new QTreeView;
    treeView_->setHeaderHidden(true);
    // Without this, the single column stretches to fill the viewport and a
    // long file/folder name just gets clipped in place - there's nothing
    // to scroll to because Qt never considers the column "wider than the
    // viewport". Sizing the column to its actual content instead makes it
    // genuinely wider than the panel when names are long, which is what
    // makes the horizontal scrollbar appear and actually do something.
    treeView_->header()->setStretchLastSection(false);
    model_ = new ArchiveTreeModel(this);
    treeView_->setModel(model_);
    treeView_->setItemDelegate(new ArchiveTreeDelegate(treeView_));

    layout->addWidget(header);
    layout->addWidget(errorLabel_);
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
    // Column must be re-sized to the new content every time - a fresh
    // archive's names are a different width than the last one's.
    treeView_->resizeColumnToContents(0);
}

void ArchivePanel::showError(const QString &message)
{
    errorLabel_->setText(message);
    errorLabel_->setVisible(true);
    // An error means this archive produced no usable entries this run - a
    // tree left over from the last successful read would sit there looking
    // current while actually describing a stale selection.
    model_->buildFrom({});
}

void ArchivePanel::clearError()
{
    errorLabel_->setVisible(false);
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
