#pragma once

#include <functional>

#include <QByteArray>
#include <QString>
#include <QVector>

#include "archivecomparisonresult.h"

// Buckets a comparison's DifferingPairs by whether the overlay can draw
// both sides (docs/superpowers/specs/2026-07-27-content-based-drawability-
// design.md). Content decides, not the extension: each side's first
// GerberFileType::kClassifySniffBytes bytes are run through
// GerberFileType::classify(), and a pair is drawable when neither side
// classifies as NotDrawable. There used to be a third bucket here for
// "exactly one side is a drill file" - deleted (see the design doc's
// "Removals" section): it was unreachable (every pair shares a filename, so
// an extension-based drill test always agreed on both sides) and, even if
// reached, would have been the wrong rule - the app doesn't police layer
// semantics anywhere else, so a drill-vs-Gerber pair now renders like any
// other mismatched pair and the user sees the mismatch on canvas.
namespace GerberDiffTrigger {

// Returns one side's head bytes for classify() to look at. A callback
// rather than a direct ArchiveReader call so this module - and its tests -
// don't need a real archive on disk; tests pass a lambda over an in-memory
// fixture map instead. relativePath is whichever of a DifferingPair's two
// paths is being asked about.
using HeadReader = std::function<QByteArray(const QString &relativePath)>;

struct PairBuckets {
    QVector<DifferingPair> drawable;    // both sides classify != NotDrawable
    QVector<DifferingPair> notDrawable; // at least one side NotDrawable
};

// readHeadA/readHeadB are asked about relativePathA/relativePathB
// respectively - the two archives being compared, kept distinct because
// content-first matching can pair files that sit at different paths (see
// the content-first-archive-matching design doc), so a single reader over
// one archive would silently miss the other side.
PairBuckets classifyPairs(const QVector<DifferingPair> &pairs, const HeadReader &readHeadA,
                           const HeadReader &readHeadB);

} // namespace GerberDiffTrigger
