#include "archiveheader.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "../common/filepicker.h"
#include "archivedroputil.h"

ArchiveHeader::ArchiveHeader(const QString &roleLabel, QWidget *parent)
    : QWidget(parent)
    , roleLabel_(roleLabel)
{
    setAcceptDrops(true);
    setAttribute(Qt::WA_StyledBackground, true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *header = new QWidget;
    header->setObjectName("treeCardHeader");
    header->setAttribute(Qt::WA_StyledBackground, true);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(10, 8, 10, 8);

    fileNameLabel_ = new QLabel(tr("%1: (drop a .zip file here)").arg(roleLabel_));
    fileNameLabel_->setWordWrap(true);
    fileNameLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    // Fixed at exactly 2 lines' height, regardless of the current text, so a
    // long archive name that wraps on one side doesn't push that side's tree
    // down relative to the other's - both trees start at the same Y because
    // both labels reserve the same height.
    fileNameLabel_->setFixedHeight(fileNameLabel_->fontMetrics().height() * 2);

    changeButton_ = new QPushButton(tr("Change..."));
    headerLayout->addWidget(fileNameLabel_, 1);
    headerLayout->addWidget(changeButton_);

    // Hidden until showError() sets it.
    errorLabel_ = new QLabel;
    errorLabel_->setObjectName("detailLabel");
    errorLabel_->setWordWrap(true);
    errorLabel_->setVisible(false);

    layout->addWidget(header);
    layout->addWidget(errorLabel_);

    connect(changeButton_, &QPushButton::clicked, this, &ArchiveHeader::pickArchive);
}

void ArchiveHeader::pickArchive()
{
    const QString path = FilePicker::pickFile(this, tr("Select %1 archive").arg(roleLabel_),
                                               tr("Zip archives (*.zip)"));
    if (path.isEmpty()) {
        return;
    }
    setArchivePath(path);
}

void ArchiveHeader::setArchivePath(const QString &path)
{
    archivePath_ = path;
    fileNameLabel_->setText(tr("%1: %2").arg(roleLabel_, QFileInfo(path).fileName()));
    emit archiveChanged(path);
}

void ArchiveHeader::showError(const QString &message)
{
    errorLabel_->setText(message);
    errorLabel_->setVisible(true);
}

void ArchiveHeader::clearError()
{
    errorLabel_->setVisible(false);
}

void ArchiveHeader::dragEnterEvent(QDragEnterEvent *event)
{
    // Not accepting is what makes the OS show the no-drop cursor, so an
    // invalid drag is refused before it is ever released (Q10).
    if (!singleDroppedZipPath(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
    }
}

void ArchiveHeader::dropEvent(QDropEvent *event)
{
    // Re-validated rather than trusting dragEnter: the two events carry
    // separate mime data, and the set of files can change between them.
    const QString path = singleDroppedZipPath(event->mimeData());
    if (path.isEmpty()) {
        return;
    }
    setArchivePath(path);
}
