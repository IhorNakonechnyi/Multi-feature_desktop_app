#include "archivecomparepage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QSplitter>
#include <QVBoxLayout>

#include "../gerber/gerberdiffrenderer.h"
#include "archivepanel.h"
#include "archivereader.h"
#include "crccomparator.h"
#include "gerberdifftrigger.h"

namespace {

// TEMPORARY (per Igor, 2026-07-15): forces the Gerber diff section visible
// regardless of whether any eligible pair exists, so the 70/30 layout is
// visible without needing a qualifying archive pair loaded first. Revert
// to false once real gating is being exercised again.
constexpr bool kAlwaysShowGerberDiffSection = true;

// "folder/path/.../filename" - the folder portion (possibly empty, for a
// root-level file) with a literal "/..." marker, then the plain filename
// on its own line, so a deep folder tree doesn't crowd out the filename.
QString formatPathLines(const QString &relativePath)
{
    const int slash = relativePath.lastIndexOf(QLatin1Char('/'));
    const QString folder = slash < 0 ? QString() : relativePath.left(slash);
    const QString filename = slash < 0 ? relativePath : relativePath.mid(slash + 1);
    return folder + QLatin1String("/...\n") + filename;
}

} // namespace

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
    layout->addWidget(splitter, 7);

    // Reserved area for the Gerber diff UI (Phase 3b.1) - fixed rather than
    // a resizable splitter, matching the tree panes above. gerberDiffContent_
    // starts hidden and only appears once runComparison finds at least one
    // eligible pair (FR-006); until then this area is blank, same as before
    // this feature existed.
    gerberDiffContent_ = new QWidget;
    auto *gerberDiffLayout = new QHBoxLayout(gerberDiffContent_);
    gerberDiffLayout->setContentsMargins(0, 0, 0, 0);

    auto *leftColumn = new QWidget;
    auto *leftColumnLayout = new QVBoxLayout(leftColumn);
    leftColumnLayout->setContentsMargins(4, 4, 4, 4);
    auto *explanation = new QLabel(
        tr("One or more Gerber layer files changed between the two archives. "
           "Select a pair below to view it."));
    explanation->setWordWrap(true);
    auto *renderTimeLabel = new QLabel(tr("Render time: -"));
    gerberPairList_ = new QListWidget;
    leftColumnLayout->addWidget(explanation);
    leftColumnLayout->addWidget(gerberPairList_, 1);
    leftColumnLayout->addWidget(renderTimeLabel);

    gerberCanvas_ = new GerberDiffRenderer;

    gerberDiffLayout->addWidget(leftColumn, 3);
    gerberDiffLayout->addWidget(gerberCanvas_, 7);
    gerberDiffContent_->setVisible(kAlwaysShowGerberDiffSection);

    layout->addWidget(gerberDiffContent_, 3);

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

    eligibleGerberPairs_ = GerberDiffTrigger::eligiblePairs(result.differingPairs);
    gerberPairList_->clear();
    for (const auto &pair : eligibleGerberPairs_) {
        const QString itemText = tr("Path to file in archive A:\n%1\n\nPath to file in archive B:\n%2")
                                      .arg(formatPathLines(pair.relativePathA), formatPathLines(pair.relativePathB));
        gerberPairList_->addItem(itemText);
    }
    gerberDiffContent_->setVisible(kAlwaysShowGerberDiffSection || !eligibleGerberPairs_.isEmpty());
}
