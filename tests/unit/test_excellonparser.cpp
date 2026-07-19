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
    void bareUnitsLineNowResolvesNonDecimalCoordinateUsingDefaults();
    void headerOnlyFileProducesNoObjects();
    void metricLeadingZeroOmittedDecodesCorrectly();
    void metricTrailingZeroOmittedDecodesCorrectly();
    void inchLeadingZeroOmittedDecodesCorrectly();
    void inchTrailingZeroOmittedDecodesCorrectly();
    void fileFormatCommentOverridesUnitLineDigitDefaults();
    void stillThrowsWhenTrulyNoFormatInfoPresent();
    void linearSlotBetweenM15AndM16ProducesOneStroke();
    void rapidMoveBeforeM15DrawsNothing();
    void clockwiseArcSlotProducesArcStroke();
    void counterClockwiseArcSlotSetsCounterClockwiseFlag();
    void modalCoordinateAfterG01ReusesLinearMode();
    void drillHoleStillWorksAfterG05ReturnsToDrillMode();
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

void TestExcellonParser::bareUnitsLineNowResolvesNonDecimalCoordinateUsingDefaults()
{
    // This was Task 1's regression test pinning down that a non-decimal
    // coordinate still threw AmbiguousFormatError even with a bare METRIC/
    // INCH header line present - that was a deliberate Task 1 scope
    // boundary (explicit-decimal coordinates only), not a permanent
    // guarantee. Task 2's whole point is to resolve exactly this case, so
    // the assertion flips here instead of leaving a test that would
    // otherwise permanently fail post-Task-2: a bare "METRIC" line (no
    // ,LZ/,TZ suffix) defaults to LZ (leading zero omitted, see
    // tryParseUnitsLine), and "015000" is exactly METRIC's default 3:3
    // digit width, so it now decodes to 15.000mm on both axes instead of
    // throwing. See stillThrowsWhenTrulyNoFormatInfoPresent() below for
    // the still-ambiguous case (no units line, no FILE_FORMAT comment at
    // all), which is what remains unresolvable.
    const QString content =
        "M48\n"
        "METRIC\n"
        "T01C0.300\n"
        "%\n"
        "T01\n"
        "X015000Y015000\n"
        "M30\n";

    const Layer layer = ExcellonParser::parseContentToObjects(content);

    QCOMPARE(layer.objects.size(), 1);
    QCOMPARE(layer.objects[0].position.x, GerberFormat::mmToNm(15.0));
    QCOMPARE(layer.objects[0].position.y, GerberFormat::mmToNm(15.0));
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

QTEST_MAIN(TestExcellonParser)
#include "test_excellonparser.moc"
