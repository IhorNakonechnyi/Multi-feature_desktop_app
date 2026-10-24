#include <QtTest/QtTest>

#include <QElapsedTimer>

#include "gerber/gerberformat.h"
#include "gerber/gerberparser.h"

class TestGerberParser : public QObject {
    Q_OBJECT

private slots:
    void parsesSingleFlashWithStandardCircleAperture();
    void parsesLinearDrawAsAStroke();
    void appliesFormatAndUnitsBeforeCoordinates();
    void parsesMacroApertureFlash();
    void parsesLinearModeCombinedWithDrawOnOneStatement();
    void parsesBareDCodeAsRepeatOfLastCoordinate();
    void g04CommentIsNeverParsedAsCoordinates();
    void parsesQuarterCircleArc();
    void parsesQuarterCircleArcClockwise();
    void parsesRegionFillAsClosedPolygon();
    void regionOpeningMoveStartsFromItsOwnDestination();
    void repeatsGeometryPerStepAndRepeatDirective();
    void repeatsArcStrokeInsideStepAndRepeatBlock();
    void parsesDenseLayerWithoutQuadraticBlowup();
    void apertureSizeIsConvertedFromInchesLikeCoordinatesAre();
    void clearPolarityCutsAHoleInDarkGeometry();
    void darkClearDarkPatternMatchesRealFileUsage();
    void overlappingSamePolarityShapesSurviveTerminalBooleanOp();
    void fileOpeningWithClearPolarityStaysEmpty();
    void consecutiveSamePolarityDirectivesDoNotBreakLaterTransitionTracking();

    // Object-model tests: these call parseContentToObjects() directly and
    // assert on the returned Layer/GraphicObject structure (nanometer
    // coordinates, polarity, arc/region fields), NOT on a flattened
    // QPainterPath. They guard the parse-produces-the-object-model half of
    // the pipeline independently of the legacy QPainterPath reconstruction
    // the tests above exercise.
    void singleFlashProducesOneFlashObjectAtTheRightPosition();
    void clearPolarityObjectGetsClearPolarity();
    void stepAndRepeatMaterializesOneObjectPerGridCell();
    void arcStrokeObjectCarriesCenterAndDirection();
    void regionObjectCarriesAllVertices();
    void g54ApertureSelectPrefixIsHonored();
    void unpaddedGCodesAreHonored();
    void operationCodeIsModalOnBareCoordinates();
    void multiQuadrantArcWithEqualEndpointsIsAFullCircle();
    void singleQuadrantArcResolvesUnsignedCenterOffsets();
    void macroBodyStopsAtApertureAttribute();
    void macroClosedOnItsOwnLineKeepsItsPrimitive();
    void arcWithOnlyXKeepsItsX();
    void regionArcWithOnlyXKeepsItsX();
    void regionArcEdgeIsKept();
    void regionArcIsDrawnAsACurve();
    void singleQuadrantRegionArcResolvesItsCenter();
    void singleQuadrantCornerJustPastAQuarterTurnKeepsItsCenter();
    void singleQuadrantClockwiseArcJustPastAQuarterTurnKeepsItsCenter();
    void singleQuadrantArcTakesTheShortWayRound();
    void singleQuadrantArcWithEqualEndsIsNotARing();
    void singleQuadrantArcWithNoShortWayRoundIsStraight();
    void fullCircleRegionIsEmitted();
    void stepAndRepeatMovesRegionArcCenters();
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
        "%AMTESTMACRO*\n"
        "5,1,8,0,0,2.0X$1,0*\n"
        "%\n"
        "%ADD11TESTMACRO,2.0*%\n"
        "D11*\n"
        "X000000Y000000D03*\n";

    const QPainterPath path = GerberParser::parseContent(content);
    QVERIFY(!path.isEmpty());
    QVERIFY(path.contains(QPointF(0.0, 0.0)));
}

void TestGerberParser::parsesLinearModeCombinedWithDrawOnOneStatement()
{
    // Real Gerber files routinely combine a mode-set G-code and a
    // coordinate + D-code on the same statement, e.g. "G01X..Y..D01*" sets
    // linear-interpolation mode and draws in one go. Same shape as
    // parsesLinearDrawAsAStroke, but the draw's G01 prefix is combined onto
    // the draw statement itself instead of appearing as its own line.
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,0.2*%\n"
        "D10*\n"
        "G01X050000Y000000D01*\n"; // set linear mode AND draw to (5.0mm, 0), from origin

    const QPainterPath path = GerberParser::parseContent(content);
    QVERIFY(!path.isEmpty());
    // A stroke from (0,0) to (5,0) with a 0.2mm-wide pen must cover a
    // point roughly halfway along it.
    QVERIFY(path.contains(QPointF(2.5, 0.0)));
}

void TestGerberParser::parsesBareDCodeAsRepeatOfLastCoordinate()
{
    // A bare "D03*" (or D01/D02) with no X/Y is legal RS-274X shorthand for
    // "repeat the last coordinate" - it must flash/draw at the
    // already-current point rather than being silently ignored.
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,1.0*%\n"
        "D10*\n"
        "X020000Y020000D02*\n" // move to (2.0mm, 2.0mm), no draw
        "D03*\n"; // bare flash - must reuse (2.0mm, 2.0mm), not silently no-op

    const QPainterPath path = GerberParser::parseContent(content);
    QVERIFY(!path.isEmpty());
    QVERIFY(path.contains(QPointF(2.0, 2.0)));
}

void TestGerberParser::g04CommentIsNeverParsedAsCoordinates()
{
    // G04 is RS-274X's dedicated comment code - its remaining text is
    // free-form and must never be fed into coordinate/D-code parsing, even
    // though ordinary English comment text routinely contains uppercase X/D
    // characters (from words like "MARKS"/"DRILL"/"DATA"). Round-2 review
    // finding: the previous fix's G-prefix fall-through didn't exclude G04,
    // so a comment like this could silently corrupt state.currentPoint.
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,1.0*%\n"
        "D10*\n"
        "X030000Y040000D02*\n" // move to (3.0mm, 4.0mm), no draw
        "G04 X MARKS THE DRILL DATA*\n" // comment containing X and D - must be a total no-op
        "D03*\n"; // bare flash - must still reuse the real (3.0mm, 4.0mm), not a
                  // point corrupted by parsing the comment text above

    const QPainterPath path = GerberParser::parseContent(content);
    QVERIFY(!path.isEmpty());
    QVERIFY(path.contains(QPointF(3.0, 4.0)));
    // If the comment were mis-parsed as a coordinate, X would be reset to 0
    // (decodeCoordinateMm silently returns 0.0 for non-numeric text) while Y
    // stayed at 4.0 - guard against that specific corruption too.
    QVERIFY(!path.contains(QPointF(0.0, 4.0)));
}

void TestGerberParser::parsesQuarterCircleArc()
{
    // A quarter-circle arc from (5,0) to (0,5), centered at the origin
    // (I/J are offsets from the arc's START point to the center, per the
    // Gerber spec - here that's (-5, 0), i.e. center = start + (I,J)).
    //
    // This supersedes the old g02CombinedWithCoordinatesIsANoOpForNow test
    // (Task 5): that test locked in "G02/G03 + coordinates is a no-op for
    // now" as a deliberate placeholder until this task (Task 6) added real
    // arc handling - it's now intentionally superseded rather than
    // regressed, since a no-op arc is no longer correct behavior.
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

void TestGerberParser::parsesQuarterCircleArcClockwise()
{
    // Mirror-image coverage for the G02 (clockwise) branch of drawArcStroke,
    // per Task 6 review finding: parsesQuarterCircleArc above only exercises
    // G03, leaving the CW branch (the "else if (!counterClockwise &&
    // endAngle > startAngle) endAngle -= 2*pi" unwrap) with zero direct
    // coverage. This is deliberately NOT just the G03 test with start/end
    // swapped - swapping (5,0)/(0,5) directly sweeps the short way without
    // ever needing the unwrap subtraction, so a broken CW branch would still
    // pass that shape. Instead this arc's raw atan2 endpoints straddle the
    // wrap point (start angle -90 degrees, end angle +180 degrees), so the
    // "long way forward" and "short way clockwise" disagree - only a correct
    // unwrap produces the right midpoint.
    //
    // Quarter-circle from (0,-5) to (-5,0), centered at the origin (I/J
    // offset from start (0,-5) to center (0,0) is (0,5)).
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,0.1*%\n"
        "D10*\n"
        "G75*\n"           // multi-quadrant mode
        "X000000Y-050000D02*\n"
        "G02*\n"            // clockwise arc
        "X-050000Y000000I000000J050000D01*\n";

    const QPainterPath path = GerberParser::parseContent(content);
    QVERIFY(!path.isEmpty());
    // The arc's midpoint (45 degrees around the quarter circle, clockwise
    // from -90 degrees to -180 degrees) sits at (-5/sqrt2, -5/sqrt2) ~
    // (-3.54, -3.54) - must be covered by the stroke.
    QVERIFY(path.contains(QPointF(-3.54, -3.54)));
    // If the CW branch's unwrap were broken (e.g. wrong comparison or sign),
    // the sweep would instead go the long way through positive-angle space,
    // whose midpoint sits near (3.54, 3.54) instead - assert that's NOT
    // where the stroke lands, so a regression here can't hide behind an
    // accidentally-overlapping shape.
    QVERIFY(!path.contains(QPointF(3.54, 3.54)));
}

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

void TestGerberParser::regionOpeningMoveStartsFromItsOwnDestination()
{
    // Regression test: a region's first vertex must be the destination of
    // its opening D02 move, not wherever the pen happened to be *before*
    // that move. Here a flash first leaves the pen at (10,10); the region
    // then opens with a D02 move to (0,0), the corner of an otherwise
    // ordinary 4x4mm square. If the region's first vertex were incorrectly
    // recorded as the pre-move pen position (10,10) instead of the move's
    // real destination (0,0), the resulting "polygon" would be a 5-cornered
    // shape stretching from (10,10) down to the square, and both of these
    // assertions would fail: the square's center would fall outside it
    // (odd-even winding no longer covers that area) while a point out
    // toward the stray (10,10) corner would incorrectly be covered.
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,0.1*%\n"
        "D10*\n"
        "X100000Y100000D03*\n" // flash at (10,10) - pen ends up here before the region starts
        "G36*\n"
        "X000000Y000000D02*\n" // region opening move to (0,0), NOT (10,10)
        "X040000Y000000D01*\n"
        "X040000Y040000D01*\n"
        "X000000Y040000D01*\n"
        "X000000Y000000D01*\n"
        "G37*\n";

    const QPainterPath path = GerberParser::parseContent(content);
    QVERIFY(path.contains(QPointF(2.0, 2.0)));
    QVERIFY(!path.contains(QPointF(7.0, 7.0)));
}

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

void TestGerberParser::repeatsArcStrokeInsideStepAndRepeatBlock()
{
    // Task 8 review finding: drawArcStroke (added in Task 6, after the
    // Task 8 brief's call-site list was written) unioned straight into
    // state.composited instead of going through addToComposited, so an arc
    // drawn inside an active %SR block would silently render only once
    // instead of being repeated across the panel grid - a real correctness
    // gap for panelized boards containing curved traces/pours. This locks
    // in the fix: a quarter-circle arc, repeated 2x1 with a 20mm X step,
    // must show up at both the original and the repeated position.
    //
    // Arc from (5,0) to (0,5), centered at the origin (same shape as
    // parsesQuarterCircleArc) - its midpoint sits at (5/sqrt2, 5/sqrt2) ~
    // (3.54, 3.54) for the first copy, and (23.54, 3.54) for the copy
    // repeated 20mm over in X.
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,0.1*%\n"
        "D10*\n"
        "%SRX2Y1I20.0J0.0*%\n"
        "G75*\n"           // multi-quadrant mode
        "X050000Y000000D02*\n"
        "G03*\n"            // counter-clockwise arc
        "X000000Y050000I-050000J000000D01*\n"
        "%SR*%\n"; // reset

    const QPainterPath path = GerberParser::parseContent(content);
    QVERIFY(!path.isEmpty());
    QVERIFY(path.contains(QPointF(3.54, 3.54)));
    QVERIFY(path.contains(QPointF(23.54, 3.54)));
    // And nowhere near a 3rd repeat that was never requested.
    QVERIFY(!path.contains(QPointF(43.54, 3.54)));
}

void TestGerberParser::parsesDenseLayerWithoutQuadraticBlowup()
{
    // Real-world regression, not a hypothetical: GerberParser::addToComposited
    // used to accumulate geometry via QPainterPath::united(), which recomputes
    // a true boolean union against the ENTIRE accumulated path on every single
    // flash/stroke - O(N^2) for N shapes. A dense real board layer (thousands
    // of pads) made the app hang indefinitely ("not responding") rather than
    // just render slowly; a handful of flashes in every other test here never
    // grew large enough to expose it. Fixed by switching to addPath() (append,
    // O(shape size) per call) plus Qt::WindingFill so overlapping same-
    // orientation shapes still render solid without needing true set union.
    //
    // 2000 flashes is enough to make O(N^2) behavior obvious (a naive revert
    // to united() takes tens of seconds here) while staying fast enough not
    // to slow down routine test runs.
    QString content = QStringLiteral("%FSLAX24Y24*%\n%MOMM*%\n%ADD10C,0.3*%\nD10*\n");
    constexpr int kFlashCount = 2000;
    for (int i = 0; i < kFlashCount; ++i) {
        const int xUm = (i % 100) * 20000;
        const int yUm = (i / 100) * 20000;
        content += QStringLiteral("X%1Y%2D03*\n")
                       .arg(xUm, 6, 10, QLatin1Char('0'))
                       .arg(yUm, 6, 10, QLatin1Char('0'));
    }

    QElapsedTimer timer;
    timer.start();
    const QPainterPath path = GerberParser::parseContent(content);
    const qint64 elapsedMs = timer.elapsed();

    QVERIFY(!path.isEmpty());
    // Generous bound: the fix completes this in well under 100ms; the old
    // O(N^2) united()-based code took tens of seconds for this input size.
    // 5 seconds leaves wide margin for slower CI machines while still
    // catching a return to quadratic behavior immediately.
    QVERIFY2(elapsedMs < 5000,
             qPrintable(QStringLiteral("parseContent took %1 ms for %2 flashes - "
                                        "expected well under 5000ms; possible O(N^2) regression")
                            .arg(elapsedMs)
                            .arg(kFlashCount)));
}

void TestGerberParser::apertureSizeIsConvertedFromInchesLikeCoordinatesAre()
{
    // Real-world regression: appliesFormatAndUnitsBeforeCoordinates (above)
    // already locked in that a flash's POSITION is correctly converted
    // from inches to mm. This test locks in that the aperture's own SIZE
    // is too - it wasn't: %ADD's modifier was read with a plain toDouble(),
    // no unit conversion, so a real inches-based archive's traces/pads
    // rendered at their raw inch number reinterpreted as millimeters -
    // about 25x too small, looking like thin schematic lines instead of
    // real copper.
    //
    // A 2.0-unit-diameter circle aperture, flashed at the origin, in an
    // inches file: the real diameter is 2.0in = 50.8mm, so the flash's
    // bounding box must be ~50.8mm wide, not 2.0mm.
    const QString content =
        "%FSLAX24Y24*%\n"
        "G70*\n"
        "%ADD10C,2.0*%\n"
        "D10*\n"
        "X000000Y000000D03*\n";

    const QPainterPath path = GerberParser::parseContent(content);
    QVERIFY(!path.isEmpty());
    const double width = path.boundingRect().width();
    QVERIFY2(qAbs(width - 50.8) < 0.01,
             qPrintable(QStringLiteral("expected ~50.8mm (2.0in), got %1mm").arg(width)));
}

void TestGerberParser::clearPolarityCutsAHoleInDarkGeometry()
{
    // A dark 10x10mm filled region at the origin, then a clear-polarity
    // circular aperture flashed in its middle - the flash must remove
    // area, not add more dark fill on top of it.
    //
    // The task brief's suggested construction drew the dark rectangle as 4
    // strokes with a ~2.0mm-wide pen rather than a filled region. Checked
    // that assumption before trusting it: strokes only fill the pen's own
    // path (a ~2mm-wide band along each edge), not the area they enclose,
    // so a 10x10 square's center at (5,5) would NOT actually be covered by
    // the "before" geometry - the cutout assertion would have passed for
    // the wrong reason (nothing there to begin with) rather than proving a
    // real subtraction happened. A G36/G37 filled region gives an
    // unambiguous solid dark area to cut into instead, matching the
    // existing parsesRegionFillAsClosedPolygon test's style.
    const QString content = QStringLiteral(
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,2.0*%\n"
        "%LPD*%\n"
        "G36*\n"
        "X000000Y000000D02*\n"
        "X100000Y000000D01*\n"
        "X100000Y100000D01*\n"
        "X000000Y100000D01*\n"
        "X000000Y000000D01*\n"
        "G37*\n"
        "%LPC*%\n"
        "D10*\n"
        "X050000Y050000D03*\n"
        "%LPD*%\n"
        "M02*\n");

    const QPainterPath path = GerberParser::parseContent(content);

    QVERIFY(path.contains(QPointF(1, 1)));    // still dark, outside the cutout
    QVERIFY(!path.contains(QPointF(5, 5)));   // cut out by the clear flash
}

void TestGerberParser::darkClearDarkPatternMatchesRealFileUsage()
{
    // Mirrors the real-world pattern found in customer data: a dark
    // region, then a clear region cut into it, then dark again - not just
    // a single dark/clear pair, to catch an off-by-one in group
    // sequencing (e.g. an implementation that only handles exactly 2
    // groups).
    //
    // Three sequential 10x10mm regions via G36/G37:
    //  - dark region A: (0,0)-(10,10), unique interior point (1,1)
    //  - clear region B: (3,3)-(7,7), entirely inside A - cuts a hole in
    //    A's middle; its center (5,5) must end up NOT contained
    //  - dark region C: (20,20)-(30,30), a separate area entirely, unique
    //    interior point (25,25) - must remain dark, proving the parser
    //    resumed accumulating a THIRD group rather than treating the file
    //    as done after the single dark/clear pair
    const QString content = QStringLiteral(
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%LPD*%\n"
        "G36*\n"
        "X000000Y000000D02*\n"
        "X100000Y000000D01*\n"
        "X100000Y100000D01*\n"
        "X000000Y100000D01*\n"
        "X000000Y000000D01*\n"
        "G37*\n"
        "%LPC*%\n"
        "G36*\n"
        "X030000Y030000D02*\n"
        "X070000Y030000D01*\n"
        "X070000Y070000D01*\n"
        "X030000Y070000D01*\n"
        "X030000Y030000D01*\n"
        "G37*\n"
        "%LPD*%\n"
        "G36*\n"
        "X200000Y200000D02*\n"
        "X300000Y200000D01*\n"
        "X300000Y300000D01*\n"
        "X200000Y300000D01*\n"
        "X200000Y200000D01*\n"
        "G37*\n");

    const QPainterPath path = GerberParser::parseContent(content);

    QVERIFY(path.contains(QPointF(1, 1)));      // region A, outside the cutout
    QVERIFY(!path.contains(QPointF(5, 5)));     // region B's area - cut out of A
    QVERIFY(path.contains(QPointF(25, 25)));    // region C - a fresh dark group after the cut
}

void TestGerberParser::overlappingSamePolarityShapesSurviveTerminalBooleanOp()
{
    // Review finding: every other polarity test above uses exactly one
    // shape per polarity group, so none of them exercise what happens when
    // a group's own multi-shape, possibly-overlapping geometry (built via
    // cheap addPath() + Qt::WindingFill accumulation) passes through the
    // real terminal boolean op (united()/subtracted()) that runs once the
    // group closes out. A silent area-loss bug at an overlap boundary -
    // e.g. Qt's boolean op mis-resolving a WindingFill self-overlap - would
    // pass every other test here undetected.
    //
    // Two overlapping dark flashes (3.0mm-diameter circles, centers 1.5mm
    // apart so they overlap), followed by a %LPC transition that forces
    // the dark group through united() at the end. The overlap zone must
    // still read as solid dark afterward.
    const QString content = QStringLiteral(
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,3.0*%\n"
        "%LPD*%\n"
        "D10*\n"
        "X000000Y000000D03*\n" // dark circle centered at (0,0), r=1.5mm
        "X150000Y000000D03*\n" // dark circle centered at (1.5,0), r=1.5mm - overlaps the first
        "%LPC*%\n"
        "X200000Y200000D03*\n" // clear flash far away - just forces the dark group's boolean op to run
        "%LPD*%\n"
        "M02*\n");

    const QPainterPath path = GerberParser::parseContent(content);

    // (0.75, 0) is 0.75mm from each circle's center - inside BOTH circles
    // (radius 1.5mm), i.e. squarely in the overlap zone.
    QVERIFY(path.contains(QPointF(0.75, 0.0)));
}

void TestGerberParser::fileOpeningWithClearPolarityStaysEmpty()
{
    // Edge case flagged in review: if the very first %LP directive in a
    // file is %LPC (clear) rather than the usual %LPD default, the
    // initial (empty) Dark group at polarityGroups[0] must still combine
    // correctly - subtracting a clear shape from nothing must leave
    // nothing, not crash or produce a spurious full-canvas fill.
    const QString content = QStringLiteral(
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,2.0*%\n"
        "%LPC*%\n"
        "D10*\n"
        "X000000Y000000D03*\n"
        "M02*\n");

    const QPainterPath path = GerberParser::parseContent(content);

    QVERIFY(path.isEmpty());
    QVERIFY(!path.contains(QPointF(0.0, 0.0)));
}

void TestGerberParser::consecutiveSamePolarityDirectivesDoNotBreakLaterTransitionTracking()
{
    // Edge case flagged in review: two "%LPD*%" directives in a row (no
    // %LPC in between) is redundant but legal - a real file might emit it
    // from a re-stated default, or from tooling that always writes an %LP
    // before every sub-block. The redundant repeat must not corrupt
    // currentPolarity tracking - if it did (e.g. by spuriously toggling
    // state so the LATER, real %LPC transition is no longer recognized as
    // a change), the clear cutouts below would silently fail to cut. That
    // would show up here as the dark flashes wrongly remaining solid, not
    // just as a missed performance optimization from an extra empty group.
    const QString content = QStringLiteral(
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,3.0*%\n"
        "%LPD*%\n"
        "D10*\n"
        "X000000Y000000D03*\n" // dark flash #1 at (0,0)
        "%LPD*%\n"              // redundant repeat - same polarity, must be a no-op
        "X100000Y000000D03*\n" // dark flash #2 at (10,0)
        "%LPC*%\n"
        "X000000Y000000D03*\n" // clear flash cutting out #1's center
        "X100000Y000000D03*\n" // clear flash cutting out #2's center
        "%LPD*%\n"
        "M02*\n");

    const QPainterPath path = GerberParser::parseContent(content);

    QVERIFY(!path.contains(QPointF(0.0, 0.0)));
    QVERIFY(!path.contains(QPointF(10.0, 0.0)));
}

void TestGerberParser::singleFlashProducesOneFlashObjectAtTheRightPosition()
{
    // The most basic object-model assertion: one D03 flash becomes exactly
    // one Flash GraphicObject, at the flash point in integer nanometers.
    // X24 format + %MOMM means "010000" decodes to 1.0000mm; the expected
    // position is built from GerberFormat::mmToNm(1.0) rather than copied
    // from whatever the parser emits, so this test can actually catch a
    // wrong-position bug instead of just echoing the code.
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,1.0*%\n"
        "D10*\n"
        "X010000Y010000D03*\n";

    const Layer layer = GerberParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    const GraphicObject &object = layer.objects.at(0);
    QCOMPARE(object.type, ObjectType::Flash);
    QCOMPARE(object.polarity, Polarity::Dark);
    QCOMPARE(object.apertureCode, 10);
    QCOMPARE(object.position.x, GerberFormat::mmToNm(1.0));
    QCOMPARE(object.position.y, GerberFormat::mmToNm(1.0));
}

void TestGerberParser::clearPolarityObjectGetsClearPolarity()
{
    // A flash preceded by %LPC*% must carry Polarity::Clear on the object
    // itself - the object model records polarity per-object rather than as
    // a live grouping the way the old parse did.
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,1.0*%\n"
        "%LPC*%\n"
        "D10*\n"
        "X000000Y000000D03*\n";

    const Layer layer = GerberParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    QCOMPARE(layer.objects.at(0).polarity, Polarity::Clear);
}

void TestGerberParser::stepAndRepeatMaterializesOneObjectPerGridCell()
{
    // %SR must be materialized at parse time into one concrete object per
    // grid cell (N x M actual objects), not a template + repeat count. A
    // 2x2 grid with a 10mm step around one flash therefore yields 4 Flash
    // objects at 4 distinct positions.
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,1.0*%\n"
        "%SRX2Y2I10.0J10.0*%\n"
        "D10*\n"
        "X000000Y000000D03*\n"
        "%SR*%\n";

    const Layer layer = GerberParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 4);

    // The four expected cell origins, in nanometers: (0,0), (10,0), (0,10),
    // (10,10) millimeters. Order isn't asserted (all four share one
    // polarity, so grid order is immaterial downstream) - only that each
    // expected position appears exactly once and every object is a Flash.
    const qint64 zero = 0;
    const qint64 ten = GerberFormat::mmToNm(10.0);
    QVector<QPair<qint64, qint64>> expected{{zero, zero}, {ten, zero}, {zero, ten}, {ten, ten}};
    for (const GraphicObject &object : layer.objects) {
        QCOMPARE(object.type, ObjectType::Flash);
        const int found = expected.removeAll(qMakePair(object.position.x, object.position.y));
        QCOMPARE(found, 1); // present exactly once - catches duplicates/wrong offsets
    }
    QVERIFY(expected.isEmpty()); // all four matched
}

void TestGerberParser::arcStrokeObjectCarriesCenterAndDirection()
{
    // A G03 (counter-clockwise) arc draw becomes one Stroke object with
    // isArc set, its absolute arc center resolved from the I/J start-offset,
    // and the direction flag preserved. Quarter circle from (5,0) to (0,5)
    // about the origin: I/J = (-5,0) from the start point, so center = (0,0).
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,0.1*%\n"
        "D10*\n"
        "G75*\n"
        "X050000Y000000D02*\n"
        "G03*\n"
        "X000000Y050000I-050000J000000D01*\n";

    const Layer layer = GerberParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    const GraphicObject &object = layer.objects.at(0);
    QCOMPARE(object.type, ObjectType::Stroke);
    QCOMPARE(object.isArc, true);
    QCOMPARE(object.arcCounterClockwise, true);
    QCOMPARE(object.position.x, GerberFormat::mmToNm(5.0)); // from (5,0)
    QCOMPARE(object.position.y, GerberFormat::mmToNm(0.0));
    QCOMPARE(object.strokeTo.x, GerberFormat::mmToNm(0.0)); // to (0,5)
    QCOMPARE(object.strokeTo.y, GerberFormat::mmToNm(5.0));
    QCOMPARE(object.arcCenter.x, GerberFormat::mmToNm(0.0)); // center (0,0)
    QCOMPARE(object.arcCenter.y, GerberFormat::mmToNm(0.0));
}

void TestGerberParser::regionObjectCarriesAllVertices()
{
    // A G36/G37 region becomes one Region object whose regionPoints hold the
    // collected contour vertices in order - the opening D02 move's
    // destination first, then each D01 vertex, not auto-closed (closing is
    // the flatten stage's job). A 4x4mm square drawn back to its start yields
    // 5 vertices: (0,0),(4,0),(4,4),(0,4),(0,0).
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

    const Layer layer = GerberParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    const GraphicObject &object = layer.objects.at(0);
    QCOMPARE(object.type, ObjectType::Region);

    const qint64 zero = GerberFormat::mmToNm(0.0);
    const qint64 four = GerberFormat::mmToNm(4.0);
    QVector<QPair<qint64, qint64>> expected{{zero, zero}, {four, zero}, {four, four}, {zero, four}, {zero, zero}};
    QCOMPARE(object.regionPoints.size(), expected.size());
    for (int i = 0; i < expected.size(); ++i) {
        QCOMPARE(object.regionPoints.at(i).x, expected.at(i).first);
        QCOMPARE(object.regionPoints.at(i).y, expected.at(i).second);
    }
}

void TestGerberParser::g54ApertureSelectPrefixIsHonored()
{
    // "G54D41*" is RS-274D's legacy "select aperture" form - deprecated
    // in RS-274X but still emitted by real CAM tools (found in a real
    // supplier archive, TARGET 3001! output, where every aperture select
    // in the file used it). The G54 prefix must be stripped and the
    // D-code applied; dropping the whole statement leaves every flash on
    // a zero-size default aperture, rendering the file as "no drawable
    // content".
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD41C,2.0*%\n"
        "G54D41*\n"
        "X010000Y010000D03*\n"
        "M02*\n";

    const Layer layer = GerberParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    QCOMPARE(layer.objects.at(0).type, ObjectType::Flash);
    QCOMPARE(layer.objects.at(0).apertureCode, 41);
}

void TestGerberParser::unpaddedGCodesAreHonored()
{
    // RS-274X makes leading zeros in function codes optional: "G1" means
    // G01, "D2" means D02. Real CAMMaster Designer exports (a real
    // supplier board-frame layer) write "G1X...D2*" for every move; if
    // the parser only recognizes the padded "G01" spelling, all those
    // moves are silently dropped and the layer renders as disconnected
    // dots and fragments from stale pen positions.
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,0.5*%\n"
        "D10*\n"
        "G1X010000Y010000D2*\n"
        "X020000Y010000D1*\n"
        "M02*\n";

    const Layer layer = GerberParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    const GraphicObject &stroke = layer.objects.at(0);
    QCOMPARE(stroke.type, ObjectType::Stroke);
    QCOMPARE(stroke.position.x, GerberFormat::mmToNm(1.0));
    QCOMPARE(stroke.position.y, GerberFormat::mmToNm(1.0));
    QCOMPARE(stroke.strokeTo.x, GerberFormat::mmToNm(2.0));
    QCOMPARE(stroke.strokeTo.y, GerberFormat::mmToNm(1.0));
}

void TestGerberParser::operationCodeIsModalOnBareCoordinates()
{
    // RS-274D operation codes are modal: after "X...D1*", following
    // coordinate statements with NO D-code at all ("Y166772*",
    // "X128858Y167795*") continue the same operation. Real CAMMaster
    // Designer board-frame layers draw nearly everything this way;
    // dropping the bare statements loses both the drawn segments AND the
    // pen movement, leaving disconnected fragments.
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,0.5*%\n"
        "D10*\n"
        "X010000Y010000D2*\n"
        "X020000Y010000D1*\n"
        "X030000*\n"
        "M02*\n";

    const Layer layer = GerberParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 2);
    const GraphicObject &second = layer.objects.at(1);
    QCOMPARE(second.type, ObjectType::Stroke);
    QCOMPARE(second.position.x, GerberFormat::mmToNm(2.0));
    QCOMPARE(second.position.y, GerberFormat::mmToNm(1.0));
    QCOMPARE(second.strokeTo.x, GerberFormat::mmToNm(3.0));
    QCOMPARE(second.strokeTo.y, GerberFormat::mmToNm(1.0));
}

void TestGerberParser::multiQuadrantArcWithEqualEndpointsIsAFullCircle()
{
    // G75 multi-quadrant mode: an arc draw whose end point equals its
    // start is a full 360-degree circle (Gerber spec 4.7) - the standard
    // way CAM tools draw round cutouts on mechanical layers. It must be
    // emitted as an arc object with equal endpoints (the renderer draws
    // the full ring from that), not collapse to a zero-length dot.
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,0.2*%\n"
        "D10*\n"
        "G75*\n"
        "X020000Y010000D2*\n"
        "G03X020000Y010000I-10000J0D1*\n"
        "M02*\n";

    const Layer layer = GerberParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    const GraphicObject &arc = layer.objects.at(0);
    QCOMPARE(arc.type, ObjectType::Stroke);
    QVERIFY(arc.isArc);
    QCOMPARE(arc.position.x, arc.strokeTo.x);
    QCOMPARE(arc.position.y, arc.strokeTo.y);
    // Center = start + (I,J) = (2,1) + (-1,0) = (1,1)mm.
    QCOMPARE(arc.arcCenter.x, GerberFormat::mmToNm(1.0));
    QCOMPARE(arc.arcCenter.y, GerberFormat::mmToNm(1.0));
}

void TestGerberParser::singleQuadrantArcResolvesUnsignedCenterOffsets()
{
    // G74 single-quadrant mode: I/J are UNSIGNED distances - the parser
    // must pick the signs that give a geometrically valid arc of at most
    // 90 degrees in the commanded direction. Here: CCW quarter arc from
    // (1,0) to (0,1) around (0,0), written with unsigned "I10000J0".
    // Treating I as +1 puts the center at (2,0), where the radii don't
    // even match - the signs must resolve to (-1,0) giving center (0,0).
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,0.2*%\n"
        "D10*\n"
        "G74*\n"
        "X010000Y000000D2*\n"
        "G03X000000Y010000I10000J0D1*\n"
        "M02*\n";

    const Layer layer = GerberParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    const GraphicObject &arc = layer.objects.at(0);
    QVERIFY(arc.isArc);
    QCOMPARE(arc.arcCenter.x, GerberFormat::mmToNm(0.0));
    QCOMPARE(arc.arcCenter.y, GerberFormat::mmToNm(0.0));
}

void TestGerberParser::macroBodyStopsAtApertureAttribute()
{
    const QString content = "%FSLAX46Y46*%\n%MOMM*%\n"
                            "%AMBOX*\n21,1,$1,$2,0,0,0*%\n"
                            "%TA.AperFunction,SMDPad*%\n"
                            "%ADD10BOX,1X2*%\n"
                            "D10*\nX0Y0D03*\nM02*\n";
    const Layer layer = GerberParser::parseContentToObjects(content);
    QVERIFY(layer.macros.contains("BOX"));
    QCOMPARE(layer.macros.value("BOX").statements, QStringList({"21,1,$1,$2,0,0,0"}));
}

void TestGerberParser::macroClosedOnItsOwnLineKeepsItsPrimitive()
{
    // The closing '%' on its own line leaves an empty statement after the
    // last '*'. It ends the macro body but adds nothing to it.
    const QString content = "%FSLAX46Y46*%\n%MOMM*%\n"
                            "%AMOC8*\n5,1,8,0,0,1.08239X$1,22.5*\n%\n"
                            "%ADD10OC8,0.5*%\n"
                            "D10*\nX0Y0D03*\nM02*\n";
    const Layer layer = GerberParser::parseContentToObjects(content);
    QVERIFY(layer.macros.contains("OC8"));
    QCOMPARE(layer.macros.value("OC8").statements, QStringList({"5,1,8,0,0,1.08239X$1,22.5"}));
    QCOMPARE(layer.objects.size(), 1);
}

void TestGerberParser::arcWithOnlyXKeepsItsX()
{
    // "X..I..J..D01" with no Y: X must stop at the I, not swallow the
    // offsets (which made it unreadable, so it silently became 0). Half
    // circle from (5,0) to (-5,0) about the origin.
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,0.1*%\n"
        "D10*\n"
        "G75*\n"
        "X050000Y000000D02*\n"
        "G03X-050000I-050000J000000D01*\n";

    const Layer layer = GerberParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    QCOMPARE(layer.objects.at(0).strokeTo.x, GerberFormat::mmToNm(-5.0));
    QCOMPARE(layer.objects.at(0).strokeTo.y, GerberFormat::mmToNm(0.0));
}

void TestGerberParser::regionArcWithOnlyXKeepsItsX()
{
    // The same form as a region edge - a corner read as X=0 twists the
    // outline. Square (1,1)-(5,5) whose top edge is an arc to (1,5).
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "G75*\n"
        "G36*\n"
        "X010000Y010000D02*\n"
        "G01X050000Y010000D01*\n"
        "X050000Y050000D01*\n"
        "G03X010000I-020000J000000D01*\n"
        "G01X010000Y010000D01*\n"
        "G37*\n";

    const Layer layer = GerberParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    const QVector<PointNm> &points = layer.objects.at(0).regionPoints;
    QCOMPARE(points.size(), 5);
    QCOMPARE(points.at(3).x, GerberFormat::mmToNm(1.0));
    QCOMPARE(points.at(3).y, GerberFormat::mmToNm(5.0));
}

namespace {

// Square (1,1)-(5,5) whose top edge is a CCW half circle from (5,5) to
// (1,5) about (3,5), bulging up to y=7. `steps` goes before the region
// (e.g. a %SR block start).
QString squareWithArcTop(const QString &before = QString(), const QString &after = QString())
{
    return "%FSLAX24Y24*%\n"
           "%MOMM*%\n"
           "G75*\n"
        + before
        + "G36*\n"
          "X010000Y010000D02*\n"
          "G01X050000Y010000D01*\n"
          "X050000Y050000D01*\n"
          "G03X010000Y050000I-020000J000000D01*\n"
          "G01X010000Y010000D01*\n"
          "G37*\n"
        + after;
}

} // namespace

void TestGerberParser::regionArcEdgeIsKept()
{
    // Each region vertex records how the outline reaches it: the arc edge
    // keeps its absolute center and direction, the straight ones do not.
    const Layer layer = GerberParser::parseContentToObjects(squareWithArcTop());

    QCOMPARE(layer.objects.size(), 1);
    const GraphicObject &region = layer.objects.at(0);
    QCOMPARE(region.regionEdges.size(), region.regionPoints.size());
    for (int i = 0; i < region.regionEdges.size(); ++i) {
        QCOMPARE(region.regionEdges.at(i).isArc, i == 3);
    }
    QCOMPARE(region.regionEdges.at(3).arcCenter.x, GerberFormat::mmToNm(3.0));
    QCOMPARE(region.regionEdges.at(3).arcCenter.y, GerberFormat::mmToNm(5.0));
    QVERIFY(region.regionEdges.at(3).arcCounterClockwise);
}

void TestGerberParser::regionArcIsDrawnAsACurve()
{
    // (3,6.5) lies under the bulge: inside the curved outline, outside the
    // straight chord the arc used to be reduced to. (3,7.2) is above it.
    const QPainterPath path = GerberParser::parseContent(squareWithArcTop());
    QVERIFY(path.contains(QPointF(3.0, 6.5)));
    QVERIFY(path.contains(QPointF(3.0, 3.0)));
    QVERIFY(!path.contains(QPointF(3.0, 7.2)));
}

void TestGerberParser::singleQuadrantRegionArcResolvesItsCenter()
{
    // G74: a quarter disc, the arc from (2,0) to (0,2) written with the
    // unsigned offset I2. The center must resolve to (0,0), not (4,0).
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "G74*\n"
        "G36*\n"
        "X000000Y000000D02*\n"
        "G01X020000Y000000D01*\n"
        "G03X000000Y020000I020000J000000D01*\n"
        "G01X000000Y000000D01*\n"
        "G37*\n";

    const Layer layer = GerberParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    const RegionEdge &arc = layer.objects.at(0).regionEdges.at(2);
    QVERIFY(arc.isArc);
    QCOMPARE(arc.arcCenter.x, GerberFormat::mmToNm(0.0));
    QCOMPARE(arc.arcCenter.y, GerberFormat::mmToNm(0.0));
    // Inside the quarter disc, beyond the chord from (2,0) to (0,2).
    QVERIFY(GerberParser::parseContent(content).contains(QPointF(1.2, 1.2)));
}

void TestGerberParser::singleQuadrantCornerJustPastAQuarterTurnKeepsItsCenter()
{
    // A rounded corner whose end point the file rounded a hair past the
    // quarter turn: (2,0) to (-0.001,2) around (0,0) is 90.03 degrees. A
    // strict 90-degree limit rejected every sign choice and fell back to
    // the unsigned offset, putting the center at (4,0) and drawing a near-
    // full loop that reaches out to x = 6.
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "G74*\n"
        "G36*\n"
        "X000000Y000000D02*\n"
        "G01X020000Y000000D01*\n"
        "G03X-000010Y020000I020000J000000D01*\n"
        "G01X000000Y000000D01*\n"
        "G37*\n";

    const RegionEdge arc = GerberParser::parseContentToObjects(content).objects.at(0).regionEdges.at(2);
    QVERIFY(arc.isArc);
    QCOMPARE(arc.arcCenter.x, GerberFormat::mmToNm(0.0));
    QCOMPARE(arc.arcCenter.y, GerberFormat::mmToNm(0.0));
    const QPainterPath path = GerberParser::parseContent(content);
    QVERIFY(path.contains(QPointF(1.2, 1.2)));
    QVERIFY(path.boundingRect().right() < 2.1);
}

void TestGerberParser::singleQuadrantClockwiseArcJustPastAQuarterTurnKeepsItsCenter()
{
    // The same rounding, clockwise, on an arc stroke: (0,2) to (2,-0.001)
    // around (0,0). The wrong center would be (0,4).
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,0.2*%\n"
        "D10*\n"
        "G74*\n"
        "X000000Y020000D2*\n"
        "G02X020000Y-000010I000000J020000D1*\n"
        "M02*\n";

    const Layer layer = GerberParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    QVERIFY(layer.objects.at(0).isArc);
    QCOMPARE(layer.objects.at(0).arcCenter.x, GerberFormat::mmToNm(0.0));
    QCOMPARE(layer.objects.at(0).arcCenter.y, GerberFormat::mmToNm(0.0));
}

void TestGerberParser::singleQuadrantArcTakesTheShortWayRound()
{
    // From (0,0) to (2,0) with offsets 1,1: both (1,1) and (1,-1) are 1.41
    // from either end, so the distances cannot choose. Counterclockwise,
    // (1,1) gives a 90-degree arc and (1,-1) a 270-degree one - G74 means
    // the short one.
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,0.2*%\n"
        "D10*\n"
        "G74*\n"
        "X000000Y000000D2*\n"
        "G03X020000Y000000I010000J010000D1*\n"
        "M02*\n";

    const GraphicObject arc = GerberParser::parseContentToObjects(content).objects.at(0);
    QVERIFY(arc.isArc);
    QCOMPARE(arc.arcCenter.x, GerberFormat::mmToNm(1.0));
    QCOMPARE(arc.arcCenter.y, GerberFormat::mmToNm(1.0));
}

void TestGerberParser::singleQuadrantArcWithEqualEndsIsNotARing()
{
    // Equal end points make a full circle only in G75. A G74 arc spans at
    // most a quarter turn, so this draws nothing more than a straight
    // stroke of zero length would.
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "%ADD10C,0.2*%\n"
        "D10*\n"
        "G74*\n"
        "X010000Y000000D2*\n"
        "G03X010000Y000000I010000J000000D1*\n"
        "M02*\n";

    const Layer layer = GerberParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    QCOMPARE(layer.objects.at(0).type, ObjectType::Stroke);
    QVERIFY(!layer.objects.at(0).isArc);
}

void TestGerberParser::singleQuadrantArcWithNoShortWayRoundIsStraight()
{
    // Zero offsets put the center on the start point: from (2,0) to (2,-1)
    // counterclockwise that is a 270-degree turn, and no sign choice
    // changes it. No G74 arc turns that far, so the edge is drawn straight.
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "G74*\n"
        "G36*\n"
        "X000000Y000000D02*\n"
        "G01X020000Y000000D01*\n"
        "G03X020000Y-010000I000000J000000D01*\n"
        "G01X000000Y000000D01*\n"
        "G37*\n";

    const GraphicObject region = GerberParser::parseContentToObjects(content).objects.at(0);
    QVERIFY(!region.regionEdges.at(2).isArc);
    QCOMPARE(region.regionEdges.at(2).arcCenter.x, 0);
}

void TestGerberParser::fullCircleRegionIsEmitted()
{
    // A round region: one D02 start, one full-circle arc back to it. Only
    // two vertices, but the arc encloses a disc of radius 2 about (0,0).
    const QString content =
        "%FSLAX24Y24*%\n"
        "%MOMM*%\n"
        "G75*\n"
        "G36*\n"
        "X020000Y000000D02*\n"
        "G03X020000Y000000I-020000J000000D01*\n"
        "G37*\n";

    QCOMPARE(GerberParser::parseContentToObjects(content).objects.size(), 1);
    const QPainterPath path = GerberParser::parseContent(content);
    QVERIFY(path.contains(QPointF(0.0, 0.0)));
    QVERIFY(path.contains(QPointF(-1.5, 0.0)));
    QVERIFY(!path.contains(QPointF(2.5, 0.0)));
}

void TestGerberParser::stepAndRepeatMovesRegionArcCenters()
{
    // Two copies, 10 mm apart in X: the copy's arc center moves with it.
    const Layer layer = GerberParser::parseContentToObjects(
        squareWithArcTop("%SRX2Y1I10J0*%\n", "%SR*%\n"));

    QCOMPARE(layer.objects.size(), 2);
    QCOMPARE(layer.objects.at(1).regionEdges.at(3).arcCenter.x, GerberFormat::mmToNm(13.0));
    QCOMPARE(layer.objects.at(1).regionEdges.at(3).arcCenter.y, GerberFormat::mmToNm(5.0));
    // A straight edge has no center to move.
    QCOMPARE(layer.objects.at(1).regionEdges.at(1).arcCenter.x, 0);
}

QTEST_MAIN(TestGerberParser)
#include "test_gerberparser.moc"
