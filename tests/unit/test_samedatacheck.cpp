#include <QtTest/QtTest>

#include "archivecompare/archivecomparisonresult.h"
#include "archivecompare/comparisonsummary.h"
#include "archivecompare/samedatacheck.h"
#include "gerber/exportnoise.h"

// Covers the re-export check end to end. Most cases are of one shape: take a
// file, change one thing, and assert the verdict. The changes that must still
// read as DIFFERENT outnumber the ones that may read as same on purpose - the
// check's whole promise is that it never hides a real change.

namespace {

// A small but complete Gerber layer: two round flashes, one rectangle-drawn
// track. Format 4.6 in mm, so 1000000 = 1 mm. `header` is spliced in where a
// CAD tool writes its export comments; `body` replaces the default geometry.
QByteArray gerber(const QString &header, const QString &body = QString())
{
    const QString defaultBody =
        "D10*\n"
        "X1000000Y1000000D03*\n"
        "X2000000Y1000000D03*\n"
        "D11*\n"
        "X0Y0D02*\n"
        "X5000000Y0D01*\n";
    return (header
            + "%FSLAX46Y46*%\n"
              "%MOMM*%\n"
              "%ADD10C,0.200*%\n"
              "%ADD11R,1.000X0.500*%\n"
            + (body.isNull() ? defaultBody : body) + "M02*\n")
        .toUtf8();
}

const QString kExportA = "G04 job: C:\\work\\first\\board.job*\n"
                         "G04 user: alice*\n"
                         "G04 date: 2026-10-01 09:00*\n";
const QString kExportB = "G04 job: D:\\other\\second\\board.job*\n"
                         "G04 user: bob*\n"
                         "G04 date: 2026-10-20 17:45*\n";

// A small drill file: one tool, two holes, decimal coordinates.
QByteArray drill(const QString &comments, const QString &tools = "T01C0.300\n",
                 const QString &holes = "X1.500Y2.250\nX3.000Y2.250\n")
{
    return ("M48\n" + comments + "METRIC,TZ\n" + tools + "%\nT01\n" + holes + "M30\n").toUtf8();
}

const QString kDrillExportA = ";job: C:\\work\\first\n;user: alice\n;date: 2026-10-01\n";
const QString kDrillExportB = ";job: D:\\other\\second\n;user: bob\n;date: 2026-10-20\n";

ArchiveEntry entry(const QString &path, EntryClassification classification, DataKind kind = DataKind::GerberData)
{
    ArchiveEntry e;
    e.relativePath = path;
    e.classification = classification;
    e.kind = kind;
    return e;
}

EntryClassification classificationOf(const QVector<ArchiveEntry> &entries, const QString &path)
{
    for (const ArchiveEntry &e : entries) {
        if (e.relativePath == path) {
            return e.classification;
        }
    }
    return EntryClassification::OnlyInThisArchive;
}

} // namespace

class TestSameDataCheck : public QObject {
    Q_OBJECT

private slots:
    // Export-noise rules
    void noiseWordsMatchOnlyAsTheFirstWholeWord();
    void standardX2NoiseAttributesMatchInBothForms();
    void descriptiveX2AttributesAreNeverNoise();

    // Gerber: what may become green
    void reexportWithNewJobUserDateIsSameData();
    void reexportWithNewX2DateAndSoftwareIsSameData();
    void reexportWithNewSameCoordinatesGuidIsSameData();
    void oldFileHeaderDirectivesAreSameData();
    void windowsLineEndingsAreSameData();
    void renumberedApertureWithSameShapeIsSameData();
    void reorderedDarkObjectsAreSameData();
    void changedOrdinaryCommentIsSameData();
    void changedObjectAttributesAreSameData();

    // Gerber: what must stay yellow
    void movedPadByOneMicronIsDifferent();
    void changedApertureSizeIsDifferent();
    void clearObjectMovedAcrossDarkObjectIsDifferent();
    void missingDuplicateObjectIsDifferent();
    void changedFileFunctionAttributeIsDifferent();
    void changedDirectiveTheParserIgnoresIsDifferent();
    void ignoredTransformDirectiveIsNeverSameData();
    void incrementalCoordinatesAreNeverSameData();
    void incrementalFormatIsNeverSameData();
    void strayPrimitiveAfterPlainStatementIsNeverSameData();
    void unknownUnitOrPolarityCodeIsNeverSameData();
    void strayMacroPrimitiveIsNeverSameData();
    void macroRenamedWithShiftedApertureIsSameData();
    void changedMacroRecipeIsDifferent();
    void redefinedMacroIsNeverSameData();
    void unusedMacroDifferenceIsSameData();
    void layerWithNoObjectsIsNeverSameData();
    void gerberAndDrillAreNeverSameData();
    void unparseableBytesAreNeverSameData();
    void positiveFilePolarityEqualsMissing();
    void drillLayerColorIsNoise();

    // Gerber: geometry only in forms the parser fully reads
    void secondRegionOutlineIsNeverSameData();
    void altiumTrailingRegionMoveIsSameData();
    void unchangedRegionArcIsSameData();
    void changedRegionArcBulgeIsDifferent();
    void bareCoordinateBeforeAnyOperationIsText();
    void coordinateWithApertureCodeIsText();
    void oldStyleCodesAreSameData();
    void regionNotStartedByMoveIsNeverSameData();
    void repeatedRegionStartMoveIsNeverSameData();
    void unclosedRegionIsNeverSameData();
    void regionTooSmallToBeDrawnIsNeverSameData();
    void arcWithXButNoYIsText();
    void modeSetWithOnlyArcOffsetsIsText();

    // Excellon
    void drillReexportWithNewJobUserDateIsSameData();
    void drillReexportWithNewKiCadBannerIsSameData();
    void drillChangedFormatOrContentsCommentIsDifferent();
    void drillChangedToolDiameterIsDifferent();
    void drillMovedHoleIsDifferent();
    void drillWithUndecodableCoordinatesIsNeverSameData();
    void drillChangedSlotEndIsDifferent();
    void drillUnchangedSlotIsSameData();
    void drillRepeatCommandIsNeverSameData();
    void drillRoutWithOffsetOnLinearMoveIsText();
    void drillMalformedCoordinateIsText();

    // Pair collapse and summary
    void sameDataPairTurnsBothFilesGreen();
    void differentPairStaysYellow();
    void provenPairDropsItsOtherPairs();
    void crossPairsCollapseToTwoGreens();
    void oneProvenPairLeavesOtherPartnersRed();
    void partnerStillPairedElsewhereStaysYellow();
    void provenFileIsNotOfferedAgain();
    void summaryAfterCollapseMatchesTheTrees();
    void pairWithUnknownPathIsNeverOffered();
    void nonLayerPairIsNeverOfferedToTheCheck();
    void summaryCountsSameDataPairAsIdentical();
};

void TestSameDataCheck::noiseWordsMatchOnlyAsTheFirstWholeWord()
{
    QVERIFY(ExportNoise::isVolatileComment(" job: C:\\path\\board"));
    QVERIFY(ExportNoise::isVolatileComment("USER admin"));
    QVERIFY(ExportNoise::isVolatileComment("Date 2026-10-20"));
    QVERIFY(ExportNoise::isVolatileComment("date:2026"));
    QVERIFY(ExportNoise::isVolatileComment("date"));

    QVERIFY(!ExportNoise::isVolatileComment("updated pads"));    // word inside another word
    QVERIFY(!ExportNoise::isVolatileComment("Layer date 2026")); // not the first word
    QVERIFY(!ExportNoise::isVolatileComment("jobs pending"));    // longer word
    QVERIFY(!ExportNoise::isVolatileComment("dated copy"));
    QVERIFY(!ExportNoise::isVolatileComment("date_code: 2345")); // underscore joins words
    QVERIFY(!ExportNoise::isVolatileComment("FILE_FORMAT=2:4"));
    QVERIFY(!ExportNoise::isVolatileComment("CONTENTS: plated"));
}

void TestSameDataCheck::standardX2NoiseAttributesMatchInBothForms()
{
    QVERIFY(ExportNoise::isVolatileAttribute("TF.CreationDate,2026-10-20T10:00:00+02:00"));
    QVERIFY(ExportNoise::isVolatileAttribute("TF.GenerationSoftware,Vendor,Tool,1.0"));
    QVERIFY(ExportNoise::isVolatileAttribute("TF.ProjectId,board,abc-123,rev2"));
    QVERIFY(ExportNoise::isVolatileAttribute("TF.MD5,0123456789abcdef"));
    QVERIFY(ExportNoise::isVolatileAttribute("TF.SameCoordinates,00000000-1111-2222-3333-444444444444"));
    // The same attribute hidden in a comment for older readers.
    QVERIFY(ExportNoise::isVolatileComment(" #@! TF.CreationDate,2026-10-20T10:00:00+02:00"));
    QVERIFY(ExportNoise::isVolatileComment("#@!TF.GenerationSoftware,Vendor,Tool,1.0"));
}

void TestSameDataCheck::descriptiveX2AttributesAreNeverNoise()
{
    QVERIFY(!ExportNoise::isVolatileAttribute("TF.FileFunction,Copper,L1,Top"));
    QVERIFY(!ExportNoise::isVolatileAttribute("TF.FilePolarity,Positive"));
    QVERIFY(!ExportNoise::isVolatileAttribute("TF.MD5extra,1"));        // only the exact name
    QVERIFY(!ExportNoise::isVolatileAttribute("TA.AperFunction,SMDPad")); // aperture attribute
    QVERIFY(!ExportNoise::isVolatileComment(" #@! TF.FileFunction,Copper,L1,Top"));
}

void TestSameDataCheck::reexportWithNewJobUserDateIsSameData()
{
    QVERIFY(SameDataCheck::isSameData(gerber(kExportA), gerber(kExportB)));
}

void TestSameDataCheck::reexportWithNewX2DateAndSoftwareIsSameData()
{
    const QString a = "%TF.GenerationSoftware,Vendor,Tool,1.0*%\n"
                      "%TF.CreationDate,2026-10-01T09:00:00+02:00*%\n"
                      "G04 #@! TF.ProjectId,board,1111,rev1*\n"
                      "%TF.FileFunction,Copper,L1,Top*%\n";
    const QString b = "%TF.GenerationSoftware,Vendor,Tool,1.1*%\n"
                      "%TF.CreationDate,2026-10-20T17:45:00+02:00*%\n"
                      "G04 #@! TF.ProjectId,board,2222,rev1*\n"
                      "%TF.FileFunction,Copper,L1,Top*%\n";
    QVERIFY(SameDataCheck::isSameData(gerber(a), gerber(b)));
}

// Altium writes a fresh GUID into .SameCoordinates on every export, in the
// comment form, between otherwise unchanged header lines.
void TestSameDataCheck::reexportWithNewSameCoordinatesGuidIsSameData()
{
    const QString header = "G04*\n"
                           "G04 #@! TF.GenerationSoftware,Vendor,Tool,1.0*\n"
                           "G04 Layer_Color=32768*\n"
                           "G04*\n"
                           "G04 #@! TF.SameCoordinates,%1*\n"
                           "G04*\n"
                           "G04 #@! TF.FilePolarity,Positive*\n";
    const QString a = header.arg("00000000-1111-2222-3333-444444444444");
    const QString b = header.arg("AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE");
    QVERIFY(SameDataCheck::isSameData(gerber(a), gerber(b)));
}

void TestSameDataCheck::windowsLineEndingsAreSameData()
{
    QByteArray gerberCrlf = gerber(kExportB);
    gerberCrlf.replace("\n", "\r\n");
    QVERIFY(SameDataCheck::isSameData(gerber(kExportA), gerberCrlf));

    QByteArray drillCrlf = drill(kDrillExportB);
    drillCrlf.replace("\n", "\r\n");
    QVERIFY(SameDataCheck::isSameData(drill(kDrillExportA), drillCrlf));
}

void TestSameDataCheck::renumberedApertureWithSameShapeIsSameData()
{
    // B calls the 0.2 mm circle D12 instead of D10 - and defines it under
    // that number - but draws exactly the same thing.
    QByteArray b = gerber(kExportB);
    b.replace("%ADD10C,0.200*%", "%ADD12C,0.200*%");
    b.replace("D10*\n", "D12*\n");
    QVERIFY(SameDataCheck::isSameData(gerber(kExportA), b));
}

void TestSameDataCheck::reorderedDarkObjectsAreSameData()
{
    const QString swapped =
        "D11*\n"
        "X0Y0D02*\n"
        "X5000000Y0D01*\n"
        "D10*\n"
        "X2000000Y1000000D03*\n"
        "X1000000Y1000000D03*\n";
    QVERIFY(SameDataCheck::isSameData(gerber(kExportA), gerber(kExportB, swapped)));
}

void TestSameDataCheck::movedPadByOneMicronIsDifferent()
{
    QByteArray b = gerber(kExportB);
    b.replace("X2000000Y1000000D03*", "X2001000Y1000000D03*");
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA), b));
}

void TestSameDataCheck::changedApertureSizeIsDifferent()
{
    QByteArray b = gerber(kExportB);
    b.replace("%ADD10C,0.200*%", "%ADD10C,0.250*%");
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA), b));
}

void TestSameDataCheck::clearObjectMovedAcrossDarkObjectIsDifferent()
{
    // A clear flash after the pad cuts a hole in it; the same clear flash
    // before the pad is painted over. Same objects, different board.
    const QString clearAfter =
        "D11*\n"
        "X1000000Y1000000D03*\n"
        "%LPC*%\n"
        "D10*\n"
        "X1000000Y1000000D03*\n"
        "%LPD*%\n";
    const QString clearBefore =
        "%LPC*%\n"
        "D10*\n"
        "X1000000Y1000000D03*\n"
        "%LPD*%\n"
        "D11*\n"
        "X1000000Y1000000D03*\n";
    QVERIFY(SameDataCheck::isSameData(gerber(kExportA, clearAfter), gerber(kExportB, clearAfter)));
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA, clearAfter), gerber(kExportB, clearBefore)));
}

void TestSameDataCheck::missingDuplicateObjectIsDifferent()
{
    const QString twice = "D10*\nX1000000Y1000000D03*\nX1000000Y1000000D03*\nX2000000Y1000000D03*\n";
    const QString once = "D10*\nX1000000Y1000000D03*\nX2000000Y1000000D03*\nX2000000Y1000000D03*\n";
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA, twice), gerber(kExportB, once)));
}

void TestSameDataCheck::changedFileFunctionAttributeIsDifferent()
{
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA + "%TF.FileFunction,Copper,L1,Top*%\n"),
                                       gerber(kExportB + "%TF.FileFunction,Copper,L2,Bot*%\n")));
}

// Net/component/pin labels (%TO) and their reset (%TD) never change the
// image: re-annotating R12 to R15 or renaming a net moves no copper. A label
// added on one side only is the same case.
void TestSameDataCheck::changedObjectAttributesAreSameData()
{
    const QString labelledA = "D10*\n"
                              "%TO.N,GND*%\n"
                              "%TO.C,R12*%\n"
                              "%TO.P,R12,1*%\n"
                              "X1000000Y1000000D03*\n"
                              "%TD*%\n"
                              "X2000000Y1000000D03*\n";
    const QString labelledB = "D10*\n"
                              "%TO.N,Net-(R15-Pad1)*%\n"
                              "%TO.C,R15*%\n"
                              "%TO.P,R15,1*%\n"
                              "X1000000Y1000000D03*\n"
                              "%TD*%\n"
                              "X2000000Y1000000D03*\n";
    const QString unlabelled = "D10*\n"
                               "X1000000Y1000000D03*\n"
                               "X2000000Y1000000D03*\n";
    QVERIFY(SameDataCheck::isSameData(gerber(kExportA, labelledA), gerber(kExportB, labelledB)));
    QVERIFY(SameDataCheck::isSameData(gerber(kExportA, labelledA), gerber(kExportB, unlabelled)));
    // The labels must not mask a real change next to them.
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA, labelledA),
                                       gerber(kExportB, QString(labelledB).replace("X2000000", "X2000001"))));
}

// A G04 comment has no effect on the image (Gerber spec), so any comment
// change - not just known export noise - leaves the data the same.
void TestSameDataCheck::changedOrdinaryCommentIsSameData()
{
    QVERIFY(SameDataCheck::isSameData(gerber(kExportA + "G04 Layer: top copper*\n"),
                                      gerber(kExportB + "G04 Layer: bottom copper*\n")));
    QVERIFY(SameDataCheck::isSameData(gerber("G04 #@! TF.FileFunction,Copper,L1,Top*\n"),
                                      gerber("G04 #@! TF.FileFunction,Copper,L2,Bot*\n")));
}

void TestSameDataCheck::changedDirectiveTheParserIgnoresIsDifferent()
{
    // The parser does not apply %LR (rotation), so both files parse to the
    // same objects - only the non-geometry comparison can catch this.
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA + "%LR0*%\n"), gerber(kExportB + "%LR90*%\n")));
}

void TestSameDataCheck::ignoredTransformDirectiveIsNeverSameData()
{
    // %LR is ignored by the parser, so even in both files it proves nothing:
    // where it sits relative to the objects is not compared.
    const QString body = "D10*\nX1000000Y1000000D03*\n%LR90*%\nX2000000Y1000000D03*\n";
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA, body), gerber(kExportB, body)));
    for (const QString &directive : {"%LMX*%\n", "%SFA2B2*%\n", "%OFA1B0*%\n", "%MIA1B0*%\n",
                                     "%ASAYBX*%\n", "%IPNEG*%\n", "%IR90*%\n", "%ABD12*%\n"}) {
        QVERIFY2(!SameDataCheck::isSameData(gerber(kExportA + directive), gerber(kExportB + directive)),
                 qPrintable(directive));
    }
}

void TestSameDataCheck::incrementalCoordinatesAreNeverSameData()
{
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA + "G91*\n"), gerber(kExportB + "G91*\n")));
    // Chained after another code in one statement.
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA + "G90G91*\n"), gerber(kExportB + "G90G91*\n")));
}

void TestSameDataCheck::incrementalFormatIsNeverSameData()
{
    // %FS...I... declares incremental notation; the parser reads absolute.
    QByteArray a = gerber(kExportA);
    QByteArray b = gerber(kExportB);
    a.replace("%FSLAX46Y46*%", "%FSLIX46Y46*%");
    b.replace("%FSLAX46Y46*%", "%FSLIX46Y46*%");
    QVERIFY(!SameDataCheck::isSameData(a, b));
}

void TestSameDataCheck::strayPrimitiveAfterPlainStatementIsNeverSameData()
{
    // A plain statement ends the macro body, so the later primitive is stray.
    const QString extra = "%AMFOO*1,1,0.5,0,0*%\nG01*\n%1,1,0.5,0,0*%\n";
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA + extra), gerber(kExportB + extra)));
}

void TestSameDataCheck::macroRenamedWithShiftedApertureIsSameData()
{
    // Altium names macros after their D-code, so a shifted aperture renames
    // the macro and reorders the definitions (real data, pair test1).
    const QString a = "%AMROUNDEDRECTD37*\n21,1,$1,$2,0,0,0*%\n%AMROUNDEDRECTD38*\n1,1,$1,0,0*%\n"
                      "%ADD37ROUNDEDRECTD37,1X2*%\n%ADD38ROUNDEDRECTD38,0.5*%\n";
    const QString b = "%AMROUNDEDRECTD40*\n1,1,$1,0,0*%\n%AMROUNDEDRECTD41*\n21,1,$1,$2,0,0,0*%\n"
                      "%ADD40ROUNDEDRECTD40,0.5*%\n%ADD41ROUNDEDRECTD41,1X2*%\n";
    QVERIFY(SameDataCheck::isSameData(gerber(kExportA + a, "D37*\nX0Y0D03*\nD38*\nX1000000Y0D03*\n"),
                                      gerber(kExportB + b, "D41*\nX0Y0D03*\nD40*\nX1000000Y0D03*\n")));
}

void TestSameDataCheck::changedMacroRecipeIsDifferent()
{
    const QString a = "%AMBOX*\n21,1,$1,$2,0,0,0*%\n%ADD37BOX,1X2*%\n";
    const QString b = "%AMBOX*\n21,1,$1,$2,0,0,45*%\n%ADD37BOX,1X2*%\n";
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA + a, "D37*\nX0Y0D03*\n"),
                                       gerber(kExportB + b, "D37*\nX0Y0D03*\n")));
}

void TestSameDataCheck::redefinedMacroIsNeverSameData()
{
    // The parser keeps only the last definition of a name, so file A's first
    // aperture is declared with recipe 1 in the source but resolves to recipe 2.
    const QString a = "%AMBOX*\n21,1,$1,$2,0,0,0*%\n%ADD10BOX,1X2*%\n"
                      "%AMBOX*\n21,1,$1,$2,0,0,45*%\n%ADD11BOX,1X2*%\n";
    const QString b = "%AMBOX*\n21,1,$1,$2,0,0,45*%\n%ADD10BOX,1X2*%\n%ADD11BOX,1X2*%\n";
    const QString draw = "D10*\nX0Y0D03*\nD11*\nX1000000Y0D03*\n";
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA + a, draw), gerber(kExportB + b, draw)));
}

void TestSameDataCheck::positiveFilePolarityEqualsMissing()
{
    // Newer Altium writes it, older does not (real data, pair test3).
    QVERIFY(SameDataCheck::isSameData(gerber(kExportA + "%TF.FilePolarity,Positive*%\n"), gerber(kExportB)));
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA + "%TF.FilePolarity,Negative*%\n"), gerber(kExportB)));
}

void TestSameDataCheck::drillLayerColorIsNoise()
{
    QVERIFY(SameDataCheck::isSameData(drill(kDrillExportA + ";Layer_Color=9474304\n"),
                                      drill(kDrillExportB + ";Layer_Color=32768\n")));
}

void TestSameDataCheck::unusedMacroDifferenceIsSameData()
{
    // An unused macro draws nothing, so defining one extra changes no image.
    const QString base = "%AMBOX*\n21,1,$1,$2,0,0,0*%\n%ADD37BOX,1X2*%\n";
    const QString a = "%AMUNUSED*\n1,1,0.5,0,0*%\n" + base;
    QVERIFY(SameDataCheck::isSameData(gerber(kExportA + a, "D37*\nX0Y0D03*\n"),
                                      gerber(kExportB + base, "D37*\nX0Y0D03*\n")));
}

void TestSameDataCheck::unknownUnitOrPolarityCodeIsNeverSameData()
{
    for (const QString &directive : {"%MOXX*%\n", "%LPX*%\n"}) {
        QVERIFY2(!SameDataCheck::isSameData(gerber(kExportA + directive), gerber(kExportB + directive)),
                 qPrintable(directive));
    }
}

void TestSameDataCheck::oldFileHeaderDirectivesAreSameData()
{
    // Older exports: image polarity positive (the default), and layer/image
    // names that differ per export.
    QVERIFY(SameDataCheck::isSameData(gerber(kExportA + "%IPPOS*%\n%LNfirst.gbr*%\n%INjob1*%\n"),
                                      gerber(kExportB + "%LNsecond.gbr*%\n%INjob2*%\n")));
}

void TestSameDataCheck::strayMacroPrimitiveIsNeverSameData()
{
    // A digit-led parameter statement outside any %AM is not something the
    // parser reads.
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA + "%1,1,0.5,0,0*%\n"),
                                       gerber(kExportB + "%1,1,0.5,0,0*%\n")));
}

void TestSameDataCheck::layerWithNoObjectsIsNeverSameData()
{
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA, ""), gerber(kExportB, "")));
}

void TestSameDataCheck::gerberAndDrillAreNeverSameData()
{
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA), drill(kDrillExportA)));
}

void TestSameDataCheck::unparseableBytesAreNeverSameData()
{
    QVERIFY(!SameDataCheck::isSameData("just some text\n", "just some other text\n"));
    QVERIFY(!SameDataCheck::isSameData("same text\n", "same text\n"));
}

void TestSameDataCheck::secondRegionOutlineIsNeverSameData()
{
    // A real second outline: D02 to a new start, then D01 draws. The parser
    // drops the second start point, so the region cannot be vouched for.
    const QString region = "G36*\nX0Y0D02*\nX1000000Y0D01*\nX1000000Y1000000D01*\n"
                           "X2000000Y2000000D02*\nX3000000Y2000000D01*\nX3000000Y3000000D01*\nG37*\n";
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA, region), gerber(kExportB, region)));
}

void TestSameDataCheck::altiumTrailingRegionMoveIsSameData()
{
    // Altium ends every region with a D02 and no D01 after it (real data Q1).
    const QString region = "G36*\nX0Y0D02*\nX1000000Y0D01*\nX1000000Y1000000D01*\nX0Y0D01*\n"
                           "X0Y0D02*\nG37*\n";
    QVERIFY(SameDataCheck::isSameData(gerber(kExportA, region), gerber(kExportB, region)));
}

void TestSameDataCheck::unchangedRegionArcIsSameData()
{
    const QString region = "G75*\nG36*\nX0Y0D02*\nG01X1000000Y0D01*\n"
                           "G03X1000000Y1000000I0J500000D01*\nG01X0Y0D01*\nG37*\n";
    QVERIFY(SameDataCheck::isSameData(gerber(kExportA, region), gerber(kExportB, region)));
}

void TestSameDataCheck::changedRegionArcBulgeIsDifferent()
{
    const QString a = "G75*\nG36*\nX0Y0D02*\nG01X1000000Y0D01*\n"
                      "G03X1000000Y1000000I0J500000D01*\nG01X0Y0D01*\nG37*\n";
    QString b = a;
    b.replace("I0J500000", "I-500000J500000");
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA, a), gerber(kExportB, b)));
}

void TestSameDataCheck::bareCoordinateBeforeAnyOperationIsText()
{
    // The parser ignores coordinates before the first D01/D02/D03.
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA, "D10*\nX1000000Y1000000*\nX0Y0D03*\n"),
                                       gerber(kExportB, "D10*\nX3000000Y1000000*\nX0Y0D03*\n")));
}

void TestSameDataCheck::coordinateWithApertureCodeIsText()
{
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA, "D10*\nX1000000Y0D10*\nX0Y0D03*\n"),
                                       gerber(kExportB, "D10*\nX2000000Y0D10*\nX0Y0D03*\n")));
}

void TestSameDataCheck::oldStyleCodesAreSameData()
{
    // Unpadded/padded codes and the G54 prefix, in a file with shifted apertures.
    const QString a = "G54D10*\nX1000000Y1000000D3*\nG1X0Y0D2*\nD11*\nX5000000Y0D001*\n";
    QByteArray b = gerber(kExportB, a);
    b.replace("%ADD10C,0.200*%", "%ADD12C,0.200*%");
    b.replace("G54D10*", "G54D12*");
    QVERIFY(SameDataCheck::isSameData(gerber(kExportA, a), b));
}

void TestSameDataCheck::regionNotStartedByMoveIsNeverSameData()
{
    // The region's first statement draws (D01) instead of moving, so its
    // start is the pen position from before G36. The parser does not record
    // that point, and the move that set it is not compared as text.
    const QString region = "G36*\nX1000000Y0D01*\nX1000000Y1000000D01*\nX0Y0D01*\nG37*\n";
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA, "D10*\nX0Y0D03*\nX0Y0D02*\n" + region),
                                       gerber(kExportB, "D10*\nX0Y0D03*\nX500000Y0D02*\n" + region)));
}

void TestSameDataCheck::repeatedRegionStartMoveIsNeverSameData()
{
    // Two moves before the first draw: the outline starts at the second one,
    // but the parser records the first.
    const QString region = "G36*\nX0Y0D02*\nX%1Y0D02*\nX1000000Y1000000D01*\nX0Y1000000D01*\nG37*\n";
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA, region.arg("500000")),
                                       gerber(kExportB, region.arg("600000"))));
}

void TestSameDataCheck::unclosedRegionIsNeverSameData()
{
    // No G37: the parser never emits the region, so its outline is lost.
    const QString region = "D10*\nX0Y0D03*\nG36*\nX0Y0D02*\nX%1Y0D01*\nX1000000Y1000000D01*\n";
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA, region.arg("1000000")),
                                       gerber(kExportB, region.arg("2000000"))));
}

void TestSameDataCheck::regionTooSmallToBeDrawnIsNeverSameData()
{
    // A full-circle region has only two vertices (start and end), so the
    // parser emits nothing for it - the start move would be lost.
    const QString region = "D10*\nX0Y0D03*\nG75*\nG36*\nX%1Y0D02*\nG03X0Y0I500000J0D01*\nG37*\n";
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA, region.arg("0")),
                                       gerber(kExportB, region.arg("1000000"))));
}

void TestSameDataCheck::arcWithXButNoYIsText()
{
    // Without a Y field the parser reads X's digits up to the end of the
    // statement ("1000000I500000J0") and decodes nothing, so X is lost.
    const QString body = "D10*\nX0Y0D02*\nG75*\nG03X%1I500000J0D01*\nG01*\n";
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA, body.arg("1000000")),
                                       gerber(kExportB, body.arg("2000000"))));
}

void TestSameDataCheck::modeSetWithOnlyArcOffsetsIsText()
{
    // A G-code followed only by I/J is a mode set to the parser; the offsets
    // are not read.
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA, "D10*\nX0Y0D03*\nG02I100J0*\nG01*\n"),
                                       gerber(kExportB, "D10*\nX0Y0D03*\nG02I200J0*\nG01*\n")));
}

void TestSameDataCheck::drillReexportWithNewJobUserDateIsSameData()
{
    QVERIFY(SameDataCheck::isSameData(drill(kDrillExportA), drill(kDrillExportB)));
}

// KiCad opens every drill file with a banner whose first word is DRILL, not
// date - so it needs its own entry, and must still be noise.
void TestSameDataCheck::drillReexportWithNewKiCadBannerIsSameData()
{
    QVERIFY(ExportNoise::isVolatileComment(" DRILL file {KiCad 9.0.5} date 2025-12-10T14:26:13-0500"));
    QVERIFY(!ExportNoise::isVolatileComment(" DRILLED holes are plated")); // longer word
    QVERIFY(SameDataCheck::isSameData(drill("; DRILL file {KiCad 9.0.5} date 2025-12-10T14:26:13-0500\n"),
                                      drill("; DRILL file {KiCad 9.0.6} date 2026-10-22T09:00:00-0500\n")));
}

void TestSameDataCheck::drillChangedFormatOrContentsCommentIsDifferent()
{
    // Coordinates are decimal here, so neither comment moves a hole - the
    // objects match and only the non-geometry comparison sees the change.
    QVERIFY(!SameDataCheck::isSameData(drill(kDrillExportA + ";FILE_FORMAT=2:4\n"),
                                       drill(kDrillExportB + ";FILE_FORMAT=3:3\n")));
    QVERIFY(!SameDataCheck::isSameData(drill(kDrillExportA + ";CONTENTS: plated\n"),
                                       drill(kDrillExportB + ";CONTENTS: non-plated\n")));
}

void TestSameDataCheck::drillChangedToolDiameterIsDifferent()
{
    QVERIFY(!SameDataCheck::isSameData(drill(kDrillExportA), drill(kDrillExportB, "T01C0.350\n")));
}

void TestSameDataCheck::drillMovedHoleIsDifferent()
{
    QVERIFY(!SameDataCheck::isSameData(drill(kDrillExportA),
                                       drill(kDrillExportB, "T01C0.300\n", "X1.500Y2.250\nX3.001Y2.250\n")));
}

void TestSameDataCheck::drillWithUndecodableCoordinatesIsNeverSameData()
{
    // No unit line, no FILE_FORMAT comment, no decimal point: the parser
    // cannot decode "15000" and refuses.
    const QByteArray a = "M48\n;job: one\nT01C0.300\n%\nT01\nX15000Y22500\nM30\n";
    const QByteArray b = "M48\n;job: two\nT01C0.300\n%\nT01\nX15000Y22500\nM30\n";
    QVERIFY(!SameDataCheck::isSameData(a, b));
}

void TestSameDataCheck::drillChangedSlotEndIsDifferent()
{
    // G85 slots are not understood by the drill parser: both parse to one
    // hole at the start point. The line must stay compared as text.
    QVERIFY(!SameDataCheck::isSameData(drill(kDrillExportA, "T01C0.300\n", "X1.0Y1.0G85X3.0Y1.0\n"),
                                       drill(kDrillExportB, "T01C0.300\n", "X1.0Y1.0G85X5.0Y1.0\n")));
}

void TestSameDataCheck::drillUnchangedSlotIsSameData()
{
    QVERIFY(SameDataCheck::isSameData(drill(kDrillExportA, "T01C0.300\n", "X1.0Y1.0G85X3.0Y1.0\n"),
                                      drill(kDrillExportB, "T01C0.300\n", "X1.0Y1.0G85X3.0Y1.0\n")));
}

void TestSameDataCheck::drillRepeatCommandIsNeverSameData()
{
    // R repeats are not parsed at all, so even identical lines prove nothing.
    QVERIFY(!SameDataCheck::isSameData(drill(kDrillExportA, "T01C0.300\n", "X1.0Y1.0\nR05X0.5\n"),
                                       drill(kDrillExportB, "T01C0.300\n", "X1.0Y1.0\nR05X0.5\n")));
}

void TestSameDataCheck::drillRoutWithOffsetOnLinearMoveIsText()
{
    // G01 ignores I/J in the parser, so a G01 line with I/J stays text.
    const QString routA = "G00X1.0Y1.0\nM15\nG01X2.0Y1.0I0.5J0.0\nM16\nG05\n";
    const QString routB = "G00X1.0Y1.0\nM15\nG01X2.0Y1.0I0.7J0.0\nM16\nG05\n";
    QVERIFY(!SameDataCheck::isSameData(drill(kDrillExportA, "T01C0.300\n", routA),
                                       drill(kDrillExportB, "T01C0.300\n", routB)));
}

void TestSameDataCheck::drillMalformedCoordinateIsText()
{
    // "1.0.5" is not a number; the parser reads it as 0, so it must stay
    // compared as text rather than be trusted as a hole.
    QVERIFY(!SameDataCheck::isSameData(drill(kDrillExportA, "T01C0.300\n", "X1.0.5Y2.0\n"),
                                       drill(kDrillExportB, "T01C0.300\n", "X1.0.7Y2.0\n")));
}

void TestSameDataCheck::sameDataPairTurnsBothFilesGreen()
{
    ArchiveComparisonResult result;
    result.entriesA = {entry("top.gtl", EntryClassification::Different)};
    result.entriesB = {entry("top.gtl", EntryClassification::Different)};
    result.differingPairs = {{"top.gtl", "top.gtl", PairReason::SameName}};

    SameDataCheck::collapseSameDataPairs(result, [](const DifferingPair &) { return true; });

    QVERIFY(result.differingPairs.isEmpty());
    QCOMPARE(result.sameDataPairs.size(), 1);
    QCOMPARE(classificationOf(result.entriesA, "top.gtl"), EntryClassification::Identical);
    QCOMPARE(classificationOf(result.entriesB, "top.gtl"), EntryClassification::Identical);
}

void TestSameDataCheck::differentPairStaysYellow()
{
    ArchiveComparisonResult result;
    result.entriesA = {entry("top.gtl", EntryClassification::Different)};
    result.entriesB = {entry("top.gtl", EntryClassification::Different)};
    result.differingPairs = {{"top.gtl", "top.gtl", PairReason::SameName}};

    SameDataCheck::collapseSameDataPairs(result, [](const DifferingPair &) { return false; });

    QCOMPARE(result.differingPairs.size(), 1);
    QVERIFY(result.sameDataPairs.isEmpty());
    QCOMPARE(classificationOf(result.entriesA, "top.gtl"), EntryClassification::Different);
    QCOMPARE(classificationOf(result.entriesB, "top.gtl"), EntryClassification::Different);
}

void TestSameDataCheck::provenPairDropsItsOtherPairs()
{
    // A's top.gtl matches v1; its pair with v2 goes, and v2 is left with no
    // counterpart at all -> red.
    ArchiveComparisonResult result;
    result.entriesA = {entry("top.gtl", EntryClassification::Different)};
    result.entriesB = {entry("v1/top.gtl", EntryClassification::Different),
                       entry("v2/top.gtl", EntryClassification::Different)};
    result.differingPairs = {{"top.gtl", "v1/top.gtl", PairReason::SameName},
                             {"top.gtl", "v2/top.gtl", PairReason::SameName}};
    SameDataCheck::collapseSameDataPairs(result, [](const DifferingPair &p) { return p.relativePathB == "v1/top.gtl"; });

    QVERIFY(result.differingPairs.isEmpty());
    QCOMPARE(result.sameDataPairs.size(), 1);
    QCOMPARE(classificationOf(result.entriesA, "top.gtl"), EntryClassification::Identical);
    QCOMPARE(classificationOf(result.entriesB, "v1/top.gtl"), EntryClassification::Identical);
    QCOMPARE(classificationOf(result.entriesB, "v2/top.gtl"), EntryClassification::OnlyInThisArchive);
}

void TestSameDataCheck::crossPairsCollapseToTwoGreens()
{
    // a/top + b/top on both sides: the name pass makes 4 pairs. a=a and b=b
    // are proven; the two cross pairs go and nothing is left yellow or red.
    ArchiveComparisonResult result;
    result.entriesA = {entry("a/top.gtl", EntryClassification::Different),
                       entry("b/top.gtl", EntryClassification::Different)};
    result.entriesB = {entry("a/top.gtl", EntryClassification::Different),
                       entry("b/top.gtl", EntryClassification::Different)};
    result.differingPairs = {{"a/top.gtl", "a/top.gtl", PairReason::SameName},
                             {"a/top.gtl", "b/top.gtl", PairReason::SameName},
                             {"b/top.gtl", "a/top.gtl", PairReason::SameName},
                             {"b/top.gtl", "b/top.gtl", PairReason::SameName}};
    SameDataCheck::collapseSameDataPairs(result, [](const DifferingPair &p) {
        return p.relativePathA.section('/', 0, 0) == p.relativePathB.section('/', 0, 0);
    });

    QVERIFY(result.differingPairs.isEmpty());
    QCOMPARE(result.sameDataPairs.size(), 2);
    QCOMPARE(classificationOf(result.entriesA, "a/top.gtl"), EntryClassification::Identical);
    QCOMPARE(classificationOf(result.entriesA, "b/top.gtl"), EntryClassification::Identical);
    QCOMPARE(classificationOf(result.entriesB, "a/top.gtl"), EntryClassification::Identical);
    QCOMPARE(classificationOf(result.entriesB, "b/top.gtl"), EntryClassification::Identical);
}

void TestSameDataCheck::oneProvenPairLeavesOtherPartnersRed()
{
    // top.gtl vs v1/, v2/, v3/; only v2 matches -> v2 green, v1 and v3 red.
    ArchiveComparisonResult result;
    result.entriesA = {entry("top.gtl", EntryClassification::Different)};
    result.entriesB = {entry("v1/top.gtl", EntryClassification::Different),
                       entry("v2/top.gtl", EntryClassification::Different),
                       entry("v3/top.gtl", EntryClassification::Different)};
    result.differingPairs = {{"top.gtl", "v1/top.gtl", PairReason::SameName},
                             {"top.gtl", "v2/top.gtl", PairReason::SameName},
                             {"top.gtl", "v3/top.gtl", PairReason::SameName}};
    SameDataCheck::collapseSameDataPairs(result, [](const DifferingPair &p) { return p.relativePathB == "v2/top.gtl"; });

    QVERIFY(result.differingPairs.isEmpty());
    QCOMPARE(result.sameDataPairs.size(), 1);
    QCOMPARE(classificationOf(result.entriesA, "top.gtl"), EntryClassification::Identical);
    QCOMPARE(classificationOf(result.entriesB, "v1/top.gtl"), EntryClassification::OnlyInThisArchive);
    QCOMPARE(classificationOf(result.entriesB, "v2/top.gtl"), EntryClassification::Identical);
    QCOMPARE(classificationOf(result.entriesB, "v3/top.gtl"), EntryClassification::OnlyInThisArchive);
}

void TestSameDataCheck::partnerStillPairedElsewhereStaysYellow()
{
    // A: x.gtl, y.gtl; B: x.gtl, other/y.gtl. Pair (A x, B x) is proven and
    // (A y, B x) is dropped. A y still pairs with B other/y (unproven), so
    // A y stays yellow, not red.
    ArchiveComparisonResult result;
    result.entriesA = {entry("x.gtl", EntryClassification::Different),
                       entry("y.gtl", EntryClassification::Different)};
    result.entriesB = {entry("x.gtl", EntryClassification::Different),
                       entry("other/y.gtl", EntryClassification::Different)};
    result.differingPairs = {{"x.gtl", "x.gtl", PairReason::SameName},
                             {"y.gtl", "x.gtl", PairReason::SameName},
                             {"y.gtl", "other/y.gtl", PairReason::SameName}};
    SameDataCheck::collapseSameDataPairs(result, [](const DifferingPair &p) { return p.relativePathA == "x.gtl"; });

    QCOMPARE(result.differingPairs.size(), 1);
    QCOMPARE(result.differingPairs.first().relativePathA, QString("y.gtl"));
    QCOMPARE(result.differingPairs.first().relativePathB, QString("other/y.gtl"));
    QCOMPARE(result.sameDataPairs.size(), 1);
    QCOMPARE(classificationOf(result.entriesA, "x.gtl"), EntryClassification::Identical);
    QCOMPARE(classificationOf(result.entriesA, "y.gtl"), EntryClassification::Different);
    QCOMPARE(classificationOf(result.entriesB, "x.gtl"), EntryClassification::Identical);
    QCOMPARE(classificationOf(result.entriesB, "other/y.gtl"), EntryClassification::Different);
}

void TestSameDataCheck::provenFileIsNotOfferedAgain()
{
    // After top.gtl-v1 is proven, top.gtl-v2 is never tested.
    ArchiveComparisonResult result;
    result.entriesA = {entry("top.gtl", EntryClassification::Different)};
    result.entriesB = {entry("v1/top.gtl", EntryClassification::Different),
                       entry("v2/top.gtl", EntryClassification::Different)};
    result.differingPairs = {{"top.gtl", "v1/top.gtl", PairReason::SameName},
                             {"top.gtl", "v2/top.gtl", PairReason::SameName}};

    int calls = 0;
    SameDataCheck::collapseSameDataPairs(result, [&calls](const DifferingPair &) {
        ++calls;
        return true;
    });

    QCOMPARE(calls, 1);
    QCOMPARE(result.sameDataPairs.size(), 1);
    QCOMPARE(result.sameDataPairs.first().relativePathB, QString("v1/top.gtl"));
    QVERIFY(result.differingPairs.isEmpty());
    QCOMPARE(classificationOf(result.entriesB, "v2/top.gtl"), EntryClassification::OnlyInThisArchive);
}

void TestSameDataCheck::summaryAfterCollapseMatchesTheTrees()
{
    // The provenPairDropsItsOtherPairs scenario with a distinct crc32 per
    // entry: the summary must agree with what the trees show.
    ArchiveComparisonResult result;
    result.entriesA = {entry("top.gtl", EntryClassification::Different)};
    result.entriesB = {entry("v1/top.gtl", EntryClassification::Different),
                       entry("v2/top.gtl", EntryClassification::Different)};
    result.entriesA[0].crc32 = 1;
    result.entriesB[0].crc32 = 2;
    result.entriesB[1].crc32 = 3;
    result.differingPairs = {{"top.gtl", "v1/top.gtl", PairReason::SameName},
                             {"top.gtl", "v2/top.gtl", PairReason::SameName}};
    SameDataCheck::collapseSameDataPairs(result, [](const DifferingPair &p) { return p.relativePathB == "v1/top.gtl"; });
    const ComparisonSummary::Summary summary = ComparisonSummary::summarize(result);

    QCOMPARE(summary.gerber.identicalClasses, 1);
    QCOMPARE(summary.gerber.pairs, 0);
    QCOMPARE(summary.gerber.onlyInB, 1);
    QVERIFY(!summary.fullyIdentical());
}

void TestSameDataCheck::pairWithUnknownPathIsNeverOffered()
{
    // A pair whose path is missing from entriesA: the kind defaults to
    // OtherData, so the check is not run and the pair stays as it was.
    ArchiveComparisonResult result;
    result.entriesB = {entry("top.gtl", EntryClassification::Different)};
    result.differingPairs = {{"ghost.gtl", "top.gtl", PairReason::SameName}};

    int calls = 0;
    SameDataCheck::collapseSameDataPairs(result, [&calls](const DifferingPair &) {
        ++calls;
        return true;
    });

    QCOMPARE(calls, 0);
    QCOMPARE(result.differingPairs.size(), 1);
    QVERIFY(result.sameDataPairs.isEmpty());
    QCOMPARE(classificationOf(result.entriesB, "top.gtl"), EntryClassification::Different);
}

void TestSameDataCheck::nonLayerPairIsNeverOfferedToTheCheck()
{
    ArchiveComparisonResult result;
    result.entriesA = {entry("notes.pdf", EntryClassification::Different, DataKind::OtherData)};
    result.entriesB = {entry("notes.pdf", EntryClassification::Different, DataKind::OtherData)};
    result.differingPairs = {{"notes.pdf", "notes.pdf", PairReason::SameName}};

    int calls = 0;
    SameDataCheck::collapseSameDataPairs(result, [&calls](const DifferingPair &) {
        ++calls;
        return true;
    });

    QCOMPARE(calls, 0);
    QCOMPARE(result.differingPairs.size(), 1);
    QCOMPARE(classificationOf(result.entriesA, "notes.pdf"), EntryClassification::Different);
}

void TestSameDataCheck::summaryCountsSameDataPairAsIdentical()
{
    ArchiveComparisonResult result;
    result.entriesA = {entry("top.gtl", EntryClassification::Different)};
    result.entriesB = {entry("top.gtl", EntryClassification::Different)};
    result.entriesA[0].crc32 = 1;
    result.entriesB[0].crc32 = 2;
    result.differingPairs = {{"top.gtl", "top.gtl", PairReason::SameName}};

    SameDataCheck::collapseSameDataPairs(result, [](const DifferingPair &) { return true; });
    const ComparisonSummary::Summary summary = ComparisonSummary::summarize(result);

    QCOMPARE(summary.gerber.identicalClasses, 1);
    QCOMPARE(summary.gerber.pairs, 0);
    QVERIFY(summary.fullyIdentical());
}

QTEST_MAIN(TestSameDataCheck)
#include "test_samedatacheck.moc"
