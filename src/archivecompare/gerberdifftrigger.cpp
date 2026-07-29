#include "gerberdifftrigger.h"

#include "../gerber/gerberfiletype.h"

GerberDiffTrigger::PairBuckets GerberDiffTrigger::classifyPairs(const QVector<DifferingPair> &pairs,
                                                                 const HeadReader &readHeadA,
                                                                 const HeadReader &readHeadB)
{
    PairBuckets buckets;
    for (const auto &pair : pairs) {
        const GerberFileType::Drawability classificationA = GerberFileType::classify(readHeadA(pair.relativePathA));
        const GerberFileType::Drawability classificationB = GerberFileType::classify(readHeadB(pair.relativePathB));
        const bool bothDrawable = classificationA != GerberFileType::Drawability::NotDrawable
            && classificationB != GerberFileType::Drawability::NotDrawable;
        if (bothDrawable) {
            buckets.drawable.append(pair);
        } else {
            buckets.notDrawable.append(pair);
        }
    }
    return buckets;
}
