#include "excellonparser.h"

#include <utility>

#include <QHash>
#include <QObject>
#include <QStringList>

#include "gerberaperture.h"
#include "gerberformat.h"

namespace {

using ExcellonParser::AmbiguousFormatError;

struct ExcellonState {
    GerberFormat format;
    // True once a usable zero-suppression digit format is known (Task 2
    // adds how that gets set - INCH/METRIC header line or a FILE_FORMAT
    // comment). Until then, every coordinate must carry an explicit
    // decimal point, which is this task's whole scope.
    bool hasResolvableFormat = false;
    // True once a FILE_FORMAT comment set integer/decimalDigits directly -
    // a later (or earlier) INCH/METRIC line's own digit-count defaults
    // must not override it, regardless of which appears first in the file.
    bool digitsExplicitlySet = false;
    QHash<int, GerberAperture> apertures; // tool number -> Circle aperture (diameter in mm)
    int currentTool = -1;
    PointNm currentPoint;
    QVector<GraphicObject> objects;
};

// Finds where a field's numeric text ends: the next uppercase letter at or
// after `start`, or the string's end. Mirrors GerberParser's own
// nextUppercaseIndex (gerberparser.cpp) - duplicated rather than shared,
// since this is a small, self-contained per-parser utility and the two
// parsers don't otherwise share an implementation file.
int nextFieldIndex(const QString &text, int start)
{
    for (int i = start; i < text.length(); ++i) {
        if (text.at(i).isUpper()) {
            return i;
        }
    }
    return text.length();
}

// A coordinate token with an explicit decimal point (e.g. "1.5") is used
// directly, in the file's current units - it needs no zero-suppression
// digit-count/LZ-TZ resolution at all. A token with no decimal point (e.g.
// "015000") needs GerberFormat::decodeCoordinateMm's zero-suppression
// logic, which is only valid once state.hasResolvableFormat is true; until
// then it's a genuine ambiguity - the file gives no way to know how many
// implied decimal places it has.
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

// Decodes an "X..Y.." token (only the axes actually present are updated,
// same convention as GerberParser::applyCoordinateToken) into `point`.
void applyCoordinateToken(const QString &token, ExcellonState &state, PointNm &point)
{
    const int xIndex = token.indexOf(QLatin1Char('X'));
    const int yIndex = token.indexOf(QLatin1Char('Y'));
    if (xIndex >= 0) {
        const int end = nextFieldIndex(token, xIndex + 1);
        point.x = GerberFormat::mmToNm(resolveCoordinateMm(token.mid(xIndex + 1, end - xIndex - 1), state));
    }
    if (yIndex >= 0) {
        const int end = nextFieldIndex(token, yIndex + 1);
        point.y = GerberFormat::mmToNm(resolveCoordinateMm(token.mid(yIndex + 1, end - yIndex - 1), state));
    }
}

// "T01C0.0135" (tool table entry: number + diameter) vs. "T01" (tool
// selection: number only) - both start the same way, so this returns the
// tool number and, only for a table entry, the diameter text after 'C'
// (empty QString for a bare selection).
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
    if (numberEnd < line.length() && line.at(numberEnd) == QLatin1Char('C')) {
        const int diameterEnd = nextFieldIndex(line, numberEnd + 1);
        result.diameterText = line.mid(numberEnd + 1, diameterEnd - numberEnd - 1);
    }
    return result;
}

// ";FILE_FORMAT=2:4" (a vendor convention naming integer:decimal digit
// counts directly) is the highest-priority format source - an explicit
// declaration beats any default. Returns true if `line` was one of these
// comments and `state` was updated.
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

void emitFlash(ExcellonState &state)
{
    GraphicObject object;
    object.type = ObjectType::Flash;
    object.polarity = Polarity::Dark;
    object.apertureCode = state.currentTool;
    object.position = state.currentPoint;
    state.objects.append(object);
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
            tryParseFileFormatComment(line, state); // no-op if not that comment
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

        if (!headerDone && tryParseUnitsLine(line, state)) {
            continue;
        }

        const ToolLine tool = parseToolLine(line);
        if (tool.isToolLine && !tool.diameterText.isEmpty()) {
            // Tool table entry: diameter is a plain decimal in the file's
            // current unit, same as a Gerber aperture modifier - not
            // subject to coordinate zero-suppression at all (real files
            // always write it with an explicit decimal point).
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
            applyCoordinateToken(line, state, state.currentPoint);
            emitFlash(state);
        }
    }

    Layer layer;
    layer.apertures = state.apertures;
    layer.objects = std::move(state.objects);
    layer.unitScale = state.format.unitsAreInches ? 25.4 : 1.0;
    return layer;
}
