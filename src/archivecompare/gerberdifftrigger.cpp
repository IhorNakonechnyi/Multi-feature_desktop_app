#include "gerberdifftrigger.h"

#include "../gerber/gerberfiletype.h"

namespace {

bool hasDrillSide(const DifferingPair &pair)
{
    return GerberFileType::isDrillFile(pair.relativePathA) || GerberFileType::isDrillFile(pair.relativePathB);
}

bool bothSidesDrillFiles(const DifferingPair &pair)
{
    return GerberFileType::isDrillFile(pair.relativePathA) && GerberFileType::isDrillFile(pair.relativePathB);
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
        if (GerberFileType::isRecognized(pair.relativePathA) && GerberFileType::isRecognized(pair.relativePathB)) {
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
        if (!GerberFileType::isRecognized(pair.relativePathA) || !GerberFileType::isRecognized(pair.relativePathB)) {
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
