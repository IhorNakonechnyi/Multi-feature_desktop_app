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
