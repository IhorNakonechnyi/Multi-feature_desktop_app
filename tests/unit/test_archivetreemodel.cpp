#include <QtTest/QtTest>

#include "archivecompare/archivecomparisonresult.h"
#include "archivecompare/archivetreemodel.h"

// ArchiveTreeModel::sortChildren (archivetreemodel.h's own comment) commits
// to a fixed sort: directories first, then files smallest-to-biggest by
// size, applied independently in every subfolder (it recurses). These
// tests exist because that promise had no coverage - only the sort
// direction was documented in a comment, not verified.
class TestArchiveTreeModel : public QObject {
    Q_OBJECT

private slots:
    void filesInSubfolderSortSmallestToBiggest();
    void topLevelFilesSortSmallestToBiggest();
    void directoriesSortBeforeFilesRegardlessOfSize();
};

namespace {

ArchiveEntry makeEntry(const QString &path, quint64 size)
{
    ArchiveEntry entry;
    entry.relativePath = path;
    entry.sizeBytes = size;
    return entry;
}

} // namespace

void TestArchiveTreeModel::filesInSubfolderSortSmallestToBiggest()
{
    // Entries deliberately given out of order, so a passing test can only
    // mean the model actually sorted them, not that they happened to
    // arrive in the right order already.
    const QVector<ArchiveEntry> entries = {
        makeEntry("sub/big.txt", 300),
        makeEntry("sub/small.txt", 10),
        makeEntry("sub/medium.txt", 100),
    };

    ArchiveTreeModel model;
    model.buildFrom(entries);

    const QModelIndex subFolder = model.index(0, 0);
    QCOMPARE(model.data(subFolder, Qt::DisplayRole).toString(), QStringLiteral("sub"));
    QCOMPARE(model.rowCount(subFolder), 3);
    QCOMPARE(model.data(model.index(0, 0, subFolder)).toString(), QStringLiteral("small.txt"));
    QCOMPARE(model.data(model.index(1, 0, subFolder)).toString(), QStringLiteral("medium.txt"));
    QCOMPARE(model.data(model.index(2, 0, subFolder)).toString(), QStringLiteral("big.txt"));
}

void TestArchiveTreeModel::topLevelFilesSortSmallestToBiggest()
{
    // Same rule at the root - files with no wrapping folder at all
    // (e.g. a zip with no internal directory) still sort by size.
    const QVector<ArchiveEntry> entries = {
        makeEntry("big.txt", 300),
        makeEntry("small.txt", 10),
        makeEntry("medium.txt", 100),
    };

    ArchiveTreeModel model;
    model.buildFrom(entries);

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.data(model.index(0, 0)).toString(), QStringLiteral("small.txt"));
    QCOMPARE(model.data(model.index(1, 0)).toString(), QStringLiteral("medium.txt"));
    QCOMPARE(model.data(model.index(2, 0)).toString(), QStringLiteral("big.txt"));
}

void TestArchiveTreeModel::directoriesSortBeforeFilesRegardlessOfSize()
{
    // Name/size would put the file first either way (alphabetically "aaa"
    // beats "zzz", and 999 loses to 1) - only the directories-first rule
    // being real explains the expected order below.
    const QVector<ArchiveEntry> entries = {
        makeEntry("zzz_smallest_file.txt", 1),
        makeEntry("aaa_folder/inner.txt", 999),
    };

    ArchiveTreeModel model;
    model.buildFrom(entries);

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0)).toString(), QStringLiteral("aaa_folder"));
    QVERIFY(model.data(model.index(0, 0), ArchiveTreeModel::IsDirectoryRole).toBool());
    QCOMPARE(model.data(model.index(1, 0)).toString(), QStringLiteral("zzz_smallest_file.txt"));
}

QTEST_MAIN(TestArchiveTreeModel)
#include "test_archivetreemodel.moc"
