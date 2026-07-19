#include <QtTest/QtTest>

#include "archivecompare/archivecomparisonresult.h"
#include "archivecompare/archivereader.h"
#include "archivecompare/crccomparator.h"

// Comparisons use static_cast<int> rather than QCOMPARE'ing the enum
// directly - EntryClassification has no QTest::toString overload, and
// casting to int keeps the failure-message machinery simple for a first
// Qt/C++ test file (constitution Principle 1).
class TestArchiveComparator : public QObject {
    Q_OBJECT

private slots:
    void identicalFile();
    void differentCrc();
    void onlyInArchiveA();
    void onlyInArchiveB();
    void partialSubfolderMove();
    void buriedSubtreePlusUnrelatedNoise();
    void duplicateClassificationPropagates();
    void secondSameNameEntryIgnoredEvenWhenFirstMatchesViaFallback();
    void selfNestedDuplicateIgnoredWithoutCounterpart();
    void firstOccurrenceStillFlaggedWhenTrulyMissing();
    void sameNameDifferentCrcStillTreatedAsSelfDuplicate();
    void realChangeSurfacesDespiteStaleNestedDuplicate();
    void differingPairFromExactPathMatch();
    void differingPairFromFilenameFallback();
    void noDifferingPairForIdenticalOrOnlyInOne();
};

void TestArchiveComparator::identicalFile()
{
    const QVector<ArchiveReader::RawFileEntry> a = {{"same.txt", 111, 10}};
    const QVector<ArchiveReader::RawFileEntry> b = {{"same.txt", 111, 10}};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(result.entriesA.size(), 1);
    QCOMPARE(static_cast<int>(result.entriesA[0].classification), static_cast<int>(EntryClassification::Identical));
    QCOMPARE(result.entriesB.size(), 1);
    QCOMPARE(static_cast<int>(result.entriesB[0].classification), static_cast<int>(EntryClassification::Identical));
}

void TestArchiveComparator::differentCrc()
{
    const QVector<ArchiveReader::RawFileEntry> a = {{"changed.txt", 111, 10}};
    const QVector<ArchiveReader::RawFileEntry> b = {{"changed.txt", 222, 12}};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(static_cast<int>(result.entriesA[0].classification), static_cast<int>(EntryClassification::Different));
    QCOMPARE(static_cast<int>(result.entriesB[0].classification), static_cast<int>(EntryClassification::Different));
}

void TestArchiveComparator::onlyInArchiveA()
{
    const QVector<ArchiveReader::RawFileEntry> a = {{"new.txt", 111, 10}};
    const QVector<ArchiveReader::RawFileEntry> b = {};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(result.entriesA.size(), 1);
    QCOMPARE(static_cast<int>(result.entriesA[0].classification),
             static_cast<int>(EntryClassification::OnlyInThisArchive));
    QCOMPARE(result.entriesB.size(), 0);
}

void TestArchiveComparator::onlyInArchiveB()
{
    const QVector<ArchiveReader::RawFileEntry> a = {};
    const QVector<ArchiveReader::RawFileEntry> b = {{"old.txt", 111, 10}};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(result.entriesA.size(), 0);
    QCOMPARE(result.entriesB.size(), 1);
    QCOMPARE(static_cast<int>(result.entriesB[0].classification),
             static_cast<int>(EntryClassification::OnlyInThisArchive));
}

// Case 2 from design discussion: 2 of 5 files move into a new subfolder,
// 3 stay put. All five must end up green since content is unchanged -
// exercises Pass 1 (unmoved files) and Pass 2's filename fallback (moved
// files) together.
void TestArchiveComparator::partialSubfolderMove()
{
    const QVector<ArchiveReader::RawFileEntry> a = {
        {"1", 1, 1}, {"2", 2, 1}, {"3", 3, 1}, {"G/12", 12, 1}, {"G/13", 13, 1}};
    const QVector<ArchiveReader::RawFileEntry> b = {{"1", 1, 1}, {"2", 2, 1}, {"3", 3, 1}, {"12", 12, 1}, {"13", 13, 1}};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(result.entriesA.size(), 5);
    for (const auto &entry : result.entriesA) {
        QCOMPARE(static_cast<int>(entry.classification), static_cast<int>(EntryClassification::Identical));
    }
    QCOMPARE(result.entriesB.size(), 5);
    for (const auto &entry : result.entriesB) {
        QCOMPARE(static_cast<int>(entry.classification), static_cast<int>(EntryClassification::Identical));
    }
}

// Case 3 from design discussion: the real files are buried under a wrapper
// folder in the new archive, alongside an unrelated nested archive's
// contents merged in as a sibling. Real files must match via filename
// fallback; the unrelated file must stay red without any special-case
// "detect the junk" logic.
void TestArchiveComparator::buriedSubtreePlusUnrelatedNoise()
{
    const QVector<ArchiveReader::RawFileEntry> a = {
        {"wrapper/archive3.zip/1", 1, 1},
        {"wrapper/archive3.zip/2", 2, 1},
        {"wrapper/archive3.zip/3", 3, 1},
        {"wrapper/archive3.zip/12", 12, 1},
        {"wrapper/archive3.zip/13", 13, 1},
        {"wrapper/archive4.zip/unrelated.txt", 999, 1},
    };
    const QVector<ArchiveReader::RawFileEntry> b = {{"1", 1, 1}, {"2", 2, 1}, {"3", 3, 1}, {"12", 12, 1}, {"13", 13, 1}};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(result.entriesA.size(), 6);
    for (const auto &entry : result.entriesA) {
        const auto expected = entry.relativePath == QLatin1String("wrapper/archive4.zip/unrelated.txt")
            ? EntryClassification::OnlyInThisArchive
            : EntryClassification::Identical;
        QCOMPARE(static_cast<int>(entry.classification), static_cast<int>(expected));
    }
    QCOMPARE(result.entriesB.size(), 5);
    for (const auto &entry : result.entriesB) {
        QCOMPARE(static_cast<int>(entry.classification), static_cast<int>(EntryClassification::Identical));
    }
}

// True duplicates (same filename and CRC, copied into multiple subfolders
// of one archive) must all inherit the same classification once one of
// them is matched, rather than only the first copy encountered.
void TestArchiveComparator::duplicateClassificationPropagates()
{
    const QVector<ArchiveReader::RawFileEntry> a = {
        {"folder1/dup.txt", 555, 10}, {"folder2/dup.txt", 555, 10}};
    const QVector<ArchiveReader::RawFileEntry> b = {{"dup.txt", 555, 10}};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(result.entriesA.size(), 2);
    QCOMPARE(static_cast<int>(result.entriesA[0].classification), static_cast<int>(EntryClassification::Identical));
    QCOMPARE(static_cast<int>(result.entriesA[1].classification), static_cast<int>(EntryClassification::Identical));
    QCOMPARE(static_cast<int>(result.entriesB[0].classification), static_cast<int>(EntryClassification::Identical));
}

// Same filename, different content, in two folders on one side - the
// second occurrence is now unconditionally treated as a duplicate of the
// first (per the filename-only dedup rule), so it never reaches Pass 2 at
// all; only the first (canonical) copy is a real candidate, and it matches
// B's single same-named file normally via the filename fallback. This
// replaces the old "leave both unmatched, ambiguous" behavior - a
// consequence of the dedup rule keying on filename alone (see
// selfDuplicateIndices), confirmed as the intended behavior against a real
// supplier archive that needed it.
void TestArchiveComparator::secondSameNameEntryIgnoredEvenWhenFirstMatchesViaFallback()
{
    const QVector<ArchiveReader::RawFileEntry> a = {
        {"folder1/conflict.txt", 100, 10}, {"folder2/conflict.txt", 200, 10}};
    const QVector<ArchiveReader::RawFileEntry> b = {{"conflict.txt", 100, 10}};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(result.entriesA.size(), 2);
    QCOMPARE(static_cast<int>(result.entriesA[0].classification), static_cast<int>(EntryClassification::Identical));
    QCOMPARE(static_cast<int>(result.entriesA[1].classification), static_cast<int>(EntryClassification::Identical));
    QCOMPARE(static_cast<int>(result.entriesB[0].classification),
             static_cast<int>(EntryClassification::Identical));
}

// A file accidentally re-embedded inside its own archive (e.g. a nested zip
// that duplicates files already present at top level - "archive contains a
// copy of itself") must not show as a false OnlyInThisArchive diff just
// because the *other* archive has no matching leftover to pair it with via
// Pass 2. The canonical (first-seen) copy matches B normally; the extra
// internal copy is recognized as a same-name/same-CRC duplicate of it and
// forced green unconditionally.
void TestArchiveComparator::selfNestedDuplicateIgnoredWithoutCounterpart()
{
    const QVector<ArchiveReader::RawFileEntry> a = {
        {"layer1.gtl", 111, 10}, {"backup.zip/layer1.gtl", 111, 10}};
    const QVector<ArchiveReader::RawFileEntry> b = {{"layer1.gtl", 111, 10}};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(result.entriesA.size(), 2);
    QCOMPARE(static_cast<int>(result.entriesA[0].classification), static_cast<int>(EntryClassification::Identical));
    QCOMPARE(static_cast<int>(result.entriesA[1].classification), static_cast<int>(EntryClassification::Identical));
    QCOMPARE(result.entriesB.size(), 1);
    QCOMPARE(static_cast<int>(result.entriesB[0].classification), static_cast<int>(EntryClassification::Identical));
    QCOMPARE(result.differingPairs.size(), 0);
}

// The duplicate-suppression rule only silences *extra* internal copies -
// it must not hide a genuine "this file (and its accidental duplicate)
// don't exist in the other archive at all" case. Only entriesA[1] (the
// second occurrence) is forced green; entriesA[0], the first occurrence,
// still surfaces as a real OnlyInThisArchive diff.
void TestArchiveComparator::firstOccurrenceStillFlaggedWhenTrulyMissing()
{
    const QVector<ArchiveReader::RawFileEntry> a = {
        {"layer1.gtl", 111, 10}, {"backup.zip/layer1.gtl", 111, 10}};
    const QVector<ArchiveReader::RawFileEntry> b = {};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(result.entriesA.size(), 2);
    QCOMPARE(static_cast<int>(result.entriesA[0].classification),
             static_cast<int>(EntryClassification::OnlyInThisArchive));
    QCOMPARE(static_cast<int>(result.entriesA[1].classification), static_cast<int>(EntryClassification::Identical));
}

// A real archive can contain a nested self-copy that is NOT byte-identical
// to the top-level file it duplicates - e.g. a stale leftover of an older
// revision, accidentally left inside a re-zipped folder. The dedup rule
// must key on filename ALONE, not (filename, crc): "this name already
// appeared in this archive" is what makes something an accidental
// duplicate to ignore, regardless of whether its content happens to also
// match. Content is irrelevant to *whether* something is a duplicate - it
// only matters for the canonical (first) copy's actual comparison against
// the other archive. Confirmed against a real supplier archive pair that
// exposed this exact shape (differing-content duplicate) after the
// filename+crc version of this rule shipped.
void TestArchiveComparator::sameNameDifferentCrcStillTreatedAsSelfDuplicate()
{
    const QVector<ArchiveReader::RawFileEntry> a = {
        {"folder1/x.txt", 100, 10}, {"folder2/x.txt", 200, 10}};
    const QVector<ArchiveReader::RawFileEntry> b = {};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(result.entriesA.size(), 2);
    QCOMPARE(static_cast<int>(result.entriesA[0].classification),
             static_cast<int>(EntryClassification::OnlyInThisArchive));
    QCOMPARE(static_cast<int>(result.entriesA[1].classification), static_cast<int>(EntryClassification::Identical));
}

// The production case the rule above guards: archive A's canonical file
// genuinely changed since B; a stale nested duplicate of the OLD content
// was accidentally left inside A too. Ignoring the duplicate (by filename
// alone) must not just silence it - the canonical copy must still find
// and correctly diff against B, rather than the duplicate's mismatched CRC
// making the filename group look "ambiguous" and swallowing the real
// change (the bug this test was written to catch).
void TestArchiveComparator::realChangeSurfacesDespiteStaleNestedDuplicate()
{
    const QVector<ArchiveReader::RawFileEntry> a = {
        {"changed.gbr", 100, 10}, {"backup.zip/changed.gbr", 200, 10}};
    const QVector<ArchiveReader::RawFileEntry> b = {{"changed.gbr", 200, 10}};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(result.entriesA.size(), 2);
    QCOMPARE(static_cast<int>(result.entriesA[0].classification), static_cast<int>(EntryClassification::Different));
    QCOMPARE(static_cast<int>(result.entriesA[1].classification), static_cast<int>(EntryClassification::Identical));
    QCOMPARE(result.entriesB.size(), 1);
    QCOMPARE(static_cast<int>(result.entriesB[0].classification), static_cast<int>(EntryClassification::Different));
    QCOMPARE(result.differingPairs.size(), 1);
    QCOMPARE(result.differingPairs[0].relativePathA, QLatin1String("changed.gbr"));
    QCOMPARE(result.differingPairs[0].relativePathB, QLatin1String("changed.gbr"));
}

// Pass 1 (exact path match) with differing CRC must record a DifferingPair
// with the same path on both sides.
void TestArchiveComparator::differingPairFromExactPathMatch()
{
    const QVector<ArchiveReader::RawFileEntry> a = {{"changed.txt", 111, 10}};
    const QVector<ArchiveReader::RawFileEntry> b = {{"changed.txt", 222, 12}};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(result.differingPairs.size(), 1);
    QCOMPARE(result.differingPairs[0].relativePathA, QLatin1String("changed.txt"));
    QCOMPARE(result.differingPairs[0].relativePathB, QLatin1String("changed.txt"));
}

// Pass 2 (filename fallback) with differing CRC must still record a
// DifferingPair, carrying each side's own (different) path.
void TestArchiveComparator::differingPairFromFilenameFallback()
{
    const QVector<ArchiveReader::RawFileEntry> a = {{"G/12", 999, 1}};
    const QVector<ArchiveReader::RawFileEntry> b = {{"12", 12, 1}};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(result.differingPairs.size(), 1);
    QCOMPARE(result.differingPairs[0].relativePathA, QLatin1String("G/12"));
    QCOMPARE(result.differingPairs[0].relativePathB, QLatin1String("12"));
}

// Identical and OnlyInThisArchive entries must never produce a
// DifferingPair - only genuine Different classifications do.
void TestArchiveComparator::noDifferingPairForIdenticalOrOnlyInOne()
{
    const QVector<ArchiveReader::RawFileEntry> a = {{"same.txt", 111, 10}, {"new.txt", 5, 1}};
    const QVector<ArchiveReader::RawFileEntry> b = {{"same.txt", 111, 10}, {"old.txt", 6, 1}};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(result.differingPairs.size(), 0);
}

QTEST_MAIN(TestArchiveComparator)
#include "test_archivecomparator.moc"
