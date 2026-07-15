#include "gerberdifftrigger.h"

#include "../gerber/gerberfiletype.h"

QVector<DifferingPair> GerberDiffTrigger::eligiblePairs(const QVector<DifferingPair> &differingPairs)
{
    QVector<DifferingPair> eligible;
    for (const auto &pair : differingPairs) {
        if (GerberFileType::isRecognized(pair.relativePathA) && GerberFileType::isRecognized(pair.relativePathB)) {
            eligible.append(pair);
        }
    }
    return eligible;
}
