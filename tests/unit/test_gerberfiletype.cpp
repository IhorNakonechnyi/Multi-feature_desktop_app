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

QTEST_MAIN(TestGerberFileType)
#include "test_gerberfiletype.moc"
