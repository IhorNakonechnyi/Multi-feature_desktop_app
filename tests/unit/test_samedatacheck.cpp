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
    void layerWithNoObjectsIsNeverSameData();
    void gerberAndDrillAreNeverSameData();
    void unparseableBytesAreNeverSameData();

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

    // Pair collapse and summary
    void sameDataPairTurnsBothFilesGreen();
    void differentPairStaysYellow();
    void fileStillInAnotherPairStaysYellow();
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

void TestSameDataCheck::fileStillInAnotherPairStaysYellow()
{
    // The name pass paired A's top.gtl with two B files. It matches the first
    // but not the second, so A's file still has something to look at.
    ArchiveComparisonResult result;
    result.entriesA = {entry("top.gtl", EntryClassification::Different)};
    result.entriesB = {entry("v1/top.gtl", EntryClassification::Different),
                       entry("v2/top.gtl", EntryClassification::Different)};
    result.differingPairs = {{"top.gtl", "v1/top.gtl", PairReason::SameName},
                             {"top.gtl", "v2/top.gtl", PairReason::SameName}};

    SameDataCheck::collapseSameDataPairs(result,
                                         [](const DifferingPair &pair) { return pair.relativePathB == "v1/top.gtl"; });

    QCOMPARE(result.differingPairs.size(), 1);
    QCOMPARE(result.differingPairs.first().relativePathB, QString("v2/top.gtl"));
    QCOMPARE(classificationOf(result.entriesA, "top.gtl"), EntryClassification::Different);
    QCOMPARE(classificationOf(result.entriesB, "v1/top.gtl"), EntryClassification::Identical);
    QCOMPARE(classificationOf(result.entriesB, "v2/top.gtl"), EntryClassification::Different);
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
