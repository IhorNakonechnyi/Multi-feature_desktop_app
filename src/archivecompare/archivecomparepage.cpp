#include "archivecomparepage.h"

#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include "../gerber/compositeview.h"
#include "../gerber/gerberobjectpaths.h"
#include "../gerber/gerberparser.h"
#include "../gerber/layermaskrenderer.h"
#include "../gerber/maskcompositor.h"
#include "archivepanel.h"
#include "archivereader.h"
#include "crccomparator.h"

namespace {

// TEMPORARY (per Igor, 2026-07-15): forces the Gerber diff section visible
// regardless of whether any eligible pair exists, so the 70/30 layout is
// visible without needing a qualifying archive pair loaded first. Revert
// to false once real gating is being exercised again.
constexpr bool kAlwaysShowGerberDiffSection = true;

// Same copper green as the standalone Gerber viewer page - one layer,
// one familiar color, until M4's overlay brings per-layer colors.
const QColor kLayerColor(60, 220, 60);

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

    // Bottom area: the Gerber view section. Fixed rather than a resizable
    // splitter, matching the tree panes above.
    gerberDiffContent_ = new QWidget;
    auto *gerberDiffLayout = new QHBoxLayout(gerberDiffContent_);
    gerberDiffLayout->setContentsMargins(0, 0, 0, 0);

    auto *leftColumn = new QWidget;
    auto *leftColumnLayout = new QVBoxLayout(leftColumn);
    leftColumnLayout->setContentsMargins(4, 4, 4, 4);
    auto *explanation = new QLabel(
        tr("All files in archive A. Select one and click Show to render it as a Gerber layer."));
    explanation->setWordWrap(true);
    showButton_ = new QPushButton(tr("Show"));
    showButton_->setEnabled(false);
    renderTimeLabel_ = new QLabel(tr("Render time: -"));
    fileListA_ = new QListWidget;
    leftColumnLayout->addWidget(explanation);
    leftColumnLayout->addWidget(fileListA_, 1);
    leftColumnLayout->addWidget(showButton_);
    leftColumnLayout->addWidget(renderTimeLabel_);

    gerberCanvas_ = new CompositeView;

    gerberDiffLayout->addWidget(leftColumn, 3);
    gerberDiffLayout->addWidget(gerberCanvas_, 7);
    gerberDiffContent_->setVisible(kAlwaysShowGerberDiffSection);

    layout->addWidget(gerberDiffContent_, 3);

    // Archive A alone is enough to populate the debugging file list;
    // the two-sided comparison still only runs once both are picked.
    connect(panelA_, &ArchivePanel::archiveChanged, this, [this]() {
        populateFileList();
        runComparison();
    });
    connect(panelB_, &ArchivePanel::archiveChanged, this, &ArchiveComparePage::runComparison);
    connect(fileListA_, &QListWidget::currentRowChanged, this,
            [this](int row) { showButton_->setEnabled(row >= 0); });
    connect(showButton_, &QPushButton::clicked, this, &ArchiveComparePage::showSelectedFile);
    connect(gerberCanvas_, &CompositeView::renderRequested, this, &ArchiveComparePage::renderShownLayer);
}

void ArchiveComparePage::populateFileList()
{
    fileListA_->clear();
    filePathsA_.clear();
    showButton_->setEnabled(false);

    const QString pathA = panelA_->archivePath();
    if (pathA.isEmpty()) {
        return;
    }
    QVector<ArchiveReader::RawFileEntry> entriesA;
    try {
        entriesA = ArchiveReader::readArchive(pathA);
    } catch (const ArchiveOpenError &) {
        // Unreadable archive: the list just stays empty. runComparison
        // reports the error dialog once both archives are picked.
        return;
    }
    for (const auto &entry : entriesA) {
        filePathsA_.append(entry.relativePath);
        fileListA_->addItem(entry.relativePath);
    }
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

void ArchiveComparePage::showSelectedFile()
{
    const int row = fileListA_->currentRow();
    if (row < 0 || row >= filePathsA_.size()) {
        return;
    }

    // Current v2 debugging stage: render the selected archive-A file,
    // alone, through the new mask pipeline. The old-vs-new overlay
    // compare is M4, gated on this rendering passing visual inspection.
    const QByteArray bytes = ArchiveReader::readEntryBytes(panelA_->archivePath(), filePathsA_.at(row));
    if (bytes.isEmpty()) {
        renderTimeLabel_->setText(tr("Could not read the file from archive A."));
        return;
    }

    shownLayer_ = GerberParser::parseContentToObjects(QString::fromUtf8(bytes));
    const QRectF boundsMm = GerberObjectPaths::layerBoundsMm(shownLayer_);
    if (boundsMm.isEmpty()) {
        renderTimeLabel_->setText(tr("No drawable Gerber content in archive A's file."));
        return;
    }
    gerberCanvas_->fitTo(boundsMm); // emits renderRequested -> renderShownLayer
}

void ArchiveComparePage::renderShownLayer(const QRectF &windowMm, double pxPerMm)
{
    if (shownLayer_.objects.isEmpty()) {
        return;
    }
    // Synchronous on purpose - the render-time label makes this the
    // measurement the constitution asks for before any threading work.
    QElapsedTimer timer;
    timer.start();
    const LayerMask mask = LayerMaskRenderer::render(shownLayer_, windowMm, pxPerMm);
    const QImage composite = MaskCompositor::composite({{mask, kLayerColor}});
    gerberCanvas_->setComposite(composite, windowMm);
    renderTimeLabel_->setText(tr("Render time: %1 ms").arg(timer.elapsed()));
}
