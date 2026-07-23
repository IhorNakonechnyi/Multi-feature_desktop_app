#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "gerber/gerberfiletype.h"

// GerberFileType persists user extensions via real QSettings
// (QSettings::IniFormat, QSettings::UserScope, "PcbSupplierWorkflow",
// "PcbSupplierWorkflow") - redirect QSettings' default storage path to a
// fresh temp location for this test binary's process lifetime so these
// tests never read or write the real developer/user settings file, and
// never see state left over from a previous test run. This must happen
// before any GerberFileType call and stays in effect for the whole
// binary (QSettings::setPath is a global, process-wide default).
class TestGerberFileType : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void recognizesBuiltInExtensionCaseInsensitively();
    void unknownExtensionIsNotRecognized();
    void addUserExtensionMakesItRecognized();
    void emptyExtensionIsNotRecognized();
    void drillExtensionsAreRecognizedAsDrillFiles();
    void gerberExtensionIsNotADrillFile();
    void emptyExtensionIsNotADrillFile();
    void txtExtensionWithM48HeaderIsADrillFile();
    void txtExtensionWithoutM48HeaderIsNotADrillFile();
    void drlExtensionIsADrillFileEvenWithUnrelatedContent();

private:
    QTemporaryDir settingsDir_;
};

void TestGerberFileType::initTestCase()
{
    QVERIFY(settingsDir_.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
}

void TestGerberFileType::recognizesBuiltInExtensionCaseInsensitively()
{
    QVERIFY(GerberFileType::isRecognized("layer.GTL"));
    QVERIFY(GerberFileType::isRecognized("layer.gtl"));
}

void TestGerberFileType::unknownExtensionIsNotRecognized()
{
    QVERIFY(!GerberFileType::isRecognized("readme.docx"));
}

void TestGerberFileType::addUserExtensionMakesItRecognized()
{
    QVERIFY(!GerberFileType::isRecognized("layer.foobar123"));
    GerberFileType::addUserExtension("foobar123");
    QVERIFY(GerberFileType::isRecognized("layer.foobar123"));
}

void TestGerberFileType::emptyExtensionIsNotRecognized()
{
    QVERIFY(!GerberFileType::isRecognized("noextension"));
}

void TestGerberFileType::drillExtensionsAreRecognizedAsDrillFiles()
{
    QVERIFY(GerberFileType::isDrillFile("board.drl"));
    QVERIFY(GerberFileType::isDrillFile("board.DRL"));
    QVERIFY(GerberFileType::isDrillFile("board.xln"));
}

void TestGerberFileType::gerberExtensionIsNotADrillFile()
{
    QVERIFY(!GerberFileType::isDrillFile("layer.gtl"));
}

void TestGerberFileType::emptyExtensionIsNotADrillFile()
{
    QVERIFY(!GerberFileType::isDrillFile("noextension"));
}

// Real supplier files have shown up with genuine Excellon content under a
// .txt extension (e.g. "347128-C_EX.TXT") - the extension-only overload
// can't catch this, so the content-aware overload must sniff the M48
// header instead of trusting the extension.
void TestGerberFileType::txtExtensionWithM48HeaderIsADrillFile()
{
    const QByteArray content = "M48\nINCH,LZ\nT01C0.030\n%\nT01\nX010Y010\nM30\n";
    QVERIFY(GerberFileType::isDrillFile("347128-C_EX.TXT", content));
}

void TestGerberFileType::txtExtensionWithoutM48HeaderIsNotADrillFile()
{
    const QByteArray content = "This is just a plain-text CAM tool report, not a drill file.\n";
    QVERIFY(!GerberFileType::isDrillFile("readme.txt", content));
}

void TestGerberFileType::drlExtensionIsADrillFileEvenWithUnrelatedContent()
{
    // The .drl/.xln fast path must not require content sniffing at all -
    // classification elsewhere (GerberDiffTrigger) runs before any bytes
    // are read.
    QVERIFY(GerberFileType::isDrillFile("board.drl", QByteArray()));
}

QTEST_MAIN(TestGerberFileType)
#include "test_gerberfiletype.moc"
