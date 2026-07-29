#include <QtTest/QtTest>

#include "archivecompare/archivecomparisonresult.h"
#include "archivecompare/gerberdifftrigger.h"

// GerberDiffTrigger::classifyPairs buckets pairs by content, not extension
// (GerberFileType::classify over each side's head bytes - see
// docs/superpowers/specs/2026-07-27-content-based-drawability-design.md).
// The two HeadReader callbacks stand in for ArchiveReader::readEntryHead so
// these tests exercise the bucketing logic without any archive on disk:
// each test builds a small in-memory map from relative path to fake head
// bytes and hands the map lookup in as the reader lambda.
class TestGerberDiffTrigger : public QObject {
    Q_OBJECT

private slots:
    void bothGerberSidesDrawable();
    void drillTxtAgainstDrlDrawable();
    void mixedDrillAndGerberDrawable();
    void plainTextSideNotDrawable();
    void unreadableSideNotDrawable();
    void bucketsPartitionInput();
};

namespace {

// Builds a HeadReader that looks a path up in a fixture map, returning an
// empty QByteArray for anything not present - the same shape a real "entry
// not found" read failure produces, so tests can reuse it to model an
// unreadable side.
GerberDiffTrigger::HeadReader readerOver(const QHash<QString, QByteArray> &heads)
{
    return [heads](const QString &relativePath) { return heads.value(relativePath); };
}

} // namespace

void TestGerberDiffTrigger::bothGerberSidesDrawable()
{
    const QVector<DifferingPair> pairs = {{"top.gtl", "top.gtl"}};
    const QHash<QString, QByteArray> heads = {{"top.gtl", "%FSLAX35Y35*%"}};

    const GerberDiffTrigger::PairBuckets buckets =
        GerberDiffTrigger::classifyPairs(pairs, readerOver(heads), readerOver(heads));

    QCOMPARE(buckets.drawable.size(), 1);
    QCOMPARE(buckets.notDrawable.size(), 0);
}

void TestGerberDiffTrigger::drillTxtAgainstDrlDrawable()
{
    // The real case that started this redesign (Q2): a supplier drill file
    // shipped with a .TXT extension. Both sides are content-classified
    // Excellon purely from a bare M48 line - the filenames ("a.txt"/"b.drl")
    // are deliberately unlike each other and unlike a recognized Gerber
    // extension, since classify() must not care.
    const QVector<DifferingPair> pairs = {{"a.txt", "b.drl"}};
    const QHash<QString, QByteArray> headsA = {{"a.txt", "M48\nINCH\n"}};
    const QHash<QString, QByteArray> headsB = {{"b.drl", "M48\n"}};

    const GerberDiffTrigger::PairBuckets buckets =
        GerberDiffTrigger::classifyPairs(pairs, readerOver(headsA), readerOver(headsB));

    QCOMPARE(buckets.drawable.size(), 1);
    QCOMPARE(buckets.notDrawable.size(), 0);
}

void TestGerberDiffTrigger::mixedDrillAndGerberDrawable()
{
    // One side Excellon, one side Gerber: the old code's "mixed drill pair"
    // bucket would have skipped this. That bucket is deleted (design doc's
    // "Removals" section - the app doesn't police layer semantics anywhere
    // else), so this must land in drawable, not a rejection bucket.
    const QVector<DifferingPair> pairs = {{"board.drl", "top.gtl"}};
    const QHash<QString, QByteArray> headsA = {{"board.drl", "M48\n"}};
    const QHash<QString, QByteArray> headsB = {{"top.gtl", "%FSLAX35Y35*%"}};

    const GerberDiffTrigger::PairBuckets buckets =
        GerberDiffTrigger::classifyPairs(pairs, readerOver(headsA), readerOver(headsB));

    QCOMPARE(buckets.drawable.size(), 1);
    QCOMPARE(buckets.notDrawable.size(), 0);
}

void TestGerberDiffTrigger::plainTextSideNotDrawable()
{
    const QVector<DifferingPair> pairs = {{"top.gtl", "notes.txt"}};
    const QHash<QString, QByteArray> headsA = {{"top.gtl", "%FSLAX35Y35*%"}};
    const QHash<QString, QByteArray> headsB = {{"notes.txt", "pick and place list\n"}};

    const GerberDiffTrigger::PairBuckets buckets =
        GerberDiffTrigger::classifyPairs(pairs, readerOver(headsA), readerOver(headsB));

    QCOMPARE(buckets.drawable.size(), 0);
    QCOMPARE(buckets.notDrawable.size(), 1);
}

void TestGerberDiffTrigger::unreadableSideNotDrawable()
{
    // "missing.gtl" is absent from the fixture map, so readerOver() returns
    // an empty QByteArray for it - the same shape a real archive read
    // failure produces. classify() on empty bytes is NotDrawable, so a read
    // failure must sink the whole pair to notDrawable rather than crash or
    // silently treat the missing side as drawable.
    const QVector<DifferingPair> pairs = {{"missing.gtl", "top.gtl"}};
    const QHash<QString, QByteArray> headsA = {};
    const QHash<QString, QByteArray> headsB = {{"top.gtl", "%FSLAX35Y35*%"}};

    const GerberDiffTrigger::PairBuckets buckets =
        GerberDiffTrigger::classifyPairs(pairs, readerOver(headsA), readerOver(headsB));

    QCOMPARE(buckets.drawable.size(), 0);
    QCOMPARE(buckets.notDrawable.size(), 1);
}

void TestGerberDiffTrigger::bucketsPartitionInput()
{
    // Five pairs spanning every classification combination exercised above.
    // A function that dumped everything into one bucket, or dropped a pair
    // entirely, would fail this even though it might pass the
    // single-pair tests above individually - this is the check that a pair
    // lands in exactly one bucket and none are lost.
    const QVector<DifferingPair> pairs = {
        {"top.gtl", "top.gtl"},         // both Gerber -> drawable
        {"a.txt", "b.drl"},             // both Excellon -> drawable
        {"board.drl", "top.gtl"},       // mixed drill/Gerber -> drawable
        {"top.gtl", "notes.txt"},       // one plain text -> notDrawable
        {"missing.gtl", "top.gtl"},     // one unreadable -> notDrawable
    };
    const QHash<QString, QByteArray> headsA = {
        {"top.gtl", "%FSLAX35Y35*%"},
        {"a.txt", "M48\nINCH\n"},
        {"board.drl", "M48\n"},
    };
    const QHash<QString, QByteArray> headsB = {
        {"top.gtl", "%FSLAX35Y35*%"},
        {"b.drl", "M48\n"},
        {"notes.txt", "pick and place list\n"},
    };

    const GerberDiffTrigger::PairBuckets buckets =
        GerberDiffTrigger::classifyPairs(pairs, readerOver(headsA), readerOver(headsB));

    QCOMPARE(buckets.drawable.size() + buckets.notDrawable.size(), pairs.size());
    QCOMPARE(buckets.drawable.size(), 3);
    QCOMPARE(buckets.notDrawable.size(), 2);
}

QTEST_MAIN(TestGerberDiffTrigger)
#include "test_gerberdifftrigger.moc"
