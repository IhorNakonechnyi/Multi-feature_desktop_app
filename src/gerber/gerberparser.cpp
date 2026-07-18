#include "gerberparser.h"

#include <cmath>
#include <numbers>
#include <utility>

#include <QFile>
#include <QHash>
#include <QPainterPathStroker>
#include <QPointF>
#include <QPolygonF>
#include <QTextStream>

#include "gerberaperture.h"
#include "gerberformat.h"
#include "gerbermacro.h"

namespace {

// Gerber has two different statement terminators in play at once: '%'
// wraps a "parameter" block (which can itself contain several '*'
// terminated sub-statements, e.g. a macro's multiple primitive lines),
// while a plain command (a G/D/M/coordinate line) is terminated by '*'
// on its own. This walks the raw text once and produces a flat list of
// trimmed statements, still tagged with whether each came from inside a
// '%...%' block, since %-block statements (%FS, %AD, %AM's own body,
// %MO, %LP) are handled separately from plain G/D commands below.
struct GerberStatement {
    QString text;
    bool isParameterStatement = false;
};

// Polarity (Dark = additive fill, Clear = subtractive cutout, set by
// %LPD*%/%LPC*%) now lives in the shared object model (gerberobjectmodel.h,
// pulled in via gerberparser.h) rather than being re-declared here, since
// each emitted GraphicObject carries its own polarity.

QVector<GerberStatement> tokenize(const QString &content)
{
    QVector<GerberStatement> statements;
    int i = 0;
    while (i < content.length()) {
        // Skip whitespace between statements before deciding what the next
        // one is. Without this, the scan pointer lands on the newline right
        // after a block's closing '%' (or after a plain command's '*'), so
        // the very next '%...%' block's opening '%' would never be
        // recognized as a fresh block start - it would instead be swallowed
        // into a bogus "plain command" run all the way to the next '*',
        // corrupting every directive after the first one in the file.
        if (content.at(i).isSpace()) {
            ++i;
            continue;
        }
        if (content.at(i) == QLatin1Char('%')) {
            const int blockEnd = content.indexOf(QLatin1Char('%'), i + 1);
            if (blockEnd < 0) {
                break; // malformed - unterminated parameter block, stop parsing
            }
            const QString block = content.mid(i + 1, blockEnd - i - 1);
            const QStringList parts = block.split(QLatin1Char('*'), Qt::SkipEmptyParts);
            for (const QString &part : parts) {
                statements.append({part.trimmed(), true});
            }
            i = blockEnd + 1;
        } else {
            const int commandEnd = content.indexOf(QLatin1Char('*'), i);
            if (commandEnd < 0) {
                break;
            }
            const QString command = content.mid(i, commandEnd - i).trimmed();
            if (!command.isEmpty()) {
                statements.append({command, false});
            }
            i = commandEnd + 1;
        }
    }
    return statements;
}

// Mutable state the parser carries while walking through statements in
// order - this is deliberately one plain struct rather than a class with
// behavior, since every "primitive" here (format, current aperture,
// position) is just data the statement loop reads and updates directly.
struct ParserState {
    GerberFormat format;
    QHash<QString, QString> macroBodySource; // macro name -> its raw statements joined by '\n', re-split on use
    QHash<int, GerberAperture> apertures;     // aperture code -> its definition
    int currentApertureCode = -1;
    QPointF currentPoint{0.0, 0.0};

    // The parse's output: one GraphicObject per flash/stroke/region, in
    // file order, each tagged with the polarity in effect when it was
    // emitted. This replaces the old "build QPainterPath geometry into a
    // live polarity group as we go" approach - geometry is no longer built
    // here at all. The eventual union/subtract compositing (and the
    // once-per-polarity-run performance behavior) now happens later, in
    // flattenAndCompositeForLegacyRender, off this object list. See
    // gerberobjectmodel.h for why the parse stops at "what the file says".
    QVector<GraphicObject> objects;

    // The polarity in effect right now (%LPD dark / %LPC clear). Only used
    // to stamp each emitted object's .polarity - there's no live geometry
    // group to switch anymore, so an %LP directive just updates this field.
    Polarity currentPolarity = Polarity::Dark;

    // Which G-code interpolation mode is currently active - set by G01/
    // G02/G03 and consulted when a draw (D01) statement is reached, since
    // the mode and the coordinate data routinely arrive on separate
    // statements (mode set once, then reused by many draws).
    enum class InterpolationMode { Linear, ClockwiseArc, CounterClockwiseArc };
    InterpolationMode interpolationMode = InterpolationMode::Linear;

    // G36/G37 region-fill state: while insideRegion is true, coordinate
    // statements collect vertices into regionPoints instead of stroking a
    // pen - the whole contour is only turned into a Region GraphicObject
    // once G37 closes it. That object is emitted via emitObject() like any
    // flash or stroke, so it picks up whichever %LP polarity is current at
    // the time G37 closes it, same as before.
    bool insideRegion = false;
    QPolygonF regionPoints;

    // %SR (step-and-repeat) state: while active, every subsequent
    // draw/flash/region is repeated across a repeatCountX x repeatCountY
    // grid, stepping repeatStepXMm/repeatStepYMm (in mm) between copies.
    // Defaults represent "no repeat" (a 1x1 grid, same as %SR being
    // inactive), so emitObject() below needs no separate "is step-and-
    // repeat active" flag - it always just runs a (possibly 1x1) grid.
    int repeatCountX = 1;
    int repeatCountY = 1;
    double repeatStepXMm = 0.0;
    double repeatStepYMm = 0.0;
};

// Gerber's native coordinate system is integer nanometers; the statement
// loop works in millimeter doubles (decodeCoordinateMm), so these two tiny
// helpers convert at the boundary where mm values become object-model
// PointNm and back. The object model is deliberately nanometer-integer
// (see gerberobjectmodel.h) - the round-trip is exact for the whole-
// nanometer values real coordinates decode to, and sub-nanometer for any
// residue, far below anything a rendered path can show.
PointNm toNm(const QPointF &mm)
{
    return PointNm{GerberFormat::mmToNm(mm.x()), GerberFormat::mmToNm(mm.y())};
}

QPointF toMm(const PointNm &nm)
{
    return QPointF{GerberFormat::nmToMm(nm.x), GerberFormat::nmToMm(nm.y)};
}

// Parses "%ADnnSHAPE,mod1,mod2,...*%" (already split from its '%...%'
// wrapper by tokenize) into a GerberAperture, keyed by its code number.
void parseApertureDefinition(const QString &statement, ParserState &state)
{
    // statement looks like "ADD10C,1.0" or "ADD11TESTMACRO,0.5"
    const QString body = statement.mid(2); // drop "AD"
    int codeEnd = 1; // codes are always a single letter 'D' followed by digits
    while (codeEnd < body.length() && body.at(codeEnd).isDigit()) {
        ++codeEnd;
    }
    const int code = body.mid(1, codeEnd - 1).toInt();

    const int commaIndex = body.indexOf(QLatin1Char(','));
    const QString shapeAndName = commaIndex < 0 ? body.mid(codeEnd) : body.mid(codeEnd, commaIndex - codeEnd);
    const QStringList modifierStrings =
        commaIndex < 0 ? QStringList() : body.mid(commaIndex + 1).split(QLatin1Char('X'));

    GerberAperture aperture;
    if (shapeAndName == QLatin1String("C")) {
        aperture.shape = GerberAperture::Shape::Circle;
    } else if (shapeAndName == QLatin1String("R")) {
        aperture.shape = GerberAperture::Shape::Rectangle;
    } else if (shapeAndName == QLatin1String("O")) {
        aperture.shape = GerberAperture::Shape::Obround;
    } else if (shapeAndName == QLatin1String("P")) {
        aperture.shape = GerberAperture::Shape::Polygon;
    } else {
        aperture.shape = GerberAperture::Shape::Macro;
        aperture.macroName = shapeAndName;
    }
    for (const QString &modifier : modifierStrings) {
        aperture.modifiers.append(modifier.toDouble());
    }

    // Standard-shape modifiers are raw file-unit lengths (diameter/width/
    // height/hole-diameter) - like %SR's step values, they're plain
    // decimals not subject to %FS's coordinate encoding, so they need the
    // same manual inches->mm conversion coordinates get automatically via
    // decodeCoordinateMm. Polygon is the one exception with non-length
    // fields mixed in (vertex count, rotation angle), so those two are
    // deliberately left unscaled. Macro apertures are NOT scaled here -
    // their modifiers only become meaningful once substituted into a
    // specific macro primitive's field, so the conversion happens there
    // instead (GerberMacroBuilder), where each field's length-vs-angle-
    // vs-count meaning is actually known.
    const double unitScale = state.format.unitsAreInches ? 25.4 : 1.0;
    if (unitScale != 1.0) {
        switch (aperture.shape) {
        case GerberAperture::Shape::Circle:
        case GerberAperture::Shape::Rectangle:
        case GerberAperture::Shape::Obround:
            for (double &modifier : aperture.modifiers) {
                modifier *= unitScale;
            }
            break;
        case GerberAperture::Shape::Polygon:
            if (aperture.modifiers.size() > 0) {
                aperture.modifiers[0] *= unitScale; // outer diameter
            }
            if (aperture.modifiers.size() > 3) {
                aperture.modifiers[3] *= unitScale; // optional hole diameter
            }
            // modifiers[1] (vertex count) and [2] (rotation) stay as-is.
            break;
        case GerberAperture::Shape::Macro:
            break; // scaled later, per-field, inside GerberMacroBuilder
        }
    }

    state.apertures.insert(code, aperture);
}

// %SRX3Y2I5.0J4.0*% - repeat every subsequent draw/flash/region 3 times
// in X and 2 in Y, stepping 5.0/4.0 between copies. Unlike ordinary X/Y
// coordinates, the step distances here are plain decimal numbers in the
// file's current unit - NOT subject to %FS's digit-count/zero-suppression
// encoding - so they're parsed directly with toDouble(), only scaled to
// mm if the file is in inches. A bare "SR" with no fields (the common
// end-of-panel marker) resets back to "no repeat".
void parseStepRepeatDirective(const QString &text, ParserState &state)
{
    const int xIndex = text.indexOf(QLatin1Char('X'));
    const int yIndex = text.indexOf(QLatin1Char('Y'));
    const int iIndex = text.indexOf(QLatin1Char('I'));
    const int jIndex = text.indexOf(QLatin1Char('J'));

    if (xIndex < 0) {
        state.repeatCountX = 1;
        state.repeatCountY = 1;
        state.repeatStepXMm = 0.0;
        state.repeatStepYMm = 0.0;
        return;
    }

    const double unitScale = state.format.unitsAreInches ? 25.4 : 1.0;
    state.repeatCountX = text.mid(xIndex + 1, yIndex - xIndex - 1).toInt();
    state.repeatCountY = text.mid(yIndex + 1, iIndex - yIndex - 1).toInt();
    state.repeatStepXMm = text.mid(iIndex + 1, jIndex - iIndex - 1).toDouble() * unitScale;
    state.repeatStepYMm = text.mid(jIndex + 1).toDouble() * unitScale;
}

// Every flash/stroke/region goes through here instead of appending
// geometry directly, so step-and-repeat (a 1x1 grid when %SR is inactive,
// same as no repeat at all) is materialized in exactly one place rather
// than duplicated at each of the four emit call sites. Given a "base"
// GraphicObject describing the shape at the current point (grid cell 0,0),
// it appends one fully-resolved copy per grid cell into state.objects,
// each translated by the cell's (col*stepX, row*stepY) offset - so
// downstream never has to know step-and-repeat existed; the object list
// already holds the N x M concrete objects, not a template plus a repeat
// count.
//
// The offset is applied to every positional field the object type could
// use (position, strokeTo, arcCenter, and each region vertex) - the fields
// an object doesn't use are left at their defaults, where translating them
// is harmless since the flatten stage never reads them for that type. The
// row-outer/col-inner loop order matches the old addToComposited(), though
// order within one grid is immaterial: every cell shares the base object's
// polarity, so they all land in the same polarity run when composited.
void emitObject(ParserState &state, const GraphicObject &base)
{
    for (int row = 0; row < state.repeatCountY; ++row) {
        for (int col = 0; col < state.repeatCountX; ++col) {
            const qint64 offX = GerberFormat::mmToNm(col * state.repeatStepXMm);
            const qint64 offY = GerberFormat::mmToNm(row * state.repeatStepYMm);
            GraphicObject object = base;
            object.position.x += offX;
            object.position.y += offY;
            object.strokeTo.x += offX;
            object.strokeTo.y += offY;
            object.arcCenter.x += offX;
            object.arcCenter.y += offY;
            for (PointNm &vertex : object.regionPoints) {
                vertex.x += offX;
                vertex.y += offY;
            }
            state.objects.append(object);
        }
    }
}

// Parses "%AMname*primitiveStatement*primitiveStatement*...*%" - tokenize
// has already split this into one GerberStatement per '*'-terminated
// piece, all flagged isParameterStatement, so this just needs to collect
// them from the "AMname" header statement until the next non-macro
// statement.
void parseMacroDefinition(const QVector<GerberStatement> &statements, int &index, ParserState &state)
{
    const QString header = statements.at(index).text; // "AMname"
    const QString name = header.mid(2);
    QStringList body;
    ++index;
    while (index < statements.size() && statements.at(index).isParameterStatement) {
        const QString &text = statements.at(index).text;
        // A new parameter directive (FS/MO/AD/LP/another AM) ends this
        // macro's body - only digit/'$'-led primitive-or-assignment
        // statements belong to it.
        if (text.startsWith(QLatin1String("FS")) || text.startsWith(QLatin1String("MO"))
            || text.startsWith(QLatin1String("AD")) || text.startsWith(QLatin1String("LP"))
            || text.startsWith(QLatin1String("AM"))) {
            break;
        }
        body.append(text);
        ++index;
    }
    --index; // the outer loop's ++index will re-advance past what we consumed
    state.macroBodySource.insert(name, body.join(QLatin1Char('\n')));
}

// Finds where a coordinate field's numeric text ends: the position of the
// next field letter (X/Y/I/J/...) at or after `start`, or the string's end
// if none follows. Used to bound every field's digits so they never
// swallow whatever field comes after them, regardless of which particular
// fields happen to be present on a given statement (plain X/Y moves,
// X/Y/I/J arcs, etc.).
int nextUppercaseIndex(const QString &text, int start)
{
    for (int i = start; i < text.length(); ++i) {
        if (text.at(i).isUpper()) {
            return i;
        }
    }
    return text.length();
}

// Decodes an "X..Y.." (or single-axis) coordinate token using the
// current format, updating `point` in place - only the axes actually
// present in the token are changed, since Gerber lets a command move
// only X or only Y and leave the other axis unchanged.
void applyCoordinateToken(const QString &token, const GerberFormat &format, QPointF &point)
{
    const int xIndex = token.indexOf(QLatin1Char('X'));
    const int yIndex = token.indexOf(QLatin1Char('Y'));
    if (xIndex >= 0) {
        const int end = (yIndex > xIndex) ? yIndex : token.length();
        point.setX(format.decodeCoordinateMm(token.mid(xIndex + 1, end - xIndex - 1)));
    }
    if (yIndex >= 0) {
        // Bounded by the next field letter after Y (e.g. the 'I' that
        // starts an arc statement's trailing center-offset modifiers), or
        // the token's end if Y is the last field present. Previously this
        // ran unbounded to the end of the string, which was harmless while
        // no statement ever had anything after Y - but arc statements
        // (I/J center offsets) do, and without this bound the Y decode
        // would silently swallow "I...J..." text into itself.
        const int end = nextUppercaseIndex(token, yIndex + 1);
        point.setY(format.decodeCoordinateMm(token.mid(yIndex + 1, end - yIndex - 1)));
    }
}

// The four emit helpers below describe what happened (a flash, a linear or
// arc stroke, a region) as a GraphicObject and hand it to emitObject().
// They deliberately do NOT build any QPainterPath geometry - that is now
// the flatten stage's job (resolve*Path + flattenAndCompositeForLegacyRender
// further down). Splitting it this way is the whole point of this task: the
// parse records the file's meaning in Gerber's native nanometer coordinates,
// and geometry construction (apertures, macros, stroker) is a separate,
// later pass that can be swapped for an exact polygon flatten without
// touching the parse.
void emitFlash(ParserState &state)
{
    GraphicObject object;
    object.type = ObjectType::Flash;
    object.polarity = state.currentPolarity;
    object.apertureCode = state.currentApertureCode;
    object.position = toNm(state.currentPoint);
    emitObject(state, object);
}

void emitLinearStroke(ParserState &state, const QPointF &from, const QPointF &to)
{
    GraphicObject object;
    object.type = ObjectType::Stroke;
    object.polarity = state.currentPolarity;
    object.apertureCode = state.currentApertureCode;
    object.position = toNm(from);
    object.strokeTo = toNm(to);
    object.isArc = false;
    emitObject(state, object);
}

void emitArcStroke(ParserState &state, const QPointF &from, const QPointF &to, const QPointF &center,
                   bool counterClockwise)
{
    GraphicObject object;
    object.type = ObjectType::Stroke;
    object.polarity = state.currentPolarity;
    object.apertureCode = state.currentApertureCode;
    object.position = toNm(from);
    object.strokeTo = toNm(to);
    object.isArc = true;
    object.arcCenter = toNm(center);
    object.arcCounterClockwise = counterClockwise;
    emitObject(state, object);
}

// ---------------------------------------------------------------------------
// Reconstruction: resolve the object model back to the legacy flattened path
// ---------------------------------------------------------------------------
//
// These four resolve*Path functions rebuild exactly the geometry the parser
// used to build inline (in flash()/drawLinearStroke()/drawArcStroke()/the G37
// block, before this task moved geometry construction out of the parse). They
// are pure: they take a GraphicObject's already-resolved millimeter fields and
// the aperture/macro definitions, and return a QPainterPath - no ParserState,
// no step-and-repeat (already materialized into separate objects at emit
// time), no polarity grouping (the flatten loop's job). Coordinates arrive as
// object-model nanometers and are converted back to mm here via toMm(); the
// aperture modules still work in mm exactly as before, untouched.

QPainterPath resolveFlashPath(const GerberAperture &aperture, const QPointF &positionMm, const Layer &layer,
                              double unitScale)
{
    // Identical to the old flash(): build the aperture's flash geometry at the
    // origin (resolving a macro aperture through the layer's macros, scaled by
    // unitScale just as the inline flash() did), then translate to the flash
    // point. layer.macros holds the same GerberMacro values the old
    // lookupMacro() produced, so macro geometry is byte-identical.
    QPainterPath flashPath = GerberApertureGeometry::buildFlashPath(
        aperture, [&layer, unitScale](const QString &name, const QVector<double> &params) {
            GerberMacro macro = layer.macros.value(name);
            return GerberMacroBuilder::buildPath(macro, params, unitScale);
        });
    QTransform translate;
    translate.translate(positionMm.x(), positionMm.y());
    return translate.map(flashPath);
}

QPainterPath resolveLinearStrokePath(const GerberAperture &aperture, const QPointF &from, const QPointF &to)
{
    // A drawn line's "pen" is the current aperture's own shape - for the
    // overwhelmingly common circular-aperture case, a stroke is just a
    // capsule (rectangle plus two round end caps) between from/to, built
    // the same way GerberMacro's Vector Line primitive builds one.
    const double width = aperture.modifiers.value(0, 0.1);
    QPainterPath stroke;
    const double dx = to.x() - from.x();
    const double dy = to.y() - from.y();
    const double length = std::hypot(dx, dy);
    const double angleDegrees = qRadiansToDegrees(std::atan2(dy, dx));
    stroke.addRoundedRect(0, -width / 2.0, length, width, width / 2.0, width / 2.0);
    QTransform transform;
    transform.translate(from.x(), from.y());
    transform.rotate(angleDegrees);
    return transform.map(stroke);
}

// Draws a circular arc as a single stroked polyline approximating the
// curve - the simplest correct way to render an arc without needing
// QPainterPath's native arc primitives (which use a different angle
// convention than Gerber's I/J center offsets). `counterClockwise` selects
// which of the two arcs between `from` and `to` (going around `center`)
// gets drawn; single- vs multi-quadrant mode (G74/G75) doesn't change this
// - it only affects how a real Gerber writer chooses which arc to ask
// for, not how one is drawn.
//
// This builds the whole arc as one polyline and strokes it in a single
// QPainterPathStroker pass, rather than unioning many short capsule
// segments: chaining dozens of near-collinear QPainterPath::united() calls
// turned out to be numerically fragile in practice - the combined path's
// winding computation could report `contains` as false for points well
// inside the visibly-correct shape once enough thin, touching pieces were
// combined. One stroke-and-unite avoids that.
QPainterPath resolveArcStrokePath(const GerberAperture &aperture, const QPointF &from, const QPointF &to,
                                  const QPointF &center, bool counterClockwise)
{
    const double width = aperture.modifiers.value(0, 0.1);

    const double startAngle = std::atan2(from.y() - center.y(), from.x() - center.x());
    double endAngle = std::atan2(to.y() - center.y(), to.x() - center.x());
    const double radius = std::hypot(from.x() - center.x(), from.y() - center.y());

    if (counterClockwise && endAngle < startAngle) {
        endAngle += 2.0 * std::numbers::pi;
    } else if (!counterClockwise && endAngle > startAngle) {
        endAngle -= 2.0 * std::numbers::pi;
    }

    constexpr int kSegments = 32; // fine enough to look smooth at any real board scale
    QPainterPath centerline;
    centerline.moveTo(from);
    for (int i = 1; i <= kSegments; ++i) {
        const double t = static_cast<double>(i) / kSegments;
        const double angle = startAngle + (endAngle - startAngle) * t;
        centerline.lineTo(center.x() + radius * std::cos(angle), center.y() + radius * std::sin(angle));
    }

    QPainterPathStroker stroker;
    stroker.setWidth(width);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(centerline);
}

QPainterPath resolveRegionPath(const QVector<PointNm> &regionPoints)
{
    // Same closed polygon the old G37 block built: the collected contour
    // vertices, closed. The caller only emits a Region object when it has at
    // least 3 vertices (matching the old size >= 3 guard), so no empty-region
    // check is needed here.
    QPolygonF polygon;
    polygon.reserve(regionPoints.size());
    for (const PointNm &vertex : regionPoints) {
        polygon << toMm(vertex);
    }
    QPainterPath region;
    region.addPolygon(polygon);
    region.closeSubpath();
    return region;
}

// Reconstructs today's exact flattened QPainterPath from a Layer, so the
// existing render pipeline is byte-for-byte unaffected by the object model
// now sitting in between. Geometry is grouped into contiguous same-polarity
// runs of layer.objects and composited with one real boolean op per run -
// united() for a dark run (adds area), subtracted() for a clear run (cuts a
// hole) - never once per shape. That once-per-run behavior is what avoids the
// O(N^2) united() trap a dense layer used to hit; within a run, shapes are
// accumulated cheaply with addPath() under Qt::WindingFill, so overlapping
// same-orientation shapes stay solid without a per-shape union.
//
// The object list already carries each object's polarity, so grouping is a
// plain run-length walk over it - unlike the old live parse, there's no need
// to track transitions against a separate "current polarity". An %LP
// transition that produced no shapes simply leaves no object, so it collapses
// away here (a union/subtract against an empty run is a no-op either way),
// which is why this needs no explicit empty-group bookkeeping to match the old
// output.
QPainterPath flattenAndCompositeForLegacyRender(const Layer &layer, double unitScale)
{
    struct PolarityGroup {
        Polarity polarity;
        QPainterPath path;
    };
    // Seed with the same initial empty Dark group the old ParserState started
    // with, so a file that opens in clear polarity still subtracts from an
    // empty dark base (staying empty) rather than starting from a clear group.
    QVector<PolarityGroup> groups;
    groups.append({Polarity::Dark, QPainterPath{}});
    groups.last().path.setFillRule(Qt::WindingFill);

    for (const GraphicObject &object : layer.objects) {
        if (object.polarity != groups.last().polarity) {
            groups.append({object.polarity, QPainterPath{}});
            groups.last().path.setFillRule(Qt::WindingFill);
        }
        QPainterPath shape;
        switch (object.type) {
        case ObjectType::Flash:
            shape = resolveFlashPath(layer.apertures.value(object.apertureCode), toMm(object.position), layer,
                                     unitScale);
            break;
        case ObjectType::Stroke:
            if (object.isArc) {
                shape = resolveArcStrokePath(layer.apertures.value(object.apertureCode), toMm(object.position),
                                             toMm(object.strokeTo), toMm(object.arcCenter),
                                             object.arcCounterClockwise);
            } else {
                shape = resolveLinearStrokePath(layer.apertures.value(object.apertureCode),
                                                toMm(object.position), toMm(object.strokeTo));
            }
            break;
        case ObjectType::Region:
            shape = resolveRegionPath(object.regionPoints);
            break;
        }
        groups.last().path.addPath(shape);
    }

    // Combine the (typically few) polarity groups in file order: each dark
    // group adds area, each clear group removes it. This is the only point
    // where a real boolean op runs - once per polarity run, not once per
    // shape.
    QPainterPath result;
    for (const PolarityGroup &group : groups) {
        result = (group.polarity == Polarity::Dark) ? result.united(group.path) : result.subtracted(group.path);
    }
    return result;
}

// The real parse: walks the tokenized statements once and records each
// flash/stroke/region as a GraphicObject, ending with a fully-populated
// Layer. Structurally identical to the old parseContent's statement loop -
// same directive handling, same region/arc/step-and-repeat/%LP state - the
// only change is that the four geometry-emitting call sites now build objects
// via emit*(), rather than building QPainterPath geometry inline. outUnitScale
// reports the file's final unit scale (25.4 for inches, 1.0 for mm) for the
// flatten stage's macro-aperture resolution; the public parseContentToObjects
// wrapper discards it, parseContent uses it.
Layer parseContentToObjectsInternal(const QString &content, double &outUnitScale)
{
    const QVector<GerberStatement> statements = tokenize(content);
    ParserState state;

    for (int index = 0; index < statements.size(); ++index) {
        const GerberStatement &statement = statements.at(index);
        // A local copy (not a reference into statement.text) - the
        // G-prefixed-statement handling below needs to strip a leading
        // mode-set token in place before falling through to the
        // coordinate/D-code handling shared with plain (non-G) statements.
        QString text = statement.text;

        if (GerberFormatParser::tryParseDirective(
                statement.isParameterStatement ? (QLatin1Char('%') + text + QLatin1String("*%")) : text,
                state.format)) {
            continue;
        }
        if (statement.isParameterStatement && text.startsWith(QLatin1String("AD"))) {
            parseApertureDefinition(text, state);
            continue;
        }
        if (statement.isParameterStatement && text.startsWith(QLatin1String("AM"))) {
            parseMacroDefinition(statements, index, state);
            continue;
        }
        if (statement.isParameterStatement && text.startsWith(QLatin1String("SR"))) {
            parseStepRepeatDirective(text, state);
            continue;
        }
        if (statement.isParameterStatement && text.startsWith(QLatin1String("LP"))) {
            // "%LPD*%" (dark, the common default) or "%LPC*%" (clear, a
            // subtractive cutout) - text is already stripped of the '%...*%'
            // wrapper by tokenize(), so text is exactly "LPD" or "LPC". This
            // just updates the polarity stamped onto subsequently emitted
            // objects; there's no live geometry group to switch anymore (the
            // flatten stage regroups by each object's own polarity later). A
            // redundant repeat (same polarity again) is a harmless no-op here.
            state.currentPolarity = (text.mid(2) == QLatin1String("C")) ? Polarity::Clear : Polarity::Dark;
            continue;
        }
        if (statement.isParameterStatement) {
            continue; // anything else not yet handled - ignored, not fatal
        }
        if (text.startsWith(QLatin1String("G04"))) {
            // G04 is RS-274X's dedicated comment code - the rest of the
            // statement is free-form human text (e.g. "G04 END OF APERTURE
            // DEFINITIONS*") and must never be parsed as coordinates/D-codes,
            // even though ordinary comment text routinely contains uppercase
            // X/Y/D characters. Always a pure no-op, regardless of content.
            continue;
        }
        if (text.startsWith(QLatin1String("G36"))) {
            // Starts a region-fill contour - coordinate statements from here
            // until G37 collect vertices instead of stroking a pen (see the
            // insideRegion branch at the bottom of this loop).
            state.insideRegion = true;
            state.regionPoints.clear();
            continue;
        }
        if (text.startsWith(QLatin1String("G37"))) {
            // Closes the region: emit it as a Region GraphicObject carrying
            // the collected contour vertices (the flatten stage closes and
            // fills the polygon later, the same way a flash or stroke is
            // resolved later). Fewer than 3 points means the region never got
            // any real coordinate data (malformed input) - nothing to emit.
            state.insideRegion = false;
            if (state.regionPoints.size() >= 3) {
                GraphicObject object;
                object.type = ObjectType::Region;
                object.polarity = state.currentPolarity;
                object.regionPoints.reserve(state.regionPoints.size());
                for (const QPointF &vertex : state.regionPoints) {
                    object.regionPoints.append(toNm(vertex));
                }
                emitObject(state, object);
            }
            continue;
        }
        if (text.startsWith(QLatin1Char('G')) && !text.startsWith(QLatin1String("G70"))
            && !text.startsWith(QLatin1String("G71"))) {
            // G01/G02/G03 select the interpolation mode used by whichever
            // draw (D01) statement comes next - the mode-set and the
            // coordinate data routinely arrive on separate statements (mode
            // set once, reused by many draws), so the mode is recorded here
            // unconditionally, before checking whether this statement also
            // carries coordinate/D-code data to act on right away.
            const bool isInterpolationModeGCode = text.startsWith(QLatin1String("G01"))
                || text.startsWith(QLatin1String("G02")) || text.startsWith(QLatin1String("G03"));
            if (text.startsWith(QLatin1String("G01"))) {
                state.interpolationMode = ParserState::InterpolationMode::Linear;
            } else if (text.startsWith(QLatin1String("G02"))) {
                state.interpolationMode = ParserState::InterpolationMode::ClockwiseArc;
            } else if (text.startsWith(QLatin1String("G03"))) {
                state.interpolationMode = ParserState::InterpolationMode::CounterClockwiseArc;
            }

            const bool hasCoordinateOrDCode = text.contains(QLatin1Char('X')) || text.contains(QLatin1Char('Y'))
                || text.contains(QLatin1Char('D'));
            if (!isInterpolationModeGCode || !hasCoordinateOrDCode) {
                // Either a pure mode-set G-code with no coordinate data this
                // time (nothing left to do), or some other G-code entirely
                // (G74/G75 quadrant mode - this parser's arc math always
                // resolves the shortest arc consistent with the requested
                // direction, so quadrant mode doesn't need tracking - or
                // anything else not yet handled). No further effect either
                // way.
                continue;
            }
            // G01/G02/G03 combined with coordinate/D-code data on the same
            // statement is the normal way real files express both moves and
            // arcs in one go (e.g. "G01X010000Y010000D01*", or an arc's
            // "G02X010000Y010000I005000J000000D01*"). Strip the leading
            // G-token here and fall through into the same coordinate+D-code
            // handling used for statements with no G prefix at all, which
            // below branches on state.interpolationMode to draw a line or
            // an arc as appropriate.
            int prefixEnd = 1;
            while (prefixEnd < text.length() && text.at(prefixEnd).isDigit()) {
                ++prefixEnd;
            }
            text = text.mid(prefixEnd);
        }
        if (text.startsWith(QLatin1Char('D')) && text.length() > 1 && text.at(1).isDigit()
            && !text.contains(QLatin1Char('X')) && !text.contains(QLatin1Char('Y'))) {
            const int code = text.mid(1).toInt();
            if (code >= 10) {
                state.currentApertureCode = code;
                continue;
            }
            // A bare D01/D02/D03 (no X/Y) is legal RS-274X shorthand for
            // "repeat the last coordinate" - route it through the same
            // draw/flash/move handling below, just using currentPoint as
            // both endpoints since no new coordinate was given.
            const QPointF &samePoint = state.currentPoint;
            if (code == 1) {
                emitLinearStroke(state, samePoint, samePoint);
            } else if (code == 3) {
                emitFlash(state);
            }
            continue;
        }

        // Anything left is a coordinate + D-code command: X..Y..D0n*
        const int dIndex = text.lastIndexOf(QLatin1Char('D'));
        if (dIndex < 0) {
            continue;
        }
        const QString coordinatePart = text.left(dIndex);
        const int opCode = text.mid(dIndex + 1).toInt();
        const QPointF previousPoint = state.currentPoint;
        applyCoordinateToken(coordinatePart, state.format, state.currentPoint);

        if (state.insideRegion) {
            // Region mode collects vertices instead of stroking a pen - the
            // contour is only turned into a filled polygon once G37 closes
            // it (see above). The first coordinate statement in a region is
            // conventionally a D02 move to the contour's starting corner, so
            // its vertex must be state.currentPoint (the move's destination,
            // decoded by applyCoordinateToken just above) - NOT
            // previousPoint, which is wherever the pen was left by whatever
            // came before the region and only happens to match the
            // destination when a region starts at the origin. See
            // regionOpeningMoveStartsFromItsOwnDestination for the
            // regression this guards against.
            if (state.regionPoints.isEmpty()) {
                state.regionPoints << state.currentPoint;
            }
            if (opCode == 1) {
                state.regionPoints << state.currentPoint;
            }
        } else if (opCode == 1 && state.interpolationMode != ParserState::InterpolationMode::Linear) {
            // An arc draw's center is given as I/J offsets from the arc's
            // *start* point (previousPoint), not from the origin or the
            // endpoint - see the Gerber spec. Each field is bounded the same
            // way applyCoordinateToken bounds X/Y, so I's digits stop at J
            // rather than running to the end of the string.
            const int iIndex = coordinatePart.indexOf(QLatin1Char('I'));
            const int jIndex = coordinatePart.indexOf(QLatin1Char('J'));
            QPointF centerOffset(0.0, 0.0);
            if (iIndex >= 0) {
                const int end = nextUppercaseIndex(coordinatePart, iIndex + 1);
                centerOffset.setX(state.format.decodeCoordinateMm(coordinatePart.mid(iIndex + 1, end - iIndex - 1)));
            }
            if (jIndex >= 0) {
                const int end = nextUppercaseIndex(coordinatePart, jIndex + 1);
                centerOffset.setY(state.format.decodeCoordinateMm(coordinatePart.mid(jIndex + 1, end - jIndex - 1)));
            }
            const QPointF center = previousPoint + centerOffset;
            emitArcStroke(state, previousPoint, state.currentPoint, center,
                          state.interpolationMode == ParserState::InterpolationMode::CounterClockwiseArc);
        } else if (opCode == 1) {
            emitLinearStroke(state, previousPoint, state.currentPoint);
        } else if (opCode == 3) {
            emitFlash(state);
        }
        // opCode == 2 (move) already updated currentPoint above and draws nothing.
    }

    // Assemble the Layer from the accumulated parse state. Macros were
    // collected as raw joined body text (macroBodySource) during parsing;
    // convert each to a GerberMacro by splitting on '\n' with SkipEmptyParts -
    // exactly what the old lookupMacro() did on each use - so the flatten
    // stage builds byte-identical macro geometry to the old inline flash().
    Layer layer;
    layer.apertures = state.apertures;
    layer.objects = std::move(state.objects);
    for (auto it = state.macroBodySource.constBegin(); it != state.macroBodySource.constEnd(); ++it) {
        GerberMacro macro;
        macro.name = it.key();
        macro.statements = it.value().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        layer.macros.insert(it.key(), macro);
    }

    // Unit scale for the flatten stage's macro-aperture resolution: 25.4 for
    // an inches file, 1.0 for mm. Standard apertures are already scaled to mm
    // at %AD time, but macro apertures defer scaling to build time (their
    // modifiers' length-vs-angle-vs-count meaning is only known once
    // substituted into a primitive). Units are declared once at the top of a
    // file (%MO / G70 / G71, before any geometry), so the final format's unit
    // is the one that was in effect for every flash - the same value the old
    // inline flash() read from state.format on each call.
    outUnitScale = state.format.unitsAreInches ? 25.4 : 1.0;
    // Also carried on the Layer itself (not just the out-param) so
    // GerberObjectFlatten - a separate consumer of this Layer that has no
    // access to outUnitScale - can scale macro-aperture geometry correctly
    // too. See Layer::unitScale's own comment (gerberobjectmodel.h) for why.
    layer.unitScale = outUnitScale;
    return layer;
}

} // namespace

Layer GerberParser::parseContentToObjects(const QString &content)
{
    double unitScale = 1.0; // discarded here - only the flatten stage needs it
    return parseContentToObjectsInternal(content, unitScale);
}

QPainterPath GerberParser::parseContent(const QString &content)
{
    double unitScale = 1.0;
    const Layer layer = parseContentToObjectsInternal(content, unitScale);
    return flattenAndCompositeForLegacyRender(layer, unitScale);
}

QPainterPath GerberParser::parseFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QPainterPath();
    }
    QTextStream stream(&file);
    return parseContent(stream.readAll());
}
