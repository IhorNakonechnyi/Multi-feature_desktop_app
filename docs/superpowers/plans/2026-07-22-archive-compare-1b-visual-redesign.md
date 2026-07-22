# Archive Compare 1b "Elevated Panels" Visual Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restyle the app's shared shell (sidebar nav + status legend) and the Archive Compare page into the 1b "Elevated Panels" dark-card visual language, per `docs/superpowers/specs/2026-07-22-archive-compare-1b-visual-redesign-design.md`.

**Architecture:** New QSS, assembled in C++ from `palette.h`'s color tokens (`src/app/theme.h`/`.cpp`) and applied app-wide via `qApp->setStyleSheet()`, targets existing widgets by Qt class name or `objectName`. `StatusLegend` gains a real API and is wired into the sidebar for the first time. `ArchiveComparePage` is restructured from a stacked top/bottom layout into a 3-column layout and its mismatched-pairs list becomes click-to-preview (no more separate "Show" button).

**Tech Stack:** Qt 6.8.2 Widgets, C++20, CMake. No new dependencies.

## Global Constraints

- C++20, Qt 6.8.2 Widgets, CMake — this repo's existing toolchain only, no new third-party dependencies.
- The app is dark-mode-only (`main.cpp` forces `Qt::ColorScheme::Dark`) — unaffected by this work.
- `palette.h`'s existing `Same`/`Changed`/`Missing` background/foreground constants are unchanged. All new tokens are additive.
- Native Qt/Windows title bar stays — no frameless window, no custom-drawn chrome.
- Qt's QSS is a CSS2.1 subset: it does **not** support `letter-spacing`. Don't write it; there's no fallback needed, just omit it.
- A plain `QWidget` (or a `QLabel` used as a colored swatch) does not paint stylesheet `background`/`border` unless `setAttribute(Qt::WA_StyledBackground, true)` is set on it first — every new card-style container in this plan sets it explicitly. Forgetting it is the most common way this kind of change silently does nothing.
- QSS `padding` is not reliably honored by a plain `QWidget` with a manually-assigned layout (unlike `background`/`border-radius`, which paint correctly once `WA_StyledBackground` is set). Internal card spacing is done with `layout->setContentsMargins(...)` in code throughout this plan, never QSS `padding`, on bare `QWidget` containers.
- Per constitution principle 4 (practical testing, not perfect testing), this is UI/glue code: no new automated/unit tests. Every task is verified by building and running the app manually, per its own Step.
- Comparison/parsing/rendering business logic (`CrcComparator`, `GerberParser`, `ExcellonParser`, `MaskCompositor`, `LayerMaskRenderer`) is not touched anywhere in this plan.
- Build commands (PowerShell, repo root): `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug` then `cmake --build build`. The app executable lands at `build\Debug\PcbSupplierWorkflow.exe`.
- Known, accepted deviations from the reference mockup (already agreed with Igor, don't "fix" them mid-implementation): tree rows keep full-row color fill (not dot indicators); the pairs list stays a vertical `QListWidget` with pill-styled rows (not a horizontally-wrapping flex of pills); the two file trees are fixed-width, not splitter-resizable.

---

### Task 1: Palette tokens for the 1b redesign

**Files:**
- Modify: `src/common/palette.h`

**Interfaces:**
- Produces: `Palette::SidebarBackground`, `Palette::SidebarTitleText`, `Palette::NavActiveBackground`, `Palette::NavActiveText`, `Palette::NavInactiveText`, `Palette::LegendCardBackground`, `Palette::LegendLabelText`, `Palette::LegendItemText`, `Palette::CardBackground`, `Palette::CardHeaderBorder`, `Palette::PairActiveBackground`, `Palette::PairInactiveBackground`, `Palette::DetailText`, `Palette::FrameBackground` — all `constexpr const char*` hex strings, same pattern as the existing `Palette::SameBackground` etc. Consumed by `theme.cpp` (Task 2) and extended there (Task 5).

- [ ] **Step 1: Add the new token constants**

Add this block to `src/common/palette.h`, right after the existing `missingForeground()` inline function and before the closing `} // namespace Palette`:

```cpp
// Neutral/card tokens for the 1b "Elevated Panels" visual redesign (see
// docs/superpowers/specs/2026-07-22-archive-compare-1b-visual-redesign-design.md).
// Additive only - the Same/Changed/Missing tokens above are unchanged and
// remain the only colors used for comparison-result meaning. These are
// pure UI-chrome colors (backgrounds, dividers, inactive/active states).
constexpr auto FrameBackground = "#090a0c";
constexpr auto SidebarBackground = "#0c0d0f";
constexpr auto SidebarTitleText = "#e2e5e8";
constexpr auto NavActiveBackground = "#1d1f23";
constexpr auto NavActiveText = "#f0f2f5";
constexpr auto NavInactiveText = "#7d8085";
constexpr auto LegendCardBackground = "#141619";
constexpr auto LegendLabelText = "#6f7276";
constexpr auto LegendItemText = "#bbbec3";
constexpr auto CardBackground = "#101214";
constexpr auto CardHeaderBorder = "#1e1f22";
constexpr auto PairActiveBackground = "#212a33";
constexpr auto PairInactiveBackground = "#141619";
constexpr auto DetailText = "#c0c4cb";
```

Note: the design doc's `accent` (`#b6c6d7`) token is intentionally **not** added here. Its only two call-outs in the design doc were the active-nav icon and the selected-pair pill — but this app's nav icons are already real SVG assets (`:/icons/archive-compare.svg` etc.), not colorable placeholder swatches like the mockup's, so there's no icon-tinting mechanism to apply it to; and the selected pill uses its own distinct `PairActiveBackground` token (a muted dark blue, not the light `accent` value, which would be illegible as a background fill anyway). If a real need for a shared accent color emerges later, add it then.

- [ ] **Step 2: Build to confirm it compiles**

Run:
```
cmake --build build
```
Expected: build succeeds (this is a header-only addition of unused-so-far constants — nothing references them yet, so there's nothing to run/observe beyond a clean compile).

- [ ] **Step 3: Commit**

```bash
git add src/common/palette.h
git commit -m "Add 1b redesign color tokens to palette.h"
```

---

### Task 2: Dynamic StatusLegend + sidebar wiring

**Files:**
- Create: `src/app/theme.h`
- Create: `src/app/theme.cpp`
- Modify: `src/app/statuslegend.h`
- Modify: `src/app/statuslegend.cpp`
- Modify: `src/app/mainwindow.h`
- Modify: `src/app/mainwindow.cpp`
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Palette::SidebarBackground` etc. from Task 1.
- Produces: `QString themeStylesheet()` (free function, extended in Task 5) — consumed by `main.cpp`. `StatusLegend::Entry{QColor color; QString label;}` and `StatusLegend::setEntries(const QVector<Entry>&)` — consumed by `MainWindow`.

Note: `src/app/sidebarnav.{h,cpp}` is **not** modified by this task (or anywhere in this plan). Its styling (`SidebarNav { ... }`, `SidebarNav::item`, `SidebarNav::item:selected`) is done entirely through `theme.cpp`'s class-name QSS selectors, which Qt applies automatically to existing `SidebarNav` instances with no code change needed on the class itself. The wordmark label lives in `MainWindow`'s new wrapper widget instead, since `SidebarNav` derives from `QListWidget` and isn't a container that can hold sibling child widgets.

- [ ] **Step 1: Create `src/app/theme.h`**

```cpp
#pragma once

#include <QString>

// The 1b "Elevated Panels" redesign's app-wide QSS, assembled from
// palette.h's tokens so the hex values live in exactly one place
// (palette.h) - this file only arranges them into selectors. Applied once
// in main.cpp via qApp->setStyleSheet().
QString themeStylesheet();
```

- [ ] **Step 2: Create `src/app/theme.cpp`**

```cpp
#include "theme.h"

#include <QStringList>

#include "../common/palette.h"

QString themeStylesheet()
{
    QStringList rules;

    rules << QString("#sidebarPanel { background: %1; }").arg(Palette::SidebarBackground);
    rules << QString("#sidebarWordmark { color: %1; font-size: 14px; font-weight: 700; }")
                 .arg(Palette::SidebarTitleText);
    rules << QString("SidebarNav { background: %1; border: none; outline: none; }")
                 .arg(Palette::SidebarBackground);
    rules << QString("SidebarNav::item { padding: 11px 12px; border-radius: 8px; color: %1; font-size: 13.5px; }")
                 .arg(Palette::NavInactiveText);
    rules << QString("SidebarNav::item:selected { background: %1; color: %2; font-weight: 600; }")
                 .arg(Palette::NavActiveBackground, Palette::NavActiveText);
    rules << QString("StatusLegend { background: %1; border-radius: 8px; }")
                 .arg(Palette::LegendCardBackground);
    rules << QString("QLabel#legendTitleLabel { color: %1; font-size: 10px; }")
                 .arg(Palette::LegendLabelText);
    rules << QString("QLabel#legendItemLabel { color: %1; font-size: 11.5px; }")
                 .arg(Palette::LegendItemText);

    return rules.join('\n');
}
```

- [ ] **Step 3: Add the new files to `CMakeLists.txt`**

In `CMakeLists.txt`, in the `add_executable(PcbSupplierWorkflow ...)` list, add two lines right after `src/app/statuslegend.cpp`:

```cmake
    src/app/statuslegend.h
    src/app/statuslegend.cpp
    src/app/theme.h
    src/app/theme.cpp
    src/common/palette.h
```

(i.e. insert `src/app/theme.h` and `src/app/theme.cpp` between the existing `src/app/statuslegend.cpp` and `src/common/palette.h` lines.)

- [ ] **Step 4: Rewrite `src/app/statuslegend.h`**

```cpp
#pragma once

#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>

class QVBoxLayout;

// Small card explaining what a comparison result's colors mean. Not fixed
// content: setEntries() replaces its rows entirely, so the same card can
// show a different legend per active feature page (MainWindow drives
// this - see mainwindow.cpp). An empty entry list hides the whole card,
// which is the initial/default state until setEntries() is called.
class StatusLegend : public QWidget {
    Q_OBJECT

public:
    struct Entry {
        QColor color;
        QString label;
    };

    explicit StatusLegend(QWidget *parent = nullptr);

    void setEntries(const QVector<Entry> &entries);

private:
    QVBoxLayout *rowsLayout_;
};
```

- [ ] **Step 5: Rewrite `src/app/statuslegend.cpp`**

```cpp
#include "statuslegend.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace {

QWidget *makeRow(const QColor &color, const QString &label)
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);

    // Per-entry color, so this stays a direct inline stylesheet rather
    // than a themeStylesheet() rule (which only knows fixed palette.h
    // tokens, not a caller-supplied QColor).
    auto *swatch = new QLabel;
    swatch->setFixedSize(9, 9);
    swatch->setAttribute(Qt::WA_StyledBackground, true);
    swatch->setStyleSheet(QString("background:%1; border-radius:4px;").arg(color.name()));

    auto *text = new QLabel(label);
    text->setObjectName("legendItemLabel");

    layout->addWidget(swatch);
    layout->addWidget(text);
    layout->addStretch();
    return row;
}

} // namespace

StatusLegend::StatusLegend(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);

    auto *title = new QLabel(tr("STATUS LEGEND"));
    title->setObjectName("legendTitleLabel");
    outer->addWidget(title);

    rowsLayout_ = new QVBoxLayout;
    rowsLayout_->setContentsMargins(0, 8, 0, 0);
    rowsLayout_->setSpacing(6);
    outer->addLayout(rowsLayout_);

    setVisible(false); // nothing to show until setEntries() is called
}

void StatusLegend::setEntries(const QVector<Entry> &entries)
{
    QLayoutItem *item;
    while ((item = rowsLayout_->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    for (const Entry &entry : entries) {
        rowsLayout_->addWidget(makeRow(entry.color, entry.label));
    }
    setVisible(!entries.isEmpty());
}
```

Note: `setObjectName("statusLegendCard")` is deliberately *not* set here — `theme.cpp`'s `StatusLegend { ... }` rule targets the C++ class name directly, which QSS supports natively, and there's only ever one purpose for this class, so an objectName would be redundant.

- [ ] **Step 6: Rewrite `src/app/mainwindow.h`**

```cpp
#pragma once

#include <QMainWindow>

class QStackedWidget;
class SidebarNav;
class StatusLegend;

// App shell: sidebar nav on the left, the active feature page on the right.
// Splitter (not just a plain layout) so the sidebar width is user-resizable
// like a normal desktop app, per docs/designUI's mockup.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void updateStatusLegend(int pageIndex);

    SidebarNav *nav_;
    QStackedWidget *pages_;
    StatusLegend *statusLegend_;
};
```

- [ ] **Step 7: Rewrite `src/app/mainwindow.cpp`**

```cpp
#include "mainwindow.h"

#include <QLabel>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "sidebarnav.h"
#include "statuslegend.h"
#include "../archivecompare/archivecomparepage.h"
#include "../common/palette.h"
#include "../gerber/gerberviewerpage.h"
#include "../quotecompare/quotecomparepage.h"
#include "../supplieradvisor/supplieradvisorpage.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    // SidebarNav is a QListWidget itself, not a container - it can't hold
    // the wordmark label or the status legend card as direct children, so
    // this wrapper stacks all three: wordmark on top, the nav list taking
    // the remaining space, the legend card pinned to the bottom. This
    // wrapper - not nav_ directly - is what goes into the splitter.
    auto *sidebarPanel = new QWidget(splitter);
    sidebarPanel->setObjectName("sidebarPanel");
    sidebarPanel->setAttribute(Qt::WA_StyledBackground, true);
    auto *sidebarLayout = new QVBoxLayout(sidebarPanel);
    sidebarLayout->setContentsMargins(10, 16, 10, 16);
    sidebarLayout->setSpacing(4);

    auto *wordmark = new QLabel(tr("PCB Suite"));
    wordmark->setObjectName("sidebarWordmark");
    sidebarLayout->addWidget(wordmark);

    nav_ = new SidebarNav(sidebarPanel);
    sidebarLayout->addWidget(nav_, 1);

    statusLegend_ = new StatusLegend(sidebarPanel);
    sidebarLayout->addWidget(statusLegend_);

    pages_ = new QStackedWidget(splitter);

    // Order must match SidebarNav's item order - currentRowChanged drives
    // setCurrentIndex directly, no separate mapping needed.
    pages_->addWidget(new ArchiveComparePage);
    pages_->addWidget(new SupplierAdvisorPage);
    pages_->addWidget(new GerberViewerPage);
    pages_->addWidget(new QuoteComparePage);

    splitter->setStretchFactor(0, 0); // sidebar: fixed
    splitter->setStretchFactor(1, 1); // content: grows

    setCentralWidget(splitter);
    setWindowTitle(tr("PCB Supplier Workflow"));
    resize(1100, 700);

    connect(nav_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
    connect(nav_, &QListWidget::currentRowChanged, this, &MainWindow::updateStatusLegend);

    updateStatusLegend(nav_->currentRow());
}

void MainWindow::updateStatusLegend(int pageIndex)
{
    // Only Archive Compare (index 0) has a comparison concept today; the
    // other 3 pages are still stub "coming later" pages (see
    // supplieradvisorpage.cpp / gerberviewerpage.cpp / quotecomparepage.cpp)
    // with nothing meaningful to show yet - setEntries({}) hides the card.
    // Colors are the same Palette calls ArchiveTreeDelegate already uses,
    // so the legend can never drift out of sync with the tree's colors.
    if (pageIndex == 0) {
        statusLegend_->setEntries({
            {Palette::sameBackground(), tr("Identical")},
            {Palette::changedBackground(), tr("Different")},
            {Palette::missingBackground(), tr("Only in this archive")},
        });
    } else {
        statusLegend_->setEntries({});
    }
}
```

- [ ] **Step 8: Apply the stylesheet in `src/main.cpp`**

Rewrite `src/main.cpp`:

```cpp
#include <QApplication>
#include <QStyleHints>

#include "app/mainwindow.h"
#include "app/theme.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Dark mode is fixed on, not a system-following or user-toggleable
    // preference - deliberately always Qt::ColorScheme::Dark regardless of
    // the Windows theme setting, per the design decision for this app.
    QApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);

    // Organization/application name set here so QSettings (LocalSettings)
    // knows where to read/write its INI file without repeating these
    // strings at every call site.
    QApplication::setOrganizationName("PcbSupplierWorkflow");
    QApplication::setApplicationName("PcbSupplierWorkflow");

    app.setStyleSheet(themeStylesheet());

    MainWindow window;
    window.show();

    return app.exec();
}
```

- [ ] **Step 9: Build**

Run:
```
cmake --build build
```
Expected: build succeeds.

- [ ] **Step 10: Manually verify**

Run `build\Debug\PcbSupplierWorkflow.exe`. Confirm:
- The sidebar has a dark background, a "PCB Suite" wordmark at the top, and the active nav row ("Archive compare") is visually highlighted differently from the other three.
- Below the nav list, a "STATUS LEGEND" card is visible with 3 rows: Identical / Different / Only in this archive, each with a small colored dot (green/amber/red).
- Click "Supplier advisor" (or any other nav row): the legend card disappears entirely.
- Click back to "Archive compare": the legend card reappears with its 3 rows.

- [ ] **Step 11: Commit**

```bash
git add src/app/theme.h src/app/theme.cpp src/app/statuslegend.h src/app/statuslegend.cpp src/app/mainwindow.h src/app/mainwindow.cpp src/main.cpp CMakeLists.txt
git commit -m "Wire a dynamic StatusLegend into the sidebar, restyle sidebar to 1b"
```

---

### Task 3: Mismatched-pairs list becomes click-to-preview

**Files:**
- Modify: `src/archivecompare/archivecomparepage.h`
- Modify: `src/archivecompare/archivecomparepage.cpp`

**Interfaces:**
- Consumes: existing `ArchiveComparePage::showSelectedPair()` (unchanged signature/body).
- Produces: nothing new — this task only removes `showButton_` and rewires the trigger.

- [ ] **Step 1: Remove `showButton_` from `src/archivecompare/archivecomparepage.h`**

Remove the line `QPushButton *showButton_;` from the private member list, and remove the now-unused forward declaration `class QPushButton;` (nothing else in this header references `QPushButton`).

- [ ] **Step 2: Remove `showButton_` construction and rewire the trigger in `src/archivecompare/archivecomparepage.cpp`**

Remove these two lines from the constructor (they currently sit right before `renderTimeLabel_ = new QLabel(...)`):
```cpp
    showButton_ = new QPushButton(tr("Show"));
    showButton_->setEnabled(false);
```

Remove this line from the `leftColumnLayout->addWidget(...)` sequence:
```cpp
    leftColumnLayout->addWidget(showButton_);
```

Change the explanation label text (still in the constructor) from:
```cpp
    auto *explanation = new QLabel(
        tr("Changed Gerber files. Select a pair and click Show to compare old vs new."));
```
to:
```cpp
    auto *explanation = new QLabel(
        tr("Changed Gerber files. Select a pair to preview the diff."));
```

Replace the two connect lines:
```cpp
    connect(gerberPairList_, &QListWidget::currentRowChanged, this,
            [this](int row) { showButton_->setEnabled(row >= 0); });
    connect(showButton_, &QPushButton::clicked, this, &ArchiveComparePage::showSelectedPair);
```
with:
```cpp
    connect(gerberPairList_, &QListWidget::currentRowChanged, this,
            [this](int row) { if (row >= 0) showSelectedPair(); });
```

In `runComparison()`, remove this line:
```cpp
    showButton_->setEnabled(false);
```
(the line right after it, `pairStatusLabel_->clear();`, stays.)

- [ ] **Step 3: Build**

Run:
```
cmake --build build
```
Expected: build succeeds.

- [ ] **Step 4: Manually verify**

Run `build\Debug\PcbSupplierWorkflow.exe`, go to Archive Compare, load two `.zip` archives that contain at least 2 differing Gerber-recognized files (any real Gerber fixture pair works). Confirm:
- No "Show" button is present anywhere in the Gerber-diff section.
- Clicking any row in the pairs list immediately renders that pair's overlay in the canvas — no second click needed.
- Clicking a different row immediately re-renders for that pair.

- [ ] **Step 5: Commit**

```bash
git add src/archivecompare/archivecomparepage.h src/archivecompare/archivecomparepage.cpp
git commit -m "Archive Compare: click a pair to preview, remove the Show button"
```

---

### Task 4: Archive Compare 3-column layout restructure

**Files:**
- Modify: `src/archivecompare/archivecomparepage.h`
- Modify: `src/archivecompare/archivecomparepage.cpp`

**Interfaces:**
- Consumes: `ArchivePanel` (unchanged constructor signature), `CompositeView` (unchanged).
- Produces: no new public interface — `gerberPairList_`, `pairStatusLabel_`, `renderTimeLabel_`, `skippedUnrecognizedLabel_`, `skippedUnrecognizedButtonRow_`, `skippedDrillLabel_` keep their existing names/types so `runComparison()`, `rebuildSkippedPairsUi()`, `showSelectedPair()`, and `renderShownPair()` need **zero** changes in this task — only the constructor's widget arrangement changes.

- [ ] **Step 1: Rewrite `src/archivecompare/archivecomparepage.h`**

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
class QWidget;

// v1.0's feature page: two side-by-side ArchivePanels, re-compared
// automatically whenever either archive changes. Also hosts the Gerber
// overlay-compare area in the page's right-hand column (M4, see
// docs/superpowers/specs/2026-07-18-gerber-render-v2-m4-overlay-design.md):
// every changed Gerber pair between the two archives is listed; selecting
// one renders both revisions in one CompositeView, additively tinted (new =
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
    Layer parseGerberOrDrill(const QByteArray &bytes, const QString &relativePath, QString &parseError);

    ArchivePanel *panelA_;
    ArchivePanel *panelB_;

    QListWidget *gerberPairList_;
    QLabel *pairStatusLabel_;
    QLabel *skippedUnrecognizedLabel_;
    QWidget *skippedUnrecognizedButtonRow_;
    QLabel *skippedDrillLabel_;
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

(This removes the `gerberDiffContent_` member — the old stacked-layout wrapper it pointed to no longer exists once the layout below replaces it.)

- [ ] **Step 2: Replace the constructor in `src/archivecompare/archivecomparepage.cpp`**

Replace the entire `ArchiveComparePage::ArchiveComparePage(...)` constructor body (from `ArchiveComparePage::ArchiveComparePage(QWidget *parent)` through its closing `}`) with:

```cpp
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

    panelA_ = new ArchivePanel(tr("New (A)"));
    panelA_->setFixedWidth(210);
    panelB_ = new ArchivePanel(tr("Old (B)"));
    panelB_->setFixedWidth(210);
    contentRow->addWidget(panelA_);
    contentRow->addWidget(panelB_);

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
    // stretched) - scrolls internally if there are more pairs than fit.
    gerberPairList_->setMaximumHeight(140);
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

    // Detail card: per-pair status text, render time, and the static
    // overlay color legend - three separate elements sharing one card.
    auto *detailCard = new QWidget;
    detailCard->setObjectName("detailCard");
    detailCard->setAttribute(Qt::WA_StyledBackground, true);
    auto *detailCardLayout = new QVBoxLayout(detailCard);
    detailCardLayout->setContentsMargins(14, 10, 14, 10);
    pairStatusLabel_ = new QLabel;
    pairStatusLabel_->setObjectName("detailLabel");
    pairStatusLabel_->setWordWrap(true);
    renderTimeLabel_ = new QLabel(tr("Render time: -"));
    renderTimeLabel_->setObjectName("detailLabel");
    auto *overlayLegend = new QLabel(tr("green = only new  |  red = only old  |  yellow = same"));
    overlayLegend->setObjectName("detailLabel");
    detailCardLayout->addWidget(pairStatusLabel_);
    detailCardLayout->addWidget(renderTimeLabel_);
    detailCardLayout->addWidget(overlayLegend);

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
```

- [ ] **Step 3: Remove the now-unused `QSplitter` include**

At the top of `src/archivecompare/archivecomparepage.cpp`, remove the line `#include <QSplitter>` (nothing in the file uses `QSplitter` anymore — `QPushButton` and `QMessageBox` stay, they're still used by `rebuildSkippedPairsUi()` and `runComparison()`).

- [ ] **Step 4: Build**

Run:
```
cmake --build build
```
Expected: build succeeds.

- [ ] **Step 5: Manually verify**

Run `build\Debug\PcbSupplierWorkflow.exe`, go to Archive Compare. Confirm:
- Layout is one full-height row: the two file trees on the left (narrower, fixed width — try resizing the window, they should not grow/shrink), then the pairs list, canvas, and status/render-time/legend text stacked to their right, filling the rest of the width and height.
- Load two archives (same fixture pair as Task 3's verification) — trees populate, pairs list populates, clicking a pair still renders immediately (Task 3's behavior still works), status text and render-time text both update correctly.
- Trigger at least one skipped-pair case if your fixtures allow it (an unrecognized extension or a mixed drill/non-drill pair) and confirm the corresponding message/button still appears in the pairs area, unstyled but functionally present.

- [ ] **Step 6: Commit**

```bash
git add src/archivecompare/archivecomparepage.h src/archivecompare/archivecomparepage.cpp
git commit -m "Archive Compare: restructure into 3-column layout (trees / pairs+canvas+detail)"
```

---

### Task 5: Card styling for trees, pairs, canvas, and detail

**Files:**
- Modify: `src/app/theme.cpp`
- Modify: `src/archivecompare/archivepanel.cpp`

**Interfaces:**
- Consumes: `Palette::CardBackground`, `Palette::CardHeaderBorder`, `Palette::PairActiveBackground`, `Palette::PairInactiveBackground`, `Palette::DetailText`, `Palette::FrameBackground`, `Palette::SidebarTitleText` (Task 1). Consumes the `objectName`s set in Task 4 (`archiveComparePage`, `pageTitle`, `pairsCard`, `pairsList`, `canvasCard`, `detailCard`, `detailLabel`).
- Produces: nothing new — this task only adds QSS rules and two `objectName`/`WA_StyledBackground` calls in `archivepanel.cpp`.

- [ ] **Step 1: Add the remaining rules to `themeStylesheet()` in `src/app/theme.cpp`**

Insert these lines into the `rules` list, right before `return rules.join('\n');`:

```cpp
    rules << QString("ArchiveComparePage { background: %1; }").arg(Palette::FrameBackground);
    rules << QString("QLabel#pageTitle { color: %1; font-size: 15px; font-weight: 700; }")
                 .arg(Palette::SidebarTitleText);
    rules << QString("ArchivePanel { background: %1; border-radius: 8px; }").arg(Palette::CardBackground);
    rules << QString("#treeCardHeader { border-bottom: 1px solid %1; }").arg(Palette::CardHeaderBorder);
    rules << QString("#pairsCard { background: %1; border-radius: 8px; }").arg(Palette::CardBackground);
    rules << QString("QListWidget#pairsList { background: transparent; border: none; outline: none; }");
    rules << QString("QListWidget#pairsList::item { border-radius: 16px; padding: 5px 12px; margin: 2px 0; "
                      "background: %1; color: %2; font-size: 11.5px; }")
                 .arg(Palette::PairInactiveBackground, Palette::DetailText);
    rules << QString("QListWidget#pairsList::item:selected { background: %1; }")
                 .arg(Palette::PairActiveBackground);
    rules << QString("#canvasCard { background: %1; border-radius: 8px; }").arg(Palette::CardBackground);
    rules << QString("#detailCard { background: %1; border-radius: 8px; }").arg(Palette::CardBackground);
    rules << QString("QLabel#detailLabel { color: %1; font-size: 12px; }").arg(Palette::DetailText);
```

The full `rules <<` sequence (Task 2's 8 lines, then these 11) must all run before the final `return rules.join('\n');` — don't duplicate the `return` line.

- [ ] **Step 2: Style `ArchivePanel`'s header divider and enable card painting, in `src/archivecompare/archivepanel.cpp`**

In the constructor, change:
```cpp
ArchivePanel::ArchivePanel(const QString &roleLabel, QWidget *parent)
    : QWidget(parent)
    , roleLabel_(roleLabel)
{
    setAcceptDrops(true);

    auto *layout = new QVBoxLayout(this);

    auto *header = new QWidget;
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(4, 4, 4, 4);
```
to:
```cpp
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
```

(Everything else in `archivepanel.cpp` is unchanged.)

- [ ] **Step 3: Build**

Run:
```
cmake --build build
```
Expected: build succeeds.

- [ ] **Step 4: Manually verify**

Run `build\Debug\PcbSupplierWorkflow.exe`, go to Archive Compare, load the same two-archive fixture pair as before. Confirm, comparing against `docs/designUI/updated_design/Qt 6.8 Widgets redesign/design_handoff_1b_dark_theme/reference_mockup.dc.html`'s "1b" panel (open it in a browser side by side):
- The page background is a near-black tone, distinctly darker than default Qt gray.
- "Archive compare" title text is large/bold.
- Both file-tree panels are rounded dark cards with a visible divider line under their filename header row.
- The pairs list rows look like rounded pill chips (not square list rows); the currently-selected pair's pill is visibly a different shade than the others.
- The canvas sits inside its own rounded dark card.
- Below it, the detail card (status text / render time / overlay-tint legend) is a distinct rounded dark card with light-gray text.
- File tree rows still show full-row green/amber/red fill per file (`ArchiveTreeDelegate` is untouched by this plan) — the card background/radius only shows around the tree, not inside it.
- Folders in each tree still expand/collapse normally and the tree still scrolls if content overflows — unaffected by the card styling.
- Nothing regressed from Task 3/4 (click-to-preview still works, resizing the window still keeps trees fixed-width).

- [ ] **Step 5: Commit**

```bash
git add src/app/theme.cpp src/archivecompare/archivepanel.cpp
git commit -m "Style Archive Compare's tree/pairs/canvas/detail cards for the 1b redesign"
```
