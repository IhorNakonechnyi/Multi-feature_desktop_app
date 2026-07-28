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

    // Extension elimination pass (design doc step 5, task 5)
    void extensionPassSingleCandidates();
    void extensionPassRefusesTwoCandidates();
    void extensionPassSkipsNoExtension();
    void extensionPassIsCaseInsensitive();
    void nameePassWinsOverExtensionPass();
    void extensionPassIgnoresClassAlreadyPlacedByContentMatch();
    void noOrphanYellow();
    void pairReasonRecorded();
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
// DifferingPair - only genuine Different classifications do. "new"/"old"
// deliberately carry DIFFERENT extensions (.abc / .xyz, not both .txt): with
// a shared extension this fixture would instead exercise the task 5
// extension-elimination pass (exactly one leftover candidate per side under
// that extension is precisely what it pairs), which is a different
// invariant than the one this test names and asserts.
void TestArchiveComparator::noDifferingPairForIdenticalOrOnlyInOne()
{
    const QVector<ArchiveReader::RawFileEntry> a = {{"same.txt", 111, 10}, {"new.abc", 5, 1}};
    const QVector<ArchiveReader::RawFileEntry> b = {{"same.txt", 111, 10}, {"old.xyz", 6, 1}};

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

// Two leftover files, different names, same extension, nothing else on
// either side under that extension - the elimination is unambiguous, so
// they pair up even though nothing about their content or name links them.
void TestArchiveComparator::extensionPassSingleCandidates()
{
    const QVector<ArchiveReader::RawFileEntry> a = {make("old_name.gtl", 7, 100)};
    const QVector<ArchiveReader::RawFileEntry> b = {make("new_name.gtl", 9, 100)};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(static_cast<int>(result.entriesA[0].classification), static_cast<int>(EntryClassification::Different));
    QCOMPARE(static_cast<int>(result.entriesB[0].classification), static_cast<int>(EntryClassification::Different));
    QCOMPARE(result.differingPairs.size(), 1);
    QCOMPARE(result.differingPairs[0].relativePathA, QLatin1String("old_name.gtl"));
    QCOMPARE(result.differingPairs[0].relativePathB, QLatin1String("new_name.gtl"));
    QCOMPARE(static_cast<int>(result.differingPairs[0].reason),
             static_cast<int>(PairReason::OnlyExtensionCandidate));
}

// The guard is deliberately stricter than the name pass's: as soon as
// either side has more than one leftover class under an extension, there is
// no way to tell which one corresponds to which, so nothing is guessed and
// every candidate stays red.
void TestArchiveComparator::extensionPassRefusesTwoCandidates()
{
    const QVector<ArchiveReader::RawFileEntry> a = {make("a1.gtl", 1, 10), make("a2.gtl", 2, 10)};
    const QVector<ArchiveReader::RawFileEntry> b = {make("b1.gtl", 3, 10)};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(static_cast<int>(result.entriesA[0].classification),
             static_cast<int>(EntryClassification::OnlyInThisArchive));
    QCOMPARE(static_cast<int>(result.entriesA[1].classification),
             static_cast<int>(EntryClassification::OnlyInThisArchive));
    QCOMPARE(static_cast<int>(result.entriesB[0].classification),
             static_cast<int>(EntryClassification::OnlyInThisArchive));
    QCOMPARE(result.differingPairs.size(), 0);
}

// A leftover file with no extension at all has no key to eliminate on -
// QFileInfo::suffix() returns an empty string, and an empty suffix is
// skipped entirely rather than treated as a shared "no extension" bucket
// (which would make every extension-less file on both sides a false
// single-candidate match).
void TestArchiveComparator::extensionPassSkipsNoExtension()
{
    const QVector<ArchiveReader::RawFileEntry> a = {make("readme", 1, 10)};
    const QVector<ArchiveReader::RawFileEntry> b = {make("notes", 2, 10)};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(static_cast<int>(result.entriesA[0].classification),
             static_cast<int>(EntryClassification::OnlyInThisArchive));
    QCOMPARE(static_cast<int>(result.entriesB[0].classification),
             static_cast<int>(EntryClassification::OnlyInThisArchive));
    QCOMPARE(result.differingPairs.size(), 0);
}

// Extensions are grouped lowercased, same as the name pass's leaf names -
// a case difference alone must not defeat the elimination.
void TestArchiveComparator::extensionPassIsCaseInsensitive()
{
    const QVector<ArchiveReader::RawFileEntry> a = {make("a.GTL", 1, 10)};
    const QVector<ArchiveReader::RawFileEntry> b = {make("b.gtl", 2, 10)};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(static_cast<int>(result.entriesA[0].classification), static_cast<int>(EntryClassification::Different));
    QCOMPARE(static_cast<int>(result.entriesB[0].classification), static_cast<int>(EntryClassification::Different));
    QCOMPARE(result.differingPairs.size(), 1);
    QCOMPARE(static_cast<int>(result.differingPairs[0].reason),
             static_cast<int>(PairReason::OnlyExtensionCandidate));
}

// A class the name pass already placed must never be revisited by the
// extension pass - both passes insert into the same placedA/placedB sets,
// and the extension pass only looks at what's still unplaced. Here
// "x.gtl" pairs by name; "old.gtl"/"new.gtl" are a genuine leftover
// single-candidate pair under the same extension. The name pair's reason
// must stay SameName, not get overwritten or duplicated by the extension
// pass that runs after it.
void TestArchiveComparator::nameePassWinsOverExtensionPass()
{
    const QVector<ArchiveReader::RawFileEntry> a = {make("x.gtl", 1, 10), make("old.gtl", 3, 20)};
    const QVector<ArchiveReader::RawFileEntry> b = {make("x.gtl", 2, 10), make("new.gtl", 4, 20)};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(result.differingPairs.size(), 2);
    QCOMPARE(result.differingPairs[0].relativePathA, QLatin1String("x.gtl"));
    QCOMPARE(result.differingPairs[0].relativePathB, QLatin1String("x.gtl"));
    QCOMPARE(static_cast<int>(result.differingPairs[0].reason), static_cast<int>(PairReason::SameName));
    QCOMPARE(result.differingPairs[1].relativePathA, QLatin1String("old.gtl"));
    QCOMPARE(result.differingPairs[1].relativePathB, QLatin1String("new.gtl"));
    QCOMPARE(static_cast<int>(result.differingPairs[1].reason),
             static_cast<int>(PairReason::OnlyExtensionCandidate));
}

// A class that content matching already placed must not count toward the
// extension pass's candidate tally for its extension - only genuinely
// UNPLACED classes are candidates. "same.txt" is byte-identical on both
// sides, so step 3 (content match) consumes it and marks it green before
// the extension pass ever runs; that leaves exactly one real leftover class
// per side under ".txt" ("new.txt" in A, "old.txt" in B), which is a
// legitimate single-candidate elimination. If the already-placed "same.txt"
// class were (incorrectly) still counted as a second ".txt" candidate, the
// guard would see two candidates on each side and refuse to pair anything -
// this test would catch that regression, which the noOrphanYellow sweep
// cannot (it only inspects entries already classified Different, so a
// regression that makes the pass stop firing here would silently pass that
// sweep with the leftovers landing on OnlyInThisArchive instead).
void TestArchiveComparator::extensionPassIgnoresClassAlreadyPlacedByContentMatch()
{
    const QVector<ArchiveReader::RawFileEntry> a = {make("same.txt", 111, 10), make("new.txt", 5, 1)};
    const QVector<ArchiveReader::RawFileEntry> b = {make("same.txt", 111, 10), make("old.txt", 6, 1)};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(static_cast<int>(result.entriesA[0].classification), static_cast<int>(EntryClassification::Identical));
    QCOMPARE(static_cast<int>(result.entriesB[0].classification), static_cast<int>(EntryClassification::Identical));
    QCOMPARE(static_cast<int>(result.entriesA[1].classification), static_cast<int>(EntryClassification::Different));
    QCOMPARE(static_cast<int>(result.entriesB[1].classification), static_cast<int>(EntryClassification::Different));
    QCOMPARE(result.differingPairs.size(), 1);
    QCOMPARE(result.differingPairs[0].relativePathA, QLatin1String("new.txt"));
    QCOMPARE(result.differingPairs[0].relativePathB, QLatin1String("old.txt"));
    QCOMPARE(static_cast<int>(result.differingPairs[0].reason),
             static_cast<int>(PairReason::OnlyExtensionCandidate));
}

// Every Different-classified entry, in every fixture used anywhere in this
// file, must belong to at least one differingPairs entry on its side - a
// genuine sweep of the whole file's fixtures rather than a single token
// check, since an orphan yellow (a row the UI colours "go compare this"
// with no counterpart to compare against) is exactly the bug this task's
// stricter guard exists to avoid introducing.
void TestArchiveComparator::noOrphanYellow()
{
    using Entries = QVector<ArchiveReader::RawFileEntry>;
    struct Fixture {
        Entries a;
        Entries b;
    };

    const QVector<Fixture> fixtures = {
        // identicalFile
        {{{"same.txt", 111, 10}}, {{"same.txt", 111, 10}}},
        // differentCrc / differingPairFromExactPathMatch
        {{{"changed.txt", 111, 10}}, {{"changed.txt", 222, 12}}},
        // onlyInArchiveA
        {{{"new.txt", 111, 10}}, {}},
        // onlyInArchiveB
        {{}, {{"old.txt", 111, 10}}},
        // partialSubfolderMove
        {{{"1", 1, 1}, {"2", 2, 1}, {"3", 3, 1}, {"G/12", 12, 1}, {"G/13", 13, 1}},
         {{"1", 1, 1}, {"2", 2, 1}, {"3", 3, 1}, {"12", 12, 1}, {"13", 13, 1}}},
        // buriedSubtreePlusUnrelatedNoise
        {{{"wrapper/archive3.zip/1", 1, 1},
          {"wrapper/archive3.zip/2", 2, 1},
          {"wrapper/archive3.zip/3", 3, 1},
          {"wrapper/archive3.zip/12", 12, 1},
          {"wrapper/archive3.zip/13", 13, 1},
          {"wrapper/archive4.zip/unrelated.txt", 999, 1}},
         {{"1", 1, 1}, {"2", 2, 1}, {"3", 3, 1}, {"12", 12, 1}, {"13", 13, 1}}},
        // duplicateClassificationPropagates
        {{{"folder1/dup.txt", 555, 10}, {"folder2/dup.txt", 555, 10}}, {{"dup.txt", 555, 10}}},
        // selfNestedDuplicateIgnoredWithoutCounterpart
        {{{"layer1.gtl", 111, 10}, {"backup.zip/layer1.gtl", 111, 10}}, {{"layer1.gtl", 111, 10}}},
        // firstOccurrenceStillFlaggedWhenTrulyMissing
        {{{"layer1.gtl", 111, 10}, {"backup.zip/layer1.gtl", 111, 10}}, {}},
        // differingPairFromFilenameFallback
        {{{"G/12", 999, 1}}, {{"12", 12, 1}}},
        // Same shape as noDifferingPairForIdenticalOrOnlyInOne, but kept with
        // its ORIGINAL shared ".txt" extension (unlike that test's own
        // fixture, which was changed to distinct extensions to stay clear of
        // this pass) - deliberately included so the sweep also covers a
        // leftover pair that the extension pass itself places.
        {{{"same.txt", 111, 10}, {"new.txt", 5, 1}}, {{"same.txt", 111, 10}, {"old.txt", 6, 1}}},
        // contentMatchAcrossRename
        {{{"top.gtl", 7, 100}}, {{"renamed.gtl", 7, 100}}},
        // contentMatchAcrossRelocation
        {{{"top.gtl", 7, 100}}, {{"backup/top.gtl", 7, 100}}},
        // representativeIsShallowestPath / duplicateOfRedClassIsGreen
        {{{"deep/a/top.gtl", 7, 100}, {"top.gtl", 7, 100}}, {}},
        // representativeTieAlphabetical
        {{{"b.gtl", 7, 100}, {"a.gtl", 7, 100}}, {}},
        // sameCrcDifferentSizeNotMatched
        {{{"x.gtl", 7, 100}}, {{"x.gtl", 7, 999}}},
        // samePathDifferentContentPairsByName
        {{{"drill.txt", 1, 50}}, {{"drill.txt", 2, 50}}},
        // namePassCrossProductOneToTwo
        {{{"x.gtl", 1, 10}}, {{"x.gtl", 2, 10}, {"other/x.gtl", 3, 10}}},
        // namePassIsCaseInsensitive
        {{{"TOP.GTL", 1, 10}}, {{"top.gtl", 2, 10}}},
        // classMatchedUnderRepresentativeName
        {{{"sub/x.gtl", 5, 10}, {"other.gtl", 5, 10}}, {{"other.gtl", 9, 10}}},
        // duplicateOfYellowClassIsGreen
        {{{"x.gtl", 7, 100}, {"copy/x.gtl", 7, 100}}, {{"x.gtl", 9, 100}}},
        // staleInArchiveDuplicateStealsContentMatch
        {{{"changed.gbr", 100, 10}, {"backup.zip/changed.gbr", 200, 10}}, {{"changed.gbr", 200, 10}}},
        // extensionPassSingleCandidates
        {{{"old_name.gtl", 7, 100}}, {{"new_name.gtl", 9, 100}}},
        // extensionPassRefusesTwoCandidates
        {{{"a1.gtl", 1, 10}, {"a2.gtl", 2, 10}}, {{"b1.gtl", 3, 10}}},
        // extensionPassSkipsNoExtension
        {{{"readme", 1, 10}}, {{"notes", 2, 10}}},
        // extensionPassIsCaseInsensitive
        {{{"a.GTL", 1, 10}}, {{"b.gtl", 2, 10}}},
        // nameePassWinsOverExtensionPass
        {{{"x.gtl", 1, 10}, {"old.gtl", 3, 20}}, {{"x.gtl", 2, 10}, {"new.gtl", 4, 20}}},
    };

    for (int fixtureIndex = 0; fixtureIndex < fixtures.size(); ++fixtureIndex) {
        const Fixture &fixture = fixtures.at(fixtureIndex);
        const ArchiveComparisonResult result = CrcComparator::compare(fixture.a, fixture.b);

        QSet<QString> pairedA;
        QSet<QString> pairedB;
        for (const DifferingPair &pair : result.differingPairs) {
            pairedA.insert(pair.relativePathA);
            pairedB.insert(pair.relativePathB);
        }

        for (const ArchiveEntry &entry : result.entriesA) {
            if (entry.classification == EntryClassification::Different) {
                QVERIFY2(pairedA.contains(entry.relativePath),
                         qPrintable(QStringLiteral("fixture %1: orphan yellow in A: %2")
                                        .arg(fixtureIndex)
                                        .arg(entry.relativePath)));
            }
        }
        for (const ArchiveEntry &entry : result.entriesB) {
            if (entry.classification == EntryClassification::Different) {
                QVERIFY2(pairedB.contains(entry.relativePath),
                         qPrintable(QStringLiteral("fixture %1: orphan yellow in B: %2")
                                        .arg(fixtureIndex)
                                        .arg(entry.relativePath)));
            }
        }
    }
}

// Both reasons asserted explicitly against the enum, in one result: the
// name-matched pair must read SameName, and the elimination-matched pair
// must read OnlyExtensionCandidate - never the other way round or blank.
void TestArchiveComparator::pairReasonRecorded()
{
    const QVector<ArchiveReader::RawFileEntry> a = {make("same.gtl", 1, 10), make("only.gtl", 3, 20)};
    const QVector<ArchiveReader::RawFileEntry> b = {make("same.gtl", 2, 10), make("alone.gtl", 4, 20)};

    const ArchiveComparisonResult result = CrcComparator::compare(a, b);

    QCOMPARE(result.differingPairs.size(), 2);
    QCOMPARE(static_cast<int>(result.differingPairs[0].reason), static_cast<int>(PairReason::SameName));
    QCOMPARE(static_cast<int>(result.differingPairs[1].reason),
             static_cast<int>(PairReason::OnlyExtensionCandidate));
}

QTEST_MAIN(TestArchiveComparator)
#include "test_archivecomparator.moc"
