#include <QtTest/QtTest>

#include <QBuffer>
#include <QTemporaryDir>

#include <quazip.h>
#include <quazipfile.h>

#include "archivecompare/archivereader.h"

// Writes one entry (name -> content) into an already-open-for-writing zip.
// Shared by every test below so each one only has to describe the archive
// layout it cares about, not repeat QuaZip's write-API boilerplate.
static void addEntry(QuaZip &zip, const QString &name, const QByteArray &content)
{
    QuaZipFile file(&zip);
    QVERIFY(file.open(QIODevice::WriteOnly, QuaZipNewInfo(name)));
    file.write(content);
    file.close();
}

// readEntryBytes only ever receives a real filesystem path for the
// top-level archive (nested zips are handled entirely in-memory) - so
// top-level test archives are written to a QTemporaryDir rather than an
// in-memory buffer.
class TestArchiveReader : public QObject {
    Q_OBJECT

private slots:
    // A plain top-level entry - no subfolder, no nested zip. This is the
    // common case and must keep working.
    void simpleTopLevelEntry();

    // An entry inside a plain subfolder, with no nested zip anywhere in the
    // path. This is the case a naive "split on every '/' and look each
    // piece up individually" implementation gets wrong: QuaZip stores this
    // as a single entry named "docs/board.gtl", not as separate "docs" and
    // "board.gtl" entries.
    void plainSubfolderEntryNoNestedZip();

    // A nested zip that itself lives inside a subfolder (matches the shape
    // already exercised by CrcComparator's own test data,
    // "wrapper/archive3.zip/1") - the nested zip's own stored entry name at
    // the top level is "wrapper/nested.zip" as one string, not two.
    void nestedZipInsideSubfolder();

    // A path with no matching entry must come back empty rather than
    // throwing or crashing.
    void missingEntryReturnsEmpty();

    // A real-world case named in this module's own comments: some writers
    // (PowerShell's Compress-Archive) store entry names with '\' as the
    // path separator instead of '/'. QuaZip's own write API does no
    // separator normalization (confirmed by reading quazipfile.cpp/
    // quazipnewinfo.cpp - the name is passed through to zipOpenNewFileInZip
    // untouched), so a literal backslash in the name given to addEntry()
    // really is stored raw - this constructs a genuine backslash-separated
    // fixture, not a simulation of one.
    void backslashSeparatedEntryNoNestedZip();

    // A plain *folder* whose name happens to end in ".zip" (e.g. someone
    // extracted an archive, kept the folder name, and re-zipped the parent).
    // The stored entry is "gerber.zip/top.gtl" as one name - there is no
    // entry actually called "gerber.zip" to open as a nested archive. The
    // old ".zip/"-boundary scan misread this as a descent target and came
    // back empty; resolveStep's exact-match-first rule must read it directly.
    void zipNamedFolderEntryIsReadable();

    // Same bug, but the misnamed folder sits a level deeper under a plain
    // subfolder, to make sure the fix isn't specific to the top level.
    void zipNamedFolderNestedDeeper();

    // An archive holding BOTH a genuine nested zip stored as "a.zip" AND a
    // direct entry literally named "a.zip/x.gtl". Asking for "a.zip/x.gtl"
    // must return the direct entry's own content, not descend into a.zip -
    // exact-match-before-prefix-descent is exactly what resolveStep's rule
    // ordering guarantees.
    void exactMatchBeatsPrefixDescent();

    // The new `found` out-param must distinguish "no such entry" from
    // "found it, and it's empty" - missing entry sets found = false.
    void missingEntrySetsFoundFalse();

    // ...while a genuinely zero-length entry that *is* found sets
    // found = true, with empty bytes for an unrelated reason (the content
    // itself is empty, not a lookup failure).
    void zeroLengthEntrySetsFoundTrue();
};

void TestArchiveReader::simpleTopLevelEntry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString archivePath = dir.filePath("top.zip");

    QuaZip zip(archivePath);
    QVERIFY(zip.open(QuaZip::mdCreate));
    addEntry(zip, "board.gtl", "G04 top level*\n");
    zip.close();

    const QByteArray bytes = ArchiveReader::readEntryBytes(archivePath, "board.gtl");
    QCOMPARE(bytes, QByteArray("G04 top level*\n"));
}

void TestArchiveReader::plainSubfolderEntryNoNestedZip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString archivePath = dir.filePath("top.zip");

    QuaZip zip(archivePath);
    QVERIFY(zip.open(QuaZip::mdCreate));
    addEntry(zip, "docs/board.gtl", "G04 in a subfolder*\n");
    zip.close();

    const QByteArray bytes = ArchiveReader::readEntryBytes(archivePath, "docs/board.gtl");
    QCOMPARE(bytes, QByteArray("G04 in a subfolder*\n"));
}

void TestArchiveReader::nestedZipInsideSubfolder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString archivePath = dir.filePath("top.zip");

    // Build the nested zip's bytes in memory first...
    QByteArray nestedZipBytes;
    QBuffer nestedBuffer(&nestedZipBytes);
    QVERIFY(nestedBuffer.open(QIODevice::WriteOnly));
    QuaZip nestedZip(&nestedBuffer);
    QVERIFY(nestedZip.open(QuaZip::mdCreate));
    addEntry(nestedZip, "1", "G04 inside nested zip*\n");
    nestedZip.close();
    nestedBuffer.close();

    // ...then store it under a subfolder in the top-level archive, exactly
    // like a real "wrapper/archive3.zip" entry.
    QuaZip zip(archivePath);
    QVERIFY(zip.open(QuaZip::mdCreate));
    addEntry(zip, "wrapper/nested.zip", nestedZipBytes);
    zip.close();

    const QByteArray bytes = ArchiveReader::readEntryBytes(archivePath, "wrapper/nested.zip/1");
    QCOMPARE(bytes, QByteArray("G04 inside nested zip*\n"));
}

void TestArchiveReader::backslashSeparatedEntryNoNestedZip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString archivePath = dir.filePath("top.zip");

    QuaZip zip(archivePath);
    QVERIFY(zip.open(QuaZip::mdCreate));
    // Literal '\' in the stored name - matches what Compress-Archive writes,
    // not what readArchive's flattened tree ('/'-normalized) looks like.
    addEntry(zip, "docs\\board.gtl", "G04 backslash-stored*\n");
    zip.close();

    // The caller (CrcComparator's flattened tree) always passes the
    // '/'-normalized form, regardless of how the archive itself stores it.
    const QByteArray bytes = ArchiveReader::readEntryBytes(archivePath, "docs/board.gtl");
    QCOMPARE(bytes, QByteArray("G04 backslash-stored*\n"));
}

void TestArchiveReader::missingEntryReturnsEmpty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString archivePath = dir.filePath("top.zip");

    QuaZip zip(archivePath);
    QVERIFY(zip.open(QuaZip::mdCreate));
    addEntry(zip, "board.gtl", "G04 top level*\n");
    zip.close();

    const QByteArray bytes = ArchiveReader::readEntryBytes(archivePath, "does-not-exist.gtl");
    QVERIFY(bytes.isEmpty());
}

void TestArchiveReader::zipNamedFolderEntryIsReadable()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString archivePath = dir.filePath("top.zip");

    QuaZip zip(archivePath);
    QVERIFY(zip.open(QuaZip::mdCreate));
    // "gerber.zip" here is a plain folder, not a nested archive - the only
    // entry that actually exists is this single stored name.
    addEntry(zip, "gerber.zip/top.gtl", "G04 folder named like a zip*\n");
    zip.close();

    const QByteArray bytes = ArchiveReader::readEntryBytes(archivePath, "gerber.zip/top.gtl");
    QCOMPARE(bytes, QByteArray("G04 folder named like a zip*\n"));
}

void TestArchiveReader::zipNamedFolderNestedDeeper()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString archivePath = dir.filePath("top.zip");

    QuaZip zip(archivePath);
    QVERIFY(zip.open(QuaZip::mdCreate));
    addEntry(zip, "wrapper/gerber.zip/sub/top.gtl", "G04 deeper folder named like a zip*\n");
    zip.close();

    const QByteArray bytes = ArchiveReader::readEntryBytes(archivePath, "wrapper/gerber.zip/sub/top.gtl");
    QCOMPARE(bytes, QByteArray("G04 deeper folder named like a zip*\n"));
}

void TestArchiveReader::exactMatchBeatsPrefixDescent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString archivePath = dir.filePath("top.zip");

    // Build a genuine nested zip's bytes in memory first...
    QByteArray nestedZipBytes;
    QBuffer nestedBuffer(&nestedZipBytes);
    QVERIFY(nestedBuffer.open(QIODevice::WriteOnly));
    QuaZip nestedZip(&nestedBuffer);
    QVERIFY(nestedZip.open(QuaZip::mdCreate));
    addEntry(nestedZip, "x.gtl", "G04 inside real nested zip*\n");
    nestedZip.close();
    nestedBuffer.close();

    QuaZip zip(archivePath);
    QVERIFY(zip.open(QuaZip::mdCreate));
    // A real nested zip stored as "a.zip"...
    addEntry(zip, "a.zip", nestedZipBytes);
    // ...AND a direct entry that happens to be named as if it were inside
    // that same nested zip. The direct entry must win.
    addEntry(zip, "a.zip/x.gtl", "G04 direct entry, not nested*\n");
    zip.close();

    const QByteArray bytes = ArchiveReader::readEntryBytes(archivePath, "a.zip/x.gtl");
    QCOMPARE(bytes, QByteArray("G04 direct entry, not nested*\n"));
}

void TestArchiveReader::missingEntrySetsFoundFalse()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString archivePath = dir.filePath("top.zip");

    QuaZip zip(archivePath);
    QVERIFY(zip.open(QuaZip::mdCreate));
    addEntry(zip, "board.gtl", "G04 top level*\n");
    zip.close();

    bool found = true;
    const QByteArray bytes = ArchiveReader::readEntryBytes(archivePath, "nope.gtl", &found);
    QVERIFY(bytes.isEmpty());
    QVERIFY(!found);
}

void TestArchiveReader::zeroLengthEntrySetsFoundTrue()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString archivePath = dir.filePath("top.zip");

    QuaZip zip(archivePath);
    QVERIFY(zip.open(QuaZip::mdCreate));
    addEntry(zip, "empty.gbr", QByteArray());
    zip.close();

    bool found = false;
    const QByteArray bytes = ArchiveReader::readEntryBytes(archivePath, "empty.gbr", &found);
    QVERIFY(bytes.isEmpty());
    QVERIFY(found);
}

QTEST_MAIN(TestArchiveReader)
#include "test_archivereader.moc"
