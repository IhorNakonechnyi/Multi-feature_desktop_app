#include <QtTest/QtTest>

#include "gerber/gerberfiletype.h"

using GerberFileType::classify;
using GerberFileType::Drawability;

// classify() is content-only (Q2/Q12): the filename plays no part, and
// there is no extension fallback to rescue an inconclusive file. Every
// case here is named for the file's actual bytes, not any extension it
// might carry in a real archive - several deliberately note an extension
// in the comment that would have misled the old extension-based mechanism.
class TestGerberFileType : public QObject {
    Q_OBJECT

private slots:
    void excellonDetectedWhateverTheName();
    void gerberDetectedByFormatSpec();
    void gerberDetectedByModeOnly();
    void gtlNamedTextIsNotDrawable();
    void excellonWinsOverGerberMarkers();
    void fsBehindLongCommentBannerFound();
    void fsBeyond8KbNotFound();
    void m48SubstringInsideLineIgnored();
};

// A genuine Excellon drill header - the real-world case this whole design
// exists for is a supplier drill file saved with a .TXT extension, so this
// must classify as ExcellonDrill purely from the bytes.
void TestGerberFileType::excellonDetectedWhateverTheName()
{
    const QByteArray head = "M48\nINCH,LZ\nT01C0.030\n%\nT01\nX010Y010\nM30\n";
    QCOMPARE(classify(head), Drawability::ExcellonDrill);
}

void TestGerberFileType::gerberDetectedByFormatSpec()
{
    const QByteArray head = "%FSLAX35Y35*%\nG04 layer header*\nX010Y010D02*\n";
    QCOMPARE(classify(head), Drawability::Gerber);
}

// %MO (units mode) alone is enough to call a file Gerber - this head
// carries no %FS at all, only %MO, so a stub that only recognized "%FS"
// literally would fail this one.
void TestGerberFileType::gerberDetectedByModeOnly()
{
    const QByteArray head = "%MOIN*%\nG04 header banner*\nX010Y010D02*\n";
    QCOMPARE(classify(head), Drawability::Gerber);
}

// Plain text with no drill or Gerber markers at all - the extension no
// longer rescues anything, so even a .gtl-named file with this content
// must be NotDrawable (see gtlNamedTextIsNotDrawable's sibling positive
// cases above and below: this file proves the negative side actually
// discriminates, since a stub returning NotDrawable unconditionally would
// also pass every other test on its own - this one confirms the *other*
// cases aren't equally satisfied by that same stub).
void TestGerberFileType::gtlNamedTextIsNotDrawable()
{
    const QByteArray head = "just a plain report\nline 2\n";
    QCOMPARE(classify(head), Drawability::NotDrawable);
}

// Excellon is checked before Gerber markers (design doc, "Classification"):
// an Excellon file may legitimately contain a bare "%" line (the tool-list
// terminator), so a head with both a bare "%" and "M48" must still resolve
// to ExcellonDrill, not Gerber.
void TestGerberFileType::excellonWinsOverGerberMarkers()
{
    const QByteArray head = "M48\nINCH,LZ\nT01C0.030\n%\nT01\nX010Y010\nM30\n";
    QCOMPARE(classify(head), Drawability::ExcellonDrill);
}

// A long G04 comment banner pushing %FS well past where a naive fixed-line
// scan would stop must still be found - this is what justifies scanning by
// byte count (8 KB), not by a small fixed number of lines.
void TestGerberFileType::fsBehindLongCommentBannerFound()
{
    QByteArray head;
    const QByteArray bannerLine = "G04 banner*\n";
    while (head.size() < 6 * 1024) {
        head += bannerLine;
    }
    head += "%FSLAX35Y35*%\n";
    QCOMPARE(classify(head), Drawability::Gerber);
}

// The mirror image of fsBehindLongCommentBannerFound: %FS pushed past the
// 8 KB window is a documented limit, not a bug - asserted here so a future
// change to kClassifySniffBytes is a deliberate, test-visible decision.
// Deliberately passes the full, UNtruncated >9 KB buffer to classify() -
// callers in production (ArchiveComparePage::parseGerberOrDrill,
// GerberViewerPage::openFile) hand classify() whole-file bytes today (the
// head-only ArchiveReader helper isn't wired in until a later task), so
// classify() must do its own truncation. Pre-truncating the buffer here
// (as an earlier version of this test did) would test nothing: the cap
// would never see an oversized argument from any test in this file, so it
// could be deleted from classify() with zero test regression - this
// version was confirmed to genuinely fail without the cap (see task-9
// report's fix addendum for the deliberate-failure run).
void TestGerberFileType::fsBeyond8KbNotFound()
{
    QByteArray full;
    const QByteArray bannerLine = "G04 banner*\n";
    while (full.size() < 9 * 1024) {
        full += bannerLine;
    }
    full += "%FSLAX35Y35*%\n";
    QCOMPARE(classify(full), Drawability::NotDrawable);
}

// "M48" must match only as a whole trimmed line, not as a substring buried
// inside another line - a naive contains("M48") check would misclassify
// this as ExcellonDrill instead of Gerber.
void TestGerberFileType::m48SubstringInsideLineIgnored()
{
    const QByteArray head = "G04 BOM48 note*\n%FSLAX24Y24*%\n";
    QCOMPARE(classify(head), Drawability::Gerber);
}

QTEST_MAIN(TestGerberFileType)
#include "test_gerberfiletype.moc"
