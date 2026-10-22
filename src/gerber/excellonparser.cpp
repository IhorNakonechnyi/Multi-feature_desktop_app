#include "excellonparser.h"

#include <algorithm>
#include <utility>

#include <QHash>
#include <QObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include "exportnoise.h"
#include "gerberaperture.h"
#include "gerberformat.h"

namespace {

using ExcellonParser::AmbiguousFormatError;

enum class RoutInterpolation { Linear, ClockwiseArc, CounterClockwiseArc };

struct ExcellonState {
    GerberFormat format;
    bool hasResolvableFormat = false;
    bool digitsExplicitlySet = false;
    QHash<int, GerberAperture> apertures; // tool number -> Circle aperture (diameter in mm)
    int currentTool = -1;
    PointNm currentPoint;
    bool routMode = false; // true after G00, until G05 returns to drill mode
    bool cutting = false;  // true between M15 (tool down) and M16 (tool up)
    RoutInterpolation routInterpolation = RoutInterpolation::Linear;
    QVector<GraphicObject> objects;
};

int nextFieldIndex(const QString &text, int start)
{
    for (int i = start; i < text.length(); ++i) {
        if (text.at(i).isUpper()) {
            return i;
        }
    }
    return text.length();
}

double resolveCoordinateMm(const QString &token, const ExcellonState &state)
{
    if (token.contains(QLatin1Char('.'))) {
        const double raw = token.toDouble();
        return state.format.unitsAreInches ? raw * 25.4 : raw;
    }
    if (!state.hasResolvableFormat) {
        throw AmbiguousFormatError(
            QObject::tr("Coordinate \"%1\" has no decimal point and no resolvable zero-suppression "
                        "format was found (no INCH/METRIC header line, no FILE_FORMAT comment).")
                .arg(token));
    }
    return state.format.decodeCoordinateMm(token);
}

// Decodes whichever of X/Y/I/J fields are present in `token` into `point`
// (X/Y) or `offset` (I/J) - only the fields actually present are touched.
// I/J are only meaningful for an arc move; drill/linear-move callers pass
// a throwaway offset they never read.
void applyFields(const QString &token, ExcellonState &state, PointNm &point, PointNm &offset)
{
    const int xIndex = token.indexOf(QLatin1Char('X'));
    const int yIndex = token.indexOf(QLatin1Char('Y'));
    const int iIndex = token.indexOf(QLatin1Char('I'));
    const int jIndex = token.indexOf(QLatin1Char('J'));
    if (xIndex >= 0) {
        const int end = nextFieldIndex(token, xIndex + 1);
        point.x = GerberFormat::mmToNm(resolveCoordinateMm(token.mid(xIndex + 1, end - xIndex - 1), state));
    }
    if (yIndex >= 0) {
        const int end = nextFieldIndex(token, yIndex + 1);
        point.y = GerberFormat::mmToNm(resolveCoordinateMm(token.mid(yIndex + 1, end - yIndex - 1), state));
    }
    if (iIndex >= 0) {
        const int end = nextFieldIndex(token, iIndex + 1);
        offset.x = GerberFormat::mmToNm(resolveCoordinateMm(token.mid(iIndex + 1, end - iIndex - 1), state));
    }
    if (jIndex >= 0) {
        const int end = nextFieldIndex(token, jIndex + 1);
        offset.y = GerberFormat::mmToNm(resolveCoordinateMm(token.mid(jIndex + 1, end - jIndex - 1), state));
    }
}

struct ToolLine {
    bool isToolLine = false;
    int number = -1;
    QString diameterText;
};

ToolLine parseToolLine(const QString &line)
{
    if (!line.startsWith(QLatin1Char('T')) || line.length() < 2 || !line.at(1).isDigit()) {
        return {};
    }
    int numberEnd = 1;
    while (numberEnd < line.length() && line.at(numberEnd).isDigit()) {
        ++numberEnd;
    }
    ToolLine result;
    result.isToolLine = true;
    result.number = line.mid(1, numberEnd - 1).toInt();

    // A tool definition may carry optional modifiers before the diameter -
    // feed rate (F) and spindle speed (S) are the common ones, and real
    // files do write "T1F00S00C0.200". So walk the line's fields looking for
    // C rather than assuming it comes first: reading only the position right
    // after the tool number leaves such a tool with no diameter, and its
    // holes then have no aperture, so the layer draws nothing at all while
    // still reporting the right number of objects.
    int fieldStart = numberEnd;
    while (fieldStart < line.length()) {
        const int fieldEnd = nextFieldIndex(line, fieldStart + 1);
        if (line.at(fieldStart) == QLatin1Char('C')) {
            result.diameterText = line.mid(fieldStart + 1, fieldEnd - fieldStart - 1);
            break;
        }
        fieldStart = fieldEnd;
    }
    return result;
}

void emitFlash(ExcellonState &state)
{
    GraphicObject object;
    object.type = ObjectType::Flash;
    object.polarity = Polarity::Dark;
    object.apertureCode = state.currentTool;
    object.position = state.currentPoint;
    state.objects.append(object);
}

void applyCoordinateTokenAndFlash(const QString &token, ExcellonState &state)
{
    PointNm offset{0, 0}; // unused for a plain drill hit
    applyFields(token, state, state.currentPoint, offset);
    emitFlash(state);
}

void emitLinearStroke(ExcellonState &state, PointNm from, PointNm to)
{
    GraphicObject object;
    object.type = ObjectType::Stroke;
    object.polarity = Polarity::Dark;
    object.apertureCode = state.currentTool;
    object.position = from;
    object.strokeTo = to;
    object.isArc = false;
    state.objects.append(object);
}

void emitArcStroke(ExcellonState &state, PointNm from, PointNm to, PointNm center, bool counterClockwise)
{
    GraphicObject object;
    object.type = ObjectType::Stroke;
    object.polarity = Polarity::Dark;
    object.apertureCode = state.currentTool;
    object.position = from;
    object.strokeTo = to;
    object.isArc = true;
    object.arcCenter = center;
    object.arcCounterClockwise = counterClockwise;
    state.objects.append(object);
}

// Applies one rout-mode move (a G01/G02/G03 cut, or the modal reuse of the
// last one on a bare coordinate line) described by `afterGCode` (the G0n
// prefix, if any, already stripped by the caller). Only draws when
// state.cutting is true (i.e. inside an M15..M16 span) - per the design
// doc's correction, a move outside that span only repositions.
void applyRoutMove(const QString &afterGCode, ExcellonState &state)
{
    const PointNm from = state.currentPoint;
    PointNm to = from;
    PointNm offset{0, 0};
    applyFields(afterGCode, state, to, offset);

    if (state.cutting) {
        if (state.routInterpolation == RoutInterpolation::Linear) {
            emitLinearStroke(state, from, to);
        } else {
            const PointNm center{from.x + offset.x, from.y + offset.y};
            const bool counterClockwise = state.routInterpolation == RoutInterpolation::CounterClockwiseArc;
            emitArcStroke(state, from, to, center, counterClockwise);
        }
    }
    state.currentPoint = to;
}

bool tryParseFileFormatComment(const QString &line, ExcellonState &state)
{
    const QString marker = QStringLiteral("FILE_FORMAT=");
    const int markerIndex = line.indexOf(marker, 0, Qt::CaseInsensitive);
    if (markerIndex < 0) {
        return false;
    }
    const QString spec = line.mid(markerIndex + marker.length());
    const int colonIndex = spec.indexOf(QLatin1Char(':'));
    if (colonIndex < 0) {
        return false;
    }
    bool integerOk = false;
    bool decimalOk = false;
    const int integerDigits = spec.left(colonIndex).toInt(&integerOk);
    const int decimalDigits = spec.mid(colonIndex + 1).toInt(&decimalOk);
    if (!integerOk || !decimalOk) {
        return false;
    }
    state.format.integerDigits = integerDigits;
    state.format.decimalDigits = decimalDigits;
    state.digitsExplicitlySet = true;
    state.hasResolvableFormat = true;
    return true;
}

// "INCH,TZ" / "METRIC,LZ,000.000" / a bare "INCH" or "METRIC" - the
// header's unit declaration, optionally followed by a zero-suppression
// mode. Sets the public-spec default digit counts (2:4 for INCH, 3:3 for
// METRIC) unless a FILE_FORMAT comment already set them explicitly (see
// digitsExplicitlySet - checked regardless of which line came first in the
// file). LZ/TZ mapping: this parser treats the header's own 'L'/'T' letter
// the same way Gerber's %FS flag does (L = leading zero omitted),
// defaulting to L when neither is present - real-world Excellon tooling is
// not fully consistent on this exact point, so this is a documented
// best-effort default; the M5.4/M5.5 visual gate against real supplier
// files is the actual correctness check, not this comment.
bool tryParseUnitsLine(const QString &line, ExcellonState &state)
{
    const bool isInch = line.startsWith(QLatin1String("INCH"), Qt::CaseInsensitive);
    const bool isMetric = line.startsWith(QLatin1String("METRIC"), Qt::CaseInsensitive);
    if (!isInch && !isMetric) {
        return false;
    }
    state.format.unitsAreInches = isInch;
    if (!state.digitsExplicitlySet) {
        state.format.integerDigits = isInch ? 2 : 3;
        state.format.decimalDigits = isInch ? 4 : 3;
    }
    state.format.leadingZeroOmitted = !line.contains(QLatin1String("TZ"), Qt::CaseInsensitive);
    state.hasResolvableFormat = true;
    return true;
}

} // namespace

Layer ExcellonParser::parseContentToObjects(const QString &content)
{
    ExcellonState state;
    const QStringList lines = content.split(QLatin1Char('\n'));
    bool headerDone = false;

    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith(QLatin1Char(';'))) {
            tryParseFileFormatComment(line, state);
            continue;
        }
        if (line.compare(QLatin1String("M48"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        if (line == QLatin1String("%") || line.compare(QLatin1String("M95"), Qt::CaseInsensitive) == 0) {
            headerDone = true;
            continue;
        }
        if (line.compare(QLatin1String("M30"), Qt::CaseInsensitive) == 0
            || line.compare(QLatin1String("M00"), Qt::CaseInsensitive) == 0) {
            break;
        }
        if (line.compare(QLatin1String("M15"), Qt::CaseInsensitive) == 0) {
            state.cutting = true;
            continue;
        }
        if (line.compare(QLatin1String("M16"), Qt::CaseInsensitive) == 0) {
            state.cutting = false;
            continue;
        }
        if (line.compare(QLatin1String("G05"), Qt::CaseInsensitive) == 0) {
            state.routMode = false;
            state.cutting = false;
            continue;
        }
        if (!headerDone && tryParseUnitsLine(line, state)) {
            continue;
        }
        if (line.startsWith(QLatin1String("G00"), Qt::CaseInsensitive)) {
            state.routMode = true;
            state.cutting = false; // G00 is a rapid reposition only, never a cut
            PointNm offset{0, 0};  // G00 never carries I/J
            applyFields(line.mid(3), state, state.currentPoint, offset);
            continue;
        }
        if (line.startsWith(QLatin1String("G01"), Qt::CaseInsensitive)) {
            state.routInterpolation = RoutInterpolation::Linear;
            applyRoutMove(line.mid(3), state);
            continue;
        }
        if (line.startsWith(QLatin1String("G02"), Qt::CaseInsensitive)) {
            state.routInterpolation = RoutInterpolation::ClockwiseArc;
            applyRoutMove(line.mid(3), state);
            continue;
        }
        if (line.startsWith(QLatin1String("G03"), Qt::CaseInsensitive)) {
            state.routInterpolation = RoutInterpolation::CounterClockwiseArc;
            applyRoutMove(line.mid(3), state);
            continue;
        }

        const ToolLine tool = parseToolLine(line);
        if (tool.isToolLine && !tool.diameterText.isEmpty()) {
            const double diameterMm =
                state.format.unitsAreInches ? tool.diameterText.toDouble() * 25.4 : tool.diameterText.toDouble();
            GerberAperture aperture;
            aperture.shape = GerberAperture::Shape::Circle;
            aperture.modifiers = {diameterMm};
            state.apertures.insert(tool.number, aperture);
            continue;
        }
        if (tool.isToolLine) {
            state.currentTool = tool.number;
            continue;
        }

        if (line.contains(QLatin1Char('X')) || line.contains(QLatin1Char('Y'))) {
            if (state.routMode) {
                // Modal reuse of the last-set G01/G02/G03 interpolation
                // mode - real files routinely give one G-code then several
                // bare coordinate lines for a multi-segment slot.
                applyRoutMove(line, state);
            } else {
                applyCoordinateTokenAndFlash(line, state);
            }
        }
    }

    Layer layer;
    layer.apertures = state.apertures;
    layer.objects = std::move(state.objects);
    layer.unitScale = state.format.unitsAreInches ? 25.4 : 1.0;
    return layer;
}

std::optional<QStringList> ExcellonParser::nonGeometryLines(const QString &content)
{
    QStringList kept;
    // The header ends at '%' or M95, exactly where the parser stops reading
    // unit lines (its headerDone flag).
    bool headerDone = false;
    // The unit the parser is using at this point: inch until a header
    // INCH/METRIC line says otherwise (its default, GerberFormat).
    bool unitIsInch = true;
    QSet<int> definedTools; // tool numbers that already have a diameter
    // Case-insensitive whole-line test, the way the parser compares its
    // M/G command lines.
    const auto lineIs = [](const QString &line, const char *command) {
        return line.compare(QLatin1String(command), Qt::CaseInsensitive) == 0;
    };
    for (const QString &rawLine : content.split(QLatin1Char('\n'))) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith(QLatin1Char(';'))) {
            if (!ExportNoise::isVolatileComment(line.mid(1))) {
                kept << line;
            }
            continue;
        }
        // Tool lines are checked before the X/Y test on purpose. Only a BARE
        // select ("T03") is left out; anything else starting with T - a
        // definition "T03C0.800", or "T03X..Y.." which the parser reads as a
        // plain select and so draws nothing for - is kept, so a difference
        // the object model cannot see still shows up here.
        const ToolLine tool = parseToolLine(line);
        if (tool.isToolLine) {
            // A definition is a tool line with a diameter (the same test the
            // parser uses). The parser keeps only the LAST diameter of a
            // tool number, and the holes store just the number, so a second
            // definition would resize holes drilled before it - and where
            // it sits among the hole lines is not compared. Refuse.
            if (!tool.diameterText.isEmpty()) {
                if (definedTools.contains(tool.number)) {
                    return std::nullopt;
                }
                definedTools.insert(tool.number);
            }
            const bool bareSelect = std::all_of(line.begin() + 1, line.end(), [](QChar c) { return c.isDigit(); });
            if (!bareSelect) {
                kept << line;
            }
            continue;
        }
        // "R05X0.5" repeats the previous hole 5 times, shifted by X0.5. The
        // parser does not read repeats at all, so nothing in the object model
        // reflects them and no comparison of the text can vouch for them:
        // a file using R is never "same data".
        static const QRegularExpression repeatLine(QStringLiteral("^R\\d"));
        if (repeatLine.match(line).hasMatch()) {
            return std::nullopt;
        }
        // A line is left out as geometry only when the WHOLE line is in a
        // form the parser really reads. Anything else falls through to the
        // allow-list below: a G85 slot or a G00-G03 line the forms here did
        // not take (an I/J offset on a straight G01) is kept and compared as
        // text; every other line refuses unless the allow-list names it.
        // I/J are only read on arcs (G02/G03).
        // A number must be well formed: optional sign, then digits with an
        // optional point, or a point followed by digits. A loose pattern would
        // accept "1.0.5", which toDouble reads as 0, so two different
        // malformed values would look identical in the object model. A
        // malformed number fails the match, is not on the allow-list, and so
        // makes the whole file refuse.
        // The digit counts are capped too, so two different numbers can never
        // decode to the same hole: without a point, at most 12 digits (the
        // same cap as the Gerber side - it always fits the integer decode);
        // with a point, at most 6 digits on each side, so the value stays
        // exact in a double and a change in the last digit (1 nm in mm) still
        // moves the hole after rounding to nanometres. A longer number fails
        // the match and makes the file refuse.
        static const QString num = QStringLiteral("[+-]?(?:\\d{1,12}|\\d{1,6}\\.\\d{0,6}|\\.\\d{1,6})");
        static const QRegularExpression holeOrModalMove(
            QStringLiteral("^(X%1)?(Y%1)?$").arg(num));
        static const QRegularExpression linearOrRoutMove(
            QStringLiteral("^G0[0-3](X%1)?(Y%1)?$").arg(num));
        static const QRegularExpression arcMove(
            QStringLiteral("^G0[23](X%1)?(Y%1)?(I%1)?(J%1)?$").arg(num));
        // The first form also matches an empty string, but empty lines were
        // skipped above, so a match here always has at least one of X/Y.
        if (holeOrModalMove.match(line).hasMatch() || linearOrRoutMove.match(line).hasMatch()
            || arcMove.match(line).hasMatch()) {
            continue; // a hole or a rout move - lives in the object model
        }

        // Everything else is kept as text ONLY if it is a line the parser
        // handles, or one that cannot move or resize a hole. Kept text loses
        // its place among the hole lines (those are left out above), so a
        // command that changes how LATER holes are read - G91 incremental,
        // a unit switch mid-program, pattern or mirror commands - would look
        // the same in both files wherever it sat. The parser ignores those,
        // so nothing can vouch for them: anything not listed here refuses.
        if (lineIs(line, "M30") || lineIs(line, "M00")) {
            kept << line;
            break; // the parser stops here too; whatever follows is never drawn
        }
        if (lineIs(line, "%") || lineIs(line, "M95")) {
            headerDone = true; // the parser's header end
            kept << line;
            continue;
        }
        // M48 (header start) and G90 (absolute, the default) change nothing;
        // M15/M16 (tool down/up) and G05 (back to drilling) are read by the
        // parser, and what they change shows up in the objects as strokes
        // versus holes.
        if (lineIs(line, "M48") || lineIs(line, "G90") || lineIs(line, "M15") || lineIs(line, "M16")
            || lineIs(line, "G05")) {
            kept << line;
            continue;
        }
        // A G00-G03 line the forms above did not take (an I/J on a straight
        // move, ...): the parser reads it as a rout move, so its effect is in
        // the objects, and the text catches what the objects cannot see.
        static const QRegularExpression routCommand(QStringLiteral("^G0[0-3]"),
                                                    QRegularExpression::CaseInsensitiveOption);
        if (routCommand.match(line).hasMatch()) {
            kept << line;
            continue;
        }
        // INCH / METRIC: the parser reads the unit only in the header. After
        // it, the line is ignored, but the holes after it are meant in the
        // new unit - refuse.
        if (line.startsWith(QLatin1String("INCH"), Qt::CaseInsensitive)
            || line.startsWith(QLatin1String("METRIC"), Qt::CaseInsensitive)) {
            if (headerDone) {
                return std::nullopt;
            }
            unitIsInch = line.startsWith(QLatin1String("INCH"), Qt::CaseInsensitive);
            kept << line;
            continue;
        }
        // M71 (metric) / M72 (inch): the parser ignores both. Eagle writes M72
        // at the top of its drill files with no INCH line, and older KiCad
        // writes M71/M72 after the '%'. Such a command changes nothing only
        // when it names the unit the parser is already using at that point;
        // one that names the other unit means the holes are in a unit the
        // parser never switched to, so the pair refuses.
        const bool isM71 = lineIs(line, "M71");
        const bool isM72 = lineIs(line, "M72");
        if (isM71 || isM72) {
            if (isM72 != unitIsInch) {
                return std::nullopt;
            }
            kept << line;
            continue;
        }
        // A G85 slot: the parser reads only its start point as a hole, so
        // the whole line is compared as text. FMAT (format version) draws
        // nothing.
        if (line.contains(QLatin1String("G85")) || line.startsWith(QLatin1String("FMAT"))) {
            kept << line;
            continue;
        }
        return std::nullopt;
    }
    return kept;
}
