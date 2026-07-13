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

QTEST_MAIN(TestArchiveComparator)
#include "test_archivecomparator.moc"
