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

namespace StageTiming {

struct MemoryUse {
    qint64 nowBytes = -1;  // working set; -1 = could not be read
    qint64 peakBytes = -1; // peak working set over the process's whole life
};

struct CompareTimes {
    qint64 totalMs = 0;
    qint64 zipMs = 0;          // outcome.compareMs
    qint64 typesMs = 0;        // both resolveKinds calls
    qint64 checkReadMs = 0;    // readEntryBytes inside the same-data predicate
    qint64 checkParseMs = 0;   // isSameData inside the same-data predicate
    qint64 overlayClassMs = 0; // classifyPairs
    int pairsChecked = 0;      // predicate calls
    int pairsGreen = 0;        // predicate returned true
    qint64 parsedBytes = 0;    // both files, every checked pair
    qint64 greenParseMs = 0;   // isSameData time, green pairs only
    qint64 greenBytes = 0;     // both files, green pairs only
    MemoryUse memory;
};

struct ClickTimes {
    qint64 readMs = 0;
    qint64 parseMs = 0;
    qint64 renderMs = -1; // -1 = nothing rendered yet
    QSize renderPx;       // size of the composited image
    MemoryUse memory;
};

MemoryUse readMemoryUse();

// The whole readout, one value per line. A missing section shows "-" for
// every value, and so does a value with nothing to measure.
QString formatReport(const std::optional<CompareTimes> &compare, const std::optional<ClickTimes> &click);

} // namespace StageTiming
