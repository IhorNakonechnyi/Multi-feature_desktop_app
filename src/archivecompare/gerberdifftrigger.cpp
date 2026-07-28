#include "gerberdifftrigger.h"

#include <QFileInfo>
#include <QSet>
#include <QString>

// TEMPORARY BRIDGE (Task 9 -> Task 10), not the new design.
//
// GerberFileType's extension-based isRecognized()/isDrillFile(fileName)
// were deleted in Task 9 - content-only classify() replaces them (see
// gerberfiletype.h and docs/superpowers/specs/2026-07-27-content-based-
// drawability-design.md). This file's real rewrite - asking classify() for
// each pair's actual bytes instead of guessing from filenames, and
// dropping the mixed-drill bucket entirely (the design doc's "Removals"
// section) - is Task 10's job; it needs archive-entry byte reads this file
// doesn't have access to today.
//
// Until then, this local copy of the old extension-only logic keeps this
// file compiling and its existing behavior/tests unchanged across the
// Task 9/10 commit boundary. It intentionally does NOT read file content
// and does NOT implement the new bucketing design.
namespace {

const QSet<QString> &legacyRecognizedExtensions()
{
    static const QSet<QString> extensions = {
        QStringLiteral("gtl"), QStringLiteral("gbl"), QStringLiteral("gts"), QStringLiteral("gbs"),
        QStringLiteral("gto"), QStringLiteral("gbo"), QStringLiteral("gtp"), QStringLiteral("gbp"),
        QStringLiteral("gko"), QStringLiteral("gml"), QStringLiteral("gbr"), QStringLiteral("drl"),
        QStringLiteral("xln"), QStringLiteral("cmp"), QStringLiteral("sol"), QStringLiteral("stc"),
        QStringLiteral("sts"), QStringLiteral("plc"), QStringLiteral("ger"),
    };
    return extensions;
}

bool legacyIsRecognized(const QString &fileName)
{
    const QString extension = QFileInfo(fileName).suffix().toLower();
    return !extension.isEmpty() && legacyRecognizedExtensions().contains(extension);
}

bool legacyIsDrillFile(const QString &fileName)
{
    const QString extension = QFileInfo(fileName).suffix().toLower();
    return extension == QStringLiteral("drl") || extension == QStringLiteral("xln");
}

bool hasDrillSide(const DifferingPair &pair)
{
    return legacyIsDrillFile(pair.relativePathA) || legacyIsDrillFile(pair.relativePathB);
}

bool bothSidesDrillFiles(const DifferingPair &pair)
{
    return legacyIsDrillFile(pair.relativePathA) && legacyIsDrillFile(pair.relativePathB);
}

bool isMixedDrillPair(const DifferingPair &pair)
{
    return hasDrillSide(pair) && !bothSidesDrillFiles(pair);
}

} // namespace

QVector<DifferingPair> GerberDiffTrigger::eligiblePairs(const QVector<DifferingPair> &differingPairs)
{
    QVector<DifferingPair> eligible;
    for (const auto &pair : differingPairs) {
        if (isMixedDrillPair(pair)) {
            continue;
        }
        if (legacyIsRecognized(pair.relativePathA) && legacyIsRecognized(pair.relativePathB)) {
            eligible.append(pair);
        }
    }
    return eligible;
}

QVector<DifferingPair> GerberDiffTrigger::unrecognizedPairs(const QVector<DifferingPair> &differingPairs)
{
    QVector<DifferingPair> unrecognized;
    for (const auto &pair : differingPairs) {
        if (hasDrillSide(pair)) {
            continue;
        }
        if (!legacyIsRecognized(pair.relativePathA) || !legacyIsRecognized(pair.relativePathB)) {
            unrecognized.append(pair);
        }
    }
    return unrecognized;
}

QVector<DifferingPair> GerberDiffTrigger::drillPairs(const QVector<DifferingPair> &differingPairs)
{
    QVector<DifferingPair> drill;
    for (const auto &pair : differingPairs) {
        if (isMixedDrillPair(pair)) {
            drill.append(pair);
        }
    }
    return drill;
}
