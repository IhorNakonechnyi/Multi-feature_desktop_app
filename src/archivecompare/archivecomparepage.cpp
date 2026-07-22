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
#include "../gerber/excellonparser.h"
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
    setObjectName("archiveComparePage");
    setAttribute(Qt::WA_StyledBackground, true);

    auto *layout = new QVBoxLayout(this);

    auto *title = new QLabel(tr("Archive compare"));
    title->setObjectName("pageTitle");
    layout->addWidget(title);

    auto *contentRow = new QHBoxLayout;
    contentRow->setSpacing(12);

    // A real (draggable) splitter, not fixed-width cards: archive names can
    // be long, so the user needs to be able to widen either tree on demand
    // rather than being stuck with whatever fixed width fits the common case.
    auto *treeSplitter = new QSplitter(Qt::Horizontal);
    panelA_ = new ArchivePanel(tr("New"));
    panelB_ = new ArchivePanel(tr("Old"));
    treeSplitter->addWidget(panelA_);
    treeSplitter->addWidget(panelB_);
    treeSplitter->setSizes({252, 252}); // base width +20% over the old fixed 210px
    contentRow->addWidget(treeSplitter);

    auto *rightColumn = new QVBoxLayout;
    rightColumn->setSpacing(12);

    // Pairs card: which changed Gerber pairs can be previewed, plus the
    // messaging for pairs that can't be (unrecognized extension, drill).
    auto *pairsCard = new QWidget;
    pairsCard->setObjectName("pairsCard");
    pairsCard->setAttribute(Qt::WA_StyledBackground, true);
    auto *pairsCardLayout = new QVBoxLayout(pairsCard);
    pairsCardLayout->setContentsMargins(10, 10, 10, 10);
    auto *explanation = new QLabel(
        tr("Changed Gerber files. Select a pair to preview the diff."));
    explanation->setWordWrap(true);
    gerberPairList_ = new QListWidget;
    gerberPairList_->setObjectName("pairsList");
    // Compact by design (the mockup's pill row is content-sized, not
    // stretched) - sized for ~3 visible rows, scrolls internally if there
    // are more pairs than fit.
    gerberPairList_->setMaximumHeight(90);
    pairStatusLabel_ = new QLabel;
    pairStatusLabel_->setObjectName("detailLabel");
    pairStatusLabel_->setWordWrap(true);
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

    pairsCardLayout->addWidget(explanation);
    pairsCardLayout->addWidget(gerberPairList_);
    pairsCardLayout->addWidget(pairStatusLabel_);
    pairsCardLayout->addWidget(skippedUnrecognizedLabel_);
    pairsCardLayout->addWidget(skippedUnrecognizedButtonRow_);
    pairsCardLayout->addWidget(skippedDrillLabel_);

    // Canvas card: the Gerber overlay render, inset in its own card.
    auto *canvasCard = new QWidget;
    canvasCard->setObjectName("canvasCard");
    canvasCard->setAttribute(Qt::WA_StyledBackground, true);
    auto *canvasCardLayout = new QVBoxLayout(canvasCard);
    canvasCardLayout->setContentsMargins(8, 8, 8, 8);
    gerberCanvas_ = new CompositeView;
    canvasCardLayout->addWidget(gerberCanvas_);

    // Detail card: the static overlay color legend, one color per line,
    // with the render time right-aligned on the first line.
    auto *detailCard = new QWidget;
    detailCard->setObjectName("detailCard");
    detailCard->setAttribute(Qt::WA_StyledBackground, true);
    auto *detailCardLayout = new QVBoxLayout(detailCard);
    detailCardLayout->setContentsMargins(14, 10, 14, 10);

    auto *legendGreen = new QLabel(tr("green = only new"));
    legendGreen->setObjectName("detailLabel");
    renderTimeLabel_ = new QLabel(tr("Render time: -"));
    renderTimeLabel_->setObjectName("detailLabel");
    auto *firstLine = new QHBoxLayout;
    firstLine->addWidget(legendGreen);
    firstLine->addStretch();
    firstLine->addWidget(renderTimeLabel_);

    auto *legendRed = new QLabel(tr("red = only old"));
    legendRed->setObjectName("detailLabel");
    auto *legendYellow = new QLabel(tr("yellow = same"));
    legendYellow->setObjectName("detailLabel");

    detailCardLayout->addLayout(firstLine);
    detailCardLayout->addWidget(legendRed);
    detailCardLayout->addWidget(legendYellow);

    rightColumn->addWidget(pairsCard);
    rightColumn->addWidget(canvasCard, 1);
    rightColumn->addWidget(detailCard);

    contentRow->addLayout(rightColumn, 1);
    layout->addLayout(contentRow, 1);

    connect(panelA_, &ArchivePanel::archiveChanged, this, &ArchiveComparePage::runComparison);
    connect(panelB_, &ArchivePanel::archiveChanged, this, &ArchiveComparePage::runComparison);
    connect(gerberPairList_, &QListWidget::currentRowChanged, this,
            [this](int row) { if (row >= 0) showSelectedPair(); });
    connect(gerberCanvas_, &CompositeView::renderRequested, this, &ArchiveComparePage::renderShownPair);
}

void ArchiveComparePage::runComparison()
{
    // Cleared unconditionally, before either early return below: a re-pick
    // that turns out empty or unreadable must not leave a previous pair's
    // stale overlay/layers on screen describing an archive selection that's
    // since changed.
    shownLayerA_ = Layer{};
    shownLayerB_ = Layer{};
    gerberCanvas_->setComposite(QImage(), QRectF());

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
    pairStatusLabel_->clear();
    // Reset to the same placeholder text the constructor starts with,
    // not a blank label - otherwise a re-comparison after picking new
    // archives leaves the *previous* pair's stale render time visible in
    // the Detail card until the user clicks a new pair.
    renderTimeLabel_->setText(tr("Render time: -"));

    const QVector<DifferingPair> unrecognized = GerberDiffTrigger::unrecognizedPairs(result.differingPairs);
    const QVector<DifferingPair> drill = GerberDiffTrigger::drillPairs(result.differingPairs);
    rebuildSkippedPairsUi(unrecognized, drill);
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
        // No button: these are mixed pairs (one side drill, one side non-drill).
        // The app can read Excellon drill files (M5 complete), but comparing
        // a drill file's holes against a copper Gerber layer's traces is
        // structurally meaningless - nothing the user clicks here would help.
        skippedDrillLabel_->setText(
            tr("%1 mixed drill/Gerber pair(s) skipped - drill files cannot be compared with non-drill layers.").arg(drill.size()));
        skippedDrillLabel_->setVisible(true);
    }
}

// Picks GerberParser or ExcellonParser by the entry's own file extension
// (GerberFileType::isDrillFile) - see the M5 design doc, section 2. An
// empty `bytes` (read failure, already handled independently per side by
// the caller) produces an empty Layer with no error. An ExcellonParser
// ambiguous-coordinate-format failure is caught here and reported via
// `parseError` instead of propagating, mirroring how a read failure is
// already just an empty Layer rather than a thrown exception at this
// layer - one file's problem must never abort rendering the other side.
Layer ArchiveComparePage::parseGerberOrDrill(const QByteArray &bytes, const QString &relativePath,
                                              QString &parseError)
{
    if (bytes.isEmpty()) {
        return Layer{};
    }
    const QString content = QString::fromUtf8(bytes);
    if (!GerberFileType::isDrillFile(relativePath)) {
        return GerberParser::parseContentToObjects(content);
    }
    try {
        return ExcellonParser::parseContentToObjects(content);
    } catch (const ExcellonParser::AmbiguousFormatError &error) {
        parseError = error.message();
        return Layer{};
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

    QString parseErrorA;
    QString parseErrorB;
    shownLayerA_ = parseGerberOrDrill(bytesA, pair.relativePathA, parseErrorA);
    shownLayerB_ = parseGerberOrDrill(bytesB, pair.relativePathB, parseErrorB);

    // Each side is judged independently: an empty-but-readable file, a read
    // failure, or a parse failure (parseErrorX set) all leave that side's
    // Layer empty, which GerberObjectPaths::layerBoundsMm correctly
    // reports as empty bounds either way - the difference is only in which
    // status text explains why.
    const QRectF boundsA = GerberObjectPaths::layerBoundsMm(shownLayerA_);
    const QRectF boundsB = GerberObjectPaths::layerBoundsMm(shownLayerB_);
    const QRectF unitedBounds = boundsA.united(boundsB);

    if (!parseErrorA.isEmpty() && !parseErrorB.isEmpty()) {
        pairStatusLabel_->setText(tr("Both sides failed to parse - A: %1 | B: %2.").arg(parseErrorA, parseErrorB));
    } else if (!parseErrorA.isEmpty()) {
        pairStatusLabel_->setText(tr("A side failed to parse: %1 - showing old file only.").arg(parseErrorA));
    } else if (!parseErrorB.isEmpty()) {
        pairStatusLabel_->setText(tr("B side failed to parse: %1 - showing new file only.").arg(parseErrorB));
    } else if (unitedBounds.isEmpty()) {
        pairStatusLabel_->setText(tr("Neither side has drawable content."));
    } else if (boundsA.isEmpty()) {
        pairStatusLabel_->setText(tr("A side: no drawable content - showing old file only."));
    } else if (boundsB.isEmpty()) {
        pairStatusLabel_->setText(tr("B side: no drawable content - showing new file only."));
    } else {
        pairStatusLabel_->clear();
    }

    if (unitedBounds.isEmpty()) {
        gerberCanvas_->setComposite(QImage(), QRectF());
        return;
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
