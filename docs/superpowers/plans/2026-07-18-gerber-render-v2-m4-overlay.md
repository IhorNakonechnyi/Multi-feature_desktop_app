# Gerber Render v2 — M4 Overlay Compare Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the temporary all-files debugging list in `ArchiveComparePage` with the real M4 feature: a list of changed Gerber pairs, and a Show button that renders both revisions overlaid in one `CompositeView` (new = green, old = red, unchanged = yellow), matching CAMMaster's compare workflow.

**Architecture:** No new rendering machinery — `MaskCompositor::composite()` already accepts N tinted `LayerMask`s. M4 is wiring inside `ArchiveComparePage`: classify each changed pair into eligible / drill / unrecognized buckets (`GerberDiffTrigger`), parse both sides on Show, render one `LayerMask` per side per view window, composite, display. A new `GerberFileType::isDrillFile()` predicate lets the classifier recognize Excellon files (`.drl`/`.xln`) so they don't silently fail as "eligible" — this app's parser only reads RS-274X, not Excellon.

**Tech Stack:** Qt 6.8.2 Widgets, C++20, QtTest (ctest-integrated). No new dependencies.

## Global Constraints

- C++20, Qt 6.8.2 Widgets only (no QML) — per `CLAUDE.md`.
- No third-party Gerber/Excellon library; hand-rolled parsing only — per `CLAUDE.md`.
- Zero outbound network calls; all inputs are local files — per `CLAUDE.md`.
- No real supplier/customer file names, parameter names, or data in code, comments, tests, or commits — confidentiality constraint, `CLAUDE.md` and the M4 design doc.
- Unit-test core/calculation logic only; UI/glue code is manually tested (constitution principle 4). `ArchiveComparePage` changes are manually verified, not unit-tested.
- `C:\Qt\6.8.2\msvc2022_64\bin` must be on `PATH` to run the built exe or test binaries (otherwise exit `0xc0000135`).
- Build: `cmake --build build --config Debug`. Tests: `ctest --test-dir build -C Debug --output-on-failure`.
- Real test archives live in the gitignored `.claude/worktrees/gerber-parser-3b2/Test_data/` (test_pair_1, test_pair_2, test_pair_3) — do not copy their contents into commits or test fixtures; synthetic Gerber snippets only in code.

---

### Task 1: `GerberFileType::isDrillFile`

**Files:**
- Modify: `src/gerber/gerberfiletype.h`
- Modify: `src/gerber/gerberfiletype.cpp`
- Test: `tests/unit/test_gerberfiletype.cpp`

**Interfaces:**
- Produces: `bool GerberFileType::isDrillFile(const QString &fileName)` — used by Task 2's `GerberDiffTrigger`.

- [ ] **Step 1: Write the failing tests**

Append three slots to the `private slots:` section of `tests/unit/test_gerberfiletype.cpp` (after `emptyExtensionIsNotRecognized();`):

```cpp
    void drillExtensionsAreRecognizedAsDrillFiles();
    void gerberExtensionIsNotADrillFile();
    void emptyExtensionIsNotADrillFile();
```

Append the definitions (after `TestGerberFileType::emptyExtensionIsNotRecognized`'s closing brace, before `QTEST_MAIN`):

```cpp
void TestGerberFileType::drillExtensionsAreRecognizedAsDrillFiles()
{
    QVERIFY(GerberFileType::isDrillFile("board.drl"));
    QVERIFY(GerberFileType::isDrillFile("board.DRL"));
    QVERIFY(GerberFileType::isDrillFile("board.xln"));
}

void TestGerberFileType::gerberExtensionIsNotADrillFile()
{
    QVERIFY(!GerberFileType::isDrillFile("layer.gtl"));
}

void TestGerberFileType::emptyExtensionIsNotADrillFile()
{
    QVERIFY(!GerberFileType::isDrillFile("noextension"));
}
```

- [ ] **Step 2: Run test to verify it fails to build**

Run: `cmake --build build --config Debug --target test_gerberfiletype`
Expected: compile error — `isDrillFile` is not a member of `GerberFileType`.

- [ ] **Step 3: Add the declaration**

In `src/gerber/gerberfiletype.h`, insert after the `isRecognized` declaration (before `addUserExtension`):

```cpp
// Whether fileName's extension identifies it as an Excellon drill file
// (drl, xln) rather than a Gerber (RS-274X) layer. Both extensions are
// already in isRecognized()'s default set (real archives mix Gerber and
// drill files under one naming convention), so callers that must not treat
// a drill file as parseable Gerber (GerberDiffTrigger, M4's overlay
// compare) check this alongside isRecognized, not instead of it - this
// app's Gerber parser doesn't speak Excellon yet.
bool isDrillFile(const QString &fileName);
```

- [ ] **Step 4: Implement**

In `src/gerber/gerberfiletype.cpp`, add after `GerberFileType::isRecognized`'s closing brace:

```cpp
bool GerberFileType::isDrillFile(const QString &fileName)
{
    const QString extension = QFileInfo(fileName).suffix().toLower();
    return extension == QStringLiteral("drl") || extension == QStringLiteral("xln");
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run (with `C:\Qt\6.8.2\msvc2022_64\bin` on `PATH`):
```
cmake --build build --config Debug --target test_gerberfiletype
ctest --test-dir build -C Debug -R test_gerberfiletype --output-on-failure
```
Expected: `100% tests passed`.

- [ ] **Step 6: Commit**

```bash
git add src/gerber/gerberfiletype.h src/gerber/gerberfiletype.cpp tests/unit/test_gerberfiletype.cpp
git commit -m "Add GerberFileType::isDrillFile for Excellon detection"
```

---

### Task 2: `GerberDiffTrigger` three-bucket split

**Files:**
- Modify: `src/archivecompare/gerberdifftrigger.h`
- Modify: `src/archivecompare/gerberdifftrigger.cpp`
- Test: `tests/unit/test_gerberdifftrigger.cpp`

**Interfaces:**
- Consumes: `GerberFileType::isDrillFile` and `GerberFileType::isRecognized` (Task 1 and existing).
- Produces:
  - `QVector<DifferingPair> GerberDiffTrigger::eligiblePairs(const QVector<DifferingPair> &)` (existing signature, behavior changes to exclude drill pairs) — consumed by Task 3.
  - `QVector<DifferingPair> GerberDiffTrigger::unrecognizedPairs(const QVector<DifferingPair> &)` (new) — consumed by Task 3.
  - `QVector<DifferingPair> GerberDiffTrigger::drillPairs(const QVector<DifferingPair> &)` (new) — consumed by Task 3.

- [ ] **Step 1: Write the failing tests**

Replace the full contents of `tests/unit/test_gerberdifftrigger.cpp` with:

```cpp
#include <QtTest/QtTest>

#include "archivecompare/archivecomparisonresult.h"
#include "archivecompare/gerberdifftrigger.h"

// GerberFileType::isRecognized checks a fixed built-in extension set plus
// any user-added extensions in QSettings (empty in this test environment),
// so these use extensions from that built-in default set directly rather
// than assuming or hardcoding the app's real (non-public) convention.
class TestGerberDiffTrigger : public QObject {
    Q_OBJECT

private slots:
    void bothSidesRecognizedGerberType();
    void oneSideNotGerberType();
    void neitherSideGerberType();
    void emptyInputProducesEmptyOutput();

    void drillSideExcludesFromEligible();
    void drillPairsBucketsDrillSide();
    void unrecognizedPairsBucketsNonDrillUnrecognized();
    void drillTakesPriorityOverUnrecognized();
};

void TestGerberDiffTrigger::bothSidesRecognizedGerberType()
{
    const QVector<DifferingPair> pairs = {{"top.gtl", "top.gtl"}};

    const QVector<DifferingPair> eligible = GerberDiffTrigger::eligiblePairs(pairs);

    QCOMPARE(eligible.size(), 1);
}

void TestGerberDiffTrigger::oneSideNotGerberType()
{
    const QVector<DifferingPair> pairs = {{"top.gtl", "notes.docx"}};

    const QVector<DifferingPair> eligible = GerberDiffTrigger::eligiblePairs(pairs);

    QCOMPARE(eligible.size(), 0);
}

void TestGerberDiffTrigger::neitherSideGerberType()
{
    const QVector<DifferingPair> pairs = {{"notes.docx", "notes.docx"}};

    const QVector<DifferingPair> eligible = GerberDiffTrigger::eligiblePairs(pairs);

    QCOMPARE(eligible.size(), 0);
}

void TestGerberDiffTrigger::emptyInputProducesEmptyOutput()
{
    const QVector<DifferingPair> eligible = GerberDiffTrigger::eligiblePairs({});

    QCOMPARE(eligible.size(), 0);
}

void TestGerberDiffTrigger::drillSideExcludesFromEligible()
{
    const QVector<DifferingPair> pairs = {{"board.drl", "board.drl"}};

    const QVector<DifferingPair> eligible = GerberDiffTrigger::eligiblePairs(pairs);

    QCOMPARE(eligible.size(), 0);
}

void TestGerberDiffTrigger::drillPairsBucketsDrillSide()
{
    const QVector<DifferingPair> pairs = {{"board.drl", "board.drl"}, {"top.gtl", "top.gtl"}};

    const QVector<DifferingPair> drill = GerberDiffTrigger::drillPairs(pairs);

    QCOMPARE(drill.size(), 1);
    QCOMPARE(drill.first().relativePathA, QString("board.drl"));
}

void TestGerberDiffTrigger::unrecognizedPairsBucketsNonDrillUnrecognized()
{
    const QVector<DifferingPair> pairs = {{"top.gtl", "notes.docx"}, {"board.drl", "board.drl"}};

    const QVector<DifferingPair> unrecognized = GerberDiffTrigger::unrecognizedPairs(pairs);

    QCOMPARE(unrecognized.size(), 1);
    QCOMPARE(unrecognized.first().relativePathB, QString("notes.docx"));
}

void TestGerberDiffTrigger::drillTakesPriorityOverUnrecognized()
{
    // One side is a drill file, the other an unrecognized extension: an
    // Add-extension button could never make this renderable in M4, so it
    // must land in drillPairs, not unrecognizedPairs.
    const QVector<DifferingPair> pairs = {{"board.drl", "board.foobar"}};

    QCOMPARE(GerberDiffTrigger::drillPairs(pairs).size(), 1);
    QCOMPARE(GerberDiffTrigger::unrecognizedPairs(pairs).size(), 0);
}

QTEST_MAIN(TestGerberDiffTrigger)
#include "test_gerberdifftrigger.moc"
```

- [ ] **Step 2: Run tests to verify they fail to build**

Run: `cmake --build build --config Debug --target test_gerberdifftrigger`
Expected: compile error — `unrecognizedPairs`/`drillPairs` are not members of `GerberDiffTrigger`.

- [ ] **Step 3: Update the header**

Replace the full contents of `src/archivecompare/gerberdifftrigger.h` with:

```cpp
#pragma once

#include <QVector>

#include "archivecomparisonresult.h"

// Splits a comparison's DifferingPairs into three buckets the M4 overlay UI
// acts on differently (2026-07-18-gerber-render-v2-m4-overlay-design.md,
// section 1): pairs the overlay can render now, pairs blocked only by an
// unrecognized extension (fixable via GerberFileType::addUserExtension),
// and pairs involving a drill (Excellon) file, which this app's Gerber
// parser cannot read yet (M5). A pair belongs to exactly one bucket - drill
// is checked first, so a pair with one drill side and one unrecognized side
// counts as drill (an Add-extension button could never make it renderable).
namespace GerberDiffTrigger {

QVector<DifferingPair> eligiblePairs(const QVector<DifferingPair> &differingPairs);

// Blocked only by an unrecognized extension on at least one side - neither
// side is a drill file.
QVector<DifferingPair> unrecognizedPairs(const QVector<DifferingPair> &differingPairs);

// At least one side is a drill (Excellon) file.
QVector<DifferingPair> drillPairs(const QVector<DifferingPair> &differingPairs);

} // namespace GerberDiffTrigger
```

- [ ] **Step 4: Implement**

Replace the full contents of `src/archivecompare/gerberdifftrigger.cpp` with:

```cpp
#include "gerberdifftrigger.h"

#include "../gerber/gerberfiletype.h"

namespace {

bool hasDrillSide(const DifferingPair &pair)
{
    return GerberFileType::isDrillFile(pair.relativePathA) || GerberFileType::isDrillFile(pair.relativePathB);
}

} // namespace

QVector<DifferingPair> GerberDiffTrigger::eligiblePairs(const QVector<DifferingPair> &differingPairs)
{
    QVector<DifferingPair> eligible;
    for (const auto &pair : differingPairs) {
        if (hasDrillSide(pair)) {
            continue;
        }
        if (GerberFileType::isRecognized(pair.relativePathA) && GerberFileType::isRecognized(pair.relativePathB)) {
            eligible.append(pair);
        }
    }
    return eligible;
}

QVector<DifferingPair> GerberDiffTrigger::unrecognizedPairs(const QVector<DifferingPair> &differingPairs)
{
    QVector<DifferingPair> unrecognized;
    for (const auto &pair : differingPairs) {
        if (hasDrillSide(pair)) {
            continue;
        }
        if (!GerberFileType::isRecognized(pair.relativePathA) || !GerberFileType::isRecognized(pair.relativePathB)) {
            unrecognized.append(pair);
        }
    }
    return unrecognized;
}

QVector<DifferingPair> GerberDiffTrigger::drillPairs(const QVector<DifferingPair> &differingPairs)
{
    QVector<DifferingPair> drill;
    for (const auto &pair : differingPairs) {
        if (hasDrillSide(pair)) {
            drill.append(pair);
        }
    }
    return drill;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run:
```
cmake --build build --config Debug --target test_gerberdifftrigger
ctest --test-dir build -C Debug -R test_gerberdifftrigger --output-on-failure
```
Expected: `100% tests passed`.

- [ ] **Step 6: Commit**

```bash
git add src/archivecompare/gerberdifftrigger.h src/archivecompare/gerberdifftrigger.cpp tests/unit/test_gerberdifftrigger.cpp
git commit -m "Split GerberDiffTrigger into eligible/unrecognized/drill buckets"
```

---

### Task 3: `ArchiveComparePage` — pairs list, overlay render, skipped-pairs UI

**Files:**
- Modify: `src/archivecompare/archivecomparepage.h`
- Modify: `src/archivecompare/archivecomparepage.cpp`

**Interfaces:**
- Consumes: `GerberDiffTrigger::eligiblePairs/unrecognizedPairs/drillPairs` (Task 2), `GerberFileType::addUserExtension` (existing), `GerberParser::parseContentToObjects`, `GerberObjectPaths::layerBoundsMm`, `LayerMaskRenderer::render`, `MaskCompositor::composite`/`TintedMask`, `CompositeView::fitTo`/`setComposite`/`renderRequested` (all existing, unchanged signatures).
- Produces: no new public API — `ArchiveComparePage`'s constructor signature is unchanged.

This task replaces the temporary all-files debugging list (`fileListA_`, `filePathsA_`, `populateFileList`, `showSelectedFile`, `renderShownLayer`, `kAlwaysShowGerberDiffSection`) with the real M4 feature end to end. It is one task because the pieces are not independently buildable: a partial edit (e.g., restoring the pairs list without the two-layer render) would leave `showSelectedPair` referencing a data model that doesn't exist yet. Manually tested per the constitution — this is UI/glue code; Task 4 is the verification pass.

- [ ] **Step 1: Replace the header**

Replace the full contents of `src/archivecompare/archivecomparepage.h` with:

```cpp
#pragma once

#include <QRectF>
#include <QVector>
#include <QWidget>

#include "../gerber/gerberobjectmodel.h"
#include "archivecomparisonresult.h"

class ArchivePanel;
class CompositeView;
class QLabel;
class QListWidget;
class QPushButton;
class QWidget;

// v1.0's feature page: two side-by-side ArchivePanels, re-compared
// automatically whenever either archive changes. Also hosts the Gerber
// overlay-compare area in the reserved bottom section (M4, see
// docs/superpowers/specs/2026-07-18-gerber-render-v2-m4-overlay-design.md):
// every changed Gerber pair between the two archives is listed; Show
// renders both revisions in one CompositeView, additively tinted (new =
// green, old = red, unchanged = yellow overlap) - the CAMMaster-style
// compare the whole v2 rendering rewrite was built toward.
class ArchiveComparePage : public QWidget {
    Q_OBJECT

public:
    explicit ArchiveComparePage(QWidget *parent = nullptr);

private:
    void runComparison();
    void rebuildSkippedPairsUi(const QVector<DifferingPair> &unrecognized, const QVector<DifferingPair> &drill);
    void showSelectedPair();
    void renderShownPair(const QRectF &windowMm, double pxPerMm);

    ArchivePanel *panelA_;
    ArchivePanel *panelB_;

    QWidget *gerberDiffContent_;
    QListWidget *gerberPairList_;
    QLabel *pairStatusLabel_;
    QLabel *skippedUnrecognizedLabel_;
    QWidget *skippedUnrecognizedButtonRow_;
    QLabel *skippedDrillLabel_;
    QPushButton *showButton_;
    QLabel *renderTimeLabel_;
    CompositeView *gerberCanvas_;

    // The pairs behind gerberPairList_'s rows, same order/index.
    QVector<DifferingPair> eligiblePairs_;

    // The two revisions currently on the canvas; either may be a
    // default-constructed empty Layer if that side failed to parse.
    Layer shownLayerA_;
    Layer shownLayerB_;
};
```

- [ ] **Step 2: Replace the implementation**

Replace the full contents of `src/archivecompare/archivecomparepage.cpp` with:

```cpp
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
```

- [ ] **Step 3: Build**

Run: `cmake --build build --config Debug`
Expected: builds clean, no errors. (Warnings about unused `QFileInfo` etc. should not appear — every include added above is used.)

- [ ] **Step 4: Commit**

```bash
git add src/archivecompare/archivecomparepage.h src/archivecompare/archivecomparepage.cpp
git commit -m "M4: real changed-pairs list, two-layer overlay render, skipped-pairs UI"
```

---

### Task 4: Full build, full test suite, and the M4 visual gate

**Files:** none (verification only).

- [ ] **Step 1: Full rebuild**

Run: `cmake --build build --config Debug`
Expected: exit 0, no errors.

- [ ] **Step 2: Full test suite**

Run (with `C:\Qt\6.8.2\msvc2022_64\bin` on `PATH`):
```
ctest --test-dir build -C Debug --output-on-failure
```
Expected: `100% tests passed` across all suites (14 total after Tasks 1–2 add no new executables, just new cases in `test_gerberfiletype` and `test_gerberdifftrigger`).

- [ ] **Step 3: Launch the app**

Run: `build/Debug/PcbSupplierWorkflow.exe` (with the same `PATH`).
Go to Archive Compare.

- [ ] **Step 4: Manual acceptance checks**

Using the real archives at `.claude/worktrees/gerber-parser-3b2/Test_data/`:

- **test_pair_1** (byte-identical re-zip): load both sides → no differing pairs → the Gerber section stays hidden entirely.
- **test_pair_2** (its two archives): load both sides → the changed `.GER` pair appears in the list. Select it, click Show → the overlay renders; the known change shows as a pure green or red patch on a yellow background (not just one flat color).
- **test_pair_3**: load both sides → skipped-pairs row appears ("N changed pair(s) skipped - unrecognized file extension:") with one Add button per extension (e.g. `.G1`, `.GPT`). Click one Add button → the matching pair moves into the eligible list immediately (no reload needed). Select it, click Show → the single added trace shows as a pure color, and the `%LPC` cutout layer still renders as a hole (not solid), confirming no regression from the M3 gate.
- Render-time label updates on every Show and every pan/zoom settle, now reflecting two-layer render cost.

- [ ] **Step 5: Report back**

Do not mark this milestone done from automated checks alone — Step 4 is Igor's visual gate, same as M3. Summarize what was checked and wait for confirmation that the overlay colors and skipped-pairs flow look right before considering M4 complete.
