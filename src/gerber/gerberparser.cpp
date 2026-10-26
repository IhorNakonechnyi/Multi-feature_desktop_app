#include "gerberparser.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <utility>

#include <QFile>
#include <QHash>
#include <QPointF>
#include <QPolygonF>
#include <QSet>
#include <QRegularExpression>
#include <QTextStream>

#include "exportnoise.h"
#include "gerberaperture.h"
#include "gerberformat.h"
#include "gerbermacro.h"
#include "gerberobjectpaths.h"

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

// `truncated` (optional) is set to true when the scan stops early at an
// unterminated '%...' block or a last command with no '*' - the text from
// there on is dropped. The parser ignores that tail, so the same-data check
// asks for this flag: a dropped tail is a part of the file nobody compared.
QVector<GerberStatement> tokenize(const QString &content, bool *truncated = nullptr)
{
    QVector<GerberStatement> statements;
    if (truncated) {
        *truncated = false;
    }
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
                // malformed - unterminated parameter block, stop parsing.
                // (Whitespace was skipped above, so real text is dropped.)
                if (truncated) {
                    *truncated = true;
                }
                break;
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
                // A last command with no '*': it is never read.
                if (truncated) {
                    *truncated = true;
                }
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

    // RS-274D modal operation code: after an explicit D01/D02/D03, a
    // coordinate statement with no D-code at all repeats the same
    // operation ("X...D1*" then "Y...*" keeps drawing). Real CAMMaster
    // Designer exports draw nearly everything this way. -1 = no
    // operation seen yet, so a bare coordinate statement is ignored
    // (a file must give an explicit D before its first operation).
    int modalOperationCode = -1;

    // G74 single-quadrant arc mode: I/J center offsets are UNSIGNED and
    // the arc spans at most 90 degrees - the parser must resolve the
    // offsets' signs geometrically (see resolveSingleQuadrantCenterOffset).
    // false = G75 multi-quadrant (signed offsets, any sweep), which is
    // also the default here: modern files declare G75 explicitly, and
    // treating offsets as signed matches this parser's long-standing
    // behavior for files that declare neither.
    bool singleQuadrantArcs = false;

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
    QVector<RegionEdge> regionEdges; // one per regionPoints entry - see GraphicObject::regionEdges

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

// G74 single-quadrant arcs give I/J as unsigned distances, so there are
// four possible centers, one per sign combination. The right one is the
// center both end points are (nearly) the same distance from - the wrong
// ones are usually far off. The distance alone cannot choose when a wrong
// center mirrors the right one across the chord: both then fit, and their
// two arcs make up a full circle together (90 + 270 degrees, say).
//
// So: keep the candidates that turn at most HALF a turn in the commanded
// direction, and of those take the best distance fit. The cut is at 180
// degrees, not at G74's own 90, on purpose: real files round their end
// points, so a quarter-circle corner often comes out at 90.03 degrees - a
// 90-degree cut rejected every candidate there and drew a near-full loop.
// A right arc is 90 plus rounding, a mirrored wrong one 270 minus it, so
// 180 sits far from both.
//
// No candidate under half a turn means no valid arc (a malformed file):
// nullopt, and the caller draws the edge straight rather than a loop.
std::optional<QPointF> resolveSingleQuadrantCenterOffset(const QPointF &from, const QPointF &to,
                                                         const QPointF &offset, bool counterClockwise)
{
    const double absI = std::abs(offset.x());
    const double absJ = std::abs(offset.y());

    std::optional<QPointF> best;
    double bestMismatch = std::numeric_limits<double>::max();
    for (const double signX : {1.0, -1.0}) {
        for (const double signY : {1.0, -1.0}) {
            const QPointF candidateOffset(signX * absI, signY * absJ);
            const QPointF center = from + candidateOffset;
            const double radiusFrom = std::hypot(from.x() - center.x(), from.y() - center.y());
            const double radiusTo = std::hypot(to.x() - center.x(), to.y() - center.y());
            const double mismatch = std::abs(radiusFrom - radiusTo);

            const double startAngle = std::atan2(from.y() - center.y(), from.x() - center.x());
            const double endAngle = std::atan2(to.y() - center.y(), to.x() - center.x());
            // Sweep in the commanded direction, normalized to [0, 2pi).
            double sweep = counterClockwise ? (endAngle - startAngle) : (startAngle - endAngle);
            sweep = std::fmod(sweep + 4.0 * std::numbers::pi, 2.0 * std::numbers::pi);

            if (sweep > std::numbers::pi) {
                continue; // the long way round - see above
            }
            if (mismatch < bestMismatch) {
                bestMismatch = mismatch;
                best = candidateOffset;
            }
        }
    }
    return best;
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
            // Only arc edges have a center; a straight edge's stays (0,0).
            for (RegionEdge &edge : object.regionEdges) {
                if (edge.isArc) {
                    edge.arcCenter.x += offX;
                    edge.arcCenter.y += offY;
                }
            }
            state.objects.append(object);
        }
    }
}

// True for a statement that can belong to a macro body. A body holds only
// primitives (they start with a numeric code, e.g. "21,1,...") and variable
// assignments (they start with '$', e.g. "$3=$1x2") - Gerber spec 4.5.
// Every other parameter statement (FS, MO, AD, LP, AM, and attributes such as
// TA/TO/TD/TF) starts with a letter, so it ends the body. Testing for what a
// body line looks like, rather than listing what ends it, means a directive
// we have never heard of still ends the body.
bool isMacroBodyStatement(const GerberStatement &statement)
{
    if (!statement.isParameterStatement || statement.text.isEmpty()) {
        return false;
    }
    const QChar first = statement.text.at(0);
    return first.isDigit() || first == QLatin1Char('$');
}

// Writes a one-digit G-code with its leading zero: "G1X5D01" becomes
// "G01X5D01". Anything else comes back unchanged. The parse loop and
// nonGeometryStatements both call this, so the two always agree on what
// "G1" and "G01" mean (the same statement).
QString normalizedGCode(QString text)
{
    if (text.length() >= 2 && text.at(0) == QLatin1Char('G') && text.at(1).isDigit()
        && (text.length() == 2 || !text.at(2).isDigit())) {
        text.insert(1, QLatin1Char('0'));
    }
    return text;
}

// Parses "%AMname*primitiveStatement*primitiveStatement*...*%" - tokenize
// has already split this into one GerberStatement per '*'-terminated
// piece, all flagged isParameterStatement, so this just needs to collect
// them from the "AMname" header statement until the first statement that
// is not a primitive or assignment.
void parseMacroDefinition(const QVector<GerberStatement> &statements, int &index, ParserState &state)
{
    const QString header = statements.at(index).text; // "AMname"
    const QString name = header.mid(2);
    QStringList body;
    ++index;
    while (index < statements.size() && isMacroBodyStatement(statements.at(index))) {
        body.append(statements.at(index).text);
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
        // Bounded by the next field letter (Y, or the I of an arc with no
        // Y). Running to the end of the token made "X..I..J.." unreadable,
        // and an unreadable number decodes as 0 - a region corner then
        // jumped to X=0 and twisted the outline.
        const int end = nextUppercaseIndex(token, xIndex + 1);
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

// The absolute center of the arc a G02/G03 draw makes from `from` to the
// pen's new position. The file gives the center as I/J offsets from the
// arc's *start* point, not from the origin or the endpoint - see the Gerber
// spec. Each field is bounded the same way applyCoordinateToken bounds X/Y,
// so I's digits stop at J rather than running to the end of the string.
// Used for arc strokes and for arc edges of a region alike.
//
// nullopt means the draw is not an arc after all and is drawn straight: a
// G74 arc with equal end points (only G75 makes that a full circle; a G74
// arc spans at most a quarter turn), or one no center fits.
std::optional<QPointF> arcCenter(const ParserState &state, const QString &coordinatePart, const QPointF &from,
                                 bool counterClockwise)
{
    if (state.singleQuadrantArcs && from == state.currentPoint) {
        return std::nullopt;
    }
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
    if (state.singleQuadrantArcs) {
        const std::optional<QPointF> resolved =
            resolveSingleQuadrantCenterOffset(from, state.currentPoint, centerOffset, counterClockwise);
        if (!resolved) {
            return std::nullopt;
        }
        centerOffset = *resolved;
    }
    return from + centerOffset;
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

// Finishes the contour being collected inside G36..G37 - at G37, or at a
// D02, which per the Gerber spec (4.10) ends the current contour and starts
// a new one. Each contour is filled on its own and the region is the union
// of them, so each becomes its own Region GraphicObject carrying its
// vertices and edges (the flatten stage closes and fills the outline
// later, the same way a flash or stroke is resolved later).
//
// A contour needs 3 points to enclose an area - or fewer with an arc edge
// (a round contour is one start point and one full-circle arc). Anything
// less, such as a D02 that no D01 follows, encloses nothing: no object.
void finishRegionContour(ParserState &state)
{
    const bool hasArc = std::any_of(state.regionEdges.begin(), state.regionEdges.end(),
                                    [](const RegionEdge &edge) { return edge.isArc; });
    if (state.regionPoints.size() >= 3 || (state.regionPoints.size() >= 2 && hasArc)) {
        GraphicObject object;
        object.type = ObjectType::Region;
        object.polarity = state.currentPolarity;
        object.regionPoints.reserve(state.regionPoints.size());
        for (const QPointF &vertex : state.regionPoints) {
            object.regionPoints.append(toNm(vertex));
        }
        object.regionEdges = state.regionEdges;
        emitObject(state, object);
    }
    state.regionPoints.clear();
    state.regionEdges.clear();
}

// ---------------------------------------------------------------------------
// Reconstruction: resolve the object model back to the legacy flattened path
// ---------------------------------------------------------------------------
//
// Per-object geometry resolution (the old resolve*Path family) now lives in
// GerberObjectPaths::objectPathMm - shared with the v2 mask renderer so both
// consumers agree on what each object looks like. Only the polarity-grouped
// boolean compositing below is still legacy-specific.

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
QPainterPath flattenAndCompositeForLegacyRender(const Layer &layer)
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
        groups.last().path.addPath(GerberObjectPaths::objectPathMm(object, layer));
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
            state.regionEdges.clear();
            continue;
        }
        if (text.startsWith(QLatin1String("G37"))) {
            // Closes the region statement and its last contour.
            state.insideRegion = false;
            finishRegionContour(state);
            continue;
        }
        // RS-274X function codes may omit leading zeros: "G1" means G01,
        // just as "D2" means D02 (the D side is already parsed
        // numerically below; only the G side is matched by literal text).
        // Real CAMMaster Designer exports write every move as
        // "G1X...D2*" - without this normalization each of those
        // statements missed every "G01"-literal branch, fell into the
        // generic G handling, and was dropped whole, rendering
        // board-frame layers as disconnected dots from stale pen
        // positions. Normalize the one-digit spelling to two digits once,
        // here, so every startsWith("G0x") below just works.
        text = normalizedGCode(text);
        if (text.startsWith(QLatin1String("G74")) || text.startsWith(QLatin1String("G75"))) {
            // Arc quadrant mode. Both are usually standalone statements;
            // strip the code and keep processing anything that follows on
            // the same statement (some writers chain "G75G03X...").
            state.singleQuadrantArcs = (text.at(2) == QLatin1Char('4'));
            text = text.mid(3);
            if (text.isEmpty()) {
                continue;
            }
        }
        if (text.startsWith(QLatin1String("G54")) || text.startsWith(QLatin1String("G55"))) {
            // RS-274D legacy prefixes still emitted by real CAM tools
            // (confirmed in a real supplier file, TARGET 3001! output):
            // G54 = "select aperture", G55 = "prepare to flash". Neither
            // changes any state of its own - the D-code that follows on
            // the same statement carries the actual meaning - so strip
            // the prefix and let the normal D-code handling below see the
            // rest. Without this, "G54D41*" fell into the generic
            // G-statement branch and was discarded whole, leaving every
            // following flash on a zero-size default aperture.
            text = text.mid(3);
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
            state.modalOperationCode = code;
            const QPointF &samePoint = state.currentPoint;
            if (code == 1) {
                emitLinearStroke(state, samePoint, samePoint);
            } else if (code == 3) {
                emitFlash(state);
            }
            continue;
        }

        // Anything left is a coordinate command: X..Y..D0n*, or bare
        // coordinates with no D-code, which repeat the modal operation
        // (see ParserState::modalOperationCode).
        const int dIndex = text.lastIndexOf(QLatin1Char('D'));
        QString coordinatePart;
        int opCode = -1;
        if (dIndex >= 0) {
            coordinatePart = text.left(dIndex);
            opCode = text.mid(dIndex + 1).toInt();
            state.modalOperationCode = opCode;
        } else {
            // Only actual coordinate data may repeat the modal operation -
            // anything else that drifted this far (an M02 end-of-file, an
            // unhandled code) must stay a no-op exactly as before.
            const bool hasCoordinateData = text.contains(QLatin1Char('X')) || text.contains(QLatin1Char('Y'))
                || text.contains(QLatin1Char('I')) || text.contains(QLatin1Char('J'));
            if (!hasCoordinateData || state.modalOperationCode < 0) {
                continue;
            }
            coordinatePart = text;
            opCode = state.modalOperationCode;
        }
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
            //
            // A later D02 finishes the contour so far and starts the next one
            // at its own destination, the same way. Joining the next contour
            // onto the last one instead (what the parser once did) drew an
            // arc that follows the move from the wrong start point - with
            // the wrong radius, a loop across the board.
            if (opCode == 2 && !state.regionPoints.isEmpty()) {
                finishRegionContour(state);
            }
            if (state.regionPoints.isEmpty()) {
                state.regionPoints << state.currentPoint;
                state.regionEdges << RegionEdge{}; // the start has no edge
            }
            if (opCode == 1) {
                // In G02/G03 mode the edge to this vertex is an arc, read
                // exactly as an arc draw outside a region is.
                RegionEdge edge;
                if (state.interpolationMode != ParserState::InterpolationMode::Linear) {
                    edge.arcCounterClockwise =
                        state.interpolationMode == ParserState::InterpolationMode::CounterClockwiseArc;
                    const std::optional<QPointF> center =
                        arcCenter(state, coordinatePart, previousPoint, edge.arcCounterClockwise);
                    // No center: the edge is straight, and a straight edge
                    // keeps its center at (0,0).
                    edge.isArc = center.has_value();
                    if (center) {
                        edge.arcCenter = toNm(*center);
                    } else {
                        edge.arcCounterClockwise = false;
                    }
                }
                state.regionPoints << state.currentPoint;
                state.regionEdges << edge;
            }
        } else if (opCode == 1 && state.interpolationMode != ParserState::InterpolationMode::Linear) {
            // An arc draw: center from its I/J offsets (see arcCenter).
            const bool counterClockwise =
                state.interpolationMode == ParserState::InterpolationMode::CounterClockwiseArc;
            const std::optional<QPointF> center = arcCenter(state, coordinatePart, previousPoint, counterClockwise);
            if (center) {
                emitArcStroke(state, previousPoint, state.currentPoint, *center, counterClockwise);
            } else {
                emitLinearStroke(state, previousPoint, state.currentPoint);
            }
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

// ---------------------------------------------------------------------------
// Same-data check: which plain statements are fully captured as objects
// ---------------------------------------------------------------------------
//
// nonGeometryStatements may leave a plain statement out of its list only if
// the parse loop above records everything the statement means in the object
// model. The forms below mirror that loop branch by branch. Anything that
// does not fit one of them exactly is kept as text instead, which is always
// safe: a missing form only costs a pair that stays yellow, never a wrong
// green.

// What the walk remembers between statements, in file order - the parts of
// the parse loop's own state that decide how a statement is read.
struct GeometryWalk {
    // G01 = linear, G02/G03 = arc. The parse loop starts linear.
    bool arcInterpolation = false;
    // True once a D01/D02/D03 has been met whose code we know for certain.
    // Before that, bare coordinates ("X..Y..*" with no D) are ignored by the
    // parser; after a statement whose effect on the parser's modal
    // operation we cannot be sure of, we fall back to false as well.
    bool operationSeen = false;
    int modalOperation = 0; // the D01/D02/D03 bare coordinates repeat
    bool insideRegion = false; // between G36 and G37
    // G74 single-quadrant arcs (see the region rule in classifyPlain). The
    // parse loop starts in G75.
    bool singleQuadrantArcs = false;
    // Region outline bookkeeping (see the region rule in classifyPlain).
    bool regionStarted = false;     // the region's first operation (a D02) was met
    bool regionMovePending = false; // a later D02 came: the next D01 starts a new outline
    int regionVertexCount = 0;      // vertices the parser records for the current outline
    bool regionHasArcEdge = false;  // ... and whether one of its edges is surely an arc
    // True once a draw or flash (D01/D03) has been left out as geometry.
    bool drawLeftOut = false;
    // Set just before classifyPlain returns Refuse: which rule it was, for
    // the diagnostic line (SameDataRefusal). The verdict never depends on it.
    const char *refuseRule = nullptr;
};

// True when the region outline the walk is counting has edges but the
// parser draws nothing for it (finishRegionContour): it needs 3 vertices,
// or 2 with an arc edge - a round outline, one start and one full circle.
// Only a G75 arc counts here: a G74 arc may still be drawn straight, which
// depends on its geometry, and the walk does not judge that. An outline of
// just its start (a D02 with no D01 after it) has no edges and says nothing,
// so it is not counted as lost.
bool outlineDrawsNothing(const GeometryWalk &walk)
{
    return walk.regionVertexCount == 2 && !walk.regionHasArcEdge;
}

enum class PlainVerdict {
    Geometry,   // fully captured in the objects - leave it out of the list
    KeepAsText, // compare it as text
    Refuse,     // the file cannot be vouched for at all (std::nullopt)
};

// The fields of a coordinate statement, in the only order the parser
// decodes correctly: optional G01/G02/G03, then X, Y, I, J (each optional).
// Capture groups: 1 = G-code, 2 = X, 3 = Y, 4 = I, 5 = J. Each value is an
// optional sign and digits; the digit count is capped so the value always
// fits the parser's integer decode (a longer string fails to convert and
// silently reads as 0).
QString coordinateFieldsPattern()
{
    return QStringLiteral("(G0[123])?(X%1)?(Y%1)?(I%1)?(J%1)?").arg(QStringLiteral("[+-]?\\d{1,12}"));
}

// "aperture select": D10 and up, optionally after the G54 prefix. The parse
// loop strips G54 and stores the code as the current aperture, which every
// later object carries. (The cap keeps the code inside an int.)
const QRegularExpression kApertureSelectForm(QStringLiteral("^(G54)?D0*[1-9]\\d{1,7}$"));
// "operation": the fields, then D01/D02/D03 with any number of leading
// zeros (group 6). The parser takes the number after the last D.
const QRegularExpression kOperationForm(QLatin1Char('^') + coordinateFieldsPattern()
                                        + QStringLiteral("D0*([123])$"));
// "modal coordinates": the same fields without a D-code; the parser repeats
// its modal operation for them.
const QRegularExpression kModalCoordinatesForm(QLatin1Char('^') + coordinateFieldsPattern() + QLatin1Char('$'));

// Decides one plain statement (already G-code-normalized, G04 and G91
// handled by the caller) and updates the walk state the way the parse loop
// would update its own.
PlainVerdict classifyPlain(const QString &plain, GeometryWalk &walk)
{
    // Interpolation mode. The parse loop strips a leading G74/G75 and then a
    // G54/G55 prefix, and then takes G01/G02/G03 as the new mode - whether
    // the statement also carries coordinates or not. Do the same for every
    // statement, geometry or text, so the arc rule below sees the mode the
    // parser will use.
    QString view = plain;
    if (view.startsWith(QLatin1String("G74")) || view.startsWith(QLatin1String("G75"))) {
        walk.singleQuadrantArcs = view.at(2) == QLatin1Char('4');
        view = view.mid(3);
    }
    if (view.startsWith(QLatin1String("G54")) || view.startsWith(QLatin1String("G55"))) {
        view = view.mid(3);
    }
    if (view.startsWith(QLatin1String("G01"))) {
        walk.arcInterpolation = false;
    } else if (view.startsWith(QLatin1String("G02")) || view.startsWith(QLatin1String("G03"))) {
        walk.arcInterpolation = true;
    }

    // G36 / G37: kept as text (as before), but they open and close the
    // region the outline rule below watches.
    if (plain.startsWith(QLatin1String("G36"))) {
        // A second G36 inside an open region makes the parser throw the
        // open region's vertices away - that outline would be lost.
        if (walk.insideRegion && walk.regionStarted) {
            walk.refuseRule = "G36 inside an open region";
            return PlainVerdict::Refuse;
        }
        walk.insideRegion = true;
        walk.regionStarted = false;
        walk.regionMovePending = false;
        walk.regionVertexCount = 0;
        walk.regionHasArcEdge = false;
        return PlainVerdict::KeepAsText;
    }
    if (plain.startsWith(QLatin1String("G37"))) {
        if (walk.insideRegion && walk.regionStarted && outlineDrawsNothing(walk)) {
            walk.refuseRule = "region outline too small to be drawn";
            return PlainVerdict::Refuse;
        }
        walk.insideRegion = false;
        return PlainVerdict::KeepAsText;
    }

    if (kApertureSelectForm.match(plain).hasMatch()) {
        return PlainVerdict::Geometry; // does not touch the operation state
    }

    // Work out the statement's operation (D01/D02/D03), if we can know it.
    int operation = 0;
    QRegularExpressionMatch fields;
    const QRegularExpressionMatch explicitOp = kOperationForm.match(plain);
    if (explicitOp.hasMatch()) {
        operation = explicitOp.captured(6).toInt();
        fields = explicitOp;
    } else {
        const QRegularExpressionMatch modal = kModalCoordinatesForm.match(plain);
        const bool hasAnyField = modal.hasMatch()
            && (modal.hasCaptured(2) || modal.hasCaptured(3) || modal.hasCaptured(4) || modal.hasCaptured(5));
        // With a G prefix the parser looks only for X, Y or D before it
        // treats the rest as coordinates; "G02I5J5" is just a mode set, and
        // its offsets are not read.
        const bool parserReadsIt = hasAnyField && (!modal.hasCaptured(1) || modal.hasCaptured(2)
                                                   || modal.hasCaptured(3));
        // Bare coordinates repeat the modal operation - which we only know
        // after an operation we are sure of.
        if (parserReadsIt && walk.operationSeen) {
            operation = walk.modalOperation;
            fields = modal;
        }
    }

    if (operation == 0) {
        // Not a form we understand. If it still looks like a coordinate or
        // D-code command (X..D10, D05, a misordered field, ...), the parser
        // may have read it and changed its modal operation in a way we
        // cannot follow - so stop trusting bare coordinates until the next
        // operation we are sure of.
        const bool looksLikeCommand = view.contains(QLatin1Char('X')) || view.contains(QLatin1Char('Y'))
            || view.contains(QLatin1Char('I')) || view.contains(QLatin1Char('J'))
            || view.contains(QLatin1Char('D'));
        if (looksLikeCommand) {
            walk.operationSeen = false;
            // Inside a region it could be a hidden move that starts a
            // second outline, which the outline rule must see.
            if (walk.insideRegion) {
                walk.refuseRule = "unreadable coordinate statement inside a region";
                return PlainVerdict::Refuse;
            }
        }
        return PlainVerdict::KeepAsText;
    }

    // From here the operation is known, whether or not the statement ends
    // up as geometry, so the parser's modal operation is known too.
    if (explicitOp.hasMatch()) {
        walk.modalOperation = operation;
        walk.operationSeen = true;
    }

    const bool hasX = fields.hasCaptured(2);
    const bool hasY = fields.hasCaptured(3);
    const bool hasOffset = fields.hasCaptured(4) || fields.hasCaptured(5);
    const bool isBare = !hasX && !hasY && !hasOffset; // "D01*" with no fields

    // The region outline rule. Inside G36..G37 the parser records the
    // destination of the region's first operation as the start vertex and
    // then one vertex per D01. A later D02 finishes that outline and starts
    // the next at its own destination (Gerber spec 4.10), and each outline
    // becomes a region object of its own:
    //     G36 -> D02 (start) -> D01 x N -> [D02 -> D01 x M ...] -> G37
    // Every D02 and D01 is then left out as geometry, so each outline must
    // reach an object: one the parser draws nothing for (see
    // outlineDrawsNothing) refuses, checked when the next outline starts or
    // at G37. A D02 that no D01 follows - Altium ends every region with one
    // - starts an outline with no edges, which draws nothing and says
    // nothing, so it is harmless. The first operation must be a D02 with
    // coordinates: a D01 there starts from the pen position before G36,
    // which the parser does not record, and a bare "D02*" does not reach
    // the region code at all. A D03 (flash) is not allowed in a region and
    // the parser ignores it.
    if (walk.insideRegion) {
        if (operation == 3) {
            walk.refuseRule = "flash (D03) inside a region";
            return PlainVerdict::Refuse;
        }
        if (!walk.regionStarted) {
            if (operation != 2 || isBare) {
                walk.refuseRule = "region does not start with a D02 move";
                return PlainVerdict::Refuse;
            }
            walk.regionStarted = true;
            walk.regionVertexCount = 1;
        } else if (isBare) {
            // A bare D01/D02 does not reach the region code: no vertex, no
            // new outline.
        } else if (operation == 2) {
            walk.regionMovePending = true;
        } else { // D01
            if (walk.regionMovePending) {
                // The outline so far is finished: it must reach an object.
                if (outlineDrawsNothing(walk)) {
                    walk.refuseRule = "region outline too small to be drawn";
                    return PlainVerdict::Refuse;
                }
                walk.regionMovePending = false;
                walk.regionVertexCount = 1; // the D02's destination
                walk.regionHasArcEdge = false;
            }
            ++walk.regionVertexCount;
            if (walk.arcInterpolation && !walk.singleQuadrantArcs) {
                walk.regionHasArcEdge = true;
            }
        }
    }

    // Only now decide geometry or text: everything the statement says must
    // reach an object.
    // - X with offsets but no Y ("X5I3J4"): before 1.8.8 the parser lost X
    //   here (read as 0). It reads it now, but the form stays text: that
    //   costs at most a false yellow, and leaving it out is a separate decision.
    if (hasX && !hasY && hasOffset) {
        return PlainVerdict::KeepAsText;
    }
    // - I/J are read only for an arc draw (D01 in G02/G03 mode). Inside a
    //   region the parser now records them too (as arc edges), but they stay
    //   text there: leaving them out is a separate decision.
    if (hasOffset && (operation != 1 || !walk.arcInterpolation || walk.insideRegion)) {
        return PlainVerdict::KeepAsText;
    }
    if (operation != 2) {
        walk.drawLeftOut = true;
    }
    return PlainVerdict::Geometry;
}

// True for an old image-transform directive (deprecated in RS-274X, written
// by older CAD tools) whose values do nothing: %OF offset 0, %SF scale 1,
// %AS normal axes, %MI no mirror, %IR rotation 0. The parser applies none of
// them, so only these values are safe to leave out. Judged by number, not
// text, so "A0.000" and "A0" are the same; a missing field is the default.
// `text` is the directive without '%' and '*'.
bool isDoNothingTransform(const QString &text)
{
    static const QString num = QStringLiteral("([+-]?(?:\\d+\\.?\\d*|\\.\\d+))");
    static const QRegularExpression offset(QStringLiteral("^OF(?:A%1)?(?:B%1)?$").arg(num));
    static const QRegularExpression scale(QStringLiteral("^SF(?:A%1)?(?:B%1)?$").arg(num));
    static const QRegularExpression mirror(QStringLiteral("^MI(?:A([01]))?(?:B([01]))?$"));
    static const QRegularExpression rotation(QStringLiteral("^IR(\\d+)$"));
    // Each absent field (an empty capture) counts as the default value.
    const auto allFieldsAre = [](const QRegularExpressionMatch &match, double value) {
        for (int field = 1; field <= 2; ++field) {
            if (match.hasCaptured(field) && match.captured(field).toDouble() != value) {
                return false;
            }
        }
        return true;
    };
    if (text == QLatin1String("ASAXBY")) {
        return true;
    }
    if (const QRegularExpressionMatch match = offset.match(text); match.hasMatch()) {
        return allFieldsAre(match, 0.0);
    }
    if (const QRegularExpressionMatch match = scale.match(text); match.hasMatch()) {
        return allFieldsAre(match, 1.0);
    }
    if (const QRegularExpressionMatch match = mirror.match(text); match.hasMatch()) {
        return allFieldsAre(match, 0.0);
    }
    if (const QRegularExpressionMatch match = rotation.match(text); match.hasMatch()) {
        return match.captured(1).toInt() == 0;
    }
    return false;
}

} // namespace

Layer GerberParser::parseContentToObjects(const QString &content)
{
    double unitScale = 1.0; // discarded here - only the flatten stage needs it
    return parseContentToObjectsInternal(content, unitScale);
}

QPainterPath GerberParser::parseContent(const QString &content)
{
    double unitScale = 1.0; // discarded - objectPathMm reads Layer::unitScale, set by the parse
    const Layer layer = parseContentToObjectsInternal(content, unitScale);
    return flattenAndCompositeForLegacyRender(layer);
}

std::optional<QStringList> GerberParser::nonGeometryStatements(const QString &content, SameDataRefusal *refusal,
                                                               bool *drawLeftOut)
{
    // Every "no answer" goes through here, so the diagnostic can say which
    // rule it was and where. `position` is the statement being looked at.
    int position = 0;
    const auto refuse = [&](const char *rule, const QString &statementText) -> std::optional<QStringList> {
        if (refusal) {
            refusal->rule = QString::fromLatin1(rule);
            refusal->statement = statementText;
            refusal->position = position;
        }
        return std::nullopt;
    };

    // A file whose last statement is cut off (an unterminated '%...' block,
    // or a final command with no '*') loses that tail in the parser without
    // a trace, so the tail is compared nowhere - refuse.
    bool truncated = false;
    const QVector<GerberStatement> statements = tokenize(content, &truncated);
    if (truncated) {
        return refuse("file cut off mid-statement", QString());
    }

    QStringList kept;
    QSet<QString> macroNames; // macro names already defined in this file
    QSet<int> apertureCodes;  // aperture D-codes already defined in this file
    bool inMacro = false; // true while the statements after a %AM header are its body
    GeometryWalk walk;    // parser state the plain-statement forms depend on

    // Whether this file says it is a solder mask (its own %TF.FileFunction),
    // read up front because the polarity label may come before it.
    const bool isSolderMask = std::any_of(statements.begin(), statements.end(), [](const GerberStatement &s) {
        return s.isParameterStatement && ExportNoise::isSolderMaskFileFunction(s.text);
    });

    // Unit switches (see the unit rule below). The parser applies the
    // file's LAST unit to every macro aperture, whenever it was defined.
    bool apertureOrGeometrySeen = false;
    std::optional<bool> declaredInches; // the unit the file declared last, if any
    // Returns false when this unit directive switches away from a unit the
    // file already declared, after an aperture or geometry has been read.
    // Such a file means different sizes before and after the switch, but
    // the parser scales macro apertures with the final unit only - and
    // where the switch sits relative to the apertures is not compared
    // (%AD and geometry are not in the text list). The first declaration
    // is allowed anywhere: the parser treats it as the unit of the whole
    // file for macros, and a standard aperture read before it is compared
    // through its scaled size anyway. Repeating the same unit is harmless.
    const auto acceptUnit = [&](bool inches) {
        if (apertureOrGeometrySeen && declaredInches.has_value() && *declaredInches != inches) {
            return false;
        }
        declaredInches = inches;
        return true;
    };

    for (const GerberStatement &statement : statements) {
        ++position;
        const QString &text = statement.text;
        // A macro body runs from the %AM header to the first statement of
        // any kind that does not look like a body line (same test the parser
        // uses) - a plain statement such as G01 ends it too.
        if (inMacro && !isMacroBodyStatement(statement)) {
            inMacro = false;
        }
        if (statement.isParameterStatement) {
            // An empty statement: a '%' block whose closing '%' sits on its
            // own line after the last '*' (common for %AM). The parser
            // ignores it - it only ends a macro body, which the reset just
            // above has done too - so it says nothing and is skipped.
            if (text.isEmpty()) {
                continue;
            }

            // Group 1 - SKIP: directives that cannot change the image, or
            // that are compared some other way.
            // %AD is compared through the objects (by aperture shape, not by
            // its D-number).
            if (text.startsWith(QLatin1String("AD"))) {
                // The parser keeps only the last definition of a D-code and
                // the objects store just the code, so a redefinition hides
                // which size an earlier flash or draw was made with - refuse
                // (same reason as the macro-name rule below). The code is
                // read exactly as parseApertureDefinition reads it: the
                // digits after "ADD", so "ADD010" and "ADD10" are one code.
                int codeEnd = 3;
                while (codeEnd < text.length() && text.at(codeEnd).isDigit()) {
                    ++codeEnd;
                }
                const int code = text.mid(3, codeEnd - 3).toInt();
                if (apertureCodes.contains(code)) {
                    return refuse("aperture D-code defined twice", QLatin1Char('%') + text);
                }
                apertureCodes.insert(code);
                apertureOrGeometrySeen = true;
                continue;
            }
            // X2 attributes are skipped only if they are export noise.
            if (ExportNoise::isVolatileAttribute(text)) {
                continue;
            }
            // %TF.FilePolarity,Positive is the Gerber default, so stating it
            // and leaving it out mean the same image. Only that exact value
            // is skipped here; ",Negative" is judged just below.
            if (ExportNoise::isDefaultFilePolarity(text)) {
                continue;
            }
            // ",Negative" is skipped only in a solder mask, where it restates
            // the convention (see ExportNoise::isNegativeFilePolarity).
            if (isSolderMask && ExportNoise::isNegativeFilePolarity(text)) {
                continue;
            }
            // %TO (net/component/pin labels on the objects that follow) and
            // %TD (clears them) never change the image - re-annotating R12 to
            // R15 or renaming a net moves no copper.
            if (text.startsWith(QLatin1String("TO")) || text.startsWith(QLatin1String("TD"))) {
                continue;
            }
            // %LN (layer name) and %IN (image name) are labels that differ
            // per export; they draw nothing. Exactly these two letters - not
            // %LM, %LR, %LS (which do change geometry) or %IR, %IP.
            if (text.startsWith(QLatin1String("LN")) || text.startsWith(QLatin1String("IN"))) {
                continue;
            }
            // %IPPOS (image polarity positive) is the default, so it says
            // nothing. Exact match: %IPNEG changes the image and falls
            // through to the refuse group.
            if (text == QLatin1String("IPPOS")) {
                continue;
            }
            // %ICAS (input code ASCII, an old header) states what every file
            // the parser can read already is. Exact match: any other input
            // code falls through to the refuse group.
            if (text == QLatin1String("ICAS")) {
                continue;
            }
            // %OF %SF %AS %MI %IR with values that do nothing (see
            // isDoNothingTransform). Any other value falls through to the
            // refuse group: the parser would not apply it.
            if (isDoNothingTransform(text)) {
                continue;
            }

            // Macro definitions (the %AM header and its body lines) are
            // skipped too. Altium names a macro after its D-code, so a
            // re-export that shifts aperture numbers renames and reorders
            // the definitions without changing the image. Each macro that is
            // actually used is compared by its recipe through the aperture
            // key (layerequivalence.cpp, apertureKey); an unused macro draws
            // nothing. inMacro is still tracked so a stray primitive outside
            // a macro falls through to the refuse group below.
            if (text.startsWith(QLatin1String("AM"))) {
                // The parser keeps only the last definition of a name, so a
                // redefinition hides which recipe an earlier aperture was
                // declared with - refuse.
                const QString name = text.mid(2);
                if (macroNames.contains(name)) {
                    return refuse("macro name defined twice", QLatin1Char('%') + text);
                }
                macroNames.insert(name);
                inMacro = true;
                continue;
            }
            if (inMacro) {
                continue;
            }

            // Group 2 - KEEP: directives the parser understands (or labels
            // that describe the file); they must be equal in both files.
            if (text.startsWith(QLatin1String("FS"))) {
                // "FSLA..." / "FSTA...": the character after the
                // zero-suppression letter is the notation, A = absolute.
                // The parser reads only zero suppression and digit counts,
                // so incremental (I) would be decoded as absolute - the
                // same hazard as G91, so refuse anything but A.
                if (text.length() < 4 || text.at(3) != QLatin1Char('A')) {
                    return refuse("%FS not in absolute notation", QLatin1Char('%') + text);
                }
                kept << QLatin1Char('%') + text;
                continue;
            }
            // Only the unit and polarity codes the parser knows; an unknown
            // code under these prefixes is not understood, so it falls
            // through to the refuse group.
            if (text == QLatin1String("MOIN") || text == QLatin1String("MOMM")) {
                if (!acceptUnit(text == QLatin1String("MOIN"))) {
                    return refuse("unit switched after apertures or geometry", QLatin1Char('%') + text);
                }
                kept << QLatin1Char('%') + text;
                continue;
            }
            if (text == QLatin1String("LPD") || text == QLatin1String("LPC")) {
                kept << QLatin1Char('%') + text;
                continue;
            }
            // %SR is kept only in the two forms parseStepRepeatDirective
            // reads correctly: a bare "SR" (end of the block), or
            // X<count>Y<count>I<step>J<step> in exactly that order. It reads
            // the counts with toInt, so a misordered or missing field, a 0,
            // or a count too long for an int gives a count of 0 - and then
            // every object inside the block silently vanishes from both
            // files, so a change there could never show. Anything else refuses.
            if (text.startsWith(QLatin1String("SR"))) {
                static const QRegularExpression stepRepeatForm(
                    QStringLiteral("^SRX[1-9]\\d{0,8}Y[1-9]\\d{0,8}I%1J%1$")
                        .arg(QStringLiteral("[+-]?(?:\\d+\\.?\\d*|\\.\\d+)")));
                if (text != QLatin1String("SR") && !stepRepeatForm.match(text).hasMatch()) {
                    return refuse("%SR in a form the parser misreads", QLatin1Char('%') + text);
                }
                kept << QLatin1Char('%') + text;
                continue;
            }
            if (text.startsWith(QLatin1String("TF")) || text.startsWith(QLatin1String("TA"))) {
                kept << QLatin1Char('%') + text;
                continue;
            }

            // Group 3 - REFUSE: everything else (%LR, %LM, %IPNEG, an
            // %OF/%SF/%AS/%MI/%IR that does something,
            // a primitive outside any %AM, directives we have never heard of).
            // The parser ignores or does not know them, and where they sit
            // relative to the objects is not compared, so equal text in both
            // files proves nothing. No answer is safer than a wrong "same".
            return refuse("% directive the parser does not read", QLatin1Char('%') + text);
        }
        // Same comment test as the parse loop above: only a literal "G04".
        // A G04 comment never affects the image (Gerber spec), so ALL of them
        // are skipped - not just known export noise. That includes the
        // "G04 #@! TF..." form: an attribute hidden in a comment is still a
        // comment. Every tool writes its own volatile comment lines, and a
        // noise list would have to chase each one.
        if (text.startsWith(QLatin1String("G04"))) {
            continue;
        }
        // Spell "G1" as "G01", exactly as the parse loop does, so the two
        // spellings of one statement compare equal.
        const QString plain = normalizedGCode(text);
        // G91 switches to incremental coordinates. The parser reads every
        // coordinate as absolute, so such a file is not understood. contains,
        // not startsWith: it can be chained ("G90G91*"). G04 comments are
        // already skipped above, so this can only over-refuse.
        if (plain.contains(QLatin1String("G91"))) {
            return refuse("G91 incremental coordinates", plain);
        }
        // G70 (inches) / G71 (mm): the old spelling of %MO, read by the
        // parser with the same startsWith test. Same unit rule as %MO; the
        // statement itself is still kept as text below.
        if (plain.startsWith(QLatin1String("G70")) || plain.startsWith(QLatin1String("G71"))) {
            if (!acceptUnit(plain.startsWith(QLatin1String("G70")))) {
                return refuse("unit switched after apertures or geometry", plain);
            }
        }
        // Geometry (a draw, move, flash or aperture select) is left out only
        // in the forms the parser fully reads into objects; everything else
        // (G01/G36/G75/G90/M02, odd coordinate forms ...) is kept in order.
        switch (classifyPlain(plain, walk)) {
        case PlainVerdict::Geometry:
            apertureOrGeometrySeen = true;
            break;
        case PlainVerdict::KeepAsText:
            kept << plain;
            break;
        case PlainVerdict::Refuse:
            return refuse(walk.refuseRule, plain);
        }
    }
    // A region never closed by G37 is never emitted by the parser, so its
    // outline would be lost.
    if (walk.insideRegion && walk.regionStarted) {
        return refuse("region never closed by G37", QString());
    }
    if (drawLeftOut) {
        *drawLeftOut = walk.drawLeftOut;
    }
    return kept;
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
