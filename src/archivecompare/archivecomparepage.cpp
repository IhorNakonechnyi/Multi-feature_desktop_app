#include "archivecomparepage.h"

#include <QLabel>
#include <QMessageBox>
#include <QSplitter>
#include <QVBoxLayout>

#include "archivepanel.h"
#include "archivereader.h"
#include "crccomparator.h"

ArchiveComparePage::ArchiveComparePage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    auto *title = new QLabel(tr("Archive compare"));
    layout->addWidget(title);

    auto *splitter = new QSplitter(Qt::Horizontal);
    panelA_ = new ArchivePanel(tr("New (A)"));
    panelB_ = new ArchivePanel(tr("Old (B)"));
    splitter->addWidget(panelA_);
    splitter->addWidget(panelB_);
    layout->addWidget(splitter, 1);

    connect(panelA_, &ArchivePanel::archiveChanged, this, &ArchiveComparePage::runComparison);
    connect(panelB_, &ArchivePanel::archiveChanged, this, &ArchiveComparePage::runComparison);
}

void ArchiveComparePage::runComparison()
{
    const QString pathA = panelA_->archivePath();
    const QString pathB = panelB_->archivePath();
    if (pathA.isEmpty() || pathB.isEmpty()) {
        return; // wait until both archives are picked
    }

    // Each archive is read independently so a failure on one side (FR-008:
    // corrupt, password-protected, or otherwise unreadable) is reported for
    // that archive specifically, without crashing and without blocking a
    // perfectly fine archive on the other side.
    QVector<ArchiveReader::RawFileEntry> entriesA;
    bool readA = true;
    try {
        entriesA = ArchiveReader::readArchive(pathA);
    } catch (const ArchiveOpenError &error) {
        readA = false;
        QMessageBox::warning(this, tr("Archive A could not be opened"), error.message());
    }

    QVector<ArchiveReader::RawFileEntry> entriesB;
    bool readB = true;
    try {
        entriesB = ArchiveReader::readArchive(pathB);
    } catch (const ArchiveOpenError &error) {
        readB = false;
        QMessageBox::warning(this, tr("Archive B could not be opened"), error.message());
    }

    if (!readA || !readB) {
        return; // can't produce a meaningful comparison with one side unreadable
    }

    const ArchiveComparisonResult result = CrcComparator::compare(entriesA, entriesB);
    panelA_->showEntries(result.entriesA);
    panelB_->showEntries(result.entriesB);
}
