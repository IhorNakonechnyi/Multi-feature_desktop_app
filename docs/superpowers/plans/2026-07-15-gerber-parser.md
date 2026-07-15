# Hand-rolled Gerber Parser (Phase 3b.2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a hand-rolled RS-274X (Gerber) parser that turns one Gerber file's text into real drawable geometry (millimeters, as a `QPainterPath`), covering standard apertures, arcs, region fills, and full aperture-macro support (expression evaluator + exposure compositing) — then wire it into the existing `GerberDiffRenderer` canvas and `ArchiveComparePage` pair list so selecting a differing Gerber pair actually renders old-vs-new instead of an empty grid.

**Architecture:** A handful of small, flat modules under `src/gerber/` — `GerberFormat` (coordinate/unit decoding), `GerberExpression` (macro arithmetic), `GerberAperture` (standard shape geometry), `GerberMacro` (macro primitive geometry + exposure compositing via `QPainterPath`), and `GerberParser` (the line-by-line state machine tying them together). Plain structs and free functions throughout — no class hierarchies — per Igor's explicit request to keep this part simple and well-commented rather than "flexible."

**Tech Stack:** Qt 6.8.2 (`QPainterPath`, `QTransform`, `QGraphicsScene`), C++20, QtTest for unit tests.

## Global Constraints

- Qt 6.8.2 Widgets, CMake, C++20 (per `CMakeLists.txt`).
- No third-party Gerber-parsing library — hand-rolled only (per `CLAUDE.md`, `research.md`).
- No real customer file content in the committed test suite — test fixtures must be hand-authored minimal synthetic Gerber snippets, not copies of the real sample archive used to scope this design (per `CLAUDE.md`'s confidentiality constraint).
- Unit-test core calculation logic (constitution Principle 4) — the format/expression/aperture/macro/arc/region/step-repeat math all qualifies; the UI wiring (Task 10) is manually tested only.
- This part of the codebase gets more explanatory (section-level, not per-line) comments than the project's usual terse default, and should stay simple/flat (plain functions, minimal classes) rather than "flexible" — per Igor's explicit request, 2026-07-15.
- All coordinates are handled in millimeters internally, matching `GerberDiffRenderer`'s existing scene units (0.1mm grid spacing).

---

### Task 1: GerberFormat — coordinate and unit decoding

**Files:**
- Create: `src/gerber/gerberformat.h`
- Create: `src/gerber/gerberformat.cpp`
- Test: `tests/unit/test_gerberformat.cpp`
- Modify: `CMakeLists.txt` (add the two new source files to the `PcbSupplierWorkflow` target)
- Modify: `tests/unit/CMakeLists.txt` (add the new test executable)

**Interfaces:**
- Produces: `struct GerberFormat { int integerDigits; int decimalDigits; bool leadingZeroOmitted; bool unitsAreInches; double decodeCoordinateMm(const QString&) const; };` and `namespace GerberFormatParser { bool tryParseDirective(const QString &line, GerberFormat &format); }` — both consumed directly by `GerberParser` in Task 5.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/test_gerberformat.cpp`:

```cpp
#include <QtTest/QtTest>

#include "gerber/gerberformat.h"

class TestGerberFormat : public QObject {
    Q_OBJECT

private slots:
    void decodesLeadingZeroOmittedInches();
    void decodesLeadingZeroOmittedMillimeters();
    void decodesNegativeCoordinate();
    void parsesFormatSpecDirective();
    void parsesModernUnitsDirective();
    void parsesLegacyUnitsDirective();
    void ignoresUnrelatedLine();
};

void TestGerberFormat::decodesLeadingZeroOmittedInches()
{
    GerberFormat format;
    format.integerDigits = 2;
    format.decimalDigits = 4;
    format.leadingZeroOmitted = true;
    format.unitsAreInches = true;

    // "021363" with 4 decimal digits -> 2.1363 inches -> mm
    const double mm = format.decodeCoordinateMm("021363");
    QVERIFY(qAbs(mm - (2.1363 * 25.4)) < 0.001);
}

void TestGerberFormat::decodesLeadingZeroOmittedMillimeters()
{
    GerberFormat format;
    format.integerDigits = 2;
    format.decimalDigits = 4;
    format.leadingZeroOmitted = true;
    format.unitsAreInches = false;

    const double mm = format.decodeCoordinateMm("021363");
    QVERIFY(qAbs(mm - 2.1363) < 0.0001);
}

void TestGerberFormat::decodesNegativeCoordinate()
{
    GerberFormat format;
    format.decimalDigits = 4;
    format.unitsAreInches = false;

    const double mm = format.decodeCoordinateMm("-021363");
    QVERIFY(qAbs(mm - (-2.1363)) < 0.0001);
}

void TestGerberFormat::parsesFormatSpecDirective()
{
    GerberFormat format;
    const bool consumed = GerberFormatParser::tryParseDirective("%FSLAX25Y25*%", format);
    QVERIFY(consumed);
    QCOMPARE(format.integerDigits, 2);
    QCOMPARE(format.decimalDigits, 5);
    QVERIFY(format.leadingZeroOmitted);
}

void TestGerberFormat::parsesModernUnitsDirective()
{
    GerberFormat format;
    format.unitsAreInches = true;
    QVERIFY(GerberFormatParser::tryParseDirective("%MOMM*%", format));
    QVERIFY(!format.unitsAreInches);
}

void TestGerberFormat::parsesLegacyUnitsDirective()
{
    GerberFormat format;
    format.unitsAreInches = false;
    QVERIFY(GerberFormatParser::tryParseDirective("G70*", format));
    QVERIFY(format.unitsAreInches);
}

void TestGerberFormat::ignoresUnrelatedLine()
{
    GerberFormat format;
    QVERIFY(!GerberFormatParser::tryParseDirective("D10*", format));
}

QTEST_MAIN(TestGerberFormat)
#include "test_gerberformat.moc"
```

- [ ] **Step 2: Run test to verify it fails (won't even compile yet)**

Run: `cmake --build build --target test_gerberformat`
Expected: FAIL — `gerber/gerberformat.h` doesn't exist yet, and the test target doesn't exist in CMake yet either (add it in Step 3 alongside the header, or the build step itself will fail with "no such target").

- [ ] **Step 3: Write `gerberformat.h`**

```cpp
#pragma once

#include <QString>

// Gerber's coordinate format varies per file - %FS declares how many
// integer/decimal digits each coordinate has and whether omitted zeros
// are the leading or trailing ones; %MO (or the legacy G70/G71 commands)
// declares whether raw numbers are inches or millimeters. GerberFormat
// captures both, so a raw coordinate token from anywhere in the file can
// be turned into real millimeters - the unit GerberDiffRenderer's canvas
// already uses for its grid.
struct GerberFormat {
    int integerDigits = 2;
    int decimalDigits = 4;
    bool leadingZeroOmitted = true; // 'L' after %FS - by far the common case
    bool unitsAreInches = true;     // legacy Gerber's default unit

    // Turns a raw coordinate token (e.g. "021363", optionally signed) into
    // millimeters, honoring the digit counts/zero-suppression mode and
    // converting from inches if needed.
    double decodeCoordinateMm(const QString &token) const;
};

namespace GerberFormatParser {

// Updates `format` in place if `line` is a recognized format/unit
// directive (%FS...*%, %MOIN*%/%MOMM*%, or the legacy G70/G71 unit
// commands). Returns true if the line was consumed this way, so the
// caller (GerberParser) knows not to also try parsing it as a draw
// command.
bool tryParseDirective(const QString &line, GerberFormat &format);

} // namespace GerberFormatParser
```

- [ ] **Step 4: Write `gerberformat.cpp`**

```cpp
#include "gerberformat.h"

#include <cmath>

double GerberFormat::decodeCoordinateMm(const QString &token) const
{
    QString digits = token;
    bool negative = false;
    if (digits.startsWith(QLatin1Char('+'))) {
        digits.remove(0, 1);
    } else if (digits.startsWith(QLatin1Char('-'))) {
        negative = true;
        digits.remove(0, 1);
    }

    const int totalDigits = integerDigits + decimalDigits;
    // Leading-zero omission ('L', the common case): the digits given are
    // the right-justified tail of the full-width number, so the decimal
    // point always sits decimalDigits from the right, however many
    // leading zeros were dropped - nothing to pad.
    // Trailing-zero omission ('T', rare/legacy): the digits given are the
    // left-justified head, so pad with trailing zeros up to the full
    // width before placing the decimal point.
    if (!leadingZeroOmitted && digits.length() < totalDigits) {
        digits += QString(totalDigits - digits.length(), QLatin1Char('0'));
    }

    bool ok = false;
    const qint64 rawValue = digits.toLongLong(&ok);
    double value = ok ? static_cast<double>(rawValue) / std::pow(10.0, decimalDigits) : 0.0;
    if (negative) {
        value = -value;
    }

    return unitsAreInches ? value * 25.4 : value;
}

namespace GerberFormatParser {

bool tryParseDirective(const QString &line, GerberFormat &format)
{
    // %FSLAX24Y24*% - zero-suppression mode (L/T) right after "%FS", then
    // "X" followed by two digits (integer, decimal), then the same for Y
    // (X and Y always match in every real file seen, so only X is read).
    if (line.startsWith(QLatin1String("%FS")) && line.length() > 4) {
        format.leadingZeroOmitted = (line.at(3) == QLatin1Char('L'));
        const int xIndex = line.indexOf(QLatin1Char('X'));
        if (xIndex >= 0 && line.length() > xIndex + 2) {
            format.integerDigits = QStringView(line).mid(xIndex + 1, 1).toInt();
            format.decimalDigits = QStringView(line).mid(xIndex + 2, 1).toInt();
        }
        return true;
    }
    if (line.startsWith(QLatin1String("%MOIN"))) {
        format.unitsAreInches = true;
        return true;
    }
    if (line.startsWith(QLatin1String("%MOMM"))) {
        format.unitsAreInches = false;
        return true;
    }
    if (line.startsWith(QLatin1String("G70"))) {
        format.unitsAreInches = true;
        return true;
    }
    if (line.startsWith(QLatin1String("G71"))) {
        format.unitsAreInches = false;
        return true;
    }
    return false;
}

} // namespace GerberFormatParser
```

- [ ] **Step 5: Wire into `CMakeLists.txt`**

In `CMakeLists.txt`, inside the `add_executable(PcbSupplierWorkflow ...)` list, add right after `src/gerber/gerberdiffrenderer.cpp`:

```
    src/gerber/gerberformat.h
    src/gerber/gerberformat.cpp
```

- [ ] **Step 6: Wire the test into `tests/unit/CMakeLists.txt`**

Append:

```cmake
add_executable(test_gerberformat
    test_gerberformat.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberformat.cpp
)
target_include_directories(test_gerberformat PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_gerberformat PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_gerberformat COMMAND test_gerberformat)
```

- [ ] **Step 7: Configure, build, and run**

Run: `cmake -S . -B build && cmake --build build --target test_gerberformat`
Expected: builds cleanly.

Run: `build\Debug\test_gerberformat.exe` (with `C:\Qt\6.8.2\msvc2022_64\bin` on `PATH`)
Expected: all 7 tests PASS.

- [ ] **Step 8: Commit**

```bash
git add src/gerber/gerberformat.h src/gerber/gerberformat.cpp tests/unit/test_gerberformat.cpp CMakeLists.txt tests/unit/CMakeLists.txt
git commit -m "Add GerberFormat for per-file coordinate/unit decoding"
```

---

### Task 2: GerberExpression — macro arithmetic evaluator

**Files:**
- Create: `src/gerber/gerberexpression.h`
- Create: `src/gerber/gerberexpression.cpp`
- Test: `tests/unit/test_gerberexpression.cpp`
- Modify: `CMakeLists.txt`, `tests/unit/CMakeLists.txt`

**Interfaces:**
- Produces: `namespace GerberExpression { double evaluate(const QString &expression, const QHash<int, double> &variables); }` — consumed by `GerberMacro` (Task 4).

- [ ] **Step 1: Write the failing test**

Create `tests/unit/test_gerberexpression.cpp`:

```cpp
#include <QtTest/QtTest>

#include "gerber/gerberexpression.h"

class TestGerberExpression : public QObject {
    Q_OBJECT

private slots:
    void evaluatesNumericLiteral();
    void evaluatesVariableSubstitution();
    void evaluatesMultiplication();
    void evaluatesAdditionAndSubtraction();
    void evaluatesParentheses();
    void evaluatesOperatorPrecedence();
};

void TestGerberExpression::evaluatesNumericLiteral()
{
    QCOMPARE(GerberExpression::evaluate("22.5", {}), 22.5);
}

void TestGerberExpression::evaluatesVariableSubstitution()
{
    const QHash<int, double> variables = {{1, 0.055}};
    QVERIFY(qAbs(GerberExpression::evaluate("$1", variables) - 0.055) < 1e-9);
}

void TestGerberExpression::evaluatesMultiplication()
{
    // The octagon macro's real expression: "1.08239X$1"
    const QHash<int, double> variables = {{1, 0.055}};
    QVERIFY(qAbs(GerberExpression::evaluate("1.08239X$1", variables) - (1.08239 * 0.055)) < 1e-9);
}

void TestGerberExpression::evaluatesAdditionAndSubtraction()
{
    QCOMPARE(GerberExpression::evaluate("3+4-1", {}), 6.0);
}

void TestGerberExpression::evaluatesParentheses()
{
    QCOMPARE(GerberExpression::evaluate("(1+2)X3", {}), 9.0);
}

void TestGerberExpression::evaluatesOperatorPrecedence()
{
    QCOMPARE(GerberExpression::evaluate("2+3X4", {}), 14.0);
}

QTEST_MAIN(TestGerberExpression)
#include "test_gerberexpression.moc"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_gerberexpression`
Expected: FAIL — target/header don't exist yet.

- [ ] **Step 3: Write `gerberexpression.h`**

```cpp
#pragma once

#include <QHash>
#include <QString>

// Aperture macros can compute a primitive's parameters from a small
// arithmetic expression over the macro's own variables ($1, $2, ...),
// e.g. the octagon macro's "1.08239X$1" (Gerber uses 'x'/'X' for
// multiply, since '*' already terminates statements). This evaluates one
// such expression given whatever variable values are currently in scope.
namespace GerberExpression {

double evaluate(const QString &expression, const QHash<int, double> &variables);

} // namespace GerberExpression
```

- [ ] **Step 4: Write `gerberexpression.cpp`**

```cpp
#include "gerberexpression.h"

namespace {

// A small recursive-descent evaluator for Gerber macro arithmetic:
//   expression := term (('+'|'-') term)*
//   term       := factor (('x'|'X'|'/') factor)*
//   factor     := number | '$' digits | '(' expression ')' | '-' factor
// This is deliberately just enough grammar for what macros actually use -
// no functions, no comparisons, nothing beyond +-x/ and grouping.
class Evaluator {
public:
    Evaluator(const QString &text, const QHash<int, double> &variables)
        : text_(text), variables_(variables)
    {
    }

    double parseExpression()
    {
        double value = parseTerm();
        while (pos_ < text_.length() && (peek() == QLatin1Char('+') || peek() == QLatin1Char('-'))) {
            const QChar op = next();
            const double rhs = parseTerm();
            value = (op == QLatin1Char('+')) ? value + rhs : value - rhs;
        }
        return value;
    }

private:
    double parseTerm()
    {
        double value = parseFactor();
        while (pos_ < text_.length()
               && (peek() == QLatin1Char('x') || peek() == QLatin1Char('X') || peek() == QLatin1Char('/'))) {
            const QChar op = next();
            const double rhs = parseFactor();
            value = (op == QLatin1Char('/')) ? value / rhs : value * rhs;
        }
        return value;
    }

    double parseFactor()
    {
        if (peek() == QLatin1Char('-')) {
            next();
            return -parseFactor();
        }
        if (peek() == QLatin1Char('(')) {
            next();
            const double value = parseExpression();
            if (peek() == QLatin1Char(')')) {
                next();
            }
            return value;
        }
        if (peek() == QLatin1Char('$')) {
            next();
            const int start = pos_;
            while (pos_ < text_.length() && text_.at(pos_).isDigit()) {
                ++pos_;
            }
            const int variableNumber = text_.mid(start, pos_ - start).toInt();
            return variables_.value(variableNumber, 0.0);
        }
        const int start = pos_;
        while (pos_ < text_.length() && (text_.at(pos_).isDigit() || text_.at(pos_) == QLatin1Char('.'))) {
            ++pos_;
        }
        return text_.mid(start, pos_ - start).toDouble();
    }

    QChar peek() const { return pos_ < text_.length() ? text_.at(pos_) : QChar(); }
    QChar next() { return text_.at(pos_++); }

    QString text_;
    const QHash<int, double> &variables_;
    int pos_ = 0;
};

} // namespace

double GerberExpression::evaluate(const QString &expression, const QHash<int, double> &variables)
{
    Evaluator evaluator(expression.trimmed(), variables);
    return evaluator.parseExpression();
}
```

- [ ] **Step 5: Wire into `CMakeLists.txt`**

Add after the `gerberformat` lines:

```
    src/gerber/gerberexpression.h
    src/gerber/gerberexpression.cpp
```

- [ ] **Step 6: Wire the test into `tests/unit/CMakeLists.txt`**

```cmake
add_executable(test_gerberexpression
    test_gerberexpression.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberexpression.cpp
)
target_include_directories(test_gerberexpression PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_gerberexpression PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_gerberexpression COMMAND test_gerberexpression)
```

- [ ] **Step 7: Configure, build, run**

Run: `cmake -S . -B build && cmake --build build --target test_gerberexpression`
Run: `build\Debug\test_gerberexpression.exe`
Expected: all 6 tests PASS.

- [ ] **Step 8: Commit**

```bash
git add src/gerber/gerberexpression.h src/gerber/gerberexpression.cpp tests/unit/test_gerberexpression.cpp CMakeLists.txt tests/unit/CMakeLists.txt
git commit -m "Add GerberExpression macro arithmetic evaluator"
```

---

### Task 3: GerberAperture — standard shape geometry

**Files:**
- Create: `src/gerber/gerberaperture.h`
- Create: `src/gerber/gerberaperture.cpp`
- Test: `tests/unit/test_gerberaperture.cpp`
- Modify: `CMakeLists.txt`, `tests/unit/CMakeLists.txt`

**Interfaces:**
- Produces: `struct GerberAperture { enum class Shape { Circle, Rectangle, Obround, Polygon, Macro }; Shape shape; QVector<double> modifiers; QString macroName; };` and `namespace GerberApertureGeometry { QPainterPath buildFlashPath(const GerberAperture&, const std::function<QPainterPath(const QString&, const QVector<double>&)> &macroLookup); }` — consumed by `GerberParser` (Task 5); the `macroLookup` callback is fulfilled there using `GerberMacroBuilder::buildPath` (Task 4).

- [ ] **Step 1: Write the failing test**

Create `tests/unit/test_gerberaperture.cpp`:

```cpp
#include <QtTest/QtTest>

#include "gerber/gerberaperture.h"

class TestGerberAperture : public QObject {
    Q_OBJECT

private slots:
    void circleHasExpectedBoundingBox();
    void rectangleHasExpectedBoundingBox();
    void obroundHasExpectedBoundingBox();
    void polygonHasExpectedVertexCount();
    void macroDelegatesToLookup();
};

namespace {
QPainterPath noMacroExpected(const QString &, const QVector<double> &)
{
    Q_ASSERT_X(false, "test", "macro lookup should not be called for non-macro shapes");
    return QPainterPath();
}
}

void TestGerberAperture::circleHasExpectedBoundingBox()
{
    GerberAperture aperture;
    aperture.shape = GerberAperture::Shape::Circle;
    aperture.modifiers = {2.0}; // diameter 2mm
    const QPainterPath path = GerberApertureGeometry::buildFlashPath(aperture, noMacroExpected);
    const QRectF bounds = path.boundingRect();
    QVERIFY(qAbs(bounds.width() - 2.0) < 0.01);
    QVERIFY(qAbs(bounds.height() - 2.0) < 0.01);
}

void TestGerberAperture::rectangleHasExpectedBoundingBox()
{
    GerberAperture aperture;
    aperture.shape = GerberAperture::Shape::Rectangle;
    aperture.modifiers = {3.0, 1.0}; // width 3mm, height 1mm
    const QPainterPath path = GerberApertureGeometry::buildFlashPath(aperture, noMacroExpected);
    const QRectF bounds = path.boundingRect();
    QVERIFY(qAbs(bounds.width() - 3.0) < 0.01);
    QVERIFY(qAbs(bounds.height() - 1.0) < 0.01);
}

void TestGerberAperture::obroundHasExpectedBoundingBox()
{
    GerberAperture aperture;
    aperture.shape = GerberAperture::Shape::Obround;
    aperture.modifiers = {3.0, 1.0};
    const QPainterPath path = GerberApertureGeometry::buildFlashPath(aperture, noMacroExpected);
    const QRectF bounds = path.boundingRect();
    QVERIFY(qAbs(bounds.width() - 3.0) < 0.01);
    QVERIFY(qAbs(bounds.height() - 1.0) < 0.01);
}

void TestGerberAperture::polygonHasExpectedVertexCount()
{
    GerberAperture aperture;
    aperture.shape = GerberAperture::Shape::Polygon;
    aperture.modifiers = {2.0, 8.0, 0.0}; // diameter 2mm, 8 vertices (octagon), no rotation
    const QPainterPath path = GerberApertureGeometry::buildFlashPath(aperture, noMacroExpected);
    QCOMPARE(path.elementCount(), 9); // 8 vertices + Qt's implicit close-back-to-start element
}

void TestGerberAperture::macroDelegatesToLookup()
{
    GerberAperture aperture;
    aperture.shape = GerberAperture::Shape::Macro;
    aperture.macroName = "OC8";
    aperture.modifiers = {0.055};

    bool lookupCalled = false;
    auto lookup = [&](const QString &name, const QVector<double> &params) {
        lookupCalled = true;
        QCOMPARE(name, QString("OC8"));
        QCOMPARE(params.size(), 1);
        QPainterPath path;
        path.addEllipse(QPointF(0, 0), 1, 1);
        return path;
    };

    GerberApertureGeometry::buildFlashPath(aperture, lookup);
    QVERIFY(lookupCalled);
}

QTEST_MAIN(TestGerberAperture)
#include "test_gerberaperture.moc"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_gerberaperture`
Expected: FAIL — header/target don't exist yet.

- [ ] **Step 3: Write `gerberaperture.h`**

```cpp
#pragma once

#include <functional>

#include <QPainterPath>
#include <QString>
#include <QVector>

// One aperture as declared by a %AD command: either a standard RS-274X
// shape (Circle/Rectangle/Obround/Polygon) or a reference to a custom
// aperture macro by name, carrying whatever modifier numbers followed it
// (e.g. a circle's diameter, or the values substituted into a macro's
// $1/$2/... variables).
struct GerberAperture {
    enum class Shape { Circle, Rectangle, Obround, Polygon, Macro };

    Shape shape = Shape::Circle;
    QVector<double> modifiers; // meaning depends on shape - see buildFlashPath
    QString macroName;         // only meaningful when shape == Macro
};

namespace GerberApertureGeometry {

// Path representing this aperture "flashed" (stamped) at the origin -
// callers translate it to the actual flash point. `macroLookup` resolves
// Shape::Macro apertures to their macro's composited geometry (built by
// GerberMacroBuilder::buildPath, wired up by GerberParser); it's a
// callback here so this module doesn't need to depend on the macro one.
QPainterPath buildFlashPath(const GerberAperture &aperture,
                            const std::function<QPainterPath(const QString &, const QVector<double> &)> &macroLookup);

} // namespace GerberApertureGeometry
```

- [ ] **Step 4: Write `gerberaperture.cpp`**

```cpp
#include "gerberaperture.h"

#include <cmath>

#include <QtMath>

QPainterPath GerberApertureGeometry::buildFlashPath(
    const GerberAperture &aperture,
    const std::function<QPainterPath(const QString &, const QVector<double> &)> &macroLookup)
{
    QPainterPath path;
    const QVector<double> &m = aperture.modifiers;

    switch (aperture.shape) {
    case GerberAperture::Shape::Circle: {
        // %ADDnnC,diameter[,holeDiameter]*% - the optional hole isn't
        // modeled (not seen in real inputs so far; would just be another
        // subtracted ellipse if it ever is).
        const double diameter = m.value(0, 0.0);
        path.addEllipse(QPointF(0, 0), diameter / 2.0, diameter / 2.0);
        break;
    }
    case GerberAperture::Shape::Rectangle: {
        // %ADDnnR,width,height[,holeDiameter]*%
        const double width = m.value(0, 0.0);
        const double height = m.value(1, 0.0);
        path.addRect(-width / 2.0, -height / 2.0, width, height);
        break;
    }
    case GerberAperture::Shape::Obround: {
        // %ADDnnO,width,height*% - a rectangle with fully-rounded short
        // ends; Qt's rounded-rect with radius = half the shorter side
        // draws exactly that.
        const double width = m.value(0, 0.0);
        const double height = m.value(1, 0.0);
        const double radius = std::min(width, height) / 2.0;
        path.addRoundedRect(-width / 2.0, -height / 2.0, width, height, radius, radius);
        break;
    }
    case GerberAperture::Shape::Polygon: {
        // %ADDnnP,diameter,vertices[,rotation]*% - a regular N-gon
        // inscribed in a circle of the given diameter. Same math as
        // macro primitive 5 (GerberMacroBuilder) - both represent the
        // exact same shape, just declared two different ways.
        const double diameter = m.value(0, 0.0);
        const int vertices = static_cast<int>(m.value(1, 3.0));
        const double rotationDegrees = m.value(2, 0.0);
        QPolygonF polygon;
        for (int i = 0; i < vertices; ++i) {
            const double angle = qDegreesToRadians(rotationDegrees + 360.0 * i / vertices);
            polygon << QPointF((diameter / 2.0) * std::cos(angle), (diameter / 2.0) * std::sin(angle));
        }
        path.addPolygon(polygon);
        path.closeSubpath();
        break;
    }
    case GerberAperture::Shape::Macro:
        path = macroLookup(aperture.macroName, m);
        break;
    }
    return path;
}
```

- [ ] **Step 5: Wire into `CMakeLists.txt`**

Add after the `gerberexpression` lines:

```
    src/gerber/gerberaperture.h
    src/gerber/gerberaperture.cpp
```

- [ ] **Step 6: Wire the test into `tests/unit/CMakeLists.txt`**

```cmake
add_executable(test_gerberaperture
    test_gerberaperture.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberaperture.cpp
)
target_include_directories(test_gerberaperture PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_gerberaperture PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_gerberaperture COMMAND test_gerberaperture)
```

Note the added `Qt6::Gui` — `QPainterPath`/`QPolygonF` live in Qt Gui, unlike the two previous purely-Core-based tests.

- [ ] **Step 7: Configure, build, run**

Run: `cmake -S . -B build && cmake --build build --target test_gerberaperture`
Run: `build\Debug\test_gerberaperture.exe`
Expected: all 5 tests PASS.

- [ ] **Step 8: Commit**

```bash
git add src/gerber/gerberaperture.h src/gerber/gerberaperture.cpp tests/unit/test_gerberaperture.cpp CMakeLists.txt tests/unit/CMakeLists.txt
git commit -m "Add GerberAperture standard shape geometry"
```

---

### Task 4: GerberMacro — aperture macro primitives and exposure compositing

**Files:**
- Create: `src/gerber/gerbermacro.h`
- Create: `src/gerber/gerbermacro.cpp`
- Test: `tests/unit/test_gerbermacro.cpp`
- Modify: `CMakeLists.txt`, `tests/unit/CMakeLists.txt`

**Interfaces:**
- Consumes: `GerberExpression::evaluate` (Task 2).
- Produces: `struct GerberMacro { QString name; QVector<QString> statements; };` and `namespace GerberMacroBuilder { QPainterPath buildPath(const GerberMacro &macro, const QVector<double> &parameters); }` — consumed by `GerberParser` (Task 5) as the `macroLookup` callback passed to `GerberApertureGeometry::buildFlashPath`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/test_gerbermacro.cpp`:

```cpp
#include <QtTest/QtTest>

#include "gerber/gerbermacro.h"

class TestGerberMacro : public QObject {
    Q_OBJECT

private slots:
    void circlePrimitiveProducesExpectedBounds();
    void polygonPrimitiveMatchesExpressionSubstitution();
    void variableAssignmentIsUsableByLaterStatement();
    void subtractiveExposureRemovesArea();
    void unsupportedPrimitiveFallsBackToMarkerCircle();
};

void TestGerberMacro::circlePrimitiveProducesExpectedBounds()
{
    GerberMacro macro;
    macro.statements = {"1,1,2,0,0"}; // circle, exposure on, diameter 2, center (0,0)
    const QPainterPath path = GerberMacroBuilder::buildPath(macro, {});
    const QRectF bounds = path.boundingRect();
    QVERIFY(qAbs(bounds.width() - 2.0) < 0.01);
}

void TestGerberMacro::polygonPrimitiveMatchesExpressionSubstitution()
{
    // This is the real octagon macro from the sample archive that
    // motivated this whole design - primitive 5 (polygon), 8 vertices,
    // diameter computed as 1.08239 * $1.
    GerberMacro macro;
    macro.statements = {"5,1,8,0,0,1.08239X$1,22.5"};
    const QPainterPath path = GerberMacroBuilder::buildPath(macro, {0.055});
    const double expectedDiameter = 1.08239 * 0.055;
    const QRectF bounds = path.boundingRect();
    QVERIFY(qAbs(bounds.width() - expectedDiameter) < 0.001);
}

void TestGerberMacro::variableAssignmentIsUsableByLaterStatement()
{
    GerberMacro macro;
    macro.statements = {"$2=$1X2", "1,1,$2,0,0"}; // diameter = 2 * $1
    const QPainterPath path = GerberMacroBuilder::buildPath(macro, {1.5});
    const QRectF bounds = path.boundingRect();
    QVERIFY(qAbs(bounds.width() - 3.0) < 0.01); // 2 * 1.5
}

void TestGerberMacro::subtractiveExposureRemovesArea()
{
    GerberMacro macro;
    // Big circle (exposure on) then a smaller circle (exposure off) cut
    // out of its middle - the result must have a hole, i.e. its area
    // must be smaller than the big circle's alone.
    macro.statements = {"1,1,4,0,0", "1,0,2,0,0"};
    const QPainterPath path = GerberMacroBuilder::buildPath(macro, {});

    QPainterPath bigCircleOnly;
    bigCircleOnly.addEllipse(QPointF(0, 0), 2.0, 2.0);

    // A path with a hole cut out has less area than the outer shape
    // alone - approximate "area" via bounding-rect-relative fill testing
    // at the center point, which should now be OUTSIDE the composited
    // path (the hole), even though it's inside the plain big circle.
    QVERIFY(bigCircleOnly.contains(QPointF(0, 0)));
    QVERIFY(!path.contains(QPointF(0, 0)));
}

void TestGerberMacro::unsupportedPrimitiveFallsBackToMarkerCircle()
{
    GerberMacro macro;
    macro.statements = {"6,0,0,1,1,1,0"}; // primitive 6 (Moire) - deliberately unimplemented
    const QPainterPath path = GerberMacroBuilder::buildPath(macro, {});
    QVERIFY(!path.isEmpty());
    const QRectF bounds = path.boundingRect();
    QVERIFY(qAbs(bounds.width() - 1.0) < 0.01); // the fixed 1mm-diameter fallback marker
}

QTEST_MAIN(TestGerberMacro)
#include "test_gerbermacro.moc"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_gerbermacro`
Expected: FAIL — header/target don't exist yet.

- [ ] **Step 3: Write `gerbermacro.h`**

```cpp
#pragma once

#include <QPainterPath>
#include <QString>
#include <QVector>

// One aperture macro (%AM name * statement * statement ... %). Statements
// are kept as raw text and only evaluated when an aperture actually
// instantiates the macro with real parameter values ($1, $2, ...) - the
// same macro can be referenced by many apertures with different
// parameters (that's the whole point of $-substitution), so there's
// nothing useful to precompute at definition time.
struct GerberMacro {
    QString name;
    QVector<QString> statements; // one raw primitive/assignment statement each, no leading/trailing '*'
};

namespace GerberMacroBuilder {

// Builds the composited path for one macro instantiation. `parameters`
// are the aperture's own modifier values, available inside the macro as
// $1, $2, .... Each statement is either a variable assignment ("$3=...")
// or a primitive draw statement (starts with a primitive code number);
// primitive shapes are combined in order using their exposure flag
// (1 = add material, 0 = subtract) via QPainterPath's boolean operations.
//
// If a statement uses a primitive code this parser doesn't implement
// (the rare/legacy ones - see the design doc), the WHOLE macro falls
// back to a small fixed-size marker circle rather than compositing a
// partially-wrong shape or guessing at unfamiliar parameter meanings.
QPainterPath buildPath(const GerberMacro &macro, const QVector<double> &parameters);

} // namespace GerberMacroBuilder
```

- [ ] **Step 4: Write `gerbermacro.cpp`**

```cpp
#include "gerbermacro.h"

#include <cmath>

#include <QHash>
#include <QStringList>
#include <QTransform>
#include <QtMath>

#include "gerberexpression.h"

namespace {

// Splits a primitive statement's comma-separated fields and evaluates
// each one as an expression (fields can be plain numbers, $-variables,
// or arithmetic like the octagon macro's "1.08239X$1") - this is the one
// place macro fields turn from text into actual numbers.
QVector<double> evaluateFields(const QStringList &fields, const QHash<int, double> &variables)
{
    QVector<double> values;
    values.reserve(fields.size());
    for (const QString &field : fields) {
        values.append(GerberExpression::evaluate(field, variables));
    }
    return values;
}

// Each buildXxx function below takes the primitive's fields AFTER the
// leading primitive-code number (already evaluated to real numbers) and
// returns that primitive's shape, centered/positioned per its own
// modifiers - see the Gerber Format Specification for each primitive's
// exact field layout, summarized in a comment above each function.

QPainterPath buildCircle(const QVector<double> &v)
{
    // 1,exposure,diameter,centerX,centerY[,rotation]
    QPainterPath path;
    path.addEllipse(QPointF(v.value(2), v.value(3)), v.value(1) / 2.0, v.value(1) / 2.0);
    return path;
}

QPainterPath buildVectorLine(const QVector<double> &v)
{
    // 20,exposure,width,startX,startY,endX,endY,rotation
    // Built as a rectangle running along +X from 0 to the line's length,
    // then rotated/translated into place - simpler than working out the
    // perpendicular offset by hand for an arbitrary angle.
    const QPointF start(v.value(2), v.value(3));
    const QPointF end(v.value(4), v.value(5));
    const double width = v.value(1);
    const double dx = end.x() - start.x();
    const double dy = end.y() - start.y();
    const double length = std::hypot(dx, dy);
    const double lineAngleDegrees = qRadiansToDegrees(std::atan2(dy, dx));

    QPainterPath path;
    path.addRect(0, -width / 2.0, length, width);

    QTransform placeAlongLine;
    placeAlongLine.translate(start.x(), start.y());
    placeAlongLine.rotate(lineAngleDegrees);
    path = placeAlongLine.map(path);

    QTransform macroRotation;
    macroRotation.rotate(v.value(6));
    return macroRotation.map(path);
}

QPainterPath buildCenterLine(const QVector<double> &v)
{
    // 21,exposure,width,height,centerX,centerY,rotation
    const double width = v.value(1);
    const double height = v.value(2);
    QPainterPath path;
    path.addRect(-width / 2.0, -height / 2.0, width, height);

    QTransform transform;
    transform.translate(v.value(3), v.value(4));
    transform.rotate(v.value(5));
    return transform.map(path);
}

QPainterPath buildOutline(const QVector<double> &v)
{
    // 4,exposure,numVertices,X1,Y1,...,X(n+1),Y(n+1),rotation
    // (n+1 points because the last one repeats the first, closing the
    // shape - an arbitrary custom polygon, unlike Polygon's regular N-gon)
    const int numVertices = static_cast<int>(v.value(1));
    QPolygonF polygon;
    for (int i = 0; i <= numVertices; ++i) {
        polygon << QPointF(v.value(2 + i * 2), v.value(3 + i * 2));
    }
    const double rotation = v.value(2 + (numVertices + 1) * 2);

    QPainterPath path;
    path.addPolygon(polygon);
    path.closeSubpath();
    QTransform transform;
    transform.rotate(rotation);
    return transform.map(path);
}

QPainterPath buildPolygon(const QVector<double> &v)
{
    // 5,exposure,numVertices,centerX,centerY,diameter,rotation
    const int vertices = static_cast<int>(v.value(1));
    const QPointF center(v.value(2), v.value(3));
    const double diameter = v.value(4);
    const double rotation = v.value(5);

    QPolygonF polygon;
    for (int i = 0; i < vertices; ++i) {
        const double angle = qDegreesToRadians(rotation + 360.0 * i / vertices);
        polygon << QPointF(center.x() + (diameter / 2.0) * std::cos(angle),
                            center.y() + (diameter / 2.0) * std::sin(angle));
    }
    QPainterPath path;
    path.addPolygon(polygon);
    path.closeSubpath();
    return path;
}

QPainterPath buildThermal(const QVector<double> &v)
{
    // 7,centerX,centerY,outerDiameter,innerDiameter,gapThickness,rotation
    // (no exposure field - a thermal relief is always exactly this "ring
    // with 4 gaps" shape, so it's built directly rather than composited
    // from separately exposure-tagged primitives like the others)
    const QPointF center(v.value(0), v.value(1));
    const double outerDiameter = v.value(2);
    const double innerDiameter = v.value(3);
    const double gap = v.value(4);
    const double rotation = v.value(5);

    QPainterPath outer;
    outer.addEllipse(center, outerDiameter / 2.0, outerDiameter / 2.0);
    QPainterPath inner;
    inner.addEllipse(center, innerDiameter / 2.0, innerDiameter / 2.0);
    QPainterPath ring = outer.subtracted(inner);

    // Four gap wedges (the "spokes" connecting the pad to the plane get
    // left behind between these cuts), 90 degrees apart starting at the
    // primitive's own rotation.
    for (int i = 0; i < 4; ++i) {
        QPainterPath gapRect;
        gapRect.addRect(-outerDiameter / 2.0, -gap / 2.0, outerDiameter, gap);
        QTransform transform;
        transform.translate(center.x(), center.y());
        transform.rotate(rotation + 90.0 * i);
        ring = ring.subtracted(transform.map(gapRect));
    }
    return ring;
}

// A macro that uses a primitive code this parser doesn't build (the
// rare/legacy ones - old vector-line alias, Moire, old lower-left-line)
// falls back to this, rather than a partially-composited wrong shape or
// a guess at unfamiliar parameter meanings. 1mm is an arbitrary but
// clearly-visible placeholder size.
QPainterPath fallbackMarkerCircle()
{
    QPainterPath path;
    path.addEllipse(QPointF(0, 0), 0.5, 0.5);
    return path;
}

} // namespace

QPainterPath GerberMacroBuilder::buildPath(const GerberMacro &macro, const QVector<double> &parameters)
{
    QHash<int, double> variables;
    for (int i = 0; i < parameters.size(); ++i) {
        variables.insert(i + 1, parameters.at(i));
    }

    QPainterPath composited;
    for (const QString &statement : macro.statements) {
        const QString trimmed = statement.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        // "$N=..." assigns a variable rather than drawing a primitive.
        if (trimmed.startsWith(QLatin1Char('$'))) {
            const int equalsIndex = trimmed.indexOf(QLatin1Char('='));
            if (equalsIndex > 0) {
                const int variableNumber = trimmed.mid(1, equalsIndex - 1).toInt();
                variables.insert(variableNumber,
                                  GerberExpression::evaluate(trimmed.mid(equalsIndex + 1), variables));
                continue;
            }
        }

        const QStringList fields = trimmed.split(QLatin1Char(','));
        const int primitiveCode = fields.first().toInt();
        if (primitiveCode == 0) {
            continue; // comment primitive - no geometry
        }

        const QVector<double> values = evaluateFields(fields.mid(1), variables);
        QPainterPath primitivePath;
        double exposure = 1.0;

        switch (primitiveCode) {
        case 1:
            primitivePath = buildCircle(values);
            exposure = values.value(0, 1.0);
            break;
        case 20:
            primitivePath = buildVectorLine(values);
            exposure = values.value(0, 1.0);
            break;
        case 21:
            primitivePath = buildCenterLine(values);
            exposure = values.value(0, 1.0);
            break;
        case 4:
            primitivePath = buildOutline(values);
            exposure = values.value(0, 1.0);
            break;
        case 5:
            primitivePath = buildPolygon(values);
            exposure = values.value(0, 1.0);
            break;
        case 7:
            primitivePath = buildThermal(values);
            exposure = 1.0;
            break;
        default:
            return fallbackMarkerCircle();
        }

        composited = (exposure != 0.0) ? composited.united(primitivePath) : composited.subtracted(primitivePath);
    }
    return composited;
}
```

- [ ] **Step 5: Wire into `CMakeLists.txt`**

Add after the `gerberaperture` lines:

```
    src/gerber/gerbermacro.h
    src/gerber/gerbermacro.cpp
```

- [ ] **Step 6: Wire the test into `tests/unit/CMakeLists.txt`**

```cmake
add_executable(test_gerbermacro
    test_gerbermacro.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerbermacro.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberexpression.cpp
)
target_include_directories(test_gerbermacro PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_gerbermacro PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_gerbermacro COMMAND test_gerbermacro)
```

- [ ] **Step 7: Configure, build, run**

Run: `cmake -S . -B build && cmake --build build --target test_gerbermacro`
Run: `build\Debug\test_gerbermacro.exe`
Expected: all 5 tests PASS.

- [ ] **Step 8: Commit**

```bash
git add src/gerber/gerbermacro.h src/gerber/gerbermacro.cpp tests/unit/test_gerbermacro.cpp CMakeLists.txt tests/unit/CMakeLists.txt
git commit -m "Add GerberMacro primitive geometry and exposure compositing"
```

---

### Task 5: GerberParser core — format directives, apertures, linear draws and flashes

**Files:**
- Create: `src/gerber/gerberparser.h`
- Create: `src/gerber/gerberparser.cpp`
- Test: `tests/unit/test_gerberparser.cpp`
- Modify: `CMakeLists.txt`, `tests/unit/CMakeLists.txt`

**Interfaces:**
- Consumes: `GerberFormat`/`GerberFormatParser` (Task 1), `GerberAperture`/`GerberApertureGeometry` (Task 3), `GerberMacro`/`GerberMacroBuilder` (Task 4).
- Produces: `namespace GerberParser { QPainterPath parseContent(const QString &content); QPainterPath parseFile(const QString &filePath); }` — `parseContent` is what Tasks 6/7/8 extend and what tests call directly (no temp files needed); `parseFile` (thin file-reading wrapper) is what Task 10's UI wiring calls. Consumed by Task 9 (bounding box) and Task 10 (UI wiring).

This task covers: tokenizing the file, dispatching format directives (Task 1), aperture definitions (`%AD`) and macro definitions (`%AM`), aperture selection (`Dnn` where nn >= 10), and **linear** moves/draws/flashes (`D02`/`D01` with `G01` or default linear mode/`D03`). Arcs (Task 6) and regions (Task 7) extend this same state machine afterward.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/test_gerberparser.cpp`:

```cpp
#include <QtTest/QtTest>

#include "gerber/gerberparser.h"

class TestGerberParser : public QObject {
    Q_OBJECT

private slots:
    void parsesSingleFlashWithStandardCircleAperture();
    void parsesLinearDrawAsAStroke();
    void appliesFormatAndUnitsBeforeCoordinates();
    void parsesMacroApertureFlash();
};

void TestGerberParser::parsesSingleFlashWithStandardCircleAperture()
{
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,1.0*%\n"
        "D10*\n"
        "X010000Y010000D03*\n"; // flash at (1.0mm, 1.0mm)

    const QPainterPath path = GerberParser::parseContent(content);
    QVERIFY(!path.isEmpty());
    QVERIFY(path.contains(QPointF(1.0, 1.0)));
}

void TestGerberParser::parsesLinearDrawAsAStroke()
{
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,0.2*%\n"
        "D10*\n"
        "X000000Y000000D02*\n" // move to origin
        "X050000Y000000D01*\n"; // draw to (5.0mm, 0)

    const QPainterPath path = GerberParser::parseContent(content);
    QVERIFY(!path.isEmpty());
    // A stroke from (0,0) to (5,0) with a 0.2mm-wide pen must cover a
    // point roughly halfway along it.
    QVERIFY(path.contains(QPointF(2.5, 0.0)));
}

void TestGerberParser::appliesFormatAndUnitsBeforeCoordinates()
{
    // Same raw digits, but inches this time - the resulting mm position
    // must differ from the millimeters case above.
    const QString content =
        "%FSLAX24Y24*%\n"
        "G70*\n"
        "%ADD10C,1.0*%\n"
        "D10*\n"
        "X010000Y010000D03*\n";

    const QPainterPath path = GerberParser::parseContent(content);
    // 1.0000 inch = 25.4mm, not 1mm - so the flash must NOT be near (1,1)
    QVERIFY(!path.contains(QPointF(1.0, 1.0)));
    QVERIFY(path.contains(QPointF(25.4, 25.4)));
}

void TestGerberParser::parsesMacroApertureFlash()
{
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%AMOC8*\n"
        "5,1,8,0,0,1.08239X$1,22.5*\n"
        "%\n"
        "%ADD11OC8,2.0*%\n"
        "D11*\n"
        "X000000Y000000D03*\n";

    const QPainterPath path = GerberParser::parseContent(content);
    QVERIFY(!path.isEmpty());
    QVERIFY(path.contains(QPointF(0.0, 0.0)));
}

QTEST_MAIN(TestGerberParser)
#include "test_gerberparser.moc"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_gerberparser`
Expected: FAIL — header/target don't exist yet.

- [ ] **Step 3: Write `gerberparser.h`**

```cpp
#pragma once

#include <QPainterPath>
#include <QString>

// Parses one Gerber (RS-274X) file's full geometry into a single
// composited path, in millimeters, ready to hand to GerberDiffRenderer.
// See docs/superpowers/specs/2026-07-15-gerber-parser-design.md for the
// overall design (format handling, standard apertures, full aperture-
// macro support, arcs, region fills).
namespace GerberParser {

// Parses already-loaded file text - what the unit tests exercise
// directly, without needing temp files on disk.
QPainterPath parseContent(const QString &content);

// Reads filePath and parses it. Returns an empty path if the file can't
// be read (no exception - an unreadable Gerber file here just means an
// empty render, the archive-open-failure case is already handled
// upstream by ArchiveReader).
QPainterPath parseFile(const QString &filePath);

} // namespace GerberParser
```

- [ ] **Step 4: Write `gerberparser.cpp`**

```cpp
#include "gerberparser.h"

#include <QFile>
#include <QHash>
#include <QPointF>
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

QVector<GerberStatement> tokenize(const QString &content)
{
    QVector<GerberStatement> statements;
    int i = 0;
    while (i < content.length()) {
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
    QPainterPath composited;
};

GerberMacro lookupMacro(const ParserState &state, const QString &name)
{
    GerberMacro macro;
    macro.name = name;
    macro.statements = state.macroBodySource.value(name).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    return macro;
}

// Parses "%ADnnSHAPE,mod1,mod2,...*%" (already split from its '%...%'
// wrapper by tokenize) into a GerberAperture, keyed by its code number.
void parseApertureDefinition(const QString &statement, ParserState &state)
{
    // statement looks like "ADD10C,1.0" or "ADD11OC8,0.055"
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

    state.apertures.insert(code, aperture);
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
        point.setY(format.decodeCoordinateMm(token.mid(yIndex + 1)));
    }
}

void flash(ParserState &state)
{
    const GerberAperture &aperture = state.apertures.value(state.currentApertureCode);
    QPainterPath flashPath = GerberApertureGeometry::buildFlashPath(
        aperture, [&state](const QString &name, const QVector<double> &params) {
            return GerberMacroBuilder::buildPath(lookupMacro(state, name), params);
        });
    QTransform translate;
    translate.translate(state.currentPoint.x(), state.currentPoint.y());
    state.composited = state.composited.united(translate.map(flashPath));
}

void drawLinearStroke(ParserState &state, const QPointF &from, const QPointF &to)
{
    const GerberAperture &aperture = state.apertures.value(state.currentApertureCode);
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
    state.composited = state.composited.united(transform.map(stroke));
}

} // namespace

QPainterPath GerberParser::parseContent(const QString &content)
{
    const QVector<GerberStatement> statements = tokenize(content);
    ParserState state;

    for (int index = 0; index < statements.size(); ++index) {
        const GerberStatement &statement = statements.at(index);
        const QString &text = statement.text;

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
        if (statement.isParameterStatement) {
            continue; // LP and anything else not yet handled - ignored, not fatal
        }
        if (text.startsWith(QLatin1Char('G')) && !text.startsWith(QLatin1String("G70"))
            && !text.startsWith(QLatin1String("G71"))) {
            continue; // G01/G04/etc. with no state effect at this stage (arcs/regions extend this in later tasks)
        }
        if (text.startsWith(QLatin1Char('D')) && text.length() > 1 && text.at(1).isDigit()
            && !text.contains(QLatin1Char('X')) && !text.contains(QLatin1Char('Y'))) {
            const int code = text.mid(1).toInt();
            if (code >= 10) {
                state.currentApertureCode = code;
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

        if (opCode == 1) {
            drawLinearStroke(state, previousPoint, state.currentPoint);
        } else if (opCode == 3) {
            flash(state);
        }
        // opCode == 2 (move) already updated currentPoint above and draws nothing.
    }

    return state.composited;
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
```

- [ ] **Step 5: Wire into `CMakeLists.txt`**

Add after the `gerbermacro` lines:

```
    src/gerber/gerberparser.h
    src/gerber/gerberparser.cpp
```

- [ ] **Step 6: Wire the test into `tests/unit/CMakeLists.txt`**

```cmake
add_executable(test_gerberparser
    test_gerberparser.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberparser.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberformat.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberaperture.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerbermacro.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberexpression.cpp
)
target_include_directories(test_gerberparser PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_gerberparser PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_gerberparser COMMAND test_gerberparser)
```

- [ ] **Step 7: Configure, build, run**

Run: `cmake -S . -B build && cmake --build build --target test_gerberparser`
Run: `build\Debug\test_gerberparser.exe`
Expected: all 4 tests PASS. If `parsesLinearDrawAsAStroke` or `appliesFormatAndUnitsBeforeCoordinates` fail, check `applyCoordinateToken`'s X/Y substring bounds first - off-by-one there is the most likely bug.

- [ ] **Step 8: Commit**

```bash
git add src/gerber/gerberparser.h src/gerber/gerberparser.cpp tests/unit/test_gerberparser.cpp CMakeLists.txt tests/unit/CMakeLists.txt
git commit -m "Add GerberParser core: format/apertures/linear draws and flashes"
```

---

### Task 6: Arc interpolation (G02/G03, single- and multi-quadrant)

**Files:**
- Modify: `src/gerber/gerberparser.cpp`
- Modify: `tests/unit/test_gerberparser.cpp`

**Interfaces:**
- Consumes/extends: `GerberParser::parseContent` (Task 5) — same public signature, no change to the header.

- [ ] **Step 1: Add the failing test**

Add to `tests/unit/test_gerberparser.cpp`'s private slots list: `void parsesQuarterCircleArc();` and its implementation:

```cpp
void TestGerberParser::parsesQuarterCircleArc()
{
    // A quarter-circle arc from (5,0) to (0,5), centered at the origin
    // (I/J are offsets from the arc's START point to the center, per the
    // Gerber spec - here that's (-5, 0), i.e. center = start + (I,J)).
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,0.1*%\n"
        "D10*\n"
        "G75*\n"           // multi-quadrant mode
        "X050000Y000000D02*\n"
        "G03*\n"            // counter-clockwise arc
        "X000000Y050000I-050000J000000D01*\n";

    const QPainterPath path = GerberParser::parseContent(content);
    QVERIFY(!path.isEmpty());
    // The arc's midpoint (45 degrees around the quarter circle) sits at
    // (5/sqrt2, 5/sqrt2) ~ (3.54, 3.54) - must be covered by the stroke.
    QVERIFY(path.contains(QPointF(3.54, 3.54)));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_gerberparser`
Run: `build\Debug\test_gerberparser.exe`
Expected: FAIL — `parsesQuarterCircleArc` fails (arc not yet drawn as anything, or misinterpreted as a G-code with no effect).

- [ ] **Step 3: Extend `gerberparser.cpp` with arc handling**

Add near the other free functions (after `drawLinearStroke`):

```cpp
// Draws a circular arc as a sequence of short straight segments - the
// simplest correct way to render a curve with QPainterPath's own stroke
// support, without needing QPainterPath's native arc primitives (which
// use a different angle convention than Gerber's I/J center offsets).
void drawArcStroke(ParserState &state, const QPointF &from, const QPointF &to, const QPointF &center,
                    bool counterClockwise)
{
    const GerberAperture &aperture = state.apertures.value(state.currentApertureCode);
    const double width = aperture.modifiers.value(0, 0.1);

    const double startAngle = std::atan2(from.y() - center.y(), from.x() - center.x());
    double endAngle = std::atan2(to.y() - center.y(), to.x() - center.x());
    const double radius = std::hypot(from.x() - center.x(), from.y() - center.y());

    if (counterClockwise && endAngle < startAngle) {
        endAngle += 2.0 * M_PI;
    } else if (!counterClockwise && endAngle > startAngle) {
        endAngle -= 2.0 * M_PI;
    }

    constexpr int kSegments = 32; // fine enough to look smooth at any real board scale
    QPointF previous = from;
    for (int i = 1; i <= kSegments; ++i) {
        const double t = static_cast<double>(i) / kSegments;
        const double angle = startAngle + (endAngle - startAngle) * t;
        const QPointF point(center.x() + radius * std::cos(angle), center.y() + radius * std::sin(angle));
        drawLinearStroke(state, previous, point);
        previous = point;
    }
    (void)width; // width is read inside drawLinearStroke itself via the current aperture
}
```

Then, in `GerberParser::parseContent`, track the interpolation mode and arc-center offsets. Replace the existing:

```cpp
        if (text.startsWith(QLatin1Char('G')) && !text.startsWith(QLatin1String("G70"))
            && !text.startsWith(QLatin1String("G71"))) {
            continue; // G01/G04/etc. with no state effect at this stage (arcs/regions extend this in later tasks)
        }
```

with:

```cpp
        if (text.startsWith(QLatin1String("G01"))) {
            state.interpolationMode = ParserState::InterpolationMode::Linear;
            continue;
        }
        if (text.startsWith(QLatin1String("G02"))) {
            state.interpolationMode = ParserState::InterpolationMode::ClockwiseArc;
            continue;
        }
        if (text.startsWith(QLatin1String("G03"))) {
            state.interpolationMode = ParserState::InterpolationMode::CounterClockwiseArc;
            continue;
        }
        if (text.startsWith(QLatin1Char('G')) && !text.startsWith(QLatin1String("G70"))
            && !text.startsWith(QLatin1String("G71"))) {
            continue; // G04 comments, G74/G75 quadrant mode (arc math here doesn't need to distinguish
                       // single- vs multi-quadrant since it always resolves the shortest matching arc
                       // consistent with the requested direction), and anything else - no state effect.
        }
```

Add `InterpolationMode` to `ParserState` (near its other fields):

```cpp
    enum class InterpolationMode { Linear, ClockwiseArc, CounterClockwiseArc };
    InterpolationMode interpolationMode = InterpolationMode::Linear;
```

Finally, extend the coordinate-command handling to read `I`/`J` and dispatch to the arc drawer. Replace:

```cpp
        if (opCode == 1) {
            drawLinearStroke(state, previousPoint, state.currentPoint);
        } else if (opCode == 3) {
```

with:

```cpp
        if (opCode == 1 && state.interpolationMode != ParserState::InterpolationMode::Linear) {
            const int iIndex = coordinatePart.indexOf(QLatin1Char('I'));
            const int jIndex = coordinatePart.indexOf(QLatin1Char('J'));
            QPointF centerOffset(0.0, 0.0);
            if (iIndex >= 0) {
                const int end = (jIndex > iIndex) ? jIndex : coordinatePart.length();
                centerOffset.setX(state.format.decodeCoordinateMm(coordinatePart.mid(iIndex + 1, end - iIndex - 1)));
            }
            if (jIndex >= 0) {
                centerOffset.setY(state.format.decodeCoordinateMm(coordinatePart.mid(jIndex + 1)));
            }
            const QPointF center = previousPoint + centerOffset;
            drawArcStroke(state, previousPoint, state.currentPoint, center,
                          state.interpolationMode == ParserState::InterpolationMode::CounterClockwiseArc);
        } else if (opCode == 1) {
            drawLinearStroke(state, previousPoint, state.currentPoint);
        } else if (opCode == 3) {
```

Note `coordinatePart` currently only extracts the `X..Y..` prefix before the trailing `D0n` in the existing code's variable of that name — since arc commands include `I`/`J` fields, this step also requires widening where `coordinatePart` is sliced from (it must include everything up to the final `D`, which it already does, so no change needed there — `I`/`J` tokens simply appear after `Y..` within that same substring).

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_gerberparser`
Run: `build\Debug\test_gerberparser.exe`
Expected: all 5 tests (4 from Task 5 + this one) PASS.

- [ ] **Step 5: Commit**

```bash
git add src/gerber/gerberparser.cpp tests/unit/test_gerberparser.cpp
git commit -m "Add arc (G02/G03) interpolation to GerberParser"
```

---

### Task 7: Region fill (G36/G37)

**Files:**
- Modify: `src/gerber/gerberparser.cpp`
- Modify: `tests/unit/test_gerberparser.cpp`

**Interfaces:**
- Consumes/extends: `GerberParser::parseContent` (Task 5/6) — same public signature.

- [ ] **Step 1: Add the failing test**

Add to `tests/unit/test_gerberparser.cpp`: `void parsesRegionFillAsClosedPolygon();` and:

```cpp
void TestGerberParser::parsesRegionFillAsClosedPolygon()
{
    // A 4x4mm square region, filled - its center must be inside the
    // resulting path even though no aperture ever "draws" through it
    // (region fill polarity is purely path-based, no pen width).
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "G36*\n"
        "X000000Y000000D02*\n"
        "X040000Y000000D01*\n"
        "X040000Y040000D01*\n"
        "X000000Y040000D01*\n"
        "X000000Y000000D01*\n"
        "G37*\n";

    const QPainterPath path = GerberParser::parseContent(content);
    QVERIFY(path.contains(QPointF(2.0, 2.0)));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_gerberparser`
Run: `build\Debug\test_gerberparser.exe`
Expected: FAIL — `G36`/`G37` currently fall into the generic "no state effect" G-code branch, so no polygon is ever built.

- [ ] **Step 3: Extend `gerberparser.cpp` with region handling**

Add to `ParserState`:

```cpp
    bool insideRegion = false;
    QPolygonF regionPoints;
```

In `GerberParser::parseContent`, add region start/end handling right alongside the other `G0n` checks (before the catch-all G-code branch):

```cpp
        if (text.startsWith(QLatin1String("G36"))) {
            state.insideRegion = true;
            state.regionPoints.clear();
            continue;
        }
        if (text.startsWith(QLatin1String("G37"))) {
            state.insideRegion = false;
            if (state.regionPoints.size() >= 3) {
                QPainterPath region;
                region.addPolygon(state.regionPoints);
                region.closeSubpath();
                state.composited = state.composited.united(region);
            }
            continue;
        }
```

Then, in the coordinate-command handling at the bottom of the loop, region mode needs to collect points instead of stroking - change:

```cpp
        if (opCode == 1 && state.interpolationMode != ParserState::InterpolationMode::Linear) {
```

to:

```cpp
        if (state.insideRegion) {
            if (state.regionPoints.isEmpty()) {
                state.regionPoints << previousPoint;
            }
            if (opCode == 1) {
                state.regionPoints << state.currentPoint;
            }
        } else if (opCode == 1 && state.interpolationMode != ParserState::InterpolationMode::Linear) {
```

(The rest of the `else if` chain stays exactly as it was, just now nested one level under the new `if (state.insideRegion)`.)

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_gerberparser`
Run: `build\Debug\test_gerberparser.exe`
Expected: all 6 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/gerber/gerberparser.cpp tests/unit/test_gerberparser.cpp
git commit -m "Add region fill (G36/G37) support to GerberParser"
```

---

### Task 8: Step-and-repeat (`%SR`, panelization)

**Files:**
- Modify: `src/gerber/gerberparser.cpp`
- Modify: `tests/unit/test_gerberparser.cpp`

**Interfaces:**
- Consumes/extends: `GerberParser::parseContent` (Tasks 5-7) — same public signature.

**Why this task exists**: added after Igor confirmed (2026-07-15, while checking whether this parser is reusable by the future board-parameter-extraction feature) that real jobs do sometimes arrive as a single Gerber set describing a panelized board. Gerber's `%SR` directive repeats every subsequent draw/flash/region across a grid until it's reset; without handling it, any panelized archive would silently render only one copy of the repeated geometry instead of the full panel - a correctness gap for the diff view itself, not just a future feature.

- [ ] **Step 1: Add the failing test**

Add to `tests/unit/test_gerberparser.cpp`: `void repeatsGeometryPerStepAndRepeatDirective();` and:

```cpp
void TestGerberParser::repeatsGeometryPerStepAndRepeatDirective()
{
    // One flash, repeated 2x2 with a 10mm step in both directions -> the
    // same aperture must appear at (0,0), (10,0), (0,10), and (10,10).
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,1.0*%\n"
        "%SRX2Y2I10.0J10.0*%\n"
        "D10*\n"
        "X000000Y000000D03*\n"
        "%SR*%\n"; // reset - not strictly needed here, but matches real files

    const QPainterPath path = GerberParser::parseContent(content);
    QVERIFY(path.contains(QPointF(0.0, 0.0)));
    QVERIFY(path.contains(QPointF(10.0, 0.0)));
    QVERIFY(path.contains(QPointF(0.0, 10.0)));
    QVERIFY(path.contains(QPointF(10.0, 10.0)));
    // And nowhere near a 3rd repeat that was never requested.
    QVERIFY(!path.contains(QPointF(20.0, 0.0)));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_gerberparser`
Run: `build\Debug\test_gerberparser.exe`
Expected: FAIL — `repeatsGeometryPerStepAndRepeatDirective` fails (only the single un-repeated flash at the origin exists; the three other expected points aren't covered).

- [ ] **Step 3: Extend `gerberparser.cpp` with step-and-repeat**

Add four fields to `ParserState` (near its other fields):

```cpp
    int repeatCountX = 1;
    int repeatCountY = 1;
    double repeatStepXMm = 0.0;
    double repeatStepYMm = 0.0;
```

Add a new free function, near `parseApertureDefinition`:

```cpp
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

// Every place that adds a shape to the drawing goes through here instead
// of calling composited.united() directly, so step-and-repeat (a 1x1
// grid when %SR is inactive, same as no repeat at all) is handled in
// exactly one place rather than duplicated at each call site.
void addToComposited(ParserState &state, const QPainterPath &shape)
{
    for (int row = 0; row < state.repeatCountY; ++row) {
        for (int col = 0; col < state.repeatCountX; ++col) {
            QTransform offset;
            offset.translate(col * state.repeatStepXMm, row * state.repeatStepYMm);
            state.composited = state.composited.united(offset.map(shape));
        }
    }
}
```

Now replace the three existing direct calls to `state.composited.united(...)` with calls to `addToComposited`:

In `flash()`, replace:
```cpp
    state.composited = state.composited.united(translate.map(flashPath));
```
with:
```cpp
    addToComposited(state, translate.map(flashPath));
```

In `drawLinearStroke()`, replace:
```cpp
    state.composited = state.composited.united(transform.map(stroke));
```
with:
```cpp
    addToComposited(state, transform.map(stroke));
```

In the `G37` handling inside `GerberParser::parseContent` (added in Task 7), replace:
```cpp
                state.composited = state.composited.united(region);
```
with:
```cpp
                addToComposited(state, region);
```

Finally, recognize the `%SR` directive itself in `GerberParser::parseContent`'s dispatch chain - add this check right alongside the existing `AD`/`AM` parameter-statement checks (before the generic `if (statement.isParameterStatement) { continue; }` fallback):

```cpp
        if (statement.isParameterStatement && text.startsWith(QLatin1String("SR"))) {
            parseStepRepeatDirective(text, state);
            continue;
        }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_gerberparser`
Run: `build\Debug\test_gerberparser.exe`
Expected: all 7 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/gerber/gerberparser.cpp tests/unit/test_gerberparser.cpp
git commit -m "Add step-and-repeat (%SR) support to GerberParser for panelized boards"
```

---

### Task 9: Bounding box helper and zoom clamping in GerberDiffRenderer

**Files:**
- Modify: `src/gerber/gerberdiffrenderer.h`
- Modify: `src/gerber/gerberdiffrenderer.cpp`
- Test: `tests/unit/test_gerberdiffrenderer_zoom.cpp` (tests the pure zoom-bound math only, not the widget itself - per constitution Principle 4, only the calculation logic gets a unit test, not `QGraphicsView` behavior)
- Modify: `CMakeLists.txt`, `tests/unit/CMakeLists.txt`

**Interfaces:**
- Produces: a free function `namespace GerberZoomBounds { double maxZoomOutScale(const QRectF &unionBounds); double maxZoomInScale(const QRectF &unionBounds, const QSizeF &viewportSizeMm); }` (kept separate from `GerberDiffRenderer` itself specifically so the math is unit-testable without instantiating a `QGraphicsView`), plus new public methods on `GerberDiffRenderer`: `void setGeometry(const QPainterPath &pathA, const QPainterPath &pathB);` (draws both, computes the union bounds, applies the zoom clamp) and an overridden `wheelEvent` that respects the clamp.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/test_gerberdiffrenderer_zoom.cpp`:

```cpp
#include <QtTest/QtTest>

#include "gerber/gerberdiffrenderer.h"

class TestGerberZoomBounds : public QObject {
    Q_OBJECT

private slots:
    void maxZoomOutUsesLargestSpanTimesTwo();
    void maxZoomInKeepsSmallerViewportDimensionAtTenDots();
};

void TestGerberZoomBounds::maxZoomOutUsesLargestSpanTimesTwo()
{
    // A 10mm-wide, 4mm-tall board -> largest span is 10mm -> max zoom-out
    // shows 20mm in that dimension. maxZoomOutScale returns a Qt QGraphicsView
    // scale factor (viewport pixels aren't relevant to this pure formula -
    // it's expressed as "scale relative to the 1:1 mm-to-scene-unit scale
    // already in effect", so the expected value is simply 0.5 = 1 / (2x)).
    const QRectF bounds(0, 0, 10.0, 4.0);
    QVERIFY(qAbs(GerberZoomBounds::maxZoomOutScale(bounds) - 0.5) < 1e-9);
}

void TestGerberZoomBounds::maxZoomInKeepsSmallerViewportDimensionAtTenDots()
{
    // 10 dots at 0.1mm spacing = 1mm. If the viewport is 800x600 (px) and
    // we're deciding the scale at which 600px maps to 1mm in the SMALLER
    // dimension, scale = 600px / 1mm = 600.
    const QRectF bounds(0, 0, 50.0, 50.0); // large board, not the limiting factor here
    const QSizeF viewport(800, 600);
    QVERIFY(qAbs(GerberZoomBounds::maxZoomInScale(bounds, viewport) - 600.0) < 1e-6);
}

QTEST_MAIN(TestGerberZoomBounds)
#include "test_gerberdiffrenderer_zoom.moc"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_gerberdiffrenderer_zoom`
Expected: FAIL — `GerberZoomBounds` doesn't exist yet.

- [ ] **Step 3: Extend `gerberdiffrenderer.h`**

Replace the whole file with:

```cpp
#pragma once

#include <QGraphicsView>
#include <QPainterPath>

// Canvas for the Gerber diff overlay (FR-015). Phase 3b.1 built the empty
// grid shell; Phase 3b.2 (this) adds real geometry via setGeometry() and
// the zoom clamp described in the design doc.
class GerberDiffRenderer : public QGraphicsView {
    Q_OBJECT

public:
    explicit GerberDiffRenderer(QWidget *parent = nullptr);

    // Draws pathA (one color) and pathB (a contrasting color) into the
    // scene, and establishes the zoom clamp from their union bounding box.
    void setGeometry(const QPainterPath &pathA, const QPainterPath &pathB);

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QRectF unionBoundsMm_;
    bool hasGeometry_ = false;
};

// Pure zoom-bound math, kept separate from GerberDiffRenderer itself so
// it's unit-testable without a QGraphicsView/QApplication - see the
// design doc's formulas.
namespace GerberZoomBounds {

// The most-zoomed-OUT scale allowed: the view may show up to 2x the
// union bounding box's largest span, no further. Returned as a scale
// factor relative to the renderer's native 1 scene-unit == 1mm baseline
// (i.e. 0.5 means "zoomed out to half normal size").
double maxZoomOutScale(const QRectF &unionBoundsMm);

// The most-zoomed-IN scale allowed: the SMALLER of the viewport's two
// pixel dimensions must never show less than 10 grid dots (1mm) of scene
// space. Returned as pixels-per-scene-unit(mm).
double maxZoomInScale(const QRectF &unionBoundsMm, const QSizeF &viewportSizePixels);

} // namespace GerberZoomBounds
```

- [ ] **Step 4: Extend `gerberdiffrenderer.cpp`**

Add near the top (after the existing includes), plus the two new functions and `setGeometry`, and update `wheelEvent` to clamp. Replace the whole file with:

```cpp
#include "gerberdiffrenderer.h"

#include <algorithm>
#include <cmath>

#include <QGraphicsScene>
#include <QPainter>
#include <QWheelEvent>

namespace {

constexpr qreal kGridSpacingMm = 0.1;
constexpr qreal kGridDotSizeMm = 0.05;
constexpr qreal kZoomFactorPerStep = 1.15;
constexpr qreal kMinZoomInDots = 10.0; // per design doc: smaller viewport dimension >= 10 dots (1mm)

} // namespace

GerberDiffRenderer::GerberDiffRenderer(QWidget *parent)
    : QGraphicsView(parent)
{
    setScene(new QGraphicsScene(this));
    setRenderHint(QPainter::Antialiasing);
}

void GerberDiffRenderer::setGeometry(const QPainterPath &pathA, const QPainterPath &pathB)
{
    scene()->clear();
    // Old (B) drawn first in a muted color, new (A) drawn on top in a
    // brighter contrasting color - so an unchanged area (where both
    // overlap exactly) still reads as "new", and only genuinely different
    // regions show the old color peeking out from underneath.
    scene()->addPath(pathB, QPen(Qt::NoPen), QBrush(QColor(80, 120, 255, 180)));
    scene()->addPath(pathA, QPen(Qt::NoPen), QBrush(QColor(255, 140, 40, 200)));

    unionBoundsMm_ = pathA.boundingRect().united(pathB.boundingRect());
    hasGeometry_ = !unionBoundsMm_.isEmpty();
}

void GerberDiffRenderer::drawBackground(QPainter *painter, const QRectF &rect)
{
    painter->fillRect(rect, Qt::black);

    const qreal startX = std::floor(rect.left() / kGridSpacingMm) * kGridSpacingMm;
    const qreal startY = std::floor(rect.top() / kGridSpacingMm) * kGridSpacingMm;
    const int columns = static_cast<int>((rect.right() - startX) / kGridSpacingMm) + 1;
    const int rows = static_cast<int>((rect.bottom() - startY) / kGridSpacingMm) + 1;

    constexpr qint64 kMaxDots = 200000;
    if (static_cast<qint64>(columns) * static_cast<qint64>(rows) > kMaxDots) {
        return;
    }

    painter->setPen(Qt::NoPen);
    painter->setBrush(Qt::white);
    for (int row = 0; row < rows; ++row) {
        const qreal y = startY + row * kGridSpacingMm;
        for (int col = 0; col < columns; ++col) {
            const qreal x = startX + col * kGridSpacingMm;
            painter->drawEllipse(QPointF(x, y), kGridDotSizeMm / 2.0, kGridDotSizeMm / 2.0);
        }
    }
}

void GerberDiffRenderer::wheelEvent(QWheelEvent *event)
{
    const bool zoomingIn = event->angleDelta().y() > 0;
    const qreal factor = zoomingIn ? kZoomFactorPerStep : 1.0 / kZoomFactorPerStep;

    if (hasGeometry_) {
        const qreal currentScale = transform().m11(); // uniform scale, so m11 == m22
        const qreal candidateScale = currentScale * factor;
        const qreal maxOut = GerberZoomBounds::maxZoomOutScale(unionBoundsMm_);
        const qreal maxIn = GerberZoomBounds::maxZoomInScale(unionBoundsMm_, viewport()->size());
        if (zoomingIn && candidateScale > maxIn) {
            event->accept();
            return; // already at (or would exceed) the max zoom-in bound
        }
        if (!zoomingIn && candidateScale < maxOut) {
            event->accept();
            return; // already at (or would exceed) the max zoom-out bound
        }
    }

    scale(factor, factor);
    event->accept();
}

double GerberZoomBounds::maxZoomOutScale(const QRectF &unionBoundsMm)
{
    const double largestSpan = std::max(unionBoundsMm.width(), unionBoundsMm.height());
    if (largestSpan <= 0.0) {
        return 1.0; // no real geometry to size against - don't clamp
    }
    // Showing 2x the largest span means the view is zoomed OUT to half
    // the scale it'd be at if that span exactly filled the view.
    return 1.0 / 2.0;
}

double GerberZoomBounds::maxZoomInScale(const QRectF &unionBoundsMm, const QSizeF &viewportSizePixels)
{
    Q_UNUSED(unionBoundsMm);
    const double smallerViewportDimension = std::min(viewportSizePixels.width(), viewportSizePixels.height());
    const double minVisibleMm = kMinZoomInDots * kGridSpacingMm; // 10 dots * 0.1mm = 1mm
    return smallerViewportDimension / minVisibleMm;
}
```

Note: `maxZoomOutScale`'s "2x the largest span" is expressed purely as a fixed `0.5` scale-factor relative to the renderer's baseline (1 scene unit = 1mm = "fit" scale) — the union bounds parameter is accepted for API symmetry with `maxZoomInScale` and to make the "no real geometry" guard possible, but the *ratio* itself doesn't depend on the actual span size (2x anything is always half-scale). Confirm this reasoning against the design doc's formula before treating the implementation as final — if the design intent was instead an absolute scale (not relative to a "fit" baseline), this needs adjusting; flag this to Igor if the manual test in Task 11 shows zoom-out behavior that doesn't look right.

- [ ] **Step 5: Wire the new test into `CMakeLists.txt`/`tests/unit/CMakeLists.txt`**

`gerberdiffrenderer.cpp` needs `Qt6::Widgets` (it's a `QGraphicsView` subclass) in its test target, unlike the previous Core/Gui-only tests:

```cmake
add_executable(test_gerberdiffrenderer_zoom
    test_gerberdiffrenderer_zoom.cpp
    ${CMAKE_SOURCE_DIR}/src/gerber/gerberdiffrenderer.cpp
)
target_include_directories(test_gerberdiffrenderer_zoom PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_gerberdiffrenderer_zoom PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
add_test(NAME test_gerberdiffrenderer_zoom COMMAND test_gerberdiffrenderer_zoom)
```

- [ ] **Step 6: Configure, build, run**

Run: `cmake -S . -B build && cmake --build build --target test_gerberdiffrenderer_zoom`
Run: `build\Debug\test_gerberdiffrenderer_zoom.exe`
Expected: both tests PASS.

- [ ] **Step 7: Rebuild the main app to confirm `GerberDiffRenderer`'s changed public interface doesn't break `ArchiveComparePage`**

Run: `cmake --build build --target PcbSupplierWorkflow`
Expected: builds cleanly — Task 10 is what actually calls `setGeometry`, so at this point it's just confirming nothing else references the old interface in an incompatible way.

- [ ] **Step 8: Commit**

```bash
git add src/gerber/gerberdiffrenderer.h src/gerber/gerberdiffrenderer.cpp tests/unit/test_gerberdiffrenderer_zoom.cpp CMakeLists.txt tests/unit/CMakeLists.txt
git commit -m "Add real geometry rendering and zoom clamping to GerberDiffRenderer"
```

---

### Task 10: Wire ArchiveComparePage's pair list to real parsing and rendering

**Files:**
- Modify: `src/archivecompare/archivecomparepage.h`
- Modify: `src/archivecompare/archivecomparepage.cpp`

**Interfaces:**
- Consumes: `GerberParser::parseFile` (Task 5), `GerberDiffRenderer::setGeometry` (Task 9).

This task is UI/glue code (constitution Principle 4) — manually tested, not unit tested.

- [ ] **Step 1: Add a render-time label member and a list-selection handler declaration**

In `archivecomparepage.h`, add a new private member and method:

```cpp
    void onPairSelected(int row);
```

right after the `runComparison();` declaration, and add:

```cpp
    QLabel *renderTimeLabel_;
```

right after `QVector<DifferingPair> eligibleGerberPairs_;`. Add `class QLabel;` to the forward-declarations near the top of the file (alongside `class QListWidget;`).

- [ ] **Step 2: Store the render-time label and connect selection in the constructor**

In `archivecomparepage.cpp`'s constructor, change:

```cpp
    auto *renderTimeLabel = new QLabel(tr("Render time: -"));
```

to:

```cpp
    renderTimeLabel_ = new QLabel(tr("Render time: -"));
```

and change the line right after it that adds it to the layout from `leftColumnLayout->addWidget(renderTimeLabel);` to `leftColumnLayout->addWidget(renderTimeLabel_);`.

Then, after the existing `connect(panelB_, &ArchivePanel::archiveChanged, ...)` line, add:

```cpp
    connect(gerberPairList_, &QListWidget::currentRowChanged, this, &ArchiveComparePage::onPairSelected);
```

- [ ] **Step 3: Implement `onPairSelected`**

Add `#include <QElapsedTimer>` and `#include "../gerber/gerberparser.h"` to the top of `archivecomparepage.cpp`, then add the method definition after `runComparison`:

```cpp
void ArchiveComparePage::onPairSelected(int row)
{
    if (row < 0 || row >= eligibleGerberPairs_.size()) {
        return;
    }
    const DifferingPair &pair = eligibleGerberPairs_.at(row);

    QElapsedTimer timer;
    timer.start();

    // Pair paths are relative to each archive's own root - re-derive the
    // absolute file path the same way ArchiveReader's caller already does
    // for the top-level archive, by re-opening from the currently-loaded
    // archive file and locating this entry inside it.
    const QPainterPath pathA = GerberParser::parseFile(panelA_->archivePath() + QLatin1Char('/') + pair.relativePathA);
    const QPainterPath pathB = GerberParser::parseFile(panelB_->archivePath() + QLatin1Char('/') + pair.relativePathB);

    gerberCanvas_->setGeometry(pathA, pathB);
    renderTimeLabel_->setText(tr("Render time: %1 ms").arg(timer.elapsed()));
}
```

**Known limitation to flag to Igor before this task is considered done**: `GerberParser::parseFile` takes a plain filesystem path, but `pair.relativePathA`/`relativePathB` are paths *inside a zip archive* (per `ArchiveReader`), not real filesystem paths — `panelA_->archivePath() + "/" + pair.relativePathA` will NOT resolve to a real file on disk, since the archive is never actually extracted to disk anywhere in this codebase (`ArchiveReader` reads zip entries directly into memory via QuaZip). This needs a real fix: either (a) add a `GerberParser::parseContent`-based path that `ArchiveComparePage` feeds by re-reading the specific entry's bytes out of the zip via QuaZip directly (mirroring what `ArchiveReader::readArchive` already does internally), or (b) add a small helper to `ArchiveReader` that returns one entry's raw bytes by path, callable from here. Don't skip this — flag it explicitly and pick one approach with Igor rather than shipping the placeholder path-concatenation above as final.

- [ ] **Step 4: Build**

Run: `cmake --build build --target PcbSupplierWorkflow`
Expected: builds cleanly (the archive-entry-resolution issue above is a correctness gap to resolve before relying on this, not a compile error).

- [ ] **Step 5: Commit**

```bash
git add src/archivecompare/archivecomparepage.h src/archivecompare/archivecomparepage.cpp
git commit -m "Wire Gerber pair selection to real parsing and rendering (entry-resolution gap flagged)"
```

---

### Task 11: Resolve archive-entry resolution gap, then end-to-end manual verification

**Files:**
- Modify: `src/archivecompare/archivereader.h`
- Modify: `src/archivecompare/archivereader.cpp`
- Modify: `src/archivecompare/archivecomparepage.cpp`

**Interfaces:**
- Produces: `namespace ArchiveReader { QByteArray readEntryBytes(const QString &archiveFilePath, const QString &entryRelativePath); }` — consumed by `ArchiveComparePage::onPairSelected`.

- [ ] **Step 1: Add `readEntryBytes` to `ArchiveReader`**

In `archivereader.h`, add to the `ArchiveReader` namespace:

```cpp
// Reads one entry's raw bytes directly out of filePath (which may itself
// be a path to a nested zip inside the top-level archive, joined with
// '/' the same way relativePath's segments are - see readArchive's own
// recursion). Returns an empty QByteArray if the entry can't be found or
// read; unlike readArchive, this doesn't throw - a single missing/corrupt
// entry when opening the Gerber diff view isn't as catastrophic as
// failing to open the whole archive.
QByteArray readEntryBytes(const QString &archiveFilePath, const QString &entryRelativePath);
```

In `archivereader.cpp`, add the implementation (near the bottom, after `ArchiveReader::readArchive`):

```cpp
QByteArray ArchiveReader::readEntryBytes(const QString &archiveFilePath, const QString &entryRelativePath)
{
    // entryRelativePath may cross into a nested zip (e.g.
    // "wrapper/archive3.zip/top.gtl") - walk each '/'-separated segment,
    // descending into a nested zip's own bytes whenever a segment ends in
    // ".zip", exactly mirroring readEntriesFrom's own recursion above.
    QuaZip zip(archiveFilePath);
    if (!zip.open(QuaZip::mdUnzip)) {
        return QByteArray();
    }

    const QStringList segments = entryRelativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QByteArray currentArchiveBytes;
    QuaZip *currentZip = &zip;
    QScopedPointer<QBuffer> nestedBuffer;
    QScopedPointer<QuaZip> nestedZip;

    for (int i = 0; i < segments.size(); ++i) {
        const bool isLastSegment = (i == segments.size() - 1);
        if (!currentZip->setCurrentFile(segments.at(i))) {
            return QByteArray();
        }
        QuaZipFile file(currentZip);
        if (!file.open(QIODevice::ReadOnly)) {
            return QByteArray();
        }
        const QByteArray bytes = file.readAll();
        file.close();

        if (isLastSegment) {
            return bytes;
        }

        // Not the last segment - this must itself be a nested zip to
        // descend into.
        currentArchiveBytes = bytes;
        nestedBuffer.reset(new QBuffer(&currentArchiveBytes));
        if (!nestedBuffer->open(QIODevice::ReadOnly)) {
            return QByteArray();
        }
        nestedZip.reset(new QuaZip(nestedBuffer.data()));
        if (!nestedZip->open(QuaZip::mdUnzip)) {
            return QByteArray();
        }
        currentZip = nestedZip.data();
    }
    return QByteArray();
}
```

Add `#include <QBuffer>` and `#include <QScopedPointer>` to `archivereader.cpp` if not already present (check the existing includes first — `QBuffer` is already included per Task context from `readEntriesFrom`'s nested-zip handling).

- [ ] **Step 2: Update `ArchiveComparePage::onPairSelected` to use it**

Replace the placeholder path-concatenation from Task 10 with:

```cpp
void ArchiveComparePage::onPairSelected(int row)
{
    if (row < 0 || row >= eligibleGerberPairs_.size()) {
        return;
    }
    const DifferingPair &pair = eligibleGerberPairs_.at(row);

    QElapsedTimer timer;
    timer.start();

    const QByteArray bytesA = ArchiveReader::readEntryBytes(panelA_->archivePath(), pair.relativePathA);
    const QByteArray bytesB = ArchiveReader::readEntryBytes(panelB_->archivePath(), pair.relativePathB);

    const QPainterPath pathA = GerberParser::parseContent(QString::fromUtf8(bytesA));
    const QPainterPath pathB = GerberParser::parseContent(QString::fromUtf8(bytesB));

    gerberCanvas_->setGeometry(pathA, pathB);
    renderTimeLabel_->setText(tr("Render time: %1 ms").arg(timer.elapsed()));
}
```

Remove the now-unused `#include "../gerber/gerberparser.h"`'s `parseFile` dependency isn't removed (the header still declares both functions) — just note `parseFile` becomes effectively only used by future callers, if any; no header change needed since both functions remain valid, general-purpose entry points.

- [ ] **Step 3: Build**

Run: `cmake --build build --target PcbSupplierWorkflow`
Expected: builds cleanly.

- [ ] **Step 4: Manual end-to-end verification**

Run: launch `build\Debug\PcbSupplierWorkflow.exe`, load the two sample archives already used earlier in this project (`build/manualtest/archiveA.zip` / `archiveB.zip`, or the real `Gerber_BT30-3b` sample pair from `Test_data/` if available locally) into panels A and B.

Expected:
- The Gerber pair list populates with the differing Gerber-type file(s).
- Clicking a row draws two overlaid colors (orange for archive A/new, blue for archive B/old, per `setGeometry`'s color choice) into the canvas — not just the empty grid.
- Mouse-wheel zoom works and stops changing scale once either the max-zoom-in (10 grid dots / 1mm in the smaller viewport dimension) or max-zoom-out (2x the geometry's largest span) bound is hit.
- The render-time label shows a real millisecond count after a pair is clicked, not the "-" placeholder.

If the canvas stays empty after clicking a row: check `ArchiveReader::readEntryBytes`'s segment-walking logic first (most likely place for an off-by-one in the nested-zip descent) before assuming the parser itself is at fault.

- [ ] **Step 5: Commit**

```bash
git add src/archivecompare/archivereader.h src/archivecompare/archivereader.cpp src/archivecompare/archivecomparepage.cpp
git commit -m "Resolve Gerber diff pair entries directly from their archive, complete end-to-end rendering"
```

---

## Plan-level self-review notes

- **Spec coverage**: format/unit handling (Task 1), macro expression evaluator (Task 2), standard apertures (Task 3), full macro primitives + compositing (Task 4), linear draws/flashes (Task 5), arcs (Task 6), region fill (Task 7), step-and-repeat/panelization (Task 8), zoom clamping (Task 9), UI wiring (Tasks 10-11) — all design-doc sections plus the panelization gap found while verifying architecture fit (2026-07-15) have a corresponding task.
- **Known gap intentionally deferred to Task 11, not silently dropped**: Task 10 ships a known-wrong placeholder for resolving a pair's file bytes from inside a zip archive, explicitly flagged rather than presented as done; Task 11 fixes it for real before end-to-end verification.
- **Type consistency check**: `GerberParser::parseContent`/`parseFile` signature is identical everywhere it's referenced (Tasks 5, 10, 11); `GerberDiffRenderer::setGeometry(const QPainterPath&, const QPainterPath&)` matches its Task 9 definition and Task 10/11 call site; `ArchiveReader::readEntryBytes` signature matches between its Task 11 declaration and call site.
- **`GerberZoomBounds::maxZoomOutScale`'s relative-vs-absolute scale question is flagged inline in Task 9** for Igor to confirm against actual behavior during Task 11's manual verification, rather than treated as silently correct.
- **Future-reuse fitness check (2026-07-15, per Igor's request)**: confirmed `tasks.md` already plans for Phase 5/US3 (board parameter extraction) to extend this same `gerberparser.h/.cpp` with layer-role metadata, not duplicate it. The "one file in, one composited path out" design fits that cleanly since real Gerber files are one-role-per-file. Two gaps identified: no filled-area utility (needed later for "finish area from solder mask geometry," not added here - `QPainterPath` has no built-in area method), and `%SR` step-and-repeat support (added in Task 8 below, confirmed necessary now, not just for later, since real jobs can be panelized).
