#pragma once

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
// file attributes (see exportnoise.h). Geometry statements (coordinates, D-codes, aperture
// selects) and %AD aperture definitions are left out - they are compared
// through the parsed objects instead. Comments are left out because they
// never affect the image. Everything else stays: format/unit directives, X2
// attributes such as .FileFunction, macro bodies, and any
// directive this parser ignores when drawing (%LR, %IP, ...). That last
// group is the point: two files can only be called "same data" if what the
// parser does not understand is identical too, so an ignored directive can
// never hide a real change. Used by SameDataCheck.
QStringList nonGeometryStatements(const QString &content);

// Reads filePath and parses it. Returns an empty path if the file can't
// be read (no exception - an unreadable Gerber file here just means an
// empty render, the archive-open-failure case is already handled
// upstream by ArchiveReader).
QPainterPath parseFile(const QString &filePath);

} // namespace GerberParser
