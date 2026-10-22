#include "samedatacheck.h"

#include <algorithm>

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
        // nullopt = the parser cannot vouch for that file: never same data.
        const auto restA = GerberParser::nonGeometryStatements(textA);
        const auto restB = GerberParser::nonGeometryStatements(textB);
        if (!restA || !restB || *restA != *restB) {
            return false;
        }
        const Layer layerA = GerberParser::parseContentToObjects(textA);
        const Layer layerB = GerberParser::parseContentToObjects(textB);
        return !layerA.objects.isEmpty() && LayerEquivalence::sameObjects(layerA, layerB);
    }

    const auto restA = ExcellonParser::nonGeometryLines(textA);
    const auto restB = ExcellonParser::nonGeometryLines(textB);
    if (!restA || !restB || *restA != *restB) {
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

    // Walk the pairs in a fixed order: sorted by path A, then path B. The
    // name pass builds its pairs from a QHash, whose order can change from
    // run to run - and with duplicates the order decides which partner
    // wins, so the same two archives could colour differently each time.
    QVector<DifferingPair> sortedPairs = result.differingPairs;
    std::sort(sortedPairs.begin(), sortedPairs.end(), [](const DifferingPair &left, const DifferingPair &right) {
        if (left.relativePathA != right.relativePathA) {
            return left.relativePathA < right.relativePathA;
        }
        return left.relativePathB < right.relativePathB;
    });

    // Greedy, one-to-one: the pairs are walked in that order and
    // the first pair the check accepts claims both of its files. A claimed
    // file is "proven" - it has its counterpart - so every other pair that
    // involves it is dropped without being offered to the check again. This
    // matters when the name pass paired one file with several same-named
    // files (a cross product): the proven pair is the answer, the rest were
    // only candidates.
    QSet<QString> provenA;
    QSet<QString> provenB;
    QSet<QString> pairedBeforeA;
    QSet<QString> pairedBeforeB;
    for (const DifferingPair &pair : sortedPairs) {
        pairedBeforeA.insert(pair.relativePathA);
        pairedBeforeB.insert(pair.relativePathB);
        if (provenA.contains(pair.relativePathA) || provenB.contains(pair.relativePathB)) {
            continue;
        }
        // A path with no entry has no known kind: treat it as not parseable.
        const bool parseable = kindsA.value(pair.relativePathA, DataKind::OtherData) == DataKind::GerberData
            && kindsB.value(pair.relativePathB, DataKind::OtherData) == DataKind::GerberData;
        if (parseable && isSame(pair)) {
            result.sameDataPairs.append(pair);
            provenA.insert(pair.relativePathA);
            provenB.insert(pair.relativePathB);
        }
    }

    // Pairs that touch no proven file stay yellow (kept in the sorted order).
    QVector<DifferingPair> remaining;
    QSet<QString> stillPairedA;
    QSet<QString> stillPairedB;
    for (const DifferingPair &pair : sortedPairs) {
        if (provenA.contains(pair.relativePathA) || provenB.contains(pair.relativePathB)) {
            continue;
        }
        remaining.append(pair);
        stillPairedA.insert(pair.relativePathA);
        stillPairedB.insert(pair.relativePathB);
    }
    result.differingPairs = remaining;

    // Proven files turn green. A file that was in a pair, is not proven and
    // is in no remaining pair turns red: every file it could have been
    // compared with already matched something else, so on the other side it
    // has no counterpart left - which is exactly what red means.
    const auto recolour = [](QVector<ArchiveEntry> &entries, const QSet<QString> &proven,
                             const QSet<QString> &pairedBefore, const QSet<QString> &stillPaired) {
        for (ArchiveEntry &entry : entries) {
            if (entry.classification != EntryClassification::Different) {
                continue;
            }
            if (proven.contains(entry.relativePath)) {
                entry.classification = EntryClassification::Identical;
            } else if (pairedBefore.contains(entry.relativePath) && !stillPaired.contains(entry.relativePath)) {
                entry.classification = EntryClassification::OnlyInThisArchive;
            }
        }
    };
    recolour(result.entriesA, provenA, pairedBeforeA, stillPairedA);
    recolour(result.entriesB, provenB, pairedBeforeB, stillPairedB);
}
