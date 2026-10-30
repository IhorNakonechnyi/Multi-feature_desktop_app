#include <QtTest/QtTest>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QRandomGenerator>

#include "gerber/excellonparser.h"
#include "gerber/gerberobjectdump.h"
#include "gerber/gerberparser.h"
#include "gerber/samedatarefusal.h"

// Golden test (corpus: 24 Gerber files of ~7,000 statements, 6 drill files):
// pins EXACTLY what both parsers produce today - the same-data
// check's settings pass and the full parse into objects - on a fixed corpus
// of made-up files. Any later change that alters one value in one object
// fails here. Meant for speed-ups that must not change behaviour
// (docs/superpowers/plans/2026-10-30-same-data-check-speedup.md).
//
// The expected results are stored as one SHA-256 per file: the corpus output
// is megabytes, a fingerprint is 64 characters, and "exactly equal or not"
// is all the test asks. To see WHAT changed, record the dumps of the old and
// the new parser (below) and diff them.
//
// Re-recording (only when a change is MEANT to alter parser output): set the
// environment variable PCB_GOLDEN_RECORD to a folder, run this test, and copy
// the parser-golden.txt written there over tests/unit/golden/. The folder
// also gets every file's full dump for diffing.

namespace {

// Everything below draws from one generator with a fixed seed, in a fixed
// order, so the corpus is the same on every run and every machine. Each draw
// is its own statement: C++ leaves the order in which function arguments (and
// the two sides of a '+') are evaluated open, so two draws in one expression
// could swap between compilers.

// A coordinate in the file's own format: up to 9 digits for the 4.6 mm
// files, up to 6 for the 2.4 inch trailing-zero files, sometimes signed.
QString coordinate(QRandomGenerator &rng, bool trailingZeroFormat)
{
    const qint64 value = trailingZeroFormat ? rng.bounded(0, 999999) : rng.bounded(0, 300000000);
    const quint32 sign = rng.bounded(10u);
    const QString digits = QString::number(value);
    if (sign == 0) {
        return QLatin1Char('-') + digits;
    }
    if (sign == 1) {
        return QLatin1Char('+') + digits;
    }
    return digits;
}

QString xy(QRandomGenerator &rng, bool tz)
{
    const QString x = coordinate(rng, tz);
    const QString y = coordinate(rng, tz);
    return QStringLiteral("X%1Y%2").arg(x, y);
}

// One region (copper-pour outline): a D02 start, then outline points. Some
// points are modal (no D-code), some edges are arcs.
QString region(QRandomGenerator &rng, bool tz)
{
    QString text = QStringLiteral("G36*\n") + xy(rng, tz) + QStringLiteral("D02*\n");
    const int points = 3 + rng.bounded(118);
    for (int i = 0; i < points; ++i) {
        const quint32 form = rng.bounded(20u);
        if (form == 0) {
            const QString end = xy(rng, tz);
            const QString i = coordinate(rng, tz);
            const QString j = coordinate(rng, tz);
            text += QStringLiteral("G03%1I%2J%3D01*\nG01*\n").arg(end, i, j);
        } else if (form < 4) {
            text += xy(rng, tz) + QStringLiteral("*\n"); // modal: repeats D01
        } else {
            text += xy(rng, tz) + QStringLiteral("D01*\n");
        }
    }
    return text + QStringLiteral("G37*\n");
}

// One statement (or a short group) outside regions, by weighted choice.
// The odd forms are rare but present: they are where a speed-up that reads a
// statement differently from today would show.
QString plainStatement(QRandomGenerator &rng, bool tz)
{
    const quint32 pick = rng.bounded(100u);
    if (pick < 20) {
        return xy(rng, tz) + QStringLiteral("D03*\n");
    }
    if (pick < 40) {
        return xy(rng, tz) + QStringLiteral("D01*\n");
    }
    if (pick < 48) {
        return xy(rng, tz) + QStringLiteral("D02*\n");
    }
    if (pick < 52) {
        return QStringLiteral("G1") + xy(rng, tz) + QStringLiteral("D2*\n");
    }
    if (pick < 56) {
        return QStringLiteral("G01") + xy(rng, tz) + QStringLiteral("D01*\n");
    }
    if (pick < 60) {
        const QString end = xy(rng, tz);
        const QString i = coordinate(rng, tz);
        const QString j = coordinate(rng, tz);
        return QStringLiteral("G75*\nG02%1I%2J%3D01*\nG01*\n").arg(end, i, j);
    }
    if (pick < 62) {
        // G74 offsets are unsigned.
        const QString end = xy(rng, tz);
        const QString i = QString::number(rng.bounded(50000));
        const QString j = QString::number(rng.bounded(50000));
        return QStringLiteral("G74*\nG03%1I%2J%3D01*\nG75*\nG01*\n").arg(end, i, j);
    }
    if (pick < 66) {
        static const char *const selects[] = {"D10*\n", "D11*\n", "D012*\n", "G54D13*\n", "D20*\n", "G55D10*\n"};
        return QLatin1String(selects[rng.bounded(6u)]);
    }
    if (pick < 70) {
        return xy(rng, tz) + QStringLiteral("*\n"); // modal coordinates
    }
    if (pick < 72) {
        return rng.bounded(2u) ? QStringLiteral("D01*\n") : QStringLiteral("D02*\n");
    }
    if (pick < 74) {
        return rng.bounded(2u) ? QStringLiteral("%LPC*%\n") : QStringLiteral("%LPD*%\n");
    }
    if (pick < 76) {
        return QStringLiteral("G04 note X1Y2D01 %1*\n").arg(rng.bounded(1000));
    }
    // Odd forms, 1 % each.
    switch (pick) {
    case 76: {
        const QString y = coordinate(rng, tz);
        const QString x = coordinate(rng, tz);
        return QStringLiteral("Y%1X%2D01*\n").arg(y, x);
    }
    case 77:
        return QStringLiteral("X%1D10*\n").arg(coordinate(rng, tz));
    case 78:
        return QStringLiteral("D05*\n");
    case 79:
        return QStringLiteral("G02I5J5*\n");
    case 80:
        return QStringLiteral("X1234567890123Y1D01*\n");
    case 81: {
        const QString x = coordinate(rng, tz);
        const QString i = coordinate(rng, tz);
        const QString j = coordinate(rng, tz);
        return QStringLiteral("X%1I%2J%3D01*\n").arg(x, i, j);
    }
    case 82:
        return QStringLiteral("G03") + xy(rng, tz) + QStringLiteral("D01*\nG01*\n");
    case 83:
        return QStringLiteral("%TO.C,R%1*%\n").arg(rng.bounded(100));
    case 84:
        return QStringLiteral("%TD*%\n");
    case 85:
        return QStringLiteral("G1*\n");
    default:
        return xy(rng, tz) + QStringLiteral("D01*\n");
    }
}

QByteArray gerberFile(QRandomGenerator &rng, int index)
{
    const bool tz = index % 2 == 1;
    QString text = QStringLiteral("G04 made-up test file %1*\n").arg(index);
    text += QStringLiteral("%TF.CreationDate,2026-10-30T10:00:00*%\n%TF.FileFunction,Copper,L1,Top*%\n");
    text += tz ? QStringLiteral("%FSTAX24Y24*%\n%MOIN*%\n") : QStringLiteral("%FSLAX46Y46*%\n%MOMM*%\n");
    text += QStringLiteral("%LNcopper*%\n");
    text += QStringLiteral("%ADD10C,0.200*%\n%ADD11R,1.000X0.500*%\n%ADD12O,0.8X0.4*%\n%ADD13P,1.0X6*%\n");
    if (index % 5 == 0) {
        // The closing '%' on its own line gives an empty statement at the
        // end of the block - a case the statement splitter must keep.
        text += QStringLiteral("%AMM1*\n21,1,$1,$2,0,0,0*\n%\n");
    } else {
        text += QStringLiteral("%AMM1*21,1,$1,$2,0,0,0*%\n");
    }
    text += QStringLiteral("%ADD20M1,1X2*%\nD10*\n");
    if (index == 7 || index == 15) {
        text += QStringLiteral("G91*\n"); // the settings pass refuses this file
    }

    // ~1,000 picks: about 100 regions (most of the statements, as in real
    // files) and about 900 other statements, so each odd form above appears
    // about 9 times per file.
    for (int pickIndex = 0; pickIndex < 1000; ++pickIndex) {
        const quint32 pick = rng.bounded(40u);
        if (pick < 4) {
            text += region(rng, tz);
        } else if (pick == 4) {
            text += QStringLiteral("%SRX2Y3I1.0J2.0*%\n");
            for (int i = 0; i < 5; ++i) {
                text += plainStatement(rng, tz);
            }
            text += QStringLiteral("%SR*%\n");
        } else {
            text += plainStatement(rng, tz);
        }
    }
    text += QStringLiteral("M02*\n");
    if (index == 23) {
        text += QStringLiteral("X100Y100D0"); // cut off: no closing '*'
    }
    return text.toUtf8();
}

QByteArray drillFile(QRandomGenerator &rng, int index)
{
    const bool inch = index % 2 == 1;
    const auto holeCoordinate = [&rng, inch]() {
        if (inch) {
            return QStringLiteral("%1").arg(rng.bounded(100000), 6, 10, QLatin1Char('0'));
        }
        const int whole = rng.bounded(300);
        const int fraction = rng.bounded(1000);
        return QStringLiteral("%1.%2").arg(whole).arg(fraction, 3, 10, QLatin1Char('0'));
    };
    // `count` coordinates, drawn in order.
    const auto draws = [&holeCoordinate](int count) {
        QStringList values;
        for (int k = 0; k < count; ++k) {
            values << holeCoordinate();
        }
        return values;
    };
    QString text = QStringLiteral("M48\n;date: 2026-10-30\n");
    text += inch ? QStringLiteral("INCH,LZ\n") : QStringLiteral("METRIC,TZ\n");
    text += QStringLiteral("T01C0.300\nT02C0.800\nT03C1.000\n%\n");
    for (int tool = 1; tool <= 3; ++tool) {
        text += QStringLiteral("T0%1\n").arg(tool);
        const int holes = 100 + rng.bounded(400);
        for (int i = 0; i < holes; ++i) {
            const quint32 form = rng.bounded(20u);
            if (form == 0) {
                const QStringList c = draws(4);
                text += QStringLiteral("X%1Y%2G85X%3Y%4\n").arg(c[0], c[1], c[2], c[3]);
            } else if (form == 1) {
                text += QStringLiteral("X%1\n").arg(holeCoordinate()); // modal Y
            } else {
                const QStringList c = draws(2);
                text += QStringLiteral("X%1Y%2\n").arg(c[0], c[1]);
            }
        }
    }
    // A rout block: move, tool down, straight and arc cuts, tool up.
    const QStringList c = draws(7);
    text += QStringLiteral("G00X%1Y%2\nM15\nG01X%3Y%4\nG02X%5Y%6I%7J0\nM16\nG05\n")
                .arg(c[0], c[1], c[2], c[3], c[4], c[5], c[6]);
    return (text + QStringLiteral("M30\n")).toUtf8();
}

struct CorpusFile {
    bool isGerber = true;
    QByteArray bytes;
};

QVector<CorpusFile> goldenCorpus()
{
    QRandomGenerator rng(20261030);
    QVector<CorpusFile> corpus;
    for (int i = 0; i < 24; ++i) {
        corpus.append({true, gerberFile(rng, i)});
    }
    for (int i = 0; i < 6; ++i) {
        corpus.append({false, drillFile(rng, i)});
    }
    return corpus;
}

QString settingsText(const std::optional<QStringList> &kept, const SameDataRefusal &refusal, bool drawLeftOut)
{
    const QString head = kept ? kept->join(QLatin1Char('\n'))
                              : QStringLiteral("REFUSED %1 @%2 '%3'").arg(refusal.rule).arg(refusal.position).arg(refusal.statement);
    return head + QStringLiteral("\ndrawLeftOut %1").arg(drawLeftOut ? 1 : 0);
}

// Everything both parsers say about one file, as text.
QByteArray parserOutput(const CorpusFile &file)
{
    const QString text = QString::fromUtf8(file.bytes);
    SameDataRefusal refusal;
    bool drawLeftOut = false;
    QByteArray out;
    if (file.isGerber) {
        const auto kept = GerberParser::nonGeometryStatements(text, &refusal, &drawLeftOut);
        out = settingsText(kept, refusal, drawLeftOut).toUtf8() + "\n--- objects\n"
            + GerberObjectDump::dump(GerberParser::parseContentToObjects(text));
    } else {
        const auto kept = ExcellonParser::nonGeometryLines(text, &refusal, &drawLeftOut);
        out = settingsText(kept, refusal, drawLeftOut).toUtf8() + "\n--- objects\n";
        try {
            out += GerberObjectDump::dump(ExcellonParser::parseContentToObjects(text));
        } catch (const ExcellonParser::AmbiguousFormatError &) {
            out += "AMBIGUOUS";
        }
    }
    return out;
}

} // namespace

class TestParserGolden : public QObject {
    Q_OBJECT

private slots:
    void goldenOutputIsUnchanged();
};

void TestParserGolden::goldenOutputIsUnchanged()
{
    const QVector<CorpusFile> corpus = goldenCorpus();
    QStringList hashes;
    QStringList dumps;
    for (const CorpusFile &file : corpus) {
        const QByteArray output = parserOutput(file);
        hashes << QString::fromLatin1(QCryptographicHash::hash(output, QCryptographicHash::Sha256).toHex());
        dumps << QString::fromUtf8(output);
    }

    const QString recordFolder = qEnvironmentVariable("PCB_GOLDEN_RECORD");
    if (!recordFolder.isEmpty()) {
        QDir().mkpath(recordFolder);
        QFile golden(QDir(recordFolder).filePath(QStringLiteral("parser-golden.txt")));
        QVERIFY(golden.open(QIODevice::WriteOnly | QIODevice::Text));
        golden.write((hashes.join(QLatin1Char('\n')) + QLatin1Char('\n')).toLatin1());
        for (int i = 0; i < dumps.size(); ++i) {
            QFile dumpFile(QDir(recordFolder).filePath(QStringLiteral("dump-%1.txt").arg(i, 2, 10, QLatin1Char('0'))));
            QVERIFY(dumpFile.open(QIODevice::WriteOnly | QIODevice::Text));
            dumpFile.write(dumps.at(i).toUtf8());
        }
        QSKIP("Recorded - copy parser-golden.txt into tests/unit/golden/");
    }

    QFile golden(QStringLiteral(GOLDEN_FILE));
    QVERIFY2(golden.open(QIODevice::ReadOnly | QIODevice::Text), "tests/unit/golden/parser-golden.txt is missing");
    const QStringList expected =
        QString::fromLatin1(golden.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(expected.size(), hashes.size());
    for (int i = 0; i < hashes.size(); ++i) {
        QVERIFY2(hashes.at(i) == expected.at(i).trimmed(),
                 qPrintable(QStringLiteral("corpus file %1 parses differently from the recorded result").arg(i)));
    }
}

QTEST_MAIN(TestParserGolden)
#include "test_parsergolden.moc"
