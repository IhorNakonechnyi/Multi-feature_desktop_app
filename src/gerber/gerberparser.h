#pragma once

#include <optional>

#include <QPainterPath>
#include <QString>
#include <QStringList>

#include "gerberobjectmodel.h"

// Parses one Gerber (RS-274X) file's full geometry into a single
// composited path, in millimeters, ready to hand to GerberDiffRenderer.
// See docs/superpowers/specs/2026-07-15-gerber-parser-design.md for the
// overall design (format handling, standard apertures, full aperture-
// macro support, arcs, region fills).
namespace GerberParser {

// The real, primary parse: walks the file once and produces the integer-
// nanometer object model (a Layer of GraphicObjects, apertures, macros) -
// a description of *what the file says*, deliberately not yet resolved
// into drawn geometry. This is the seam the rest of the pipeline
// (canonicalization, cheap equality, polygon diff) will build on. See
// gerberobjectmodel.h for the model's own rationale.
Layer parseContentToObjects(const QString &content);

// Parses already-loaded file text into a single composited QPainterPath -
// what the existing render pipeline (GerberDiffRenderer via
// ArchiveComparePage) consumes, and what most unit tests exercise. This
// is now a thin wrapper: it calls parseContentToObjects() and then
// reconstructs the exact same flattened path the parser produced before
// the object model existed. The signature and behavior are unchanged for
// its callers.
QPainterPath parseContent(const QString &content);

// Every statement in the file that is NOT captured by the object model,
// in file order, minus G04 comments, %TO/%TD object labels and volatile X2
// file attributes (see exportnoise.h). Geometry statements (draws, moves,
// flashes, aperture selects) and %AD aperture definitions are left out - they
// are compared through the parsed objects instead. A geometry statement is
// left out only in a form the parser fully reads into objects (fields in
// G/X/Y/I/J order, I/J only on an arc draw outside a region, bare coordinates
// only after a known operation); any other form is kept as text. Comments are left out because they
// never affect the image. %LN and %IN (layer/image names) and %IPPOS (the
// default polarity) are left out too: they differ per export and draw nothing.
//
// Macro definitions (%AM and its body) are left out too: Altium names macros
// after their D-code, and each used macro is compared by its recipe through
// the aperture key instead.
//
// Kept (must be equal): %FS %MO %SR %LP %TF %TA,
// and plain statements such as G01/G36/G75/G90/M02 (a one-digit G-code is
// written with its leading zero, so G1 and G01 compare equal).
//
// Unknown means no answer: any other % directive (%LR, %LM, %SF, %IPNEG, a
// stray primitive outside %AM, ...) and G91 (incremental coordinates) make
// the result std::nullopt. So do a %SR in a form the parser misreads, an
// aperture or macro defined twice, a unit switch after apertures or geometry,
// and a file cut off mid-statement. So does a region whose outline the parser cannot
// record exactly (a second outline after a D02, a start that is not a D02
// move, a flash, an unreadable statement, fewer than 3 vertices, no G37).
// The parser ignores or does not read these, and
// where they sit relative to the objects is not compared, so equal text in
// both files proves nothing. A file with std::nullopt is never the same
// data. Used by SameDataCheck.
std::optional<QStringList> nonGeometryStatements(const QString &content);

// Reads filePath and parses it. Returns an empty path if the file can't
// be read (no exception - an unreadable Gerber file here just means an
// empty render, the archive-open-failure case is already handled
// upstream by ArchiveReader).
QPainterPath parseFile(const QString &filePath);

} // namespace GerberParser
