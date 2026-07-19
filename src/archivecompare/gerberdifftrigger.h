#pragma once

#include <QVector>

#include "archivecomparisonresult.h"

// Splits a comparison's DifferingPairs into three buckets the Archive
// Compare UI acts on differently (2026-07-18-gerber-render-v2-m4-overlay-
// design.md section 1, updated by 2026-07-19-gerber-render-v2-m5-drill-
// design.md section 2): pairs the overlay can render now (both sides
// Gerber, or both sides drill - dispatched to the matching parser), pairs
// blocked only by an unrecognized extension (fixable via
// GerberFileType::addUserExtension), and pairs with exactly one drill
// side, which can never be compared (a drill file against a copper layer
// is meaningless). A pair belongs to exactly one bucket - a mixed drill
// pair is checked first, so a pair with one drill side and one
// unrecognized side counts as drill (an Add-extension button could never
// make it renderable).
namespace GerberDiffTrigger {

QVector<DifferingPair> eligiblePairs(const QVector<DifferingPair> &differingPairs);

// Blocked only by an unrecognized extension on at least one side - neither
// side is a drill file.
QVector<DifferingPair> unrecognizedPairs(const QVector<DifferingPair> &differingPairs);

// Exactly one side is a drill (Excellon) file - a both-drill pair is
// eligible instead (see eligiblePairs).
QVector<DifferingPair> drillPairs(const QVector<DifferingPair> &differingPairs);

} // namespace GerberDiffTrigger
