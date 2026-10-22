#pragma once

#include <functional>

#include <QByteArray>

#include "archivecomparisonresult.h"

// The second look at a yellow pair. The CRC comparison calls two files
// different as soon as one byte differs - and a plain re-export rewrites the
// date, user and job lines in every file, so a resaved but unchanged project
// comes out entirely yellow. This check parses both files and asks whether
// the DATA is the same: if it is, the pair is moved back to green.
//
// Strict by design: it may leave a truly unchanged pair yellow (different CAD
// tool, different export settings - the overlay is still there for that), but
// it must never turn a real change green.
namespace SameDataCheck {

// True when both files are the same kind of layer data (both Gerber, or both
// Excellon drill) and carry the same data. Two conditions must BOTH hold:
//
// 1. The parsed objects match (LayerEquivalence::sameObjects - apertures by
//    shape, order ignored inside each polarity run).
// 2. Every line the object model does not capture matches too, in order -
//    format directives, attributes, the drill tool table, drill comments
//    (Gerber G04 comments are skipped - they never affect the image),
//    and directives the parser ignores - except export noise (exportnoise.h).
//
// Anything unparseable, or a layer with no objects at all, is never "same":
// an empty parse proves nothing about what the file contains.
bool isSameData(const QByteArray &bytesA, const QByteArray &bytesB);

using PairPredicate = std::function<bool(const DifferingPair &pair)>;

// Removes every pair `isSame` accepts from result.differingPairs, records it
// in result.sameDataPairs, and turns its two files green - unless a file is
// still part of another pair that stayed (the name pass can pair one file
// with several), in which case it stays yellow, because there is still
// something to look at. Only pairs where both files are GerberData are
// offered to `isSame`; nothing else can be parsed.
void collapseSameDataPairs(ArchiveComparisonResult &result, const PairPredicate &isSame);

} // namespace SameDataCheck
