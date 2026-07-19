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
    void nonDecimalCoordinateStillThrowsEvenWithUnitsLinePresent();
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

void TestExcellonParser::nonDecimalCoordinateStillThrowsEvenWithUnitsLinePresent()
{
    // Regression test: non-decimal coordinates must throw AmbiguousFormatError
    // even when a METRIC or INCH header line is present. Task 1's scope is
    // explicit-decimal coordinates only; zero-suppression resolution is Task 2.
    const QString content =
        "M48\n"
        "METRIC\n"
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
