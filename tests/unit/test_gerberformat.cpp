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
    void decodesMillimeterCoordinateToExactNanometers();
    void decodesInchCoordinateToNanometersWithDocumentedRounding();
    void inchCoordinateRoundingIsHalfAwayFromZero();
    void handlesRealisticHighDecimalDigitCountWithoutOverflow();
    void mmToNmAndNmToMmRoundTrip();
    void decodeCoordinateMmUnchangedAfterRefactor();
    void decodesNegativeCoordinateToNanometers();
    void decodesZeroCoordinateToNanometers();
    void viewAndStringDecodeAgree();
    void oddTokensDecodeToPinnedValues();
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

void TestGerberFormat::decodesMillimeterCoordinateToExactNanometers()
{
    // %FSLAX24Y24*% style: 2 integer digits, 4 decimal digits, mm-native.
    GerberFormat format;
    format.integerDigits = 2;
    format.decimalDigits = 4;
    format.leadingZeroOmitted = true;
    format.unitsAreInches = false;

    // "010000" -> 1.0000 in file units (mm) -> exactly 1,000,000 nm.
    QCOMPARE(format.decodeCoordinateNm("010000"), qint64(1'000'000));
}

void TestGerberFormat::decodesInchCoordinateToNanometersWithDocumentedRounding()
{
    GerberFormat format;
    format.integerDigits = 2;
    format.decimalDigits = 4;
    format.leadingZeroOmitted = true;
    format.unitsAreInches = true;

    // "010000" -> 1.0000 inch -> exactly 25,400,000 nm (25.4mm/inch is
    // exact, and this particular decimalDigits count happens to divide
    // evenly here too - see the next test for a case where rounding
    // actually engages).
    QCOMPARE(format.decodeCoordinateNm("010000"), qint64(25'400'000));
}

void TestGerberFormat::inchCoordinateRoundingIsHalfAwayFromZero()
{
    GerberFormat format;
    format.integerDigits = 3;
    format.decimalDigits = 6;
    format.leadingZeroOmitted = true;
    format.unitsAreInches = true;

    // Hand computation (done BEFORE running any code - see task brief):
    //
    // "000002" -> rawValue = 2, i.e. the coordinate is 2 * 10^-6 inch in
    // file-native units (rawValue is the digit string interpreted as an
    // integer, scaled by 10^decimalDigits = 10^6).
    //
    // nmPerFileUnit = kNanometersPerInch = 25,400,000 (exact: 1 inch =
    // 25.4mm = 25,400,000nm).
    //
    // product = rawValue * nmPerFileUnit = 2 * 25,400,000 = 50,800,000.
    // divisor = 10^decimalDigits = 10^6 = 1,000,000.
    // 50,800,000 / 1,000,000 = 50 remainder 800,000 (exactly 50.8).
    //
    // 800,000 is more than half of the 1,000,000 divisor (500,000), so
    // round-half-away-from-zero rounds UP (away from zero, since the
    // value is positive) to 51 nm - NOT 50 nm, which is what plain
    // truncating integer division would give. This is the case that
    // actually exercises the rounding path (contrast with the previous
    // test's "010000", which divides evenly and never engages it).
    //
    // Expected result: 51 nm.
    QCOMPARE(format.decodeCoordinateNm("000002"), qint64(51));
}

void TestGerberFormat::handlesRealisticHighDecimalDigitCountWithoutOverflow()
{
    // A 3.6-style format (3 integer digits, 6 decimal digits) - confirms
    // no overflow/wraparound at the higher end of realistic real-world
    // %FS precision.
    GerberFormat format;
    format.integerDigits = 3;
    format.decimalDigits = 6;
    format.leadingZeroOmitted = true;
    format.unitsAreInches = false;

    // "123456789" -> rawValue 123,456,789 -> 123.456789 mm (dividing by
    // 10^decimalDigits = 10^6). Converting back to nm multiplies by
    // kNanometersPerMillimeter = 10^6 - the two 10^6 factors cancel
    // exactly (decimalDigits happens to equal 6 here), so the nanometer
    // value is numerically identical to rawValue: 123.456789 mm ==
    // 123,456,789 nm exactly (123mm = 123,000,000nm, plus
    // 0.456789mm = 456,789nm).
    QCOMPARE(format.decodeCoordinateNm("123456789"), qint64(123'456'789));
}

void TestGerberFormat::mmToNmAndNmToMmRoundTrip()
{
    QCOMPARE(GerberFormat::mmToNm(1.0), qint64(1'000'000));
    QCOMPARE(GerberFormat::mmToNm(0.0), qint64(0));
    QVERIFY(qAbs(GerberFormat::nmToMm(1'000'000) - 1.0) < 1e-9);
}

void TestGerberFormat::decodeCoordinateMmUnchangedAfterRefactor()
{
    // Regression guard: the extractDigits refactor (Task 1 Step 2) must
    // not change decodeCoordinateMm's existing behavior at all. Re-assert
    // one already-covered case here as an explicit tripwire, in addition
    // to running the full pre-existing test suite.
    GerberFormat format;
    format.integerDigits = 2;
    format.decimalDigits = 4;
    format.leadingZeroOmitted = true;
    format.unitsAreInches = false;
    QVERIFY(qAbs(format.decodeCoordinateMm("010000") - 1.0) < 1e-9);
}

void TestGerberFormat::decodesNegativeCoordinateToNanometers()
{
    // Mirrors decodesNegativeCoordinate (the pre-existing decodeCoordinateMm
    // negative-sign test, test_gerberformat.cpp:49-57) but for the integer
    // nm path - decodeCoordinateNm's sign handling (negate the *rounded
    // integer* after the fact: `decoded.negative ? -scaled : scaled`) is
    // structurally different code from decodeCoordinateMm's negation
    // (negate a double before unit conversion), so it needs its own test
    // rather than relying on the mm-path test to cover it by proxy.
    GerberFormat format;
    format.integerDigits = 2;
    format.decimalDigits = 4;
    format.leadingZeroOmitted = true;
    format.unitsAreInches = false;

    // Hand computation: "-010000" -> sign stripped -> digits "010000" ->
    // rawValue = 10000 (i.e. 1.0000 mm scaled by 10^decimalDigits = 10^4).
    // nmPerFileUnit = kNanometersPerMillimeter = 1,000,000.
    // product = 10000 * 1,000,000 = 10,000,000,000.
    // divisor = 10^4 = 10,000.
    // 10,000,000,000 / 10,000 = 1,000,000 exactly (no remainder, so
    // rounding is a no-op here - this test is purely about the sign).
    // Negative sign reapplied: -1,000,000 nm.
    QCOMPARE(format.decodeCoordinateNm("-010000"), qint64(-1'000'000));
}

void TestGerberFormat::decodesZeroCoordinateToNanometers()
{
    // All-zero token: rawValue = 0, so scaled = 0 regardless of unit or
    // rounding - a one-line sanity check on the zero edge case.
    GerberFormat format;
    format.integerDigits = 2;
    format.decimalDigits = 4;
    format.leadingZeroOmitted = true;
    format.unitsAreInches = false;

    QCOMPARE(format.decodeCoordinateNm("000000"), qint64(0));
}

void TestGerberFormat::viewAndStringDecodeAgree()
{
    // The parser decodes from views into the statement text (no copy); the
    // QString versions are what every other test pins. Both must give the
    // same value for every token, including the odd ones: signs, empty,
    // too long for the format, and too long for a 64-bit integer (decodes
    // as 0). Each view is cut from the middle of a longer statement, as
    // the parser cuts it, so a decoder reading past the view's end would
    // see the "Y1D01" after it.
    const QStringList tokens = {"021363", "-021363", "+5", "0", "", "1234567890123", "99999999999999999999"};
    for (const bool leadingZeroOmitted : {true, false}) {
        for (const bool inches : {true, false}) {
            GerberFormat format;
            format.integerDigits = 2;
            format.decimalDigits = 4;
            format.leadingZeroOmitted = leadingZeroOmitted;
            format.unitsAreInches = inches;
            for (const QString &token : tokens) {
                const QString statement = QLatin1Char('X') + token + QLatin1String("Y1D01");
                const QStringView view = QStringView(statement).mid(1, token.size());
                const QByteArray where = (token + (leadingZeroOmitted ? " L" : " T") + (inches ? " inch" : " mm"))
                                             .toUtf8();
                QVERIFY2(format.decodeCoordinateMm(view) == format.decodeCoordinateMm(token), where.constData());
                QVERIFY2(format.decodeCoordinateNm(view) == format.decodeCoordinateNm(token), where.constData());
            }
        }
    }
}

void TestGerberFormat::oddTokensDecodeToPinnedValues()
{
    // viewAndStringDecodeAgree only proves the two overloads agree - the
    // QString one forwards to the view one, so a change to both would pass
    // it. These values are worked out by hand (format 2.4) and pin the
    // result itself, for the tokens the other tests don't cover.
    GerberFormat format;
    format.integerDigits = 2;
    format.decimalDigits = 4;
    format.unitsAreInches = false;

    // Leading zeros omitted: the digits are the right end of the number.
    format.leadingZeroOmitted = true;
    QCOMPARE(format.decodeCoordinateNm(QStringView(u"+5")), qint64(500));      // 0.0005 mm
    QCOMPARE(format.decodeCoordinateNm(QStringView(u"")), qint64(0));          // nothing to read
    QCOMPARE(format.decodeCoordinateNm(QStringView(u"99999999999999999999")),  // too long for 64 bits:
             qint64(0));                                                      // unreadable reads as 0
    QCOMPARE(format.decodeCoordinateNm(QStringView(u"1234567890123")),        // longer than the format:
             qint64(123'456'789'012'300));                                    // still read in full

    // Trailing zeros omitted: the digits are the left end, padded to six.
    format.leadingZeroOmitted = false;
    QCOMPARE(format.decodeCoordinateNm(QStringView(u"5")), qint64(50'000'000));   // "500000" = 50 mm
    QCOMPARE(format.decodeCoordinateNm(QStringView(u"-5")), qint64(-50'000'000));
    QCOMPARE(format.decodeCoordinateNm(QStringView(u"")), qint64(0));             // "000000"
    format.unitsAreInches = true;
    QCOMPARE(format.decodeCoordinateNm(QStringView(u"5")), qint64(1'270'000'000)); // 50 inches
    QVERIFY(qAbs(format.decodeCoordinateMm(QStringView(u"5")) - 1270.0) < 1e-9);
}

QTEST_MAIN(TestGerberFormat)
#include "test_gerberformat.moc"
