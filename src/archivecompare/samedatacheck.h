#pragma once

#include <functional>

#include <QByteArray>

#include "../gerber/gerberobjectmodel.h"
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
// Excellon drill) and carry the same data. There are two ways to "same": the
// fast path described at the end, and the parsing check, for which two
// conditions must BOTH hold:
//
// 1. The parsed objects match (LayerEquivalence::sameObjects - apertures by
//    shape, order ignored inside each polarity run).
// 2. Every line the object model does not capture matches too, in order -
//    format directives, attributes, the drill tool table, drill comments
//    (Gerber G04 comments are skipped - they never affect the image) -
//    except export noise (exportnoise.h). Directives and commands the
//    parser ignores are not compared: they make the list come back as "no
//    answer", and if either file holds something the parser cannot vouch
//    for that way, the pair is never same.
//
// For the parsing check, anything unparseable is never "same". Two layers with
// no objects are the same only when neither file left a draw, flash or hole
// line out of the text comparison (sameObjectsOrBothEmpty).
//
// Before the parsing, a fast path (GerberParser::sameStatementsApartFromNoise,
// ExcellonParser::sameLinesApartFromNoise) answers "same" without parsing when
// both files are the same statement for statement apart from noise the parser
// ignores - the usual plain re-save. Then the parser gets the same input, so
// that is the same data even for a file the parsing check would refuse.
// When the fast path cannot prove it, isSameDataByParsing decides.
bool isSameData(const QByteArray &bytesA, const QByteArray &bytesB);

// The check above WITHOUT the fast path: the kind test, then conditions 1 and
// 2. isSameData uses it for every pair the fast path passes on; the tests
// use it to pin the parsing rules on their own.
bool isSameDataByParsing(const QByteArray &bytesA, const QByteArray &bytesB);

// Step 1 of the rule above, shared with SameDataDiagnosis so the two always
// agree. `drawLeftOut`: either file left a draw, flash or hole line out of
// its text list (the drawLeftOut flag of nonGeometryStatements/Lines).
bool sameObjectsOrBothEmpty(const Layer &layerA, const Layer &layerB, bool drawLeftOut);

using PairPredicate = std::function<bool(const DifferingPair &pair)>;

// Moves every pair `isSame` accepts from result.differingPairs to
// result.sameDataPairs and turns its two files green. One-to-one: once a file
// is proven same-data (the first accepted pair wins, walking the pairs sorted
// by path A then path B - so the outcome never depends on the input order),
// its other pairs are dropped without being tested. A file left with no pair
// at all because of that turns red (OnlyInThisArchive) - its possible
// partners already matched other files. A file that still has an unproven
// pair stays yellow. Only pairs where both files are GerberData are offered
// to `isSame`; nothing else can be parsed.
void collapseSameDataPairs(ArchiveComparisonResult &result, const PairPredicate &isSame);

} // namespace SameDataCheck
