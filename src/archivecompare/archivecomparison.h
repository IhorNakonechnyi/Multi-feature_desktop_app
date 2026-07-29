#pragma once

#include <QString>

#include "archivecomparisonresult.h"

// The whole read-expand-compare pipeline as one pure call: reads both
// archives' top levels, classifies content, then expands only the
// nested-archive candidates that turn out to genuinely differ (or exist on
// one side only) between the two archives - repeating level by level until
// nothing is left to expand. See
// docs/superpowers/specs/2026-07-27-content-first-archive-matching-design.md
// (Q4 point 3 / Q7) for the full rationale, and archivecomparison.cpp for
// the loop itself and why it terminates.
//
// Errors are data, not dialogs (Q7 rule): this module never shows a
// dialog, touches a widget, or includes a widget header - only the page
// that calls it is allowed to display anything. That keeps this logic
// unit-testable on its own and free to move off the GUI thread later
// without rework.
namespace ArchiveComparison {

struct Outcome {
    ArchiveComparisonResult result;
    QString errorA;       // non-empty = archive A unreadable; result is empty then
    QString errorB;       // non-empty = archive B unreadable; result is empty then
    qint64 compareMs = 0; // wall time of the whole pipeline (Q7: time compare AND render)
};

Outcome compareArchives(const QString &pathA, const QString &pathB);

} // namespace ArchiveComparison
