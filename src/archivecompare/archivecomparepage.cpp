#include "archivecomparepage.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

#include "../gerber/compositeview.h"
#include "../gerber/excellonparser.h"
#include "../gerber/gerberfiletype.h"
#include "../gerber/gerberobjectpaths.h"
#include "../gerber/gerberparser.h"
#include "../gerber/layermaskrenderer.h"
#include "../gerber/maskcompositor.h"
#include "archivecomparison.h"
#include "archivepanel.h"
#include "archivereader.h"
#include "gerberdifftrigger.h"

namespace {

// A in green, B in red - A and B are symmetric slots (either archive can be
// loaded on either side), not "new"/"old". MaskCompositor's additive
// overlap (CompositionMode_Plus) turns unchanged copper yellow, matching
// CAMMaster's compare view (M4 design doc, section 2). Fixed for M4; a
// color picker is an explicit non-goal until the overlay is in daily use.
const QColor kColorA(60, 220, 60);
const QColor kColorB(220, 60, 60);

// The additive overlap color MaskCompositor::composite() actually produces
// for unchanged copper (CompositionMode_Plus sums each channel, capped at
// 255) - used so the Detail card's legend swatch shows the exact on-canvas
// color, not just an approximate "yellow".
const QColor kColorOverlap(qMin(255, kColorA.red() + kColorB.red()),
                            qMin(255, kColorA.green() + kColorB.green()),
                            qMin(255, kColorA.blue() + kColorB.blue()));

// One legend row: a short colored line (not a dot, to read as distinct from
// the StatusLegend's circular swatches) followed by its description.
QWidget *makeColorLegendRow(const QColor &color, const QString &description)
{
    auto *row = new QWidget;
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(8);

    auto *line = new QLabel;
    line->setFixedSize(20, 4);
    line->setAttribute(Qt::WA_StyledBackground, true);
    line->setStyleSheet(QString("background:%1;").arg(color.name()));

    auto *text = new QLabel(description);
    text->setObjectName("detailLabel");

    rowLayout->addWidget(line);
    rowLayout->addWidget(text);
    return row;
}

// One list row's label: the A-side path, plus " <-> B-path" only when the
// two sides differ (content-first matching's name/extension passes can pair
// files at different paths - see the content-first-archive-matching design
// doc), followed by the pair's reason. Every row gets a reason suffix, even
// a same-path pair: content-first matching never treats "same path" as
// proof, only as one more assumption the user must be able to see (Q1).
QString formatPairLabel(const DifferingPair &pair)
{
    QString label = (pair.relativePathA == pair.relativePathB)
        ? pair.relativePathA
        : QObject::tr("%1  <->  %2").arg(pair.relativePathA, pair.relativePathB);
    switch (pair.reason) {
    case PairReason::SameName:
        label += QObject::tr(" - same name");
        break;
    case PairReason::OnlyExtensionCandidate:
        label += QObject::tr(" - only .%1 left on each side")
                      .arg(QFileInfo(pair.relativePathA).suffix().toLower());
        break;
    }
    return label;
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

    // A and B are symmetric slots - either archive may be loaded on either
    // side, so the labels name the slot, not an implied direction.
    panelA_ = new ArchivePanel(tr("A"));
    panelA_->setFixedWidth(252); // +20% over the original 210px
    panelB_ = new ArchivePanel(tr("B"));
    panelB_->setFixedWidth(252);
    contentRow->addWidget(panelA_);
    contentRow->addWidget(panelB_);

    auto *rightColumn = new QVBoxLayout;
    rightColumn->setSpacing(12);

    // Pairs card: which changed Gerber/drill pairs can be previewed, plus a
    // count of the ones that can't (neither side classified as drawable
    // content - Q2/Q12).
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
    notDrawableLabel_ = new QLabel;
    notDrawableLabel_->setWordWrap(true);
    notDrawableLabel_->setVisible(false);

    pairsCardLayout->addWidget(explanation);
    pairsCardLayout->addWidget(gerberPairList_);
    pairsCardLayout->addWidget(pairStatusLabel_);
    pairsCardLayout->addWidget(notDrawableLabel_);

    // Canvas card: the Gerber overlay render, inset in its own card.
    auto *canvasCard = new QWidget;
    canvasCard->setObjectName("canvasCard");
    canvasCard->setAttribute(Qt::WA_StyledBackground, true);
    auto *canvasCardLayout = new QVBoxLayout(canvasCard);
    canvasCardLayout->setContentsMargins(8, 8, 8, 8);
    gerberCanvas_ = new CompositeView;
    canvasCardLayout->addWidget(gerberCanvas_);

    // Detail card: the static overlay color legend, one color per line,
    // with the render time right-aligned on the first line and the compare
    // time right-aligned on the second (Q7: timing compare and render
    // separately means a future slowdown can be attributed to the right
    // half instead of guessed at).
    auto *detailCard = new QWidget;
    detailCard->setObjectName("detailCard");
    detailCard->setAttribute(Qt::WA_StyledBackground, true);
    auto *detailCardLayout = new QVBoxLayout(detailCard);
    detailCardLayout->setContentsMargins(14, 10, 14, 10);

    renderTimeLabel_ = new QLabel(tr("Render time: -"));
    renderTimeLabel_->setObjectName("detailLabel");
    auto *firstLine = new QHBoxLayout;
    firstLine->addWidget(makeColorLegendRow(kColorA, tr("only in A")));
    firstLine->addStretch();
    firstLine->addWidget(renderTimeLabel_);

    compareTimeLabel_ = new QLabel(tr("Compare time: -"));
    compareTimeLabel_->setObjectName("detailLabel");
    auto *secondLine = new QHBoxLayout;
    secondLine->addWidget(makeColorLegendRow(kColorB, tr("only in B")));
    secondLine->addStretch();
    secondLine->addWidget(compareTimeLabel_);

    detailCardLayout->addLayout(firstLine);
    detailCardLayout->addLayout(secondLine);
    detailCardLayout->addWidget(makeColorLegendRow(kColorOverlap, tr("same")));

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
    // stale overlay/layers, render/compare times, or classification trees on
    // screen describing an archive selection that's since changed. The trees
    // in particular are a claim about a comparison - green means "identical in
    // both archives" - so stale coloured rows after a failed re-pick would
    // assert something now false. A successful comparison repopulates them
    // below, so the only visible effect is on the failure paths.
    shownLayerA_ = Layer{};
    shownLayerB_ = Layer{};
    gerberCanvas_->setComposite(QImage(), QRectF());
    compareTimeLabel_->setText(tr("Compare time: -"));
    panelA_->showEntries({});
    panelB_->showEntries({});

    const QString pathA = panelA_->archivePath();
    const QString pathB = panelB_->archivePath();
    if (pathA.isEmpty() || pathB.isEmpty()) {
        return; // wait until both archives are picked
    }

    // The whole read-expand-compare pipeline in one call (Q4/Q7). Errors
    // come back as data, not exceptions: each panel shows its own failure
    // inline via showError() instead of a modal dialog that would force the
    // user to dismiss it before seeing which side failed and how the other
    // side fared.
    const ArchiveComparison::Outcome outcome = ArchiveComparison::compareArchives(pathA, pathB);

    if (!outcome.errorA.isEmpty()) {
        panelA_->showError(outcome.errorA);
    } else {
        panelA_->clearError();
    }
    if (!outcome.errorB.isEmpty()) {
        panelB_->showError(outcome.errorB);
    } else {
        panelB_->clearError();
    }
    if (!outcome.errorA.isEmpty() || !outcome.errorB.isEmpty()) {
        return; // can't produce a meaningful comparison with one side unreadable
    }

    panelA_->showEntries(outcome.result.entriesA);
    panelB_->showEntries(outcome.result.entriesB);
    // The pipeline just ran to completion, so - unlike renderTimeLabel_
    // below - this IS the value to show, not a placeholder: rendering only
    // happens once the user selects a pair, but the compare itself is
    // already done.
    compareTimeLabel_->setText(tr("Compare time: %1 ms").arg(outcome.compareMs));

    // Content, not extension, decides which pairs the overlay can draw
    // (Q2/Q12; docs/superpowers/specs/2026-07-27-content-based-drawability-
    // design.md). Each reader only reads the small head each side actually
    // needs classified - readEntryHead caps the read at
    // GerberFileType::kClassifySniffBytes instead of decompressing the
    // whole entry, which matters because a differing pair's real file can
    // run tens of KB and classification only ever looks at the first few
    // lines.
    const auto readHeadA = [this](const QString &relativePath) {
        return ArchiveReader::readEntryHead(panelA_->archivePath(), relativePath, GerberFileType::kClassifySniffBytes);
    };
    const auto readHeadB = [this](const QString &relativePath) {
        return ArchiveReader::readEntryHead(panelB_->archivePath(), relativePath, GerberFileType::kClassifySniffBytes);
    };
    const GerberDiffTrigger::PairBuckets buckets =
        GerberDiffTrigger::classifyPairs(outcome.result.differingPairs, readHeadA, readHeadB);

    eligiblePairs_ = buckets.drawable;
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

    updateNotDrawableLabel(buckets.notDrawable.size());
}

void ArchiveComparePage::updateNotDrawableLabel(int count)
{
    // The mixed drill/Gerber special case and its "Add .<ext>" button are
    // both gone (Q2/Q12 - see the content-based-drawability design doc):
    // drawability is content-only now, so there's nothing left for a
    // button to add, and a drill-vs-Gerber pair is drawable like any other
    // pair, not a separate rejection reason. What's left is one plain
    // count of pairs where at least one side didn't classify as drawable.
    if (count == 0) {
        notDrawableLabel_->setVisible(false);
        return;
    }
    notDrawableLabel_->setText(
        tr("%n changed pair(s) can't be previewed - not recognized as Gerber or drill data.", "", count));
    notDrawableLabel_->setVisible(true);
}

// Picks GerberParser or ExcellonParser by the entry's own content
// (GerberFileType::classify) - content-only, no filename involved (Q2/Q12;
// docs/superpowers/specs/2026-07-27-content-based-drawability-design.md).
// An empty `bytes` (read failure, already handled independently per side by
// the caller) produces an empty Layer with no error. A NotDrawable
// classification also falls through to GerberParser, same as before this
// change - that parser then finds nothing to draw and layerBoundsMm reports
// empty bounds, which showSelectedPair already turns into a "no drawable
// content" status; there is no separate NotDrawable path to wire up here.
// An ExcellonParser ambiguous-coordinate-format failure is caught here and
// reported via `parseError` instead of propagating, mirroring how a read
// failure is already just an empty Layer rather than a thrown exception at
// this layer - one file's problem must never abort rendering the other
// side.
Layer ArchiveComparePage::parseGerberOrDrill(const QByteArray &bytes, QString &parseError)
{
    if (bytes.isEmpty()) {
        return Layer{};
    }
    const QString content = QString::fromUtf8(bytes);
    if (GerberFileType::classify(bytes) != GerberFileType::Drawability::ExcellonDrill) {
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

    bool foundA = false;
    bool foundB = false;
    const QByteArray bytesA = ArchiveReader::readEntryBytes(panelA_->archivePath(), pair.relativePathA, &foundA);
    const QByteArray bytesB = ArchiveReader::readEntryBytes(panelB_->archivePath(), pair.relativePathB, &foundB);

    QString parseErrorA;
    QString parseErrorB;
    shownLayerA_ = parseGerberOrDrill(bytesA, parseErrorA);
    shownLayerB_ = parseGerberOrDrill(bytesB, parseErrorB);

    // Each side is judged independently: an empty-but-readable file, a read
    // failure, or a parse failure (parseErrorX set) all leave that side's
    // Layer empty, which GerberObjectPaths::layerBoundsMm correctly
    // reports as empty bounds either way - the difference is only in which
    // status text explains why.
    const QRectF boundsA = GerberObjectPaths::layerBoundsMm(shownLayerA_);
    const QRectF boundsB = GerberObjectPaths::layerBoundsMm(shownLayerB_);
    const QRectF unitedBounds = boundsA.united(boundsB);

    // A read failure (found == false) is checked before parse errors and
    // before the "no drawable content" bounds checks below: parseGerberOrDrill
    // returns an empty Layer with no parseError for empty bytes (its own
    // comment says as much), so a read failure would otherwise silently fall
    // through to "Neither side has drawable content" - blaming the file's
    // content when the file was never actually read. That message must stay
    // reserved for a file that WAS read and parsed to nothing.
    if (!foundA) {
        pairStatusLabel_->setText(
            tr("Could not read \"%1\" from archive %2.")
                .arg(pair.relativePathA, QFileInfo(panelA_->archivePath()).fileName()));
    } else if (!foundB) {
        pairStatusLabel_->setText(
            tr("Could not read \"%1\" from archive %2.")
                .arg(pair.relativePathB, QFileInfo(panelB_->archivePath()).fileName()));
    } else if (!parseErrorA.isEmpty() && !parseErrorB.isEmpty()) {
        pairStatusLabel_->setText(tr("Both sides failed to parse - A: %1 | B: %2.").arg(parseErrorA, parseErrorB));
    } else if (!parseErrorA.isEmpty()) {
        pairStatusLabel_->setText(tr("A side failed to parse: %1 - showing B only.").arg(parseErrorA));
    } else if (!parseErrorB.isEmpty()) {
        pairStatusLabel_->setText(tr("B side failed to parse: %1 - showing A only.").arg(parseErrorB));
    } else if (unitedBounds.isEmpty()) {
        pairStatusLabel_->setText(tr("Neither side has drawable content."));
    } else if (boundsA.isEmpty()) {
        pairStatusLabel_->setText(tr("A side: no drawable content - showing B only."));
    } else if (boundsB.isEmpty()) {
        pairStatusLabel_->setText(tr("B side: no drawable content - showing A only."));
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
