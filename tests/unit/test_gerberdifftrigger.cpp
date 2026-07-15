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

QTEST_MAIN(TestGerberDiffTrigger)
#include "test_gerberdifftrigger.moc"
