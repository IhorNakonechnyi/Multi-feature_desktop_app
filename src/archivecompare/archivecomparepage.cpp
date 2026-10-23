#include "archivecomparepage.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QResizeEvent>
#include <QStringList>
#include <QVBoxLayout>

#include "../gerber/compositeview.h"
#include "../gerber/excellonparser.h"
#include "../gerber/gerberfiletype.h"
#include "../gerber/gerberobjectpaths.h"
#include "../gerber/gerberparser.h"
#include "../gerber/layermaskrenderer.h"
#include "../gerber/maskcompositor.h"
#include "archivecomparison.h"
#include "archiveheader.h"
#include "archivepanel.h"
#include "archivereader.h"
#include "comparisonsummary.h"
#include "gerberdifftrigger.h"
#include "samedatacheck.h"
#include "samedatadiagnosis.h"

namespace {

// The key a pair is remembered by in offeredPairs_.
QString pairKey(const DifferingPair &pair)
{
    return pair.relativePathA + QLatin1Char('\n') + pair.relativePathB;
}

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

    summaryLabel_ = new QLabel;
    summaryLabel_->setObjectName("detailLabel");
    layout->addWidget(summaryLabel_);

    // A banner, deliberately not a modal: "the customer resent what we
    // already have" is a common outcome, and making the user dismiss a
    // dialog every time would tax the case that costs them the least work.
    identicalBanner_ = new QLabel;
    identicalBanner_->setObjectName("identicalBanner");
    identicalBanner_->setWordWrap(true);
    identicalBanner_->setVisible(false);
    layout->addWidget(identicalBanner_);

    auto *contentRow = new QHBoxLayout;
    contentRow->setSpacing(12);

    // A and B are symmetric slots - either archive may be loaded on either
    // side, so the labels name the slot, not an implied direction.
    //
    // Grid rather than two self-contained columns, because the filter bar
    // has to span both: one bar acting on one tree would be ambiguous about
    // whether the other tree is filtered too (Q8).
    //
    //   row 0:  headerA | headerB
    //   row 1:  [ filter bar, spanning both ]
    //   row 2:  panelA  | panelB
    auto *archiveGrid = new QGridLayout;
    archiveGrid->setSpacing(8);

    // Widths are not fixed here - updatePanelWidths() sets them from the
    // window size (see resizeEvent).
    headerA_ = new ArchiveHeader(tr("A"));
    headerB_ = new ArchiveHeader(tr("B"));
    panelA_ = new ArchivePanel;
    panelB_ = new ArchivePanel;
    updatePanelWidths();

    auto *controlBar = new QWidget;
    auto *controlBarLayout = new QHBoxLayout(controlBar);
    controlBarLayout->setContentsMargins(0, 0, 0, 0);
    onlyChangesCheck_ = new QCheckBox(tr("Only changes"));
    typeCombo_ = new QComboBox;
    // -1 stands for "no kind filter"; the other two are DataKind values.
    typeCombo_->addItem(tr("All types"), -1);
    typeCombo_->addItem(tr("Gerber"), static_cast<int>(DataKind::GerberData));
    typeCombo_->addItem(tr("Other"), static_cast<int>(DataKind::OtherData));
    controlBarLayout->addWidget(onlyChangesCheck_);
    controlBarLayout->addWidget(typeCombo_);
    controlBarLayout->addStretch(1);

    archiveGrid->addWidget(headerA_, 0, 0);
    archiveGrid->addWidget(headerB_, 0, 1);
    archiveGrid->addWidget(controlBar, 1, 0, 1, 2);
    archiveGrid->addWidget(panelA_, 2, 0);
    archiveGrid->addWidget(panelB_, 2, 1);
    archiveGrid->setRowStretch(2, 1);
    contentRow->addLayout(archiveGrid);

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

    // Diagnostics for the 1.8.7 test round (samedatadiagnosis.h): why the
    // selected pair stayed yellow. Selectable so it can be copied out.
    // Temporary - removed again before the branch is merged.
    sameDataLabel_ = new QLabel;
    sameDataLabel_->setObjectName("detailLabel");
    sameDataLabel_->setWordWrap(true);
    sameDataLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sameDataLabel_->setVisible(false);
    detailCardLayout->addWidget(sameDataLabel_);

    rightColumn->addWidget(pairsCard);
    rightColumn->addWidget(canvasCard, 1);
    rightColumn->addWidget(detailCard);

    contentRow->addLayout(rightColumn, 1);
    layout->addLayout(contentRow, 1);

    connect(headerA_, &ArchiveHeader::archiveChanged, this, &ArchiveComparePage::runComparison);
    connect(headerB_, &ArchiveHeader::archiveChanged, this, &ArchiveComparePage::runComparison);
    // A drop on a tree belongs to that side's header, which owns the path -
    // the header then emits archiveChanged and the comparison re-runs, so a
    // drop on either widget follows exactly one code path.
    connect(panelA_, &ArchivePanel::archiveDropped, headerA_, &ArchiveHeader::setArchivePathFromDrop);
    connect(panelB_, &ArchivePanel::archiveDropped, headerB_, &ArchiveHeader::setArchivePathFromDrop);

    connect(onlyChangesCheck_, &QCheckBox::toggled, this, &ArchiveComparePage::applyFilters);
    connect(typeCombo_, &QComboBox::currentIndexChanged, this, &ArchiveComparePage::applyFilters);

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
    sameDataLabel_->clear();
    sameDataLabel_->setVisible(false);
    offeredPairs_.clear();
    clearSummary();
    panelA_->showEntries({});
    panelB_->showEntries({});
    // Blank, not "no matching files" - nothing has been compared yet, so an
    // empty-filter-result message would be answering a question the user
    // hasn't asked.
    panelA_->showEmptyMessage(QString());
    panelB_->showEmptyMessage(QString());

    const QString pathA = headerA_->archivePath();
    const QString pathB = headerB_->archivePath();
    if (pathA.isEmpty() || pathB.isEmpty()) {
        return; // wait until both archives are picked
    }

    // The whole read-expand-compare pipeline in one call (Q4/Q7). Errors
    // come back as data, not exceptions: each panel shows its own failure
    // inline via showError() instead of a modal dialog that would force the
    // user to dismiss it before seeing which side failed and how the other
    // side fared.
    // Not const: resolveKinds fills in each entry's DataKind in place below.
    ArchiveComparison::Outcome outcome = ArchiveComparison::compareArchives(pathA, pathB);

    if (!outcome.errorA.isEmpty()) {
        headerA_->showError(outcome.errorA);
    } else {
        headerA_->clearError();
    }
    if (!outcome.errorB.isEmpty()) {
        headerB_->showError(outcome.errorB);
    } else {
        headerB_->clearError();
    }
    if (!outcome.errorA.isEmpty() || !outcome.errorB.isEmpty()) {
        return; // can't produce a meaningful comparison with one side unreadable
    }

    // Each reader only reads the small head its side actually needs
    // classified - readEntryHead caps the read at kClassifySniffBytes
    // instead of decompressing the whole entry, which matters because a
    // real file can run tens of KB and classification only ever looks at
    // the first few lines. Declared here because both the summary's kind
    // resolution and the overlay's pair bucketing below use them.
    const auto readHeadA = [this](const QString &relativePath) {
        return ArchiveReader::readEntryHead(headerA_->archivePath(), relativePath, GerberFileType::kClassifySniffBytes);
    };
    const auto readHeadB = [this](const QString &relativePath) {
        return ArchiveReader::readEntryHead(headerB_->archivePath(), relativePath, GerberFileType::kClassifySniffBytes);
    };

    // Before showEntries, so the entries the panels receive already carry
    // their kind. Conclusive extensions cost nothing here; only genuinely
    // ambiguous files are read (Q8).
    ComparisonSummary::resolveKinds(outcome.result.entriesA, readHeadA);
    ComparisonSummary::resolveKinds(outcome.result.entriesB, readHeadB);

    // A re-exported but unchanged file differs only in its date/user/job
    // lines, so its CRC differs and it arrives here yellow. Parse each
    // Gerber/drill pair once and move the ones carrying the same data back to
    // green, before anything is shown or counted (samedatacheck.h). Needs
    // whole files, unlike the head-only reads above. Synchronous, like the
    // rest of the compare - its time is part of the compare time shown.
    QElapsedTimer sameDataTimer;
    sameDataTimer.start();
    SameDataCheck::collapseSameDataPairs(outcome.result, [this](const DifferingPair &pair) {
        offeredPairs_.insert(pairKey(pair)); // diagnostics: this pair did reach the check
        bool foundA = false;
        bool foundB = false;
        const QByteArray bytesA = ArchiveReader::readEntryBytes(headerA_->archivePath(), pair.relativePathA, &foundA);
        const QByteArray bytesB = ArchiveReader::readEntryBytes(headerB_->archivePath(), pair.relativePathB, &foundB);
        return foundA && foundB && SameDataCheck::isSameData(bytesA, bytesB);
    });
    const qint64 sameDataMs = sameDataTimer.elapsed();

    const ComparisonSummary::Summary summary = ComparisonSummary::summarize(outcome.result);
    summaryLabel_->setText(ComparisonSummary::formatSummaryLine(summary));

    identicalBanner_->setText(tr("No differences - both archives contain the same data."));
    identicalBanner_->setVisible(summary.fullyIdentical());

    panelA_->showEntries(outcome.result.entriesA);
    panelB_->showEntries(outcome.result.entriesB);
    // Re-applied over the fresh entries: the bar keeps its state across a
    // re-pick, so the trees must honour it immediately rather than showing
    // everything until the user touches a control.
    applyFilters();
    // The pipeline just ran to completion, so - unlike renderTimeLabel_
    // below - this IS the value to show, not a placeholder: rendering only
    // happens once the user selects a pair, but the compare itself is
    // already done.
    compareTimeLabel_->setText(tr("Compare time: %1 ms").arg(outcome.compareMs + sameDataMs));

    // Content, not extension, decides which pairs the overlay can draw
    // (Q2/Q12; docs/superpowers/specs/2026-07-27-content-based-drawability-
    // design.md). Reuses the same head readers declared above.
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

void ArchiveComparePage::clearSummary()
{
    // Both are claims about a completed comparison, so neither may outlive
    // one. A stale "No differences" banner after a failed re-pick would be
    // the most misleading thing on the page.
    summaryLabel_->clear();
    identicalBanner_->setVisible(false);
}

void ArchiveComparePage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updatePanelWidths();
}

void ArchiveComparePage::updatePanelWidths()
{
    // The archive columns used to be a flat 252 px at every window size, so
    // maximizing handed all of the extra width to the overlay canvas and the
    // columns' share actually *fell* - from ~46% of a 1100 px window to ~26%
    // of a 1920 px one. Long archive filenames were therefore hardest to
    // read exactly when the screen had the most room to spare.
    //
    // Two calibration points, interpolated linearly between and extrapolated
    // beyond: the standard window keeps precisely today's appearance, and a
    // maximized 1920 px window gives the two columns 40% of its width.
    constexpr int kStandardWindow = 1100;
    constexpr int kStandardPanel = 252; // 2 x 252 = ~46% of 1100
    constexpr int kWideWindow = 1920;
    constexpr int kWidePanel = 384; // 2 x 384 = 768 = 40% of 1920

    // Measured against the WINDOW, not this page: the page sits beside a
    // fixed sidebar, so its own width is the same quantity offset by a
    // constant, and the window is what the 40% is a percentage of.
    //
    // QWidget::window() never returns null - during construction, before this
    // page is reparented into the shell, it returns `this` with a small
    // default width, which the floor below turns into the standard width.
    const int windowWidth = window()->width();

    constexpr double kGrowthPerPixel =
        static_cast<double>(kWidePanel - kStandardPanel) / (kWideWindow - kStandardWindow);
    const int scaled =
        kStandardPanel + qRound((windowWidth - kStandardWindow) * kGrowthPerPixel);

    // Never narrower than today's standard: a small window should lose space
    // from the overlay, which can scale, rather than from filenames, which
    // just get elided.
    const int panelWidth = qMax(kStandardPanel, scaled);

    headerA_->setFixedWidth(panelWidth);
    headerB_->setFixedWidth(panelWidth);
    panelA_->setFixedWidth(panelWidth);
    panelB_->setFixedWidth(panelWidth);
}

void ArchiveComparePage::applyFilters()
{
    TreeFilter filter;
    filter.changedOnly = onlyChangesCheck_->isChecked();
    const int kindValue = typeCombo_->currentData().toInt();
    if (kindValue >= 0) {
        filter.kind = static_cast<DataKind>(kindValue);
    }

    // Both panels, always - one bar means one view of both archives. A bar
    // that silently applied to only one side would make the two trees
    // describe different questions.
    panelA_->setFilter(filter);
    panelB_->setFilter(filter);

    const QString message = emptyAfterFilterMessage(filter);
    panelA_->showEmptyMessage(panelA_->isEmptyAfterFilter() ? message : QString());
    panelB_->showEmptyMessage(panelB_->isEmptyAfterFilter() ? message : QString());
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
    const QByteArray bytesA = ArchiveReader::readEntryBytes(headerA_->archivePath(), pair.relativePathA, &foundA);
    const QByteArray bytesB = ArchiveReader::readEntryBytes(headerB_->archivePath(), pair.relativePathB, &foundB);

    // Diagnostics (1.8.7 test round): why this pair is not green.
    if (foundA && foundB) {
        QString diagnosis = SameDataDiagnosis::explain(bytesA, bytesB);
        if (!offeredPairs_.contains(pairKey(pair))) {
            diagnosis.prepend(QStringLiteral("Not offered to the check (archive entry kind is not Gerber data on "
                                             "both sides). What the check would say:\n"));
        }
        sameDataLabel_->setText(diagnosis);
        sameDataLabel_->setVisible(true);
    } else {
        sameDataLabel_->clear();
        sameDataLabel_->setVisible(false);
    }

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
                .arg(pair.relativePathA, QFileInfo(headerA_->archivePath()).fileName()));
    } else if (!foundB) {
        pairStatusLabel_->setText(
            tr("Could not read \"%1\" from archive %2.")
                .arg(pair.relativePathB, QFileInfo(headerB_->archivePath()).fileName()));
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
