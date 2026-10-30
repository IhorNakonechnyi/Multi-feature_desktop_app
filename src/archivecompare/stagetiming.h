#pragma once

// TIMING-DIAG: temporary, multi-threading branch only - delete before merge.
//
// Per-stage timing for Archive Compare, built into the app only with the
// CMake option PCB_TIMING (docs/superpowers/specs/2026-10-26-stage-timing-
// diagnostic-design.md). The page fills these structs while it works and
// shows formatReport()'s text in a box the user copies out. The text holds
// numbers, sizes and counts only - never a file name, path or file content,
// so it can be pasted anywhere.

#include <optional>

#include <QSize>
#include <QString>
#include <QStringList>

namespace StageTiming {

struct MemoryUse {
    qint64 nowBytes = -1;     // working set; -1 = could not be read
    qint64 peakBytes = -1;    // peak working set over the process's whole life
    qint64 privateBytes = -1; // memory the process itself allocated (commit)
};

// The tree panels add to this while they rebuild; the page resets it at the
// start of each comparison. Global because the panels have no other link to
// the page's timing - acceptable only because all of it is deleted before
// merge.
struct TreeTimes {
    qint64 buildMs = 0;  // ArchiveTreeModel rebuild (buildFrom / setFilter)
    qint64 expandMs = 0; // QTreeView::expandAll
    qint64 widthMs = 0;  // QTreeView::resizeColumnToContents
    int builds = 0;      // how many rebuilds, both panels together
};
TreeTimes &treeTimes();

// The stages inside SameDataCheck::isSameData, summed over every checked
// pair. Global for the same reason as treeTimes(). Nanoseconds, because one
// small pair's stage is often under a millisecond.
struct CheckStages {
    qint64 textNs = 0;     // bytes -> text (QString::fromUtf8), both files
    qint64 settingsNs = 0; // the settings pass (nonGeometryStatements/Lines)
    qint64 shapesNs = 0;   // the full parse into shapes
    qint64 compareNs = 0;  // comparing the two shape lists
    // Result fingerprint: a SHA-256 chained over every checked pair - what
    // the check produced for it (settings lists, parsed objects) and its
    // verdict. Equal fingerprints on the same archives = the check gave the
    // same answers in every detail. A hash says nothing about the files.
    QByteArray fingerprint;
    qint64 fingerprintNs = 0; // building it; part of "Check parse", not of the four stages
    // The fast path (samedatacheck.cpp). The timing build runs it AND the
    // parsing check on every pair, so the two can be compared.
    qint64 fastNs = 0;          // the fast path itself
    int fastHits = 0;           // pairs it proved same
    qint64 parsingOnHitsNs = 0; // the parsing check on those pairs, minus its fingerprint hashing - what the normal build saves
    int disagreements = 0;      // fast path "same", parsing check not
    int greensMissed = 0;       // parsing check "same", fast path not
    // Why the fast path missed those greens (FastPathMiss): how many a fast
    // path that skips noise would have caught, and one value-free line per
    // missed pair (the first few). Building them is timed apart, so it can
    // be left out of the normal-build estimate.
    int missesSameWithoutNoise = 0;
    QStringList missLines;
    qint64 missDetailNs = 0;
};
CheckStages &checkStages();

// One archive's side of the comparison, for scaling the numbers.
struct SideCounts {
    int files = 0;            // entries after nested zips were expanded
    int rowsShown = 0;        // tree rows after the filter, folders included
    int gerberFiles = 0;      // entries whose content is Gerber/drill
    qint64 gerberBytes = 0;   // their uncompressed size
};

struct CompareTimes {
    qint64 totalMs = 0;
    qint64 zipMs = 0;          // outcome.compareMs
    qint64 typesMs = 0;        // both resolveKinds calls
    qint64 checkReadMs = 0;    // readEntryBytes inside the same-data predicate
    qint64 checkParseMs = 0;   // isSameData inside the same-data predicate
    qint64 checkTotalMs = 0;   // the whole collapseSameDataPairs call
    CheckStages check;         // copied from checkStages() at the end
    qint64 summaryMs = 0;      // summarize + summary line + banner
    TreeTimes trees;           // copied from treeTimes() at the end
    qint64 overlayClassMs = 0; // classifyPairs
    qint64 pairListMs = 0;     // filling the pair list + not-drawable label
    qint64 untilPaintedMs = -1; // compare start -> trees painted; -1 = not yet
    int pairsChecked = 0;      // predicate calls
    int pairsGreen = 0;        // predicate returned true
    qint64 parsedBytes = 0;    // both files, every checked pair
    qint64 greenParseMs = 0;   // isSameData time, green pairs only
    qint64 greenBytes = 0;     // both files, green pairs only
    qint64 slowestPairMs = -1; // read + parse of the slowest single pair
    qint64 slowestPairBytes = 0;
    int differingPairs = 0;    // pairs before the check
    int drawablePairs = 0;     // pairs the overlay list offers
    SideCounts sideA;
    SideCounts sideB;
    qint64 sincePreviousPickMs = -1; // gap since the previous archive pick
    qint64 privateStartBytes = -1;   // private memory at compare start
    qint64 privateAfterCheckBytes = -1;
    qint64 peakBeforeBytes = -1;     // peak working set at compare start
    MemoryUse memory;                // at the end
};

struct ClickTimes {
    qint64 readMs = 0;
    qint64 parseMs = 0;
    qint64 renderMs = -1; // -1 = nothing rendered yet
    QSize renderPx;       // size of the composited image
    qint64 slowestRenderMs = -1; // slowest render since the click (zoom/pan)
    QSize slowestRenderPx;
    qint64 fileBytes = 0;  // both files
    int objects = 0;       // parsed objects, both layers
    qint64 privateBeforeBytes = -1; // private memory before the read
    qint64 privateAfterParseBytes = -1;
    MemoryUse memory;
};

MemoryUse readMemoryUse();

// The whole readout, one value per line. A missing section shows "-" for
// every value, and so does a value with nothing to measure.
QString formatReport(const std::optional<CompareTimes> &compare, const std::optional<ClickTimes> &click);

} // namespace StageTiming
