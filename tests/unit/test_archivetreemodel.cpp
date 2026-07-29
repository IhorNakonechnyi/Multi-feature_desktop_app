#include <QtTest/QtTest>

#include "archivecompare/archivecomparisonresult.h"
#include "archivecompare/archivetreemodel.h"

// ArchiveTreeModel sorts rows into work-demanded status bands (Q9):
// yellow (Different) first, then red (OnlyInThisArchive), then green
// (Identical) - so the rows a person still has to act on sit at the top.
// Within one band: directories first, then case-insensitive alphabetical.
// A directory inherits the highest-priority band of any descendant, which
// is what makes a folder holding one changed file surface above a folder
// full of identical ones.
//
// Size no longer participates in the order at all - it is internal
// plumbing for content-class counting and is never displayed (Q9), so
// sorting by it would order rows on something the user cannot see.
class TestArchiveTreeModel : public QObject {
    Q_OBJECT

private slots:
    void filesSortYellowRedGreen();
    void alphabeticalWithinBand();
    void folderInheritsWorstBand();
    void directoriesFirstWithinBand();
    void sortBandRoleExposed();
    void sizeNoLongerOrders();

    void changedOnlyHidesGreenFiles();
    void kindFilterHidesOtherKind();
    void filtersCombine();
    void emptiedFolderHides();
    void isEmptyAfterFilterReflectsResult();
    void filterSurvivesRebuild();
    void emptyMessageNamesTheActiveCriteria();
};

namespace {

ArchiveEntry makeEntry(const QString &path, EntryClassification classification, quint64 size = 0)
{
    ArchiveEntry entry;
    entry.relativePath = path;
    entry.classification = classification;
    entry.sizeBytes = size;
    return entry;
}

ArchiveEntry makeEntry(const QString &path, EntryClassification classification, DataKind kind)
{
    ArchiveEntry entry = makeEntry(path, classification);
    entry.kind = kind;
    return entry;
}

// Row names under `parent`, in model order - what every assertion below
// actually compares, so a failure reports the whole wrong order at once
// instead of dying on the first mismatched row.
QStringList rowNames(const ArchiveTreeModel &model, const QModelIndex &parent = QModelIndex())
{
    QStringList names;
    for (int row = 0; row < model.rowCount(parent); ++row) {
        names << model.data(model.index(row, 0, parent), Qt::DisplayRole).toString();
    }
    return names;
}

} // namespace

void TestArchiveTreeModel::filesSortYellowRedGreen()
{
    // Entries arrive green-red-yellow and are named so that alphabetical
    // order ("green" < "red" < "yellow") reproduces the arrival order -
    // only the band rule being real explains the reversed expectation.
    const QVector<ArchiveEntry> entries = {
        makeEntry("green.gbr", EntryClassification::Identical),
        makeEntry("red.gbr", EntryClassification::OnlyInThisArchive),
        makeEntry("yellow.gbr", EntryClassification::Different),
    };

    ArchiveTreeModel model;
    model.buildFrom(entries);

    QCOMPARE(rowNames(model), QStringList({ "yellow.gbr", "red.gbr", "green.gbr" }));
}

void TestArchiveTreeModel::alphabeticalWithinBand()
{
    // Same band, so the tie-break is all that is under test. Mixed case
    // pins the comparison as case-insensitive: a case-sensitive '<' would
    // put uppercase "B.gbr" before lowercase "a.gbr".
    const QVector<ArchiveEntry> entries = {
        makeEntry("B.gbr", EntryClassification::Different),
        makeEntry("a.gbr", EntryClassification::Different),
    };

    ArchiveTreeModel model;
    model.buildFrom(entries);

    QCOMPARE(rowNames(model), QStringList({ "a.gbr", "B.gbr" }));
}

void TestArchiveTreeModel::folderInheritsWorstBand()
{
    // "docs" holds only a green file, "gerber" only a yellow one. If
    // folders had no band of their own they would both sort as green and
    // land after z.txt; if directories-first outranked the band, both
    // folders would lead. The expected order can only come from folders
    // carrying their descendants' band AND bands outranking directory-ness.
    const QVector<ArchiveEntry> entries = {
        makeEntry("docs/readme.txt", EntryClassification::Identical),
        makeEntry("gerber/x.gbr", EntryClassification::Different),
        makeEntry("z.txt", EntryClassification::OnlyInThisArchive),
    };

    ArchiveTreeModel model;
    model.buildFrom(entries);

    QCOMPARE(rowNames(model), QStringList({ "gerber", "z.txt", "docs" }));
}

void TestArchiveTreeModel::directoriesFirstWithinBand()
{
    // Both rows land in the yellow band, and the folder's name sorts last
    // alphabetically - so only the directories-first tie-break puts it on
    // top.
    const QVector<ArchiveEntry> entries = {
        makeEntry("a_file.gbr", EntryClassification::Different),
        makeEntry("z_folder/inner.gbr", EntryClassification::Different),
    };

    ArchiveTreeModel model;
    model.buildFrom(entries);

    QCOMPARE(rowNames(model), QStringList({ "z_folder", "a_file.gbr" }));
    QVERIFY(model.data(model.index(0, 0), ArchiveTreeModel::IsDirectoryRole).toBool());
}

void TestArchiveTreeModel::sortBandRoleExposed()
{
    // The delegate tints folders from this role, so it has to be readable
    // through data() - on a folder as well as on files.
    const QVector<ArchiveEntry> entries = {
        makeEntry("sub/changed.gbr", EntryClassification::Different),
        makeEntry("gone.gbr", EntryClassification::OnlyInThisArchive),
        makeEntry("same.gbr", EntryClassification::Identical),
    };

    ArchiveTreeModel model;
    model.buildFrom(entries);

    const QModelIndex folder = model.index(0, 0);
    QCOMPARE(rowNames(model), QStringList({ "sub", "gone.gbr", "same.gbr" }));

    QCOMPARE(model.data(folder, ArchiveTreeModel::SortBandRole).toInt(), 0);
    QCOMPARE(model.data(model.index(0, 0, folder), ArchiveTreeModel::SortBandRole).toInt(), 0);
    QCOMPARE(model.data(model.index(1, 0), ArchiveTreeModel::SortBandRole).toInt(), 1);
    QCOMPARE(model.data(model.index(2, 0), ArchiveTreeModel::SortBandRole).toInt(), 2);
}

void TestArchiveTreeModel::sizeNoLongerOrders()
{
    // Under the old size-ascending rule this expectation was exactly
    // inverted (b.gbr, 1 byte, came first). Same band, so size is the only
    // thing that could still reorder these - and it must not.
    const QVector<ArchiveEntry> entries = {
        makeEntry("a.gbr", EntryClassification::Identical, 999),
        makeEntry("b.gbr", EntryClassification::Identical, 1),
    };

    ArchiveTreeModel model;
    model.buildFrom(entries);

    QCOMPARE(rowNames(model), QStringList({ "a.gbr", "b.gbr" }));
}

// --- Task 17: filtering ---------------------------------------------------
//
// Filtering rebuilds the tree from the stored entry list rather than proxying
// it, so these also pin the property that makes that choice worthwhile:
// folders are inferred from surviving paths, so an emptied folder disappears
// without any explicit hide-empty-parent rule.

void TestArchiveTreeModel::changedOnlyHidesGreenFiles()
{
    const QVector<ArchiveEntry> entries = {
        makeEntry("green.gbr", EntryClassification::Identical),
        makeEntry("yellow.gbr", EntryClassification::Different),
        makeEntry("red.gbr", EntryClassification::OnlyInThisArchive),
    };

    ArchiveTreeModel model;
    model.buildFrom(entries);
    QCOMPARE(rowNames(model).size(), 3);

    TreeFilter filter;
    filter.changedOnly = true;
    model.setFilter(filter);

    QCOMPARE(rowNames(model), QStringList({ "yellow.gbr", "red.gbr" }));
}

void TestArchiveTreeModel::kindFilterHidesOtherKind()
{
    const QVector<ArchiveEntry> entries = {
        makeEntry("top.gtl", EntryClassification::Identical, DataKind::GerberData),
        makeEntry("readme.txt", EntryClassification::Identical, DataKind::OtherData),
    };

    ArchiveTreeModel model;
    model.buildFrom(entries);

    TreeFilter filter;
    filter.kind = DataKind::GerberData;
    model.setFilter(filter);

    QCOMPARE(rowNames(model), QStringList({ "top.gtl" }));
}

void TestArchiveTreeModel::filtersCombine()
{
    // Only the changed Gerber row satisfies both criteria; each of the other
    // three fails exactly one of them, so a filter that AND-combines is the
    // only one that produces this answer.
    const QVector<ArchiveEntry> entries = {
        makeEntry("changed.gtl", EntryClassification::Different, DataKind::GerberData),
        makeEntry("same.gtl", EntryClassification::Identical, DataKind::GerberData),
        makeEntry("changed.txt", EntryClassification::Different, DataKind::OtherData),
        makeEntry("same.txt", EntryClassification::Identical, DataKind::OtherData),
    };

    ArchiveTreeModel model;
    model.buildFrom(entries);

    TreeFilter filter;
    filter.changedOnly = true;
    filter.kind = DataKind::GerberData;
    model.setFilter(filter);

    QCOMPARE(rowNames(model), QStringList({ "changed.gtl" }));
}

void TestArchiveTreeModel::emptiedFolderHides()
{
    // "docs" holds nothing but a green file, so under changed-only it has no
    // surviving children - and therefore no reason to exist as a row.
    const QVector<ArchiveEntry> entries = {
        makeEntry("docs/readme.txt", EntryClassification::Identical),
        makeEntry("gerber/changed.gbr", EntryClassification::Different),
    };

    ArchiveTreeModel model;
    model.buildFrom(entries);
    QCOMPARE(rowNames(model).size(), 2);

    TreeFilter filter;
    filter.changedOnly = true;
    model.setFilter(filter);

    QCOMPARE(rowNames(model), QStringList({ "gerber" }));
}

void TestArchiveTreeModel::isEmptyAfterFilterReflectsResult()
{
    const QVector<ArchiveEntry> entries = {
        makeEntry("a.gbr", EntryClassification::Identical),
        makeEntry("b.gbr", EntryClassification::Identical),
    };

    ArchiveTreeModel model;
    model.buildFrom(entries);
    QVERIFY(!model.isEmptyAfterFilter());

    TreeFilter changedOnly;
    changedOnly.changedOnly = true;
    model.setFilter(changedOnly);
    QVERIFY(model.isEmptyAfterFilter()); // every row was green

    model.setFilter(TreeFilter{}); // cleared - the rows must come back
    QVERIFY(!model.isEmptyAfterFilter());
    QCOMPARE(rowNames(model).size(), 2);
}

void TestArchiveTreeModel::filterSurvivesRebuild()
{
    // A new comparison must not silently re-show what the user chose to
    // hide: the control bar's state stays put, so the model's must too.
    ArchiveTreeModel model;
    TreeFilter filter;
    filter.changedOnly = true;
    model.setFilter(filter);

    model.buildFrom({
        makeEntry("fresh_green.gbr", EntryClassification::Identical),
        makeEntry("fresh_yellow.gbr", EntryClassification::Different),
    });

    QCOMPARE(rowNames(model), QStringList({ "fresh_yellow.gbr" }));
}

void TestArchiveTreeModel::emptyMessageNamesTheActiveCriteria()
{
    // The message exists so an empty pane doesn't read as a bug, which only
    // works if it names what the user actually switched on.
    QCOMPARE(emptyAfterFilterMessage(TreeFilter{}), QStringLiteral("No files to show"));

    TreeFilter changed;
    changed.changedOnly = true;
    QCOMPARE(emptyAfterFilterMessage(changed), QStringLiteral("No changed files to show"));

    TreeFilter gerber;
    gerber.kind = DataKind::GerberData;
    QCOMPARE(emptyAfterFilterMessage(gerber), QStringLiteral("No Gerber files to show"));

    TreeFilter other;
    other.kind = DataKind::OtherData;
    QCOMPARE(emptyAfterFilterMessage(other), QStringLiteral("No other files to show"));

    // Both criteria read as one phrase, not a list.
    TreeFilter both;
    both.changedOnly = true;
    both.kind = DataKind::GerberData;
    QCOMPARE(emptyAfterFilterMessage(both), QStringLiteral("No changed Gerber files to show"));
}

QTEST_MAIN(TestArchiveTreeModel)
#include "test_archivetreemodel.moc"
