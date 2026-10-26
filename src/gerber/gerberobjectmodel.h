// src/gerber/gerberobjectmodel.h
#pragma once

#include <QHash>
#include <QString>
#include <QVector>

#include "gerberaperture.h"
#include "gerbermacro.h"

// A per-object description of one Gerber file's content, in Gerber's
// native coordinate system (integer nanometers, Y increasing upward per
// the RS-274X spec) - deliberately NOT yet resolved into final drawn
// geometry. This is the seam between "what does the file say" (this
// header, produced by GerberParser::parseContentToObjects) and "what does
// it look like" (GerberObjectFlatten, a separate, later stage) - keeping
// them separate is what makes canonicalization, a cheap equality check,
// and an exact polygon diff (Clipper2) possible later, none of which are
// possible against a single flattened QPainterPath.

// Not QPointF/QPoint: QPointF reintroduces the float-rounding risk this
// object model exists to avoid; QPoint's 32-bit int is too tight a margin
// for nanometers across a real board's extent (a 500mm board is
// 500,000,000nm - comfortably outside int32's ~2.1 billion range once
// you're not centered near the origin).
struct PointNm {
    qint64 x = 0;
    qint64 y = 0;
};

// Dark = additive (normal copper/fill). Clear = subtractive (a cutout -
// e.g. an anti-pad ring cut into a copper pour). Matches GerberParser's
// existing internal Polarity concept - this is the shared, public home
// for it now that more than one file needs it.
enum class Polarity { Dark, Clear };

enum class ObjectType { Flash, Stroke, Region };

// How a region outline reaches one of its vertices from the vertex before
// it: a straight edge, or an arc around an absolute center (already
// resolved from the file's I/J offsets, G74 signs included). The center
// means nothing for a straight edge and is left at (0,0).
struct RegionEdge {
    bool isArc = false;
    PointNm arcCenter;
    bool arcCounterClockwise = false;
};

// One drawn shape from a Gerber file. Fields are tagged by which
// ObjectType actually uses them - one flat struct rather than a
// std::variant-of-three-substructs, matching this codebase's existing
// "one struct, comments explain which fields matter when" convention
// (see GerberParser's ParserState) rather than introducing a new pattern
// this codebase hasn't used before.
struct GraphicObject {
    ObjectType type = ObjectType::Flash;
    Polarity polarity = Polarity::Dark;

    // Flash only: which aperture (by code, look up in Layer::apertures)
    // is flashed, and where.
    int apertureCode = -1;
    PointNm position;

    // Stroke only: draws from `position` to `strokeTo` using the
    // aperture at `apertureCode` as the pen shape. isArc selects a
    // curved path (via arcCenter/arcCounterClockwise) instead of a
    // straight line - same semantics as GerberParser's existing
    // drawArcStroke (arc center is an absolute point here, already
    // resolved from the file's I/J offset-from-start-point encoding).
    PointNm strokeTo;
    bool isArc = false;
    PointNm arcCenter;
    bool arcCounterClockwise = false;

    // Region only: explicit closed-contour vertices, in order. Mirrors
    // GerberParser's existing regionPoints collection (first point is the
    // opening move's destination, not auto-closed - closing is the
    // flatten stage's job, same as today's G37 handling).
    QVector<PointNm> regionPoints;
    // Region only, one entry per regionPoints entry: regionEdges[i] is the
    // edge that ENDS at regionPoints[i]. Entry 0 (the start) has no edge
    // and stays straight. The closing edge back to the start is straight;
    // a file that closes with an arc ends on the start point, so that arc
    // is the last entry. Shorter than regionPoints (an object built by
    // hand) means the missing edges are straight.
    QVector<RegionEdge> regionEdges;
};

// One Gerber file's full parsed content, before any geometry
// construction - GerberParser::parseContentToObjects's return type.
struct Layer {
    QHash<int, GerberAperture> apertures; // aperture code -> its definition
    QHash<QString, GerberMacro> macros;   // macro name -> its resolved statement list
    QVector<GraphicObject> objects;       // in file order

    // The file's declared unit scale (nanometers/mm per file-native unit -
    // 25.4 for an inches file (%MOIN*%), 1.0 for mm) - needed by the flatten
    // stage ONLY for macro apertures. Standard apertures (Circle/Rectangle/
    // Obround/Polygon) are already scaled to mm when GerberParser builds
    // `apertures` above, so their stored modifiers are correct regardless of
    // this field. A macro aperture's own instantiation parameters are NOT
    // pre-scaled (their length-vs-angle-vs-count meaning is only known once
    // substituted into a specific macro primitive field), so the flatten
    // stage needs this to scale them at that point instead. Getting this
    // wrong silently produces geometry off by exactly this factor - this
    // project already hit that once for real (see
    // .superpowers/sdd/progress.md's "aperture/macro sizes never unit-
    // converted" root cause), so this field exists specifically to carry
    // the file's unit scale across the parse/flatten seam rather than
    // letting it get lost the way it once did.
    double unitScale = 1.0;
};
