#include <QtTest/QtTest>

#include <QDir>
#include <QMimeData>
#include <QTemporaryDir>
#include <QUrl>

#include "archivecompare/archivedroputil.h"

// Q10: a drop is accepted only for exactly one local .zip FILE. Everything
// else must be refused at dragEnter time, so the OS shows the no-drop
// cursor instead of the app accepting the drag and then complaining.
//
// This is pure decision logic with no widget involved, so it is unit-tested
// even though the drag-drop plumbing around it is manually tested glue
// (constitution Principle 4). Two of these cases are the actual bugs Q10
// was written to fix: a FOLDER named "*.zip" used to be accepted, and a
// multi-file drop used to be silently reduced to its first URL.
class TestArchiveDropUtil : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void acceptsSingleLocalZipFile();
    void acceptsUppercaseZipExtension();
    void rejectsFolder();
    void rejectsFolderNamedZip();
    void rejectsTwoUrls();
    void rejectsNonZipFile();
    void rejectsNonLocalUrl();
    void rejectsEmptyDrop();

private:
    QTemporaryDir dir_;
    QString zipPath_;
    QString upperZipPath_;
    QString pdfPath_;
    QString plainFolder_;
    QString folderNamedZip_;
};

namespace {

// QMimeData is a QObject and so non-copyable - it has to be filled in
// place rather than returned by value.
void setLocalPaths(QMimeData &mime, const QStringList &paths)
{
    QList<QUrl> urls;
    for (const QString &path : paths) {
        urls << QUrl::fromLocalFile(path);
    }
    mime.setUrls(urls);
}

void writeFile(const QString &path)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(path));
    file.write("not really an archive - only the name and the isFile() check matter here");
}

} // namespace

void TestArchiveDropUtil::initTestCase()
{
    QVERIFY(dir_.isValid());
    const QDir root(dir_.path());

    zipPath_ = root.filePath("archive.zip");
    upperZipPath_ = root.filePath("ARCHIVE.ZIP");
    pdfPath_ = root.filePath("datasheet.pdf");
    writeFile(zipPath_);
    writeFile(upperZipPath_);
    writeFile(pdfPath_);

    plainFolder_ = root.filePath("a_folder");
    folderNamedZip_ = root.filePath("looks_like.zip");
    QVERIFY(root.mkpath("a_folder"));
    QVERIFY(root.mkpath("looks_like.zip"));
}

void TestArchiveDropUtil::acceptsSingleLocalZipFile()
{
    QMimeData mime;
    setLocalPaths(mime, { zipPath_ });
    QCOMPARE(singleDroppedZipPath(&mime), zipPath_);
}

void TestArchiveDropUtil::acceptsUppercaseZipExtension()
{
    // The file picker's *.zip filter is case-insensitive; drop must agree,
    // or a .ZIP from a Windows share would refuse to drop.
    QMimeData mime;
    setLocalPaths(mime, { upperZipPath_ });
    QCOMPARE(singleDroppedZipPath(&mime), upperZipPath_);
}

void TestArchiveDropUtil::rejectsFolder()
{
    QMimeData mime;
    setLocalPaths(mime, { plainFolder_ });
    QVERIFY(singleDroppedZipPath(&mime).isEmpty());
}

void TestArchiveDropUtil::rejectsFolderNamedZip()
{
    // The interesting one: the name passes the filter, so only the
    // isFile() check can reject it. Q3's rule at the other end - content
    // decides what is really an archive - has no chance to run if a
    // directory gets this far.
    QMimeData mime;
    setLocalPaths(mime, { folderNamedZip_ });
    QVERIFY(singleDroppedZipPath(&mime).isEmpty());
}

void TestArchiveDropUtil::rejectsTwoUrls()
{
    // Both are perfectly good zips - it is the COUNT that must refuse.
    // Previously the drop silently kept the first and discarded the rest.
    QMimeData mime;
    setLocalPaths(mime, { zipPath_, upperZipPath_ });
    QVERIFY(singleDroppedZipPath(&mime).isEmpty());
}

void TestArchiveDropUtil::rejectsNonZipFile()
{
    QMimeData mime;
    setLocalPaths(mime, { pdfPath_ });
    QVERIFY(singleDroppedZipPath(&mime).isEmpty());
}

void TestArchiveDropUtil::rejectsNonLocalUrl()
{
    // A zip dragged out of a browser is a remote URL: the name would pass
    // but there is no local path to open.
    //
    // Documented weakness: this case cannot currently fail. Removing the
    // isLocalFile() check entirely leaves it passing, because toLocalFile()
    // already returns an empty string for a remote URL and the .zip check
    // rejects that independently. Kept as a statement of intent and a guard
    // for the day the later checks change - not as proof of anything.
    QMimeData mime;
    mime.setUrls({ QUrl(QStringLiteral("https://example.invalid/gerbers.zip")) });
    QVERIFY(singleDroppedZipPath(&mime).isEmpty());
}

void TestArchiveDropUtil::rejectsEmptyDrop()
{
    // Load-bearing despite looking trivial: it is what stops the count
    // guard from being loosened to `urls.size() > 1`, which would let an
    // empty drop reach urls.first() and trip QList's !isEmpty() assert.
    QMimeData mime; // e.g. dragged text, which carries no urls at all
    QVERIFY(singleDroppedZipPath(&mime).isEmpty());
}

QTEST_MAIN(TestArchiveDropUtil)
#include "test_archivedroputil.moc"
