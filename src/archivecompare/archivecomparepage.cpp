#include "archivecomparepage.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include "../gerber/compositeview.h"
#include "../gerber/gerberfiletype.h"
#include "../gerber/gerberobjectpaths.h"
#include "../gerber/gerberparser.h"
#include "../gerber/layermaskrenderer.h"
#include "../gerber/maskcompositor.h"
#include "archivepanel.h"
#include "archivereader.h"
#include "crccomparator.h"
#include "gerberdifftrigger.h"

namespace {

// New (A) in green, old (B) in red - MaskCompositor's additive overlap
// (CompositionMode_Plus) turns unchanged copper yellow, matching
// CAMMaster's compare view (M4 design doc, section 2). Fixed for M4; a
// color picker is an explicit non-goal until the overlay is in daily use.
const QColor kColorA(60, 220, 60);
const QColor kColorB(220, 60, 60);

// One list row's label: the A-side path, plus " <-> B-path" only when the
// two sides differ (a filename-fallback match can pair files at different
// paths - see the path-tolerant-matching design doc).
QString formatPairLabel(const DifferingPair &pair)
{
    if (pair.relativePathA == pair.relativePathB) {
        return pair.relativePathA;
    }
    return QObject::tr("%1  <->  %2").arg(pair.relativePathA, pair.relativePathB);
}

// Distinct lowercase file extensions among unrecognized pairs' paths (both
// sides) - what the "Add extension" buttons are built from.
// QFileInfo::suffix() matches the extraction GerberFileType::isRecognized
// itself uses.
QStringList distinctUnrecognizedExtensions(const QVector<DifferingPair> &unrecognizedPairs)
{
    QStringList extensions;
    for (const auto &pair : unrecognizedPairs) {
        for (const QString &path : {pair.relativePathA, pair.relativePathB}) {
            const QString extension = QFileInfo(path).suffix().toLower();
            if (!extension.isEmpty() && !GerberFileType::isRecognized(path) && !extensions.contains(extension)) {
                extensions.append(extension);
            }
        }
    }
    return extensions;
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

    // Bottom area: the Gerber overlay-compare section. Fixed rather than a
    // resizable splitter, matching the tree panes above. Starts hidden;
    // runComparison() shows it once there's at least one changed pair
    // worth telling the user about (eligible, drill, or unrecognized).
    gerberDiffContent_ = new QWidget;
    auto *gerberDiffLayout = new QHBoxLayout(gerberDiffContent_);
    gerberDiffLayout->setContentsMargins(0, 0, 0, 0);

    auto *leftColumn = new QWidget;
    auto *leftColumnLayout = new QVBoxLayout(leftColumn);
    leftColumnLayout->setContentsMargins(4, 4, 4, 4);
    auto *explanation = new QLabel(
        tr("Changed Gerber files. Select a pair and click Show to compare old vs new."));
    explanation->setWordWrap(true);
    gerberPairList_ = new QListWidget;
    pairStatusLabel_ = new QLabel;
    pairStatusLabel_->setWordWrap(true);

    // Shown only when runComparison() finds changed pairs it can't compare
    // yet: unrecognized extensions (with an Add button, ported from the
    // 3b2 parts bin) or drill files (informational only - this app's
    // Gerber parser doesn't speak Excellon yet, see the design doc's M5).
    // Both hidden by default; runComparison() shows/hides and repopulates.
    skippedUnrecognizedLabel_ = new QLabel;
    skippedUnrecognizedLabel_->setWordWrap(true);
    skippedUnrecognizedLabel_->setVisible(false);
    skippedUnrecognizedButtonRow_ = new QWidget;
    auto *skippedButtonLayout = new QHBoxLayout(skippedUnrecognizedButtonRow_);
    skippedButtonLayout->setContentsMargins(0, 0, 0, 0);
    skippedUnrecognizedButtonRow_->setVisible(false);
    skippedDrillLabel_ = new QLabel;
    skippedDrillLabel_->setWordWrap(true);
    skippedDrillLabel_->setVisible(false);

    showButton_ = new QPushButton(tr("Show"));
    showButton_->setEnabled(false);
    renderTimeLabel_ = new QLabel(tr("Render time: -"));
    auto *legend = new QLabel(tr("green = only new  |  red = only old  |  yellow = same"));

    leftColumnLayout->addWidget(explanation);
    leftColumnLayout->addWidget(gerberPairList_, 1);
    leftColumnLayout->addWidget(pairStatusLabel_);
    leftColumnLayout->addWidget(skippedUnrecognizedLabel_);
    leftColumnLayout->addWidget(skippedUnrecognizedButtonRow_);
    leftColumnLayout->addWidget(skippedDrillLabel_);
    leftColumnLayout->addWidget(showButton_);
    leftColumnLayout->addWidget(renderTimeLabel_);
    leftColumnLayout->addWidget(legend);

    gerberCanvas_ = new CompositeView;

    gerberDiffLayout->addWidget(leftColumn, 3);
    gerberDiffLayout->addWidget(gerberCanvas_, 7);
    gerberDiffContent_->setVisible(false);

    layout->addWidget(gerberDiffContent_, 3);

    connect(panelA_, &ArchivePanel::archiveChanged, this, &ArchiveComparePage::runComparison);
    connect(panelB_, &ArchivePanel::archiveChanged, this, &ArchiveComparePage::runComparison);
    connect(gerberPairList_, &QListWidget::currentRowChanged, this,
            [this](int row) { showButton_->setEnabled(row >= 0); });
    connect(showButton_, &QPushButton::clicked, this, &ArchiveComparePage::showSelectedPair);
    connect(gerberCanvas_, &CompositeView::renderRequested, this, &ArchiveComparePage::renderShownPair);
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

    eligiblePairs_ = GerberDiffTrigger::eligiblePairs(result.differingPairs);
    gerberPairList_->clear();
    for (const auto &pair : eligiblePairs_) {
        gerberPairList_->addItem(formatPairLabel(pair));
    }
    showButton_->setEnabled(false);
    pairStatusLabel_->clear();

    const QVector<DifferingPair> unrecognized = GerberDiffTrigger::unrecognizedPairs(result.differingPairs);
    const QVector<DifferingPair> drill = GerberDiffTrigger::drillPairs(result.differingPairs);
    rebuildSkippedPairsUi(unrecognized, drill);

    gerberDiffContent_->setVisible(!eligiblePairs_.isEmpty() || !unrecognized.isEmpty() || !drill.isEmpty());
}

void ArchiveComparePage::rebuildSkippedPairsUi(const QVector<DifferingPair> &unrecognized,
                                               const QVector<DifferingPair> &drill)
{
    // Clear old buttons before rebuilding - runComparison() re-runs on
    // every archive change, and the previous run's buttons captured the
    // previous run's lambda state.
    QLayoutItem *item;
    while ((item = skippedUnrecognizedButtonRow_->layout()->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    if (unrecognized.isEmpty()) {
        skippedUnrecognizedLabel_->setVisible(false);
        skippedUnrecognizedButtonRow_->setVisible(false);
    } else {
        // Visibility is driven by `unrecognized`, not the derived
        // extension list: a skipped pair with no extension on either side
        // (or one already covered by a just-added extension) yields no
        // button, but is still genuinely skipped and must not make the
        // label - the user's only signal something was dropped - disappear.
        skippedUnrecognizedLabel_->setText(
            tr("%1 changed pair(s) skipped - unrecognized file extension:").arg(unrecognized.size()));
        skippedUnrecognizedLabel_->setVisible(true);
        for (const QString &extension : distinctUnrecognizedExtensions(unrecognized)) {
            auto *addButton = new QPushButton(tr("Add .%1").arg(extension));
            connect(addButton, &QPushButton::clicked, this, [this, extension]() {
                GerberFileType::addUserExtension(extension);
                runComparison();
            });
            skippedUnrecognizedButtonRow_->layout()->addWidget(addButton);
        }
        skippedUnrecognizedButtonRow_->setVisible(true);
    }

    if (drill.isEmpty()) {
        skippedDrillLabel_->setVisible(false);
    } else {
        // No button: an unrecognized extension can be taught to the app,
        // but this app's Gerber parser genuinely cannot read Excellon
        // drill files yet (M5 in the design doc) - nothing the user clicks
        // here would help.
        skippedDrillLabel_->setText(
            tr("%1 drill file pair(s) skipped - drill compare comes in a later version.").arg(drill.size()));
        skippedDrillLabel_->setVisible(true);
    }
}

void ArchiveComparePage::showSelectedPair()
{
    const int row = gerberPairList_->currentRow();
    if (row < 0 || row >= eligiblePairs_.size()) {
        return;
    }
    const DifferingPair &pair = eligiblePairs_.at(row);

    const QByteArray bytesA = ArchiveReader::readEntryBytes(panelA_->archivePath(), pair.relativePathA);
    const QByteArray bytesB = ArchiveReader::readEntryBytes(panelB_->archivePath(), pair.relativePathB);

    shownLayerA_ = bytesA.isEmpty() ? Layer{} : GerberParser::parseContentToObjects(QString::fromUtf8(bytesA));
    shownLayerB_ = bytesB.isEmpty() ? Layer{} : GerberParser::parseContentToObjects(QString::fromUtf8(bytesB));

    // Each side is judged independently: an empty-but-readable file (or a
    // read failure - bytes.isEmpty() covers both, ArchiveReader::
    // readEntryBytes never throws) parses to a Layer with no objects,
    // which GerberObjectPaths::layerBoundsMm correctly reports as empty
    // bounds either way.
    const QRectF boundsA = GerberObjectPaths::layerBoundsMm(shownLayerA_);
    const QRectF boundsB = GerberObjectPaths::layerBoundsMm(shownLayerB_);
    const QRectF unitedBounds = boundsA.united(boundsB);

    if (unitedBounds.isEmpty()) {
        pairStatusLabel_->setText(tr("Neither side has drawable Gerber content."));
        return;
    }
    if (boundsA.isEmpty()) {
        pairStatusLabel_->setText(tr("A side: no drawable Gerber content - showing old file only."));
    } else if (boundsB.isEmpty()) {
        pairStatusLabel_->setText(tr("B side: no drawable Gerber content - showing new file only."));
    } else {
        pairStatusLabel_->clear();
    }

    gerberCanvas_->fitTo(unitedBounds); // emits renderRequested -> renderShownPair
}

void ArchiveComparePage::renderShownPair(const QRectF &windowMm, double pxPerMm)
{
    if (shownLayerA_.objects.isEmpty() && shownLayerB_.objects.isEmpty()) {
        return;
    }
    // Synchronous on purpose - the render-time label makes this the
    // measurement the constitution asks for before any threading work.
    // Two masks now instead of one: this number is the gate for any future
    // threading/GL work (design doc section 2).
    QElapsedTimer timer;
    timer.start();
    QVector<MaskCompositor::TintedMask> layers;
    if (!shownLayerA_.objects.isEmpty()) {
        layers.append({LayerMaskRenderer::render(shownLayerA_, windowMm, pxPerMm), kColorA});
    }
    if (!shownLayerB_.objects.isEmpty()) {
        layers.append({LayerMaskRenderer::render(shownLayerB_, windowMm, pxPerMm), kColorB});
    }
    gerberCanvas_->setComposite(MaskCompositor::composite(layers), windowMm);
    renderTimeLabel_->setText(tr("Render time: %1 ms").arg(timer.elapsed()));
}
