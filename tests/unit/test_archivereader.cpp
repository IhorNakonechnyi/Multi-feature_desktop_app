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

QTEST_MAIN(TestArchiveReader)
#include "test_archivereader.moc"
