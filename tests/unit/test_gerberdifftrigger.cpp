#include <QtTest/QtTest>

#include "archivecompare/archivecomparisonresult.h"
#include "archivecompare/gerberdifftrigger.h"

// GerberFileType::isRecognized checks a fixed built-in extension set plus
// any user-added extensions in QSettings (empty in this test environment),
// so these use extensions from that built-in default set directly rather
// than assuming or hardcoding the app's real (non-public) convention.
class TestGerberDiffTrigger : public QObject {
    Q_OBJECT

private slots:
    void bothSidesRecognizedGerberType();
    void oneSideNotGerberType();
    void neitherSideGerberType();
    void emptyInputProducesEmptyOutput();

    void bothSidesDrillFilesAreEligible();
    void mixedDrillAndGerberSideExcludesFromEligible();
    void drillPairsBucketsOnlyMixedDrillPairs();
    void unrecognizedPairsBucketsNonDrillUnrecognized();
    void mixedDrillTakesPriorityOverUnrecognized();
};

void TestGerberDiffTrigger::bothSidesRecognizedGerberType()
{
    const QVector<DifferingPair> pairs = {{"top.gtl", "top.gtl"}};

    const QVector<DifferingPair> eligible = GerberDiffTrigger::eligiblePairs(pairs);

    QCOMPARE(eligible.size(), 1);
}

void TestGerberDiffTrigger::oneSideNotGerberType()
{
    const QVector<DifferingPair> pairs = {{"top.gtl", "notes.docx"}};

    const QVector<DifferingPair> eligible = GerberDiffTrigger::eligiblePairs(pairs);

    QCOMPARE(eligible.size(), 0);
}

void TestGerberDiffTrigger::neitherSideGerberType()
{
    const QVector<DifferingPair> pairs = {{"notes.docx", "notes.docx"}};

    const QVector<DifferingPair> eligible = GerberDiffTrigger::eligiblePairs(pairs);

    QCOMPARE(eligible.size(), 0);
}

void TestGerberDiffTrigger::emptyInputProducesEmptyOutput()
{
    const QVector<DifferingPair> eligible = GerberDiffTrigger::eligiblePairs({});

    QCOMPARE(eligible.size(), 0);
}

void TestGerberDiffTrigger::bothSidesDrillFilesAreEligible()
{
    const QVector<DifferingPair> pairs = {{"board.drl", "board.drl"}};

    const QVector<DifferingPair> eligible = GerberDiffTrigger::eligiblePairs(pairs);

    QCOMPARE(eligible.size(), 1);
}

void TestGerberDiffTrigger::mixedDrillAndGerberSideExcludesFromEligible()
{
    const QVector<DifferingPair> pairs = {{"board.drl", "top.gtl"}};

    const QVector<DifferingPair> eligible = GerberDiffTrigger::eligiblePairs(pairs);

    QCOMPARE(eligible.size(), 0);
}

void TestGerberDiffTrigger::drillPairsBucketsOnlyMixedDrillPairs()
{
    const QVector<DifferingPair> pairs = {
        {"board.drl", "top.gtl"}, {"board.drl", "board.drl"}, {"top.gtl", "top.gtl"}};

    const QVector<DifferingPair> drill = GerberDiffTrigger::drillPairs(pairs);

    QCOMPARE(drill.size(), 1);
    QCOMPARE(drill.first().relativePathA, QString("board.drl"));
    QCOMPARE(drill.first().relativePathB, QString("top.gtl"));
}

void TestGerberDiffTrigger::unrecognizedPairsBucketsNonDrillUnrecognized()
{
    const QVector<DifferingPair> pairs = {{"top.gtl", "notes.docx"}, {"board.drl", "board.drl"}};

    const QVector<DifferingPair> unrecognized = GerberDiffTrigger::unrecognizedPairs(pairs);

    QCOMPARE(unrecognized.size(), 1);
    QCOMPARE(unrecognized.first().relativePathB, QString("notes.docx"));
}

void TestGerberDiffTrigger::mixedDrillTakesPriorityOverUnrecognized()
{
    // One side is a drill file, the other an unrecognized extension: an
    // Add-extension button could never make this renderable, so it must
    // land in drillPairs, not unrecognizedPairs.
    const QVector<DifferingPair> pairs = {{"board.drl", "board.foobar"}};

    QCOMPARE(GerberDiffTrigger::drillPairs(pairs).size(), 1);
    QCOMPARE(GerberDiffTrigger::unrecognizedPairs(pairs).size(), 0);
}

QTEST_MAIN(TestGerberDiffTrigger)
#include "test_gerberdifftrigger.moc"
