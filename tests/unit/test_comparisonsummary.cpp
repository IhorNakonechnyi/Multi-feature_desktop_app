#include <QtTest/QtTest>

#include <QHash>
#include <QSet>
#include <QStringList>

#include "archivecompare/archivecomparisonresult.h"
#include "archivecompare/comparisonsummary.h"

// Q8's summary counts CONTENT CLASSES, not tree rows. Everything below is
// really one question asked from different angles: does duplication - the
// same bytes appearing several times, in one archive or across both - ever
// inflate a number the user reads as "amount of work"? It must not.
//
// Fixtures are built by hand rather than run through the comparator, so a
// comparator change can never silently rewrite what these assert.
class TestComparisonSummary : public QObject {
    Q_OBJECT

private slots:
    void identicalClassesCountedOnceGlobally();
    void duplicateRowsDoNotInflate();
    void redCountedPerSideAsClasses();
    void pairsCountedAsRows();
    void kindsSplitGerberVsOther();
    void fullyIdenticalDetected();
    void yellowClassIsNotToCheck();
    void onlyRepresentativeCarriesTheClassification();

    void conclusiveGerberExtensionNotRead();
    void conclusiveOtherExtensionNotRead();
    void txtIsSniffed();
    void unknownExtensionSniffed();
    void noExtensionSniffed();
};

namespace {

ArchiveEntry entry(const QString &path, quint32 crc, quint64 size,
                   EntryClassification classification, DataKind kind = DataKind::OtherData)
{
    ArchiveEntry e;
    e.relativePath = path;
    e.crc32 = crc;
    e.sizeBytes = size;
    e.classification = classification;
    e.kind = kind;
    return e;
}

} // namespace

void TestComparisonSummary::identicalClassesCountedOnceGlobally()
{
    // One content class, present on both sides, but archive A happens to
    // carry two copies of it. The answer is 1: there is one distinct thing,
    // and it is identical. Not 2 (A's rows) and not 3 (all rows).
    ArchiveComparisonResult result;
    result.entriesA = {
        entry("copy1.txt", 0xAA, 10, EntryClassification::Identical),
        entry("nested/copy2.txt", 0xAA, 10, EntryClassification::Identical),
    };
    result.entriesB = {
        entry("copy1.txt", 0xAA, 10, EntryClassification::Identical),
    };

    const auto summary = ComparisonSummary::summarize(result);
    QCOMPARE(summary.other.identicalClasses, 1);
}

void TestComparisonSummary::duplicateRowsDoNotInflate()
{
    // Same idea pushed harder - 3 rows against 1.
    ArchiveComparisonResult result;
    result.entriesA = {
        entry("a.txt", 0xBB, 20, EntryClassification::Identical),
        entry("b.txt", 0xBB, 20, EntryClassification::Identical),
        entry("deep/c.txt", 0xBB, 20, EntryClassification::Identical),
    };
    result.entriesB = {
        entry("a.txt", 0xBB, 20, EntryClassification::Identical),
    };

    const auto summary = ComparisonSummary::summarize(result);
    QCOMPARE(summary.other.identicalClasses, 1);
}

void TestComparisonSummary::redCountedPerSideAsClasses()
{
    // Two red ROWS in A that are byte-identical to each other: one class,
    // so one thing for the user to check, not two.
    ArchiveComparisonResult result;
    result.entriesA = {
        entry("gone1.txt", 0xCC, 30, EntryClassification::OnlyInThisArchive),
        entry("gone2.txt", 0xCC, 30, EntryClassification::OnlyInThisArchive),
    };

    const auto summary = ComparisonSummary::summarize(result);
    QCOMPARE(summary.other.onlyInA, 1);
    QCOMPARE(summary.other.onlyInB, 0);
}

void TestComparisonSummary::pairsCountedAsRows()
{
    // Pairs are the exception to class counting: each pair is a separate
    // comparison the user can open, so a cross-product of two really is two.
    ArchiveComparisonResult result;
    result.entriesA = {
        entry("x.txt", 0x01, 40, EntryClassification::Different),
        entry("y.txt", 0x02, 41, EntryClassification::Different),
    };
    result.entriesB = {
        entry("x.txt", 0x03, 42, EntryClassification::Different),
        entry("y.txt", 0x04, 43, EntryClassification::Different),
    };
    result.differingPairs = {
        { "x.txt", "x.txt", PairReason::SameName },
        { "y.txt", "y.txt", PairReason::SameName },
    };

    const auto summary = ComparisonSummary::summarize(result);
    QCOMPARE(summary.other.pairs, 2);
}

void TestComparisonSummary::kindsSplitGerberVsOther()
{
    // One identical Gerber class and one identical other class must land in
    // separate buckets - the whole reason the line is split.
    ArchiveComparisonResult result;
    result.entriesA = {
        entry("top.gtl", 0x11, 50, EntryClassification::Identical, DataKind::GerberData),
        entry("readme.txt", 0x22, 60, EntryClassification::Identical, DataKind::OtherData),
    };
    result.entriesB = result.entriesA;

    const auto summary = ComparisonSummary::summarize(result);
    QCOMPARE(summary.gerber.identicalClasses, 1);
    QCOMPARE(summary.other.identicalClasses, 1);
}

void TestComparisonSummary::fullyIdenticalDetected()
{
    ArchiveComparisonResult result;
    result.entriesA = { entry("same.txt", 0x33, 70, EntryClassification::Identical) };
    result.entriesB = { entry("same.txt", 0x33, 70, EntryClassification::Identical) };

    QVERIFY(ComparisonSummary::summarize(result).fullyIdentical());

    // One red row on either side is enough to break it.
    result.entriesA.append(entry("extra.txt", 0x44, 80, EntryClassification::OnlyInThisArchive));
    QVERIFY(!ComparisonSummary::summarize(result).fullyIdentical());
}

void TestComparisonSummary::yellowClassIsNotToCheck()
{
    // The subtle one. Both files are Different: content-first found no key
    // match, so each side's class key IS absent from the other side - the
    // same shape as a red class. But they are paired and already accounted
    // for as a pair, so counting them as "only in A/B" would double-report
    // the same work AND colour it wrongly. Only a row actually classified
    // OnlyInThisArchive counts as missing.
    ArchiveComparisonResult result;
    result.entriesA = { entry("changed.txt", 0x55, 90, EntryClassification::Different) };
    result.entriesB = { entry("changed.txt", 0x66, 91, EntryClassification::Different) };
    result.differingPairs = { { "changed.txt", "changed.txt", PairReason::SameName } };

    const auto summary = ComparisonSummary::summarize(result);
    QCOMPARE(summary.other.onlyInA, 0);
    QCOMPARE(summary.other.onlyInB, 0);
    QCOMPARE(summary.other.pairs, 1);
}

void TestComparisonSummary::onlyRepresentativeCarriesTheClassification()
{
    // The shape real CrcComparator output actually takes, and the reason
    // summarize() may reduce a class with "any member" instead of having to
    // re-derive which row is the representative: the comparator forces every
    // NON-representative member of a class to plain Identical, so a red
    // class looks like one red row plus green ones. Rows of a class do NOT
    // all share a classification - only the representative carries it.
    //
    // If summarize() ever switched to "all members" or "the first member in
    // iteration order", this class would silently stop counting as missing.
    ArchiveComparisonResult result;
    result.entriesA = {
        entry("deep/nested/copy.txt", 0x77, 100, EntryClassification::Identical),
        entry("shallow.txt", 0x77, 100, EntryClassification::OnlyInThisArchive), // representative
        entry("another/copy.txt", 0x77, 100, EntryClassification::Identical),
    };

    const auto summary = ComparisonSummary::summarize(result);
    QCOMPARE(summary.other.onlyInA, 1);
    QCOMPARE(summary.other.identicalClasses, 0); // one-sided, so not identical
}

// --- Task 15: kind resolution -------------------------------------------
//
// Every case below asserts BOTH the resulting kind AND exactly which paths
// the reader was asked for. The read set is the load-bearing half: without
// it, "sniffed and found to be Other" is indistinguishable from "never
// looked at", since OtherData is also the default.

namespace {

// Records what got read, so a test can tell trusting an extension apart
// from sniffing a file that happened to give the same answer.
struct RecordingReader {
    QHash<QString, QByteArray> heads;
    mutable QStringList readPaths;

    ComparisonSummary::HeadReader reader()
    {
        return [this](const QString &path) {
            readPaths << path;
            return heads.value(path);
        };
    }
};

ArchiveEntry pathOnly(const QString &path)
{
    ArchiveEntry e;
    e.relativePath = path;
    return e;
}

} // namespace

void TestComparisonSummary::conclusiveGerberExtensionNotRead()
{
    QVector<ArchiveEntry> entries = { pathOnly("gerber/top.gtl") };
    RecordingReader reader;

    ComparisonSummary::resolveKinds(entries, reader.reader());

    QCOMPARE(entries.at(0).kind, DataKind::GerberData);
    QVERIFY2(reader.readPaths.isEmpty(), "a conclusive extension must not cost a read");
}

void TestComparisonSummary::conclusiveOtherExtensionNotRead()
{
    QVector<ArchiveEntry> entries = { pathOnly("docs/spec.pdf") };
    RecordingReader reader;

    ComparisonSummary::resolveKinds(entries, reader.reader());

    QCOMPARE(entries.at(0).kind, DataKind::OtherData);
    QVERIFY2(reader.readPaths.isEmpty(), "a conclusive extension must not cost a read");
}

void TestComparisonSummary::txtIsSniffed()
{
    // The case the whole rule exists for: the real drill file in the sample
    // data is a .TXT. Trusting the extension would file it under Other and
    // the summary would claim there is nothing previewable to look at.
    QVector<ArchiveEntry> entries = { pathOnly("drill.txt"), pathOnly("notes.txt") };
    RecordingReader reader;
    reader.heads.insert("drill.txt", "M48\nFMAT,2\n");
    reader.heads.insert("notes.txt", "hello, this is not layer data");

    ComparisonSummary::resolveKinds(entries, reader.reader());

    QCOMPARE(entries.at(0).kind, DataKind::GerberData);
    QCOMPARE(entries.at(1).kind, DataKind::OtherData);
    QCOMPARE(QSet<QString>(reader.readPaths.begin(), reader.readPaths.end()),
             QSet<QString>({ "drill.txt", "notes.txt" }));
}

void TestComparisonSummary::unknownExtensionSniffed()
{
    QVector<ArchiveEntry> entries = { pathOnly("layer.g1") };
    RecordingReader reader;
    reader.heads.insert("layer.g1", "%FSLAX35Y35*%\n%MOMM*%\n");

    ComparisonSummary::resolveKinds(entries, reader.reader());

    QCOMPARE(entries.at(0).kind, DataKind::GerberData);
    QCOMPARE(reader.readPaths, QStringList({ "layer.g1" }));
}

void TestComparisonSummary::noExtensionSniffed()
{
    QVector<ArchiveEntry> entries = { pathOnly("readme") };
    RecordingReader reader;
    reader.heads.insert("readme", "just some text");

    ComparisonSummary::resolveKinds(entries, reader.reader());

    QCOMPARE(entries.at(0).kind, DataKind::OtherData);
    // Asserting the read happened is what stops this passing by default.
    QCOMPARE(reader.readPaths, QStringList({ "readme" }));
}

QTEST_MAIN(TestComparisonSummary)
#include "test_comparisonsummary.moc"
