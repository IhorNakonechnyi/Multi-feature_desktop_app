#include <QtTest/QtTest>

#include <algorithm>

#include <QBuffer>
#include <QFile>
#include <QTemporaryDir>

#include <quazip.h>
#include <quazipfile.h>

#include "archivecompare/archivecomparison.h"

// Writes one entry (name -> content) into an already-open-for-writing zip.
// Copied from test_archivereader.cpp per the task brief - same helper, same
// job: let each test describe only the archive layout it cares about.
static void addEntry(QuaZip &zip, const QString &name, const QByteArray &content)
{
    QuaZipFile file(&zip);
    QVERIFY(file.open(QIODevice::WriteOnly, QuaZipNewInfo(name)));
    file.write(content);
    file.close();
}

// Finds an ArchiveEntry by relativePath, or entries.end() if not present -
// shared by several tests below to make the presence/absence assertions
// read as plain statements rather than repeated std::find_if boilerplate.
static QVector<ArchiveEntry>::const_iterator findByPath(const QVector<ArchiveEntry> &entries, const QString &path)
{
    return std::find_if(entries.begin(), entries.end(), [&path](const ArchiveEntry &e) { return e.relativePath == path; });
}

class TestArchiveComparison : public QObject {
    Q_OBJECT

private slots:
    // A and B both contain a byte-identical "inner.zip". The whole point of
    // the two-phase design (Q4 point 3) is that a container that cannot
    // possibly contain anything different must never be opened - it stays
    // one green row standing for everything inside it. Paired with positive
    // assertions about the row that IS there (size 1, path, classification)
    // so a no-op compareArchives can't pass this vacuously - see the "no
    // inner.zip/... rows" check alone would also be true of an empty result.
    void identicalNestedArchiveStaysClosed();

    // A's inner.zip has {"a.gtl","X"}, B's has {"a.gtl","Y"} - genuinely
    // different content. inner.zip itself must disappear as a row (it is
    // replaced by its own contents, not kept alongside them) and
    // "inner.zip/a.gtl" must appear on both sides, classified Different and
    // paired.
    void differingNestedArchiveExpands();

    // inner.zip exists only in A. Its contents must still surface (the
    // "mixed revision" consequence: a one-sided container is exactly as
    // interesting as a one-sided plain file) rather than staying collapsed
    // as an opaque, unopened row.
    void oneSidedNestedArchiveExpands();

    // "notes.zip" is a plain text file, not a real archive, with different
    // bytes on each side. It must show as a single row per side (matched by
    // name, since the bytes differ) and never explode into child rows -
    // content decides, not the ".zip" name (Q3). If the orchestrator ever
    // failed to clear the "candidate" flag on a non-zip after trying it,
    // this case would also hang (endless re-attempts every loop iteration)
    // rather than merely produce a wrong answer.
    void misnamedZipComparedAsFile();

    // pathA points at a plain (non-zip) file. The top-level read must fail
    // for A only, reported as data (errorA), with B's side (a normal valid
    // archive) unaffected and the result left empty - never a thrown
    // exception, never a dialog.
    void unreadableArchiveReportsError();

    // A differing "deep.zip" sits inside a differing "inner.zip" on both
    // sides. Getting to the leaf row requires the loop to run twice -
    // expand inner.zip (revealing deep.zip as a fresh candidate), then
    // expand deep.zip (revealing its leaf file) - proving the "level by
    // level" repeat, not just a single expand pass.
    void nestedInsideNestedExpands();
};

void TestArchiveComparison::identicalNestedArchiveStaysClosed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QByteArray innerZipBytes;
    QBuffer innerBuffer(&innerZipBytes);
    QVERIFY(innerBuffer.open(QIODevice::WriteOnly));
    QuaZip innerZip(&innerBuffer);
    QVERIFY(innerZip.open(QuaZip::mdCreate));
    addEntry(innerZip, "a.gtl", "G04 inside nested zip*\n");
    innerZip.close();
    innerBuffer.close();

    const QString pathA = dir.filePath("a.zip");
    QuaZip zipA(pathA);
    QVERIFY(zipA.open(QuaZip::mdCreate));
    addEntry(zipA, "inner.zip", innerZipBytes); // byte-identical to B's copy below
    zipA.close();

    const QString pathB = dir.filePath("b.zip");
    QuaZip zipB(pathB);
    QVERIFY(zipB.open(QuaZip::mdCreate));
    addEntry(zipB, "inner.zip", innerZipBytes);
    zipB.close();

    const ArchiveComparison::Outcome outcome = ArchiveComparison::compareArchives(pathA, pathB);
    QVERIFY(outcome.errorA.isEmpty());
    QVERIFY(outcome.errorB.isEmpty());

    // Positive assertions first: exactly one row per side, and it IS
    // inner.zip itself, classified Identical.
    QCOMPARE(outcome.result.entriesA.size(), 1);
    QCOMPARE(outcome.result.entriesA.first().relativePath, QString("inner.zip"));
    QCOMPARE(outcome.result.entriesA.first().classification, EntryClassification::Identical);
    QCOMPARE(outcome.result.entriesB.size(), 1);
    QCOMPARE(outcome.result.entriesB.first().relativePath, QString("inner.zip"));
    QCOMPARE(outcome.result.entriesB.first().classification, EntryClassification::Identical);

    // Now the negative half: nothing from inside inner.zip leaked out.
    QVERIFY(findByPath(outcome.result.entriesA, "inner.zip/a.gtl") == outcome.result.entriesA.end());
    QVERIFY(findByPath(outcome.result.entriesB, "inner.zip/a.gtl") == outcome.result.entriesB.end());
}

void TestArchiveComparison::differingNestedArchiveExpands()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QByteArray innerZipBytesA;
    QBuffer innerBufferA(&innerZipBytesA);
    QVERIFY(innerBufferA.open(QIODevice::WriteOnly));
    QuaZip innerZipA(&innerBufferA);
    QVERIFY(innerZipA.open(QuaZip::mdCreate));
    addEntry(innerZipA, "a.gtl", "X");
    innerZipA.close();
    innerBufferA.close();

    QByteArray innerZipBytesB;
    QBuffer innerBufferB(&innerZipBytesB);
    QVERIFY(innerBufferB.open(QIODevice::WriteOnly));
    QuaZip innerZipB(&innerBufferB);
    QVERIFY(innerZipB.open(QuaZip::mdCreate));
    addEntry(innerZipB, "a.gtl", "Y");
    innerZipB.close();
    innerBufferB.close();

    const QString pathA = dir.filePath("a.zip");
    QuaZip zipA(pathA);
    QVERIFY(zipA.open(QuaZip::mdCreate));
    addEntry(zipA, "inner.zip", innerZipBytesA);
    zipA.close();

    const QString pathB = dir.filePath("b.zip");
    QuaZip zipB(pathB);
    QVERIFY(zipB.open(QuaZip::mdCreate));
    addEntry(zipB, "inner.zip", innerZipBytesB);
    zipB.close();

    const ArchiveComparison::Outcome outcome = ArchiveComparison::compareArchives(pathA, pathB);
    QVERIFY(outcome.errorA.isEmpty());
    QVERIFY(outcome.errorB.isEmpty());

    // The container row itself is gone - replaced by its contents, not kept
    // alongside them.
    QVERIFY(findByPath(outcome.result.entriesA, "inner.zip") == outcome.result.entriesA.end());
    QVERIFY(findByPath(outcome.result.entriesB, "inner.zip") == outcome.result.entriesB.end());

    QCOMPARE(outcome.result.entriesA.size(), 1);
    const auto leafA = findByPath(outcome.result.entriesA, "inner.zip/a.gtl");
    QVERIFY(leafA != outcome.result.entriesA.end());
    QCOMPARE(leafA->classification, EntryClassification::Different);

    QCOMPARE(outcome.result.entriesB.size(), 1);
    const auto leafB = findByPath(outcome.result.entriesB, "inner.zip/a.gtl");
    QVERIFY(leafB != outcome.result.entriesB.end());
    QCOMPARE(leafB->classification, EntryClassification::Different);

    QCOMPARE(outcome.result.differingPairs.size(), 1);
    QCOMPARE(outcome.result.differingPairs.first().relativePathA, QString("inner.zip/a.gtl"));
    QCOMPARE(outcome.result.differingPairs.first().relativePathB, QString("inner.zip/a.gtl"));
}

void TestArchiveComparison::oneSidedNestedArchiveExpands()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QByteArray innerZipBytes;
    QBuffer innerBuffer(&innerZipBytes);
    QVERIFY(innerBuffer.open(QIODevice::WriteOnly));
    QuaZip innerZip(&innerBuffer);
    QVERIFY(innerZip.open(QuaZip::mdCreate));
    addEntry(innerZip, "a.gtl", "X");
    innerZip.close();
    innerBuffer.close();

    const QString pathA = dir.filePath("a.zip");
    QuaZip zipA(pathA);
    QVERIFY(zipA.open(QuaZip::mdCreate));
    addEntry(zipA, "inner.zip", innerZipBytes);
    zipA.close();

    // B has nothing whose content, name, or extension could plausibly match
    // inner.zip or anything inside it - keeps this test deterministic
    // (OnlyInThisArchive, not the content-matched-green branch of the
    // "mixed revision" consequence, which is a different scenario).
    const QString pathB = dir.filePath("b.zip");
    QuaZip zipB(pathB);
    QVERIFY(zipB.open(QuaZip::mdCreate));
    addEntry(zipB, "unrelated.gbr", "totally different content");
    zipB.close();

    const ArchiveComparison::Outcome outcome = ArchiveComparison::compareArchives(pathA, pathB);
    QVERIFY(outcome.errorA.isEmpty());
    QVERIFY(outcome.errorB.isEmpty());

    // inner.zip's container row is gone from A, replaced by its contents.
    QVERIFY(findByPath(outcome.result.entriesA, "inner.zip") == outcome.result.entriesA.end());
    const auto leafA = findByPath(outcome.result.entriesA, "inner.zip/a.gtl");
    QVERIFY(leafA != outcome.result.entriesA.end());
    QCOMPARE(leafA->classification, EntryClassification::OnlyInThisArchive);

    // B never had inner.zip at all - neither the container nor anything
    // that looks like its contents should appear there.
    QVERIFY(findByPath(outcome.result.entriesB, "inner.zip") == outcome.result.entriesB.end());
    QVERIFY(findByPath(outcome.result.entriesB, "inner.zip/a.gtl") == outcome.result.entriesB.end());
}

void TestArchiveComparison::misnamedZipComparedAsFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString pathA = dir.filePath("a.zip");
    QuaZip zipA(pathA);
    QVERIFY(zipA.open(QuaZip::mdCreate));
    addEntry(zipA, "notes.zip", "notes from side A, not a real archive");
    zipA.close();

    const QString pathB = dir.filePath("b.zip");
    QuaZip zipB(pathB);
    QVERIFY(zipB.open(QuaZip::mdCreate));
    addEntry(zipB, "notes.zip", "notes from side B, different bytes entirely");
    zipB.close();

    const ArchiveComparison::Outcome outcome = ArchiveComparison::compareArchives(pathA, pathB);
    QVERIFY(outcome.errorA.isEmpty());
    QVERIFY(outcome.errorB.isEmpty());

    // Exactly one row per side - never exploded into (nonexistent) child
    // entries, and matched to each other by name since the content differs.
    QCOMPARE(outcome.result.entriesA.size(), 1);
    QCOMPARE(outcome.result.entriesA.first().relativePath, QString("notes.zip"));
    QCOMPARE(outcome.result.entriesA.first().classification, EntryClassification::Different);

    QCOMPARE(outcome.result.entriesB.size(), 1);
    QCOMPARE(outcome.result.entriesB.first().relativePath, QString("notes.zip"));
    QCOMPARE(outcome.result.entriesB.first().classification, EntryClassification::Different);

    QCOMPARE(outcome.result.differingPairs.size(), 1);
}

void TestArchiveComparison::unreadableArchiveReportsError()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString pathA = dir.filePath("not-a-zip.txt");
    QFile plainFile(pathA);
    QVERIFY(plainFile.open(QIODevice::WriteOnly));
    plainFile.write("this is not a zip archive at all");
    plainFile.close();

    const QString pathB = dir.filePath("b.zip");
    QuaZip zipB(pathB);
    QVERIFY(zipB.open(QuaZip::mdCreate));
    addEntry(zipB, "top.gtl", "G04 top level*\n");
    zipB.close();

    const ArchiveComparison::Outcome outcome = ArchiveComparison::compareArchives(pathA, pathB);
    QVERIFY(!outcome.errorA.isEmpty());
    QVERIFY(outcome.errorB.isEmpty());
    QVERIFY(outcome.result.entriesA.isEmpty());
    QVERIFY(outcome.result.entriesB.isEmpty());
    QVERIFY(outcome.result.differingPairs.isEmpty());
}

void TestArchiveComparison::nestedInsideNestedExpands()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Innermost level: deep.zip, different content on each side.
    QByteArray deepZipBytesA;
    QBuffer deepBufferA(&deepZipBytesA);
    QVERIFY(deepBufferA.open(QIODevice::WriteOnly));
    QuaZip deepZipA(&deepBufferA);
    QVERIFY(deepZipA.open(QuaZip::mdCreate));
    addEntry(deepZipA, "z.gtl", "DeepX");
    deepZipA.close();
    deepBufferA.close();

    QByteArray deepZipBytesB;
    QBuffer deepBufferB(&deepZipBytesB);
    QVERIFY(deepBufferB.open(QIODevice::WriteOnly));
    QuaZip deepZipB(&deepBufferB);
    QVERIFY(deepZipB.open(QuaZip::mdCreate));
    addEntry(deepZipB, "z.gtl", "DeepY");
    deepZipB.close();
    deepBufferB.close();

    // Middle level: inner.zip, holding deep.zip. Because deep.zip's bytes
    // differ per side, inner.zip's own bytes differ too - it must expand.
    QByteArray innerZipBytesA;
    QBuffer innerBufferA(&innerZipBytesA);
    QVERIFY(innerBufferA.open(QIODevice::WriteOnly));
    QuaZip innerZipA(&innerBufferA);
    QVERIFY(innerZipA.open(QuaZip::mdCreate));
    addEntry(innerZipA, "deep.zip", deepZipBytesA);
    innerZipA.close();
    innerBufferA.close();

    QByteArray innerZipBytesB;
    QBuffer innerBufferB(&innerZipBytesB);
    QVERIFY(innerBufferB.open(QIODevice::WriteOnly));
    QuaZip innerZipB(&innerBufferB);
    QVERIFY(innerZipB.open(QuaZip::mdCreate));
    addEntry(innerZipB, "deep.zip", deepZipBytesB);
    innerZipB.close();
    innerBufferB.close();

    const QString pathA = dir.filePath("a.zip");
    QuaZip zipA(pathA);
    QVERIFY(zipA.open(QuaZip::mdCreate));
    addEntry(zipA, "inner.zip", innerZipBytesA);
    zipA.close();

    const QString pathB = dir.filePath("b.zip");
    QuaZip zipB(pathB);
    QVERIFY(zipB.open(QuaZip::mdCreate));
    addEntry(zipB, "inner.zip", innerZipBytesB);
    zipB.close();

    const ArchiveComparison::Outcome outcome = ArchiveComparison::compareArchives(pathA, pathB);
    QVERIFY(outcome.errorA.isEmpty());
    QVERIFY(outcome.errorB.isEmpty());

    // Neither intermediate container survives as its own row - only the
    // fully-descended leaf does. This is only reachable if the loop ran
    // twice: once to reveal deep.zip as a fresh candidate inside inner.zip,
    // once more to open deep.zip itself.
    QVERIFY(findByPath(outcome.result.entriesA, "inner.zip") == outcome.result.entriesA.end());
    QVERIFY(findByPath(outcome.result.entriesA, "inner.zip/deep.zip") == outcome.result.entriesA.end());
    QVERIFY(findByPath(outcome.result.entriesB, "inner.zip") == outcome.result.entriesB.end());
    QVERIFY(findByPath(outcome.result.entriesB, "inner.zip/deep.zip") == outcome.result.entriesB.end());

    QCOMPARE(outcome.result.entriesA.size(), 1);
    const auto leafA = findByPath(outcome.result.entriesA, "inner.zip/deep.zip/z.gtl");
    QVERIFY(leafA != outcome.result.entriesA.end());
    QCOMPARE(leafA->classification, EntryClassification::Different);

    QCOMPARE(outcome.result.entriesB.size(), 1);
    const auto leafB = findByPath(outcome.result.entriesB, "inner.zip/deep.zip/z.gtl");
    QVERIFY(leafB != outcome.result.entriesB.end());
    QCOMPARE(leafB->classification, EntryClassification::Different);

    QCOMPARE(outcome.result.differingPairs.size(), 1);
    QCOMPARE(outcome.result.differingPairs.first().relativePathA, QString("inner.zip/deep.zip/z.gtl"));
    QCOMPARE(outcome.result.differingPairs.first().relativePathB, QString("inner.zip/deep.zip/z.gtl"));
}

QTEST_MAIN(TestArchiveComparison)
#include "test_archivecomparison.moc"
