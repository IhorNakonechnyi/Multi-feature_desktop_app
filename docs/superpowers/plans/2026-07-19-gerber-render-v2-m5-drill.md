# Gerber Render v2 — M5 Drill Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Excellon (`.drl`/`.xln`) drill-file support so drill pairs render and overlay-compare exactly like Gerber pairs do today, replacing the current "drill compare comes in a later version" skip row.

**Architecture:** A new hand-rolled `ExcellonParser` (mirroring `GerberParser`'s structure) translates drill syntax into the *same* `Layer` object model `GerberParser` already produces — a drilled hole becomes a `Flash` using a `Circle` aperture at the tool's diameter (aperture code = tool number), a routed slot becomes a `Stroke` (linear or arc). `LayerMaskRenderer`, `MaskCompositor`, and `CompositeView` need zero changes. `GerberDiffTrigger` and `ArchiveComparePage` are updated so a drill-drill pair is eligible (dispatched through `ExcellonParser`) while a mixed drill/Gerber pair stays skipped.

**Tech Stack:** Qt 6.8.2 Widgets, C++20, QtTest (ctest-integrated). No new dependencies.

## Global Constraints

- C++20, Qt 6.8.2 Widgets only (no QML) — per `CLAUDE.md`.
- No third-party Gerber/Excellon library; hand-rolled parsing only — per `CLAUDE.md`.
- Zero outbound network calls; all inputs are local files — per `CLAUDE.md`.
- No real supplier/customer file names, parameter names, or data in code, comments, tests, or commits — confidentiality constraint, `CLAUDE.md` and the design doc. Test fixtures are synthetic Excellon snippets written from the public format description only.
- Unit-test core/calculation logic only; UI/glue code is manually tested (constitution principle 4). `ArchiveComparePage` changes are manually verified, not unit-tested.
- `C:\Qt\6.8.2\msvc2022_64\bin` must be on `PATH` to run the built exe or test binaries (otherwise exit `0xc0000135`).
- Build: `cmake --build build --config Debug`. Tests: `ctest --test-dir build -C Debug --output-on-failure`.
- Real test archives live in the gitignored `.claude/worktrees/gerber-parser-3b2/Test_data/` — do not copy their contents into commits or test fixtures.
- Design doc: `docs/superpowers/specs/2026-07-19-gerber-render-v2-m5-drill-design.md` (as corrected — `G00` never draws; `M15`/`M16` bracket real cuts).

---

### Task 1: `ExcellonParser` — header, tool table, explicit-decimal holes

**Files:**
- Create: `src/gerber/excellonparser.h`
- Create: `src/gerber/excellonparser.cpp`
- Test: `tests/unit/test_excellonparser.cpp`
- Modify: `tests/unit/CMakeLists.txt`

**Interfaces:**
- Produces: `Layer ExcellonParser::parseContentToObjects(const QString &content)` and `class ExcellonParser::AmbiguousFormatError : public std::runtime_error` (with `QString message() const`) — both consumed by Task 2, Task 3, and Task 4's `ArchiveComparePage` dispatch.

This task covers explicit-decimal coordinates only (a non-decimal coordinate always throws — Task 2 adds real zero-suppression resolution) and drill holes only (no rout/slot support — Task 3 adds that). This is real, correctly-scoped M5.1 behavior, not a stub.

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/test_excellonparser.cpp`:

```cpp
#include <QtTest/QtTest>

#include "gerber/excellonparser.h"
#include "gerber/gerberformat.h"

class TestExcellonParser : public QObject {
    Q_OBJECT

private slots:
    void explicitDecimalHoleProducesFlashAtRightPosition();
    void toolTableDiameterBecomesCircleApertureInMm();
    void toolTableDiameterConvertedFromInches();
    void secondToolSelectionSwitchesApertureForSubsequentHoles();
    void nonDecimalCoordinateWithNoFormatInfoThrows();
    void headerOnlyFileProducesNoObjects();
};

void TestExcellonParser::explicitDecimalHoleProducesFlashAtRightPosition()
{
    const QString content =
        "M48\n"
        "METRIC\n"
        "T01C0.300\n"
        "%\n"
        "T01\n"
        "X1.500Y2.250\n"
        "M30\n";

    const Layer layer = ExcellonParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    QCOMPARE(static_cast<int>(layer.objects[0].type), static_cast<int>(ObjectType::Flash));
    QCOMPARE(layer.objects[0].apertureCode, 1);
    QCOMPARE(layer.objects[0].position.x, GerberFormat::mmToNm(1.5));
    QCOMPARE(layer.objects[0].position.y, GerberFormat::mmToNm(2.25));
}

void TestExcellonParser::toolTableDiameterBecomesCircleApertureInMm()
{
    const QString content =
        "M48\n"
        "METRIC\n"
        "T01C0.300\n"
        "%\n"
        "T01\n"
        "X1.500Y2.250\n"
        "M30\n";

    const Layer layer = ExcellonParser::parseContentToObjects(content);

    QVERIFY(layer.apertures.contains(1));
    const GerberAperture &aperture = layer.apertures.value(1);
    QCOMPARE(static_cast<int>(aperture.shape), static_cast<int>(GerberAperture::Shape::Circle));
    QCOMPARE(aperture.modifiers.size(), 1);
    QVERIFY(qFuzzyCompare(aperture.modifiers[0], 0.300));
}

void TestExcellonParser::toolTableDiameterConvertedFromInches()
{
    const QString content =
        "M48\n"
        "INCH\n"
        "T01C0.0135\n"
        "%\n"
        "T01\n"
        "X0.100Y0.100\n"
        "M30\n";

    const Layer layer = ExcellonParser::parseContentToObjects(content);

    QVERIFY(layer.apertures.contains(1));
    QVERIFY(qFuzzyCompare(layer.apertures.value(1).modifiers[0], 0.0135 * 25.4));
}

void TestExcellonParser::secondToolSelectionSwitchesApertureForSubsequentHoles()
{
    const QString content =
        "M48\n"
        "METRIC\n"
        "T01C0.300\n"
        "T02C0.600\n"
        "%\n"
        "T01\n"
        "X1.000Y1.000\n"
        "T02\n"
        "X2.000Y2.000\n"
        "M30\n";

    const Layer layer = ExcellonParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 2);
    QCOMPARE(layer.objects[0].apertureCode, 1);
    QCOMPARE(layer.objects[1].apertureCode, 2);
}

void TestExcellonParser::nonDecimalCoordinateWithNoFormatInfoThrows()
{
    const QString content =
        "M48\n"
        "T01C0.300\n"
        "%\n"
        "T01\n"
        "X015000Y015000\n"
        "M30\n";

    QVERIFY_EXCEPTION_THROWN(ExcellonParser::parseContentToObjects(content), ExcellonParser::AmbiguousFormatError);
}

void TestExcellonParser::headerOnlyFileProducesNoObjects()
{
    const QString content =
        "M48\n"
        "METRIC\n"
        "T01C0.300\n"
        "%\n"
        "M30\n";

    const Layer layer = ExcellonParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 0);
}

QTEST_MAIN(TestExcellonParser)
#include "test_excellonparser.moc"
```

- [ ] **Step 2: Add the CMake target**

Append to `tests/unit/CMakeLists.txt` (after the `test_gerberparser` block at the end of the file):

```cmake
add_executable(test_excellonparser
    test_excellonparser.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/excellonparser.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberformat.cpp
)
target_include_directories(test_excellonparser PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_excellonparser PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_excellonparser COMMAND test_excellonparser)
```

- [ ] **Step 3: Run to verify it fails to build**

Run: `cmake -S . -B build` (to pick up the new CMakeLists entry), then `cmake --build build --config Debug --target test_excellonparser`
Expected: configure succeeds; build fails — `excellonparser.h`/`excellonparser.cpp` don't exist yet.

- [ ] **Step 4: Create the header**

Create `src/gerber/excellonparser.h`:

```cpp
#pragma once

#include <stdexcept>

#include <QString>

#include "gerberobjectmodel.h"

// Parses an Excellon (.drl/.xln) drill file into the same integer-nm object
// model GerberParser produces (see gerberobjectmodel.h), so drill layers
// flow through the identical LayerMaskRenderer/MaskCompositor pipeline used
// for copper Gerber layers. See
// docs/superpowers/specs/2026-07-19-gerber-render-v2-m5-drill-design.md.
// A drilled hole becomes a Flash using a Circle aperture at the tool's
// diameter (aperture code = tool number); a routed slot (an M15..M16 span)
// becomes a Stroke, linear or arc, at the same diameter.
namespace ExcellonParser {

// Thrown when a coordinate has no decimal point and no resolvable
// zero-suppression format (no ";FILE_FORMAT=" comment, no usable
// INCH/METRIC header line) - see the design doc's "Coordinate format
// resolution". Distinct from returning an empty Layer so the caller can
// show a specific reason, mirroring ArchiveOpenError's role for unreadable
// archives.
class AmbiguousFormatError : public std::runtime_error {
public:
    explicit AmbiguousFormatError(const QString &message)
        : std::runtime_error(message.toStdString()), message_(message)
    {
    }

    QString message() const { return message_; }

private:
    QString message_;
};

Layer parseContentToObjects(const QString &content);

} // namespace ExcellonParser
```

- [ ] **Step 5: Implement**

Create `src/gerber/excellonparser.cpp`:

```cpp
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

    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char(';'))) {
            continue;
        }
        if (line.compare(QLatin1String("M48"), Qt::CaseInsensitive) == 0 || line == QLatin1String("%")
            || line.compare(QLatin1String("M95"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        if (line.compare(QLatin1String("M30"), Qt::CaseInsensitive) == 0
            || line.compare(QLatin1String("M00"), Qt::CaseInsensitive) == 0) {
            break;
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
```

- [ ] **Step 6: Run tests to verify they pass**

Run:
```
cmake --build build --config Debug --target test_excellonparser
ctest --test-dir build -C Debug -R test_excellonparser --output-on-failure
```
Expected: `100% tests passed`.

- [ ] **Step 7: Commit**

```bash
git add src/gerber/excellonparser.h src/gerber/excellonparser.cpp tests/unit/test_excellonparser.cpp tests/unit/CMakeLists.txt
git commit -m "M5.1: ExcellonParser - tool table + explicit-decimal drill holes"
```

---

### Task 2: `ExcellonParser` — zero-suppressed coordinate formats

**Files:**
- Modify: `src/gerber/excellonparser.cpp`
- Test: `tests/unit/test_excellonparser.cpp`

**Interfaces:** unchanged from Task 1 (`ExcellonParser::parseContentToObjects` signature and behavior for explicit-decimal input are unaffected; this task only makes non-decimal coordinates resolvable when the file declares a format).

- [ ] **Step 1: Write the failing tests**

Add these six slots to `tests/unit/test_excellonparser.cpp`'s `private slots:` list (after `headerOnlyFileProducesNoObjects();`):

```cpp
    void metricLeadingZeroOmittedDecodesCorrectly();
    void metricTrailingZeroOmittedDecodesCorrectly();
    void inchLeadingZeroOmittedDecodesCorrectly();
    void inchTrailingZeroOmittedDecodesCorrectly();
    void fileFormatCommentOverridesUnitLineDigitDefaults();
    void stillThrowsWhenTrulyNoFormatInfoPresent();
```

Add the definitions (before `QTEST_MAIN`):

```cpp
void TestExcellonParser::metricLeadingZeroOmittedDecodesCorrectly()
{
    // LZ (leading zeros omitted): the digits given are the right-justified
    // tail, so a shorter token still decodes correctly with no padding.
    const QString content =
        "M48\n"
        "METRIC,LZ\n"
        "T01C0.300\n"
        "%\n"
        "T01\n"
        "X1500Y750\n" // 3:3 default digits -> 1.500mm, 0.750mm
        "M30\n";

    const Layer layer = ExcellonParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    QCOMPARE(layer.objects[0].position.x, GerberFormat::mmToNm(1.5));
    QCOMPARE(layer.objects[0].position.y, GerberFormat::mmToNm(0.75));
}

void TestExcellonParser::metricTrailingZeroOmittedDecodesCorrectly()
{
    // TZ (trailing zeros omitted): the digits given are the left-justified
    // head, padded with trailing zeros up to the full 3:3 width - "0015"
    // pads to "001500" (1.500mm), matching the LZ test's same logical value.
    const QString content =
        "M48\n"
        "METRIC,TZ\n"
        "T01C0.300\n"
        "%\n"
        "T01\n"
        "X0015Y00075\n"
        "M30\n";

    const Layer layer = ExcellonParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    QCOMPARE(layer.objects[0].position.x, GerberFormat::mmToNm(1.5));
    QCOMPARE(layer.objects[0].position.y, GerberFormat::mmToNm(0.75));
}

void TestExcellonParser::inchLeadingZeroOmittedDecodesCorrectly()
{
    // 2:4 default digits, LZ - "15000" -> 1.5000in -> 38.1mm.
    const QString content =
        "M48\n"
        "INCH,LZ\n"
        "T01C0.010\n"
        "%\n"
        "T01\n"
        "X15000Y7500\n"
        "M30\n";

    const Layer layer = ExcellonParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    QVERIFY(qAbs(GerberFormat::nmToMm(layer.objects[0].position.x) - 38.1) < 1e-6);
    QVERIFY(qAbs(GerberFormat::nmToMm(layer.objects[0].position.y) - 19.05) < 1e-6);
}

void TestExcellonParser::inchTrailingZeroOmittedDecodesCorrectly()
{
    // 2:4 default digits, TZ - "015" pads to "015000" -> 1.5000in -> 38.1mm,
    // same logical value as the LZ test above.
    const QString content =
        "M48\n"
        "INCH,TZ\n"
        "T01C0.010\n"
        "%\n"
        "T01\n"
        "X015Y0075\n"
        "M30\n";

    const Layer layer = ExcellonParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    QVERIFY(qAbs(GerberFormat::nmToMm(layer.objects[0].position.x) - 38.1) < 1e-6);
    QVERIFY(qAbs(GerberFormat::nmToMm(layer.objects[0].position.y) - 19.05) < 1e-6);
}

void TestExcellonParser::fileFormatCommentOverridesUnitLineDigitDefaults()
{
    // FILE_FORMAT=2:2 overrides METRIC's own 3:3 default, even though the
    // comment appears BEFORE the METRIC line in the file - order must not
    // matter. With 2:2 (4 total digits), "1500" -> 15.00mm; under the
    // un-overridden 3:3 default it would have decoded to 1.5mm instead.
    const QString content =
        "M48\n"
        ";FILE_FORMAT=2:2\n"
        "METRIC\n"
        "T01C0.300\n"
        "%\n"
        "T01\n"
        "X1500Y0\n"
        "M30\n";

    const Layer layer = ExcellonParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    QCOMPARE(layer.objects[0].position.x, GerberFormat::mmToNm(15.0));
}

void TestExcellonParser::stillThrowsWhenTrulyNoFormatInfoPresent()
{
    // No INCH/METRIC line, no FILE_FORMAT comment - unchanged from Task 1's
    // coverage, re-asserted here alongside the new resolvable cases so a
    // regression that made the resolver too permissive is caught in the
    // same place as the cases it must NOT wrongly resolve.
    const QString content =
        "M48\n"
        "T01C0.300\n"
        "%\n"
        "T01\n"
        "X015000Y015000\n"
        "M30\n";

    QVERIFY_EXCEPTION_THROWN(ExcellonParser::parseContentToObjects(content), ExcellonParser::AmbiguousFormatError);
}
```

- [ ] **Step 2: Run tests to verify the new ones fail**

Run:
```
cmake --build build --config Debug --target test_excellonparser
ctest --test-dir build -C Debug -R test_excellonparser --output-on-failure
```
Expected: the six new tests FAIL (all non-decimal coordinates still throw `AmbiguousFormatError`, since `hasResolvableFormat` is never set yet); the Task 1 tests still pass.

- [ ] **Step 3: Implement**

In `src/gerber/excellonparser.cpp`, add these two new functions to the anonymous namespace, right after `parseToolLine` and before `emitFlash`:

```cpp
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
```

Add one field to `ExcellonState` (right after `hasResolvableFormat`):

```cpp
    // True once a FILE_FORMAT comment set integer/decimalDigits directly -
    // a later (or earlier) INCH/METRIC line's own digit-count defaults
    // must not override it, regardless of which appears first in the file.
    bool digitsExplicitlySet = false;
```

Replace the loop body's comment-skip and header-marker handling (the two `if` statements right after the `line.isEmpty()` check) with:

```cpp
        if (line.startsWith(QLatin1Char(';'))) {
            tryParseFileFormatComment(line, state); // no-op if not that comment
            continue;
        }
        bool headerDone = false;
        if (line.compare(QLatin1String("M48"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        if (line == QLatin1String("%") || line.compare(QLatin1String("M95"), Qt::CaseInsensitive) == 0) {
            headerDone = true;
            continue;
        }
```

Wait — `headerDone` must persist across loop iterations, not be redeclared each time. Move its declaration to just before the `for` loop instead: in `Layer ExcellonParser::parseContentToObjects`, change

```cpp
    ExcellonState state;
    const QStringList lines = content.split(QLatin1Char('\n'));

    for (const QString &rawLine : lines) {
```

to

```cpp
    ExcellonState state;
    const QStringList lines = content.split(QLatin1Char('\n'));
    bool headerDone = false;

    for (const QString &rawLine : lines) {
```

and inside the loop, the `%`/`M95` branch just sets `headerDone = true;` (no `bool` keyword, since it's now the outer variable) — same as shown above minus the redeclaration.

Finally, insert a call to `tryParseUnitsLine` right before the existing `const ToolLine tool = parseToolLine(line);` line:

```cpp
        if (!headerDone && tryParseUnitsLine(line, state)) {
            continue;
        }

        const ToolLine tool = parseToolLine(line);
```

- [ ] **Step 4: Run tests to verify they pass**

Run:
```
cmake --build build --config Debug --target test_excellonparser
ctest --test-dir build -C Debug -R test_excellonparser --output-on-failure
```
Expected: `100% tests passed` (all 12 tests: Task 1's six plus Task 2's six).

- [ ] **Step 5: Commit**

```bash
git add src/gerber/excellonparser.cpp tests/unit/test_excellonparser.cpp
git commit -m "M5.2: ExcellonParser - zero-suppressed coordinate format resolution"
```

---

### Task 3: `ExcellonParser` — routed slots (M15/M16, G00/G01/G02/G03, G05)

**Files:**
- Modify: `src/gerber/excellonparser.cpp`
- Test: `tests/unit/test_excellonparser.cpp`

**Interfaces:** unchanged (`parseContentToObjects` signature is the same; this task adds `Stroke` objects to its output for files that use rout-mode commands, on top of Tasks 1-2's `Flash` support).

- [ ] **Step 1: Write the failing tests**

Add these six slots to `tests/unit/test_excellonparser.cpp`'s `private slots:` list (after `stillThrowsWhenTrulyNoFormatInfoPresent();`):

```cpp
    void linearSlotBetweenM15AndM16ProducesOneStroke();
    void rapidMoveBeforeM15DrawsNothing();
    void clockwiseArcSlotProducesArcStroke();
    void counterClockwiseArcSlotSetsCounterClockwiseFlag();
    void modalCoordinateAfterG01ReusesLinearMode();
    void drillHoleStillWorksAfterG05ReturnsToDrillMode();
```

Add the definitions (before `QTEST_MAIN`):

```cpp
void TestExcellonParser::linearSlotBetweenM15AndM16ProducesOneStroke()
{
    const QString content =
        "M48\n"
        "METRIC\n"
        "T01C0.500\n"
        "%\n"
        "T01\n"
        "G00X0.000Y0.000\n"
        "M15\n"
        "G01X5.000Y0.000\n"
        "M16\n"
        "G05\n"
        "M30\n";

    const Layer layer = ExcellonParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    QCOMPARE(static_cast<int>(layer.objects[0].type), static_cast<int>(ObjectType::Stroke));
    QVERIFY(!layer.objects[0].isArc);
    QCOMPARE(layer.objects[0].position.x, GerberFormat::mmToNm(0.0));
    QCOMPARE(layer.objects[0].position.y, GerberFormat::mmToNm(0.0));
    QCOMPARE(layer.objects[0].strokeTo.x, GerberFormat::mmToNm(5.0));
    QCOMPARE(layer.objects[0].strokeTo.y, GerberFormat::mmToNm(0.0));
    QCOMPARE(layer.objects[0].apertureCode, 1);
}

void TestExcellonParser::rapidMoveBeforeM15DrawsNothing()
{
    // Same file as the test above: exactly one object total proves the G00
    // rapid reposition itself drew nothing - only the M15..M16 span did.
    const QString content =
        "M48\n"
        "METRIC\n"
        "T01C0.500\n"
        "%\n"
        "T01\n"
        "G00X3.000Y3.000\n"
        "M15\n"
        "G01X8.000Y3.000\n"
        "M16\n"
        "G05\n"
        "M30\n";

    const Layer layer = ExcellonParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    QCOMPARE(layer.objects[0].position.x, GerberFormat::mmToNm(3.0));
}

void TestExcellonParser::clockwiseArcSlotProducesArcStroke()
{
    const QString content =
        "M48\n"
        "METRIC\n"
        "T01C0.500\n"
        "%\n"
        "T01\n"
        "G00X5.000Y0.000\n"
        "M15\n"
        "G02X0.000Y5.000I-5.000J0.000\n"
        "M16\n"
        "G05\n"
        "M30\n";

    const Layer layer = ExcellonParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    QVERIFY(layer.objects[0].isArc);
    QVERIFY(!layer.objects[0].arcCounterClockwise);
    QCOMPARE(layer.objects[0].arcCenter.x, GerberFormat::mmToNm(0.0));
    QCOMPARE(layer.objects[0].arcCenter.y, GerberFormat::mmToNm(0.0));
    QCOMPARE(layer.objects[0].strokeTo.x, GerberFormat::mmToNm(0.0));
    QCOMPARE(layer.objects[0].strokeTo.y, GerberFormat::mmToNm(5.0));
}

void TestExcellonParser::counterClockwiseArcSlotSetsCounterClockwiseFlag()
{
    const QString content =
        "M48\n"
        "METRIC\n"
        "T01C0.500\n"
        "%\n"
        "T01\n"
        "G00X5.000Y0.000\n"
        "M15\n"
        "G03X0.000Y5.000I-5.000J0.000\n"
        "M16\n"
        "G05\n"
        "M30\n";

    const Layer layer = ExcellonParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    QVERIFY(layer.objects[0].isArc);
    QVERIFY(layer.objects[0].arcCounterClockwise);
}

void TestExcellonParser::modalCoordinateAfterG01ReusesLinearMode()
{
    const QString content =
        "M48\n"
        "METRIC\n"
        "T01C0.500\n"
        "%\n"
        "T01\n"
        "G00X0.000Y0.000\n"
        "M15\n"
        "G01X1.000Y0.000\n"
        "X2.000Y0.000\n" // no G-word - modal reuse of G01's linear mode
        "M16\n"
        "G05\n"
        "M30\n";

    const Layer layer = ExcellonParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 2);
    QVERIFY(!layer.objects[0].isArc);
    QCOMPARE(layer.objects[0].strokeTo.x, GerberFormat::mmToNm(1.0));
    QVERIFY(!layer.objects[1].isArc);
    QCOMPARE(layer.objects[1].position.x, GerberFormat::mmToNm(1.0));
    QCOMPARE(layer.objects[1].strokeTo.x, GerberFormat::mmToNm(2.0));
}

void TestExcellonParser::drillHoleStillWorksAfterG05ReturnsToDrillMode()
{
    const QString content =
        "M48\n"
        "METRIC\n"
        "T01C0.500\n"
        "%\n"
        "T01\n"
        "G00X0.000Y0.000\n"
        "M15\n"
        "G01X5.000Y0.000\n"
        "M16\n"
        "G05\n"
        "X9.000Y9.000\n" // back in drill mode - this must be a Flash, not a Stroke
        "M30\n";

    const Layer layer = ExcellonParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 2);
    QCOMPARE(static_cast<int>(layer.objects[1].type), static_cast<int>(ObjectType::Flash));
    QCOMPARE(layer.objects[1].position.x, GerberFormat::mmToNm(9.0));
}
```

- [ ] **Step 2: Run tests to verify the new ones fail**

Run:
```
cmake --build build --config Debug --target test_excellonparser
ctest --test-dir build -C Debug -R test_excellonparser --output-on-failure
```
Expected: the six new tests FAIL or don't compile as expected (rout-mode commands currently fall through to the bare X/Y branch and are wrongly treated as drill holes, or `G00`/`M15`/`M16`/`G02`/`G03`/`G05` lines are silently ignored since they contain no `X`/`Y` at line start after the `G0n` prefix stripping that doesn't exist yet). The Task 1/2 tests still pass.

- [ ] **Step 3: Implement**

Replace the full contents of `src/gerber/excellonparser.cpp` with:

```cpp
#include "excellonparser.h"

#include <utility>

#include <QHash>
#include <QObject>
#include <QStringList>

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
    if (numberEnd < line.length() && line.at(numberEnd) == QLatin1Char('C')) {
        const int diameterEnd = nextFieldIndex(line, numberEnd + 1);
        result.diameterText = line.mid(numberEnd + 1, diameterEnd - numberEnd - 1);
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
```

- [ ] **Step 4: Run tests to verify they pass**

Run:
```
cmake --build build --config Debug --target test_excellonparser
ctest --test-dir build -C Debug -R test_excellonparser --output-on-failure
```
Expected: `100% tests passed` (all 18 tests: Tasks 1-3 combined).

- [ ] **Step 5: Commit**

```bash
git add src/gerber/excellonparser.cpp tests/unit/test_excellonparser.cpp
git commit -m "M5.3: ExcellonParser - routed slots (M15/M16, G00/G01/G02/G03, G05)"
```

---

### Task 4: Wire drill pairs into Archive Compare

**Files:**
- Modify: `src/archivecompare/gerberdifftrigger.h`
- Modify: `src/archivecompare/gerberdifftrigger.cpp`
- Modify: `tests/unit/test_gerberdifftrigger.cpp`
- Modify: `src/archivecompare/archivecomparepage.h`
- Modify: `src/archivecompare/archivecomparepage.cpp`

**Interfaces:**
- Consumes: `ExcellonParser::parseContentToObjects` / `ExcellonParser::AmbiguousFormatError` (Tasks 1-3), `GerberFileType::isDrillFile` (existing).
- Produces: `GerberDiffTrigger::eligiblePairs`/`drillPairs` behavior changes (same signatures) - a both-sides-drill pair is now eligible; `drillPairs` now reports only mixed (one drill, one non-drill) pairs.

- [ ] **Step 1: Write the failing `GerberDiffTrigger` tests**

Replace the full contents of `tests/unit/test_gerberdifftrigger.cpp` with:

```cpp
#include <QtTest/QtTest>

#include "archivecompare/archivecomparisonresult.h"
#include "archivecompare/gerberdifftrigger.h"

// GerberFileType::isRecognized checks a fixed built-in extension set plus
// any user-added extensions in QSettings (empty in this test environment),
// so these use extensions from that built-in default set directly rather
// than assuming or hardcoding the app's real (non-public) convention.
class TestGerberDiffTrigger : public QObject {
    Q_OBJECT

private slots:
    void bothSidesRecognizedGerberType();
    void oneSideNotGerberType();
    void neitherSideGerberType();
    void emptyInputProducesEmptyOutput();

    void bothSidesDrillFilesAreEligible();
    void mixedDrillAndGerberSideExcludesFromEligible();
    void drillPairsBucketsOnlyMixedDrillPairs();
    void unrecognizedPairsBucketsNonDrillUnrecognized();
    void mixedDrillTakesPriorityOverUnrecognized();
};

void TestGerberDiffTrigger::bothSidesRecognizedGerberType()
{
    const QVector<DifferingPair> pairs = {{"top.gtl", "top.gtl"}};

    const QVector<DifferingPair> eligible = GerberDiffTrigger::eligiblePairs(pairs);

    QCOMPARE(eligible.size(), 1);
}

void TestGerberDiffTrigger::oneSideNotGerberType()
{
    const QVector<DifferingPair> pairs = {{"top.gtl", "notes.docx"}};

    const QVector<DifferingPair> eligible = GerberDiffTrigger::eligiblePairs(pairs);

    QCOMPARE(eligible.size(), 0);
}

void TestGerberDiffTrigger::neitherSideGerberType()
{
    const QVector<DifferingPair> pairs = {{"notes.docx", "notes.docx"}};

    const QVector<DifferingPair> eligible = GerberDiffTrigger::eligiblePairs(pairs);

    QCOMPARE(eligible.size(), 0);
}

void TestGerberDiffTrigger::emptyInputProducesEmptyOutput()
{
    const QVector<DifferingPair> eligible = GerberDiffTrigger::eligiblePairs({});

    QCOMPARE(eligible.size(), 0);
}

void TestGerberDiffTrigger::bothSidesDrillFilesAreEligible()
{
    const QVector<DifferingPair> pairs = {{"board.drl", "board.drl"}};

    const QVector<DifferingPair> eligible = GerberDiffTrigger::eligiblePairs(pairs);

    QCOMPARE(eligible.size(), 1);
}

void TestGerberDiffTrigger::mixedDrillAndGerberSideExcludesFromEligible()
{
    const QVector<DifferingPair> pairs = {{"board.drl", "top.gtl"}};

    const QVector<DifferingPair> eligible = GerberDiffTrigger::eligiblePairs(pairs);

    QCOMPARE(eligible.size(), 0);
}

void TestGerberDiffTrigger::drillPairsBucketsOnlyMixedDrillPairs()
{
    const QVector<DifferingPair> pairs = {
        {"board.drl", "top.gtl"}, {"board.drl", "board.drl"}, {"top.gtl", "top.gtl"}};

    const QVector<DifferingPair> drill = GerberDiffTrigger::drillPairs(pairs);

    QCOMPARE(drill.size(), 1);
    QCOMPARE(drill.first().relativePathA, QString("board.drl"));
    QCOMPARE(drill.first().relativePathB, QString("top.gtl"));
}

void TestGerberDiffTrigger::unrecognizedPairsBucketsNonDrillUnrecognized()
{
    const QVector<DifferingPair> pairs = {{"top.gtl", "notes.docx"}, {"board.drl", "board.drl"}};

    const QVector<DifferingPair> unrecognized = GerberDiffTrigger::unrecognizedPairs(pairs);

    QCOMPARE(unrecognized.size(), 1);
    QCOMPARE(unrecognized.first().relativePathB, QString("notes.docx"));
}

void TestGerberDiffTrigger::mixedDrillTakesPriorityOverUnrecognized()
{
    // One side is a drill file, the other an unrecognized extension: an
    // Add-extension button could never make this renderable, so it must
    // land in drillPairs, not unrecognizedPairs.
    const QVector<DifferingPair> pairs = {{"board.drl", "board.foobar"}};

    QCOMPARE(GerberDiffTrigger::drillPairs(pairs).size(), 1);
    QCOMPARE(GerberDiffTrigger::unrecognizedPairs(pairs).size(), 0);
}

QTEST_MAIN(TestGerberDiffTrigger)
#include "test_gerberdifftrigger.moc"
```

- [ ] **Step 2: Run to verify the changed tests fail**

Run:
```
cmake --build build --config Debug --target test_gerberdifftrigger
ctest --test-dir build -C Debug -R test_gerberdifftrigger --output-on-failure
```
Expected: `bothSidesDrillFilesAreEligible` and `drillPairsBucketsOnlyMixedDrillPairs` FAIL against the current M4 behavior (both-drill pairs are currently excluded from eligible and currently land in `drillPairs`).

- [ ] **Step 3: Implement the `GerberDiffTrigger` change**

Replace the full contents of `src/archivecompare/gerberdifftrigger.h` with:

```cpp
#pragma once

#include <QVector>

#include "archivecomparisonresult.h"

// Splits a comparison's DifferingPairs into three buckets the Archive
// Compare UI acts on differently (2026-07-18-gerber-render-v2-m4-overlay-
// design.md section 1, updated by 2026-07-19-gerber-render-v2-m5-drill-
// design.md section 2): pairs the overlay can render now (both sides
// Gerber, or both sides drill - dispatched to the matching parser), pairs
// blocked only by an unrecognized extension (fixable via
// GerberFileType::addUserExtension), and pairs with exactly one drill
// side, which can never be compared (a drill file against a copper layer
// is meaningless). A pair belongs to exactly one bucket - a mixed drill
// pair is checked first, so a pair with one drill side and one
// unrecognized side counts as drill (an Add-extension button could never
// make it renderable).
namespace GerberDiffTrigger {

QVector<DifferingPair> eligiblePairs(const QVector<DifferingPair> &differingPairs);

// Blocked only by an unrecognized extension on at least one side - neither
// side is a drill file.
QVector<DifferingPair> unrecognizedPairs(const QVector<DifferingPair> &differingPairs);

// Exactly one side is a drill (Excellon) file - a both-drill pair is
// eligible instead (see eligiblePairs).
QVector<DifferingPair> drillPairs(const QVector<DifferingPair> &differingPairs);

} // namespace GerberDiffTrigger
```

Replace the full contents of `src/archivecompare/gerberdifftrigger.cpp` with:

```cpp
#include "gerberdifftrigger.h"

#include "../gerber/gerberfiletype.h"

namespace {

bool hasDrillSide(const DifferingPair &pair)
{
    return GerberFileType::isDrillFile(pair.relativePathA) || GerberFileType::isDrillFile(pair.relativePathB);
}

bool bothSidesDrillFiles(const DifferingPair &pair)
{
    return GerberFileType::isDrillFile(pair.relativePathA) && GerberFileType::isDrillFile(pair.relativePathB);
}

bool isMixedDrillPair(const DifferingPair &pair)
{
    return hasDrillSide(pair) && !bothSidesDrillFiles(pair);
}

} // namespace

QVector<DifferingPair> GerberDiffTrigger::eligiblePairs(const QVector<DifferingPair> &differingPairs)
{
    QVector<DifferingPair> eligible;
    for (const auto &pair : differingPairs) {
        if (isMixedDrillPair(pair)) {
            continue;
        }
        if (GerberFileType::isRecognized(pair.relativePathA) && GerberFileType::isRecognized(pair.relativePathB)) {
            eligible.append(pair);
        }
    }
    return eligible;
}

QVector<DifferingPair> GerberDiffTrigger::unrecognizedPairs(const QVector<DifferingPair> &differingPairs)
{
    QVector<DifferingPair> unrecognized;
    for (const auto &pair : differingPairs) {
        if (hasDrillSide(pair)) {
            continue;
        }
        if (!GerberFileType::isRecognized(pair.relativePathA) || !GerberFileType::isRecognized(pair.relativePathB)) {
            unrecognized.append(pair);
        }
    }
    return unrecognized;
}

QVector<DifferingPair> GerberDiffTrigger::drillPairs(const QVector<DifferingPair> &differingPairs)
{
    QVector<DifferingPair> drill;
    for (const auto &pair : differingPairs) {
        if (isMixedDrillPair(pair)) {
            drill.append(pair);
        }
    }
    return drill;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run:
```
cmake --build build --config Debug --target test_gerberdifftrigger
ctest --test-dir build -C Debug -R test_gerberdifftrigger --output-on-failure
```
Expected: `100% tests passed`.

- [ ] **Step 5: Commit the `GerberDiffTrigger` change**

```bash
git add src/archivecompare/gerberdifftrigger.h src/archivecompare/gerberdifftrigger.cpp tests/unit/test_gerberdifftrigger.cpp
git commit -m "M5.4: both-sides-drill pairs are eligible, not skipped"
```

- [ ] **Step 6: Add the `ExcellonParser` include and parser-dispatch helper to `ArchiveComparePage`**

In `src/archivecompare/archivecomparepage.h`, add this include after `#include "../gerber/gerberobjectmodel.h"`:

```cpp
#include "../gerber/excellonparser.h"
```

Add this private method declaration to the class, right after the existing `void renderShownPair(const QRectF &windowMm, double pxPerMm);` line:

```cpp
    Layer parseGerberOrDrill(const QByteArray &bytes, const QString &relativePath, QString &parseError);
```

In `src/archivecompare/archivecomparepage.cpp`, add this include after `#include "../gerber/gerberparser.h"`:

```cpp
#include "../gerber/excellonparser.h"
```

Add this new method definition right before `void ArchiveComparePage::showSelectedPair()`:

```cpp
// Picks GerberParser or ExcellonParser by the entry's own file extension
// (GerberFileType::isDrillFile) - see the M5 design doc, section 2. An
// empty `bytes` (read failure, already handled independently per side by
// the caller) produces an empty Layer with no error. An ExcellonParser
// ambiguous-coordinate-format failure is caught here and reported via
// `parseError` instead of propagating, mirroring how a read failure is
// already just an empty Layer rather than a thrown exception at this
// layer - one file's problem must never abort rendering the other side.
Layer ArchiveComparePage::parseGerberOrDrill(const QByteArray &bytes, const QString &relativePath,
                                              QString &parseError)
{
    if (bytes.isEmpty()) {
        return Layer{};
    }
    const QString content = QString::fromUtf8(bytes);
    if (!GerberFileType::isDrillFile(relativePath)) {
        return GerberParser::parseContentToObjects(content);
    }
    try {
        return ExcellonParser::parseContentToObjects(content);
    } catch (const ExcellonParser::AmbiguousFormatError &error) {
        parseError = error.message();
        return Layer{};
    }
}
```

- [ ] **Step 7: Update `showSelectedPair` to use the dispatch helper and report parse errors**

In `src/archivecompare/archivecomparepage.cpp`, replace the full contents of `ArchiveComparePage::showSelectedPair` with:

```cpp
void ArchiveComparePage::showSelectedPair()
{
    const int row = gerberPairList_->currentRow();
    if (row < 0 || row >= eligiblePairs_.size()) {
        return;
    }
    const DifferingPair &pair = eligiblePairs_.at(row);

    const QByteArray bytesA = ArchiveReader::readEntryBytes(panelA_->archivePath(), pair.relativePathA);
    const QByteArray bytesB = ArchiveReader::readEntryBytes(panelB_->archivePath(), pair.relativePathB);

    QString parseErrorA;
    QString parseErrorB;
    shownLayerA_ = parseGerberOrDrill(bytesA, pair.relativePathA, parseErrorA);
    shownLayerB_ = parseGerberOrDrill(bytesB, pair.relativePathB, parseErrorB);

    // Each side is judged independently: an empty-but-readable file, a read
    // failure, or a parse failure (parseErrorX set) all leave that side's
    // Layer empty, which GerberObjectPaths::layerBoundsMm correctly
    // reports as empty bounds either way - the difference is only in which
    // status text explains why.
    const QRectF boundsA = GerberObjectPaths::layerBoundsMm(shownLayerA_);
    const QRectF boundsB = GerberObjectPaths::layerBoundsMm(shownLayerB_);
    const QRectF unitedBounds = boundsA.united(boundsB);

    if (!parseErrorA.isEmpty() && !parseErrorB.isEmpty()) {
        pairStatusLabel_->setText(tr("Both sides failed to parse - A: %1 | B: %2").arg(parseErrorA, parseErrorB));
    } else if (!parseErrorA.isEmpty()) {
        pairStatusLabel_->setText(tr("A side failed to parse: %1 - showing old file only.").arg(parseErrorA));
    } else if (!parseErrorB.isEmpty()) {
        pairStatusLabel_->setText(tr("B side failed to parse: %1 - showing new file only.").arg(parseErrorB));
    } else if (unitedBounds.isEmpty()) {
        pairStatusLabel_->setText(tr("Neither side has drawable content."));
    } else if (boundsA.isEmpty()) {
        pairStatusLabel_->setText(tr("A side: no drawable content - showing old file only."));
    } else if (boundsB.isEmpty()) {
        pairStatusLabel_->setText(tr("B side: no drawable content - showing new file only."));
    } else {
        pairStatusLabel_->clear();
    }

    if (unitedBounds.isEmpty()) {
        return;
    }
    gerberCanvas_->fitTo(unitedBounds); // emits renderRequested -> renderShownPair
}
```

- [ ] **Step 8: Build**

Run: `cmake --build build --config Debug`
Expected: builds clean, no errors.

- [ ] **Step 9: Commit**

```bash
git add src/archivecompare/archivecomparepage.h src/archivecompare/archivecomparepage.cpp
git commit -m "M5.4: dispatch drill pairs through ExcellonParser in Archive Compare"
```

---

### Task 5: Full build, full test suite, and the M5 visual gate

**Files:** none (verification only).

- [ ] **Step 1: Full rebuild**

Run: `cmake --build build --config Debug`
Expected: exit 0, no errors.

- [ ] **Step 2: Full test suite**

Run (with `C:\Qt\6.8.2\msvc2022_64\bin` on `PATH`):
```
ctest --test-dir build -C Debug --output-on-failure
```
Expected: `100% tests passed` across all suites, including the new `test_excellonparser` (18 test cases) and the updated `test_gerberdifftrigger`.

- [ ] **Step 3: Launch the app**

Run: `build/Debug/PcbSupplierWorkflow.exe` (with the same `PATH`).
Go to Archive Compare.

- [ ] **Step 4: Manual acceptance checks (M5.4 gate)**

Using a real supplier archive pair that includes a changed drill file (not one of the three `Test_data` pairs used for M3/M4, which don't include drill changes per the earlier gate notes - pick or construct one with a `.drl`/`.xln` difference):

- A changed drill-drill pair appears in the eligible list (not the "drill file pair(s) skipped" row).
- Select it, click Show → the overlay renders holes (and slots, if the file has any) the same way copper flashes/strokes render today - true circles, no tessellation artifacts, correct relative positions and diameters.
- A pair with one drill side and one Gerber side (if constructible from available data) still shows in the "drill file pair(s) skipped" row, unchanged from M4 behavior.
- If any real file exercises the LZ/TZ zero-suppression fallback (no `FILE_FORMAT` comment), visually confirm hole positions land where expected - this is the actual check on this plan's documented "best-effort" LZ/TZ mapping (see Task 2's `tryParseUnitsLine` comment); if it looks backward (holes clustered at the wrong scale or mirrored), flip `leadingZeroOmitted = !line.contains(...)` to `leadingZeroOmitted = line.contains(...)` and re-verify - a one-line fix, not a redesign.

- [ ] **Step 5: Single-file and overlay-compare judgment (M5.4/M5.5 gate)**

Do not mark this milestone done from automated checks alone - Igor's visual judgment against CAMMaster on a real drill file (single-file) and a real changed drill pair (overlay) is the milestone gate, same as M3 and M4. Summarize what was checked and wait for confirmation before considering M5 complete.
