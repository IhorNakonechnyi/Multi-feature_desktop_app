#pragma once

#include <QVector>

#include "archivecomparisonresult.h"

// Splits a comparison's DifferingPairs into three buckets the Archive
// Compare UI acts on differently (2026-07-18-gerber-render-v2-m4-overlay-
// design.md section 1, updated by 2026-07-19-gerber-render-v2-m5-drill-
// design.md section 2): pairs the overlay can render now (both sides
// Gerber, or both sides drill - dispatched to the matching parser), pairs
// blocked only by an unrecognized extension, and pairs with exactly one
// drill side, which can never be compared (a drill file against a copper
// layer is meaningless). A pair belongs to exactly one bucket - a mixed
// drill pair is checked first, so a pair with one drill side and one
// unrecognized side counts as drill.
//
// TEMPORARY (Task 9 -> Task 10): the "unrecognized extension" classification
// here is still extension-based, using a local copy of the logic
// GerberFileType used to provide before Task 9 deleted it in favor of
// content-only classify() (Q2/Q12). Task 10 rewrites this whole module to
// ask classify() for each pair's actual bytes and drops the mixed-drill
// bucket entirely - see docs/superpowers/specs/2026-07-27-content-based-
// drawability-design.md, "Removals". There is no longer an Add-extension
// button behind the "unrecognized" bucket - see that doc for why one
// could never be reinstated as useful.
namespace GerberDiffTrigger {

QVector<DifferingPair> eligiblePairs(const QVector<DifferingPair> &differingPairs);

// Blocked only by an unrecognized extension on at least one side - neither
// side is a drill file.
QVector<DifferingPair> unrecognizedPairs(const QVector<DifferingPair> &differingPairs);

// Exactly one side is a drill (Excellon) file - a both-drill pair is
// eligible instead (see eligiblePairs).
QVector<DifferingPair> drillPairs(const QVector<DifferingPair> &differingPairs);

} // namespace GerberDiffTrigger
