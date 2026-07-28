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
    void selfNestedDuplicateIgnoredWithoutCounterpart();
    void firstOccurrenceStillFlaggedWhenTrulyMissing();
    void differingPairFromExactPathMatch();
    void differingPairFromFilenameFallback();
    void noDifferingPairForIdenticalOrOnlyInOne();

    // Content-first matching (docs/superpowers/specs/2026-07-27-content-first-archive-matching-design.md)
    void contentMatchAcrossRename();
    void contentMatchAcrossRelocation();
    void representativeIsShallowestPath();
    void representativeTieAlphabetical();
    void duplicateOfRedClassIsGreen();
    void sameCrcDifferentSizeNotMatched();
    void samePathDifferentContentPairsByName();
    void namePassCrossProductOneToTwo();
    void namePassIsCaseInsensitive();
    void classMatchedUnderRepresentativeName();
    void duplicateOfYellowClassIsGreen();
    void staleInArchiveDuplicateStealsContentMatch();
};

static ArchiveReader::RawFileEntry make(const QString &path, quint32 crc, quint64 size)
{
    ArchiveReader::RawFileEntry entry;
    entry.relativePath = path;
    entry.crc32 = crc;
    entry.sizeBytes = size;
    return entry;
}

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
// 3 stay put. All five must end up green since content is unchanged. Under
// content-first matching this is now a direct content match for every file
// (crc+size identical regardless of path) - no name pass needed at all.
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
// contents merged in as a sibling. Real files match via content (crc+size),
// name-blind; the unrelated file must stay red without any special-case
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

// Two members of the same content class (same crc+size) in one archive
// collapse to a single actionable row; both must inherit the class's final
// status (here: matched against B, so both green).
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

// A file that exists twice in one archive under the same name and content
// (e.g. a nested zip re-embedding a file already present at top level) must
// not show as a false diff just because the *other* archive only has one
// copy to match against. The class's representative (shallowest path)
// matches B normally; the extra copy collapses to green as a duplicate of
// the same content class - not because "this name was already seen".
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

// The duplicate-collapse rule only silences *extra* copies within a class -
// it must not hide a genuine "this content doesn't exist in the other
// archive at all" case. Only the non-representative collapses to green;
// the representative (shallowest path) still surfaces as a real diff.
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

// Differing content, same path on both sides - the simplest case that
// reaches the name pass (content classes differ, but the representatives'
// leaf names match).
void TestArchiveComparator::differingPairFromExactPathMatch()
{
    const QVector<ArchiveReader::RawFileEntry> a = {{"changed.txt", 111, 10}};
    const QVector<ArchiveReader::RawFileEntry> b = {{"changed.txt", 222, 12}};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(result.differingPairs.size(), 1);
    QCOMPARE(result.differingPairs[0].relativePathA, QLatin1String("changed.txt"));
    QCOMPARE(result.differingPairs[0].relativePathB, QLatin1String("changed.txt"));
}

// The name pass matches leaf filenames regardless of directory, so a file
// that was relocated between two differently-organised archives, and whose
// content also changed, still forms a pair - carrying each side's own path.
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

// Content match is name-blind: a byte-identical file that was renamed is
// recognised as unchanged for free, with no pair (nothing to compare).
void TestArchiveComparator::contentMatchAcrossRename()
{
    const QVector<ArchiveReader::RawFileEntry> a = {make("top.gtl", 7, 100)};
    const QVector<ArchiveReader::RawFileEntry> b = {make("renamed.gtl", 7, 100)};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(static_cast<int>(result.entriesA[0].classification), static_cast<int>(EntryClassification::Identical));
    QCOMPARE(static_cast<int>(result.entriesB[0].classification), static_cast<int>(EntryClassification::Identical));
    QCOMPARE(result.differingPairs.size(), 0);
}

// Same as above, but relocated to a different folder rather than renamed.
void TestArchiveComparator::contentMatchAcrossRelocation()
{
    const QVector<ArchiveReader::RawFileEntry> a = {make("top.gtl", 7, 100)};
    const QVector<ArchiveReader::RawFileEntry> b = {make("backup/top.gtl", 7, 100)};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(static_cast<int>(result.entriesA[0].classification), static_cast<int>(EntryClassification::Identical));
    QCOMPARE(static_cast<int>(result.entriesB[0].classification), static_cast<int>(EntryClassification::Identical));
    QCOMPARE(result.differingPairs.size(), 0);
}

// The representative of a content class is the member with the fewest '/'
// separators, never archive (central-directory) order. Here the shallow
// copy is listed second in the input vector, and must still be the one
// that stays red when unmatched in B - the deep copy collapses to green.
void TestArchiveComparator::representativeIsShallowestPath()
{
    const QVector<ArchiveReader::RawFileEntry> a = {make("deep/a/top.gtl", 7, 100), make("top.gtl", 7, 100)};
    const QVector<ArchiveReader::RawFileEntry> b = {};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(result.entriesA.size(), 2);
    QCOMPARE(static_cast<int>(result.entriesA[0].classification), static_cast<int>(EntryClassification::Identical));
    QCOMPARE(static_cast<int>(result.entriesA[1].classification),
             static_cast<int>(EntryClassification::OnlyInThisArchive));
}

// When two members of a class sit at equal depth, the representative is
// chosen by case-insensitive path comparison, not input/archive order.
void TestArchiveComparator::representativeTieAlphabetical()
{
    const QVector<ArchiveReader::RawFileEntry> a = {make("b.gtl", 7, 100), make("a.gtl", 7, 100)};
    const QVector<ArchiveReader::RawFileEntry> b = {};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(static_cast<int>(result.entriesA[0].classification), static_cast<int>(EntryClassification::Identical));
    QCOMPARE(static_cast<int>(result.entriesA[1].classification),
             static_cast<int>(EntryClassification::OnlyInThisArchive));
}

// Explicit restatement of representativeIsShallowestPath's point: a
// duplicate of a class that ends up red (OnlyInThisArchive) is still green
// itself - the non-representative's classification never tracks its
// class's outcome.
void TestArchiveComparator::duplicateOfRedClassIsGreen()
{
    const QVector<ArchiveReader::RawFileEntry> a = {make("deep/a/top.gtl", 7, 100), make("top.gtl", 7, 100)};
    const QVector<ArchiveReader::RawFileEntry> b = {};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(static_cast<int>(result.entriesA[0].classification), static_cast<int>(EntryClassification::Identical));
    QCOMPARE(static_cast<int>(result.entriesA[1].classification),
             static_cast<int>(EntryClassification::OnlyInThisArchive));
}

// Size joins crc32 in the class key purely as a cheap collision guard -
// equal crc with differing size must NOT be treated as a content match.
// The name pass still catches it afterwards (same leaf name, both sides).
void TestArchiveComparator::sameCrcDifferentSizeNotMatched()
{
    const QVector<ArchiveReader::RawFileEntry> a = {make("x.gtl", 7, 100)};
    const QVector<ArchiveReader::RawFileEntry> b = {make("x.gtl", 7, 999)};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(static_cast<int>(result.entriesA[0].classification), static_cast<int>(EntryClassification::Different));
    QCOMPARE(static_cast<int>(result.entriesB[0].classification), static_cast<int>(EntryClassification::Different));
    QCOMPARE(result.differingPairs.size(), 1);
}

// Same filename on both sides, differing content - the case the retired
// exact-path pass used to cover is now covered entirely by the name pass,
// since there is no path-keyed pass left at all.
void TestArchiveComparator::samePathDifferentContentPairsByName()
{
    const QVector<ArchiveReader::RawFileEntry> a = {make("drill.txt", 1, 50)};
    const QVector<ArchiveReader::RawFileEntry> b = {make("drill.txt", 2, 50)};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(static_cast<int>(result.entriesA[0].classification), static_cast<int>(EntryClassification::Different));
    QCOMPARE(static_cast<int>(result.entriesB[0].classification), static_cast<int>(EntryClassification::Different));
    QCOMPARE(result.differingPairs.size(), 1);
    QCOMPARE(result.differingPairs[0].relativePathA, QLatin1String("drill.txt"));
    QCOMPARE(result.differingPairs[0].relativePathB, QLatin1String("drill.txt"));
    QCOMPARE(static_cast<int>(result.differingPairs[0].reason), static_cast<int>(PairReason::SameName));
}

// The name pass forms a full cross product, not one representative pair:
// if B has two distinct-content classes under the same name, the user
// needs to be able to open A's file against each of them.
void TestArchiveComparator::namePassCrossProductOneToTwo()
{
    const QVector<ArchiveReader::RawFileEntry> a = {make("x.gtl", 1, 10)};
    const QVector<ArchiveReader::RawFileEntry> b = {make("x.gtl", 2, 10), make("other/x.gtl", 3, 10)};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(static_cast<int>(result.entriesA[0].classification), static_cast<int>(EntryClassification::Different));
    QCOMPARE(static_cast<int>(result.entriesB[0].classification), static_cast<int>(EntryClassification::Different));
    QCOMPARE(static_cast<int>(result.entriesB[1].classification), static_cast<int>(EntryClassification::Different));
    QCOMPARE(result.differingPairs.size(), 2);
}

// The name pass groups by leaf filename lowercased - a case difference
// alone must not prevent a pair from forming.
void TestArchiveComparator::namePassIsCaseInsensitive()
{
    const QVector<ArchiveReader::RawFileEntry> a = {make("TOP.GTL", 1, 10)};
    const QVector<ArchiveReader::RawFileEntry> b = {make("top.gtl", 2, 10)};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(static_cast<int>(result.entriesA[0].classification), static_cast<int>(EntryClassification::Different));
    QCOMPARE(static_cast<int>(result.entriesB[0].classification), static_cast<int>(EntryClassification::Different));
    QCOMPARE(result.differingPairs.size(), 1);
}

// A class whose members have different names is matched under the
// representative's name only - "sub/x.gtl"'s name never enters matching,
// because "other.gtl" (shallower) is the representative.
void TestArchiveComparator::classMatchedUnderRepresentativeName()
{
    const QVector<ArchiveReader::RawFileEntry> a = {make("sub/x.gtl", 5, 10), make("other.gtl", 5, 10)};
    const QVector<ArchiveReader::RawFileEntry> b = {make("other.gtl", 9, 10)};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(static_cast<int>(result.entriesA[0].classification), static_cast<int>(EntryClassification::Identical));
    QCOMPARE(static_cast<int>(result.entriesA[1].classification), static_cast<int>(EntryClassification::Different));
    QCOMPARE(static_cast<int>(result.entriesB[0].classification), static_cast<int>(EntryClassification::Different));
    QCOMPARE(result.differingPairs.size(), 1);
    QCOMPARE(result.differingPairs[0].relativePathA, QLatin1String("other.gtl"));
    QCOMPARE(result.differingPairs[0].relativePathB, QLatin1String("other.gtl"));
}

// A duplicate within a class that ends up yellow (Different) is still
// green itself - the non-representative never inherits the class's
// matched/paired status either way.
void TestArchiveComparator::duplicateOfYellowClassIsGreen()
{
    const QVector<ArchiveReader::RawFileEntry> a = {make("x.gtl", 7, 100), make("copy/x.gtl", 7, 100)};
    const QVector<ArchiveReader::RawFileEntry> b = {make("x.gtl", 9, 100)};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(static_cast<int>(result.entriesA[0].classification), static_cast<int>(EntryClassification::Different));
    QCOMPARE(static_cast<int>(result.entriesA[1].classification), static_cast<int>(EntryClassification::Identical));
    QCOMPARE(static_cast<int>(result.entriesB[0].classification), static_cast<int>(EntryClassification::Different));
    QCOMPARE(result.differingPairs.size(), 1);
}

// Deliberately accepted consequence of content-match-before-name-pass with
// no set re-alignment (design doc "Explicitly out of scope -> Mixed-
// revision archives"): a stale, byte-identical-to-B copy left inside A
// "steals" the content match away from A's genuinely current file, because
// content matching is name-blind and doesn't know which copy is current.
// A holds the real current file, changed.gbr (crc 100), plus a stale
// leftover copy nested inside backup.zip (crc 200) that happens to equal
// B's current content. Step 3 pairs A's stale copy with B by content
// alone, both go green; A's real current file is left with no B-side
// leftover to pair with by name (B's only class was already consumed), so
// it surfaces as an orphan red rather than a yellow pair. This is intended
// - not a regression to fix - so a future refactor of the matching
// ordering doesn't accidentally re-introduce path-based "real change
// always wins" behaviour without a deliberate decision to do so.
void TestArchiveComparator::staleInArchiveDuplicateStealsContentMatch()
{
    const QVector<ArchiveReader::RawFileEntry> a = {make("changed.gbr", 100, 10), make("backup.zip/changed.gbr", 200, 10)};
    const QVector<ArchiveReader::RawFileEntry> b = {make("changed.gbr", 200, 10)};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(static_cast<int>(result.entriesA[0].classification),
             static_cast<int>(EntryClassification::OnlyInThisArchive));
    QCOMPARE(static_cast<int>(result.entriesA[1].classification), static_cast<int>(EntryClassification::Identical));
    QCOMPARE(static_cast<int>(result.entriesB[0].classification), static_cast<int>(EntryClassification::Identical));
    QCOMPARE(result.differingPairs.size(), 0);
}

QTEST_MAIN(TestArchiveComparator)
#include "test_archivecomparator.moc"
