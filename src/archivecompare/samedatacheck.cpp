#include "samedatacheck.h"

#include <QHash>
#include <QSet>
#include <QString>

#include "../gerber/excellonparser.h"
#include "../gerber/gerberfiletype.h"
#include "../gerber/gerberparser.h"
#include "../gerber/layerequivalence.h"

bool SameDataCheck::isSameData(const QByteArray &bytesA, const QByteArray &bytesB)
{
    // Content decides the kind, the same way the overlay decides which parser
    // to use. A Gerber file and a drill file are never the same data.
    const GerberFileType::Drawability kind = GerberFileType::classify(bytesA);
    if (kind == GerberFileType::Drawability::NotDrawable || GerberFileType::classify(bytesB) != kind) {
        return false;
    }

    const QString textA = QString::fromUtf8(bytesA);
    const QString textB = QString::fromUtf8(bytesB);

    if (kind == GerberFileType::Drawability::Gerber) {
        // The cheaper test first: on a real change the non-geometry lines
        // usually differ already, and the full parse is skipped.
        if (GerberParser::nonGeometryStatements(textA) != GerberParser::nonGeometryStatements(textB)) {
            return false;
        }
        const Layer layerA = GerberParser::parseContentToObjects(textA);
        const Layer layerB = GerberParser::parseContentToObjects(textB);
        return !layerA.objects.isEmpty() && LayerEquivalence::sameObjects(layerA, layerB);
    }

    if (ExcellonParser::nonGeometryLines(textA) != ExcellonParser::nonGeometryLines(textB)) {
        return false;
    }
    try {
        const Layer layerA = ExcellonParser::parseContentToObjects(textA);
        const Layer layerB = ExcellonParser::parseContentToObjects(textB);
        return !layerA.objects.isEmpty() && LayerEquivalence::sameObjects(layerA, layerB);
    } catch (const ExcellonParser::AmbiguousFormatError &) {
        return false; // the coordinates can't be decoded, so nothing can be claimed
    }
}

void SameDataCheck::collapseSameDataPairs(ArchiveComparisonResult &result, const PairPredicate &isSame)
{
    QHash<QString, DataKind> kindsA;
    for (const ArchiveEntry &entry : result.entriesA) {
        kindsA.insert(entry.relativePath, entry.kind);
    }
    QHash<QString, DataKind> kindsB;
    for (const ArchiveEntry &entry : result.entriesB) {
        kindsB.insert(entry.relativePath, entry.kind);
    }

    QVector<DifferingPair> remaining;
    for (const DifferingPair &pair : result.differingPairs) {
        const bool parseable = kindsA.value(pair.relativePathA) == DataKind::GerberData
            && kindsB.value(pair.relativePathB) == DataKind::GerberData;
        if (parseable && isSame(pair)) {
            result.sameDataPairs.append(pair);
        } else {
            remaining.append(pair);
        }
    }
    result.differingPairs = remaining;

    // A file may only turn green once no remaining pair still involves it.
    QSet<QString> stillPairedA;
    QSet<QString> stillPairedB;
    for (const DifferingPair &pair : result.differingPairs) {
        stillPairedA.insert(pair.relativePathA);
        stillPairedB.insert(pair.relativePathB);
    }
    QSet<QString> sameA;
    QSet<QString> sameB;
    for (const DifferingPair &pair : result.sameDataPairs) {
        sameA.insert(pair.relativePathA);
        sameB.insert(pair.relativePathB);
    }

    const auto turnGreen = [](QVector<ArchiveEntry> &entries, const QSet<QString> &same,
                              const QSet<QString> &stillPaired) {
        for (ArchiveEntry &entry : entries) {
            if (entry.classification == EntryClassification::Different && same.contains(entry.relativePath)
                && !stillPaired.contains(entry.relativePath)) {
                entry.classification = EntryClassification::Identical;
            }
        }
    };
    turnGreen(result.entriesA, sameA, stillPairedA);
    turnGreen(result.entriesB, sameB, stillPairedB);
}
