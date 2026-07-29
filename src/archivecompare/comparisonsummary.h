#pragma once

#include "archivecomparisonresult.h"

// The one-line summary above the two panels (Q8), split by data kind so the
// user can see at a glance whether the changes are in files this app can
// actually show them.
//
// The counting unit is the CONTENT CLASS - a distinct (crc32, sizeBytes) -
// not the tree row. That is the whole point: after Q1's duplicate collapse a
// single file copied three times inside one archive is one thing to look at,
// and reporting "3" would overstate the work. Rows are counted only for
// pairs, where each pair genuinely is a separate comparison to make.
namespace ComparisonSummary {

struct TypeCounts {
    int identicalClasses = 0; // classes whose key is present on BOTH sides
    int pairs = 0;            // rows of differingPairs of this kind
    int onlyInA = 0;          // red classes per side - one-sided by nature
    int onlyInB = 0;
};

struct Summary {
    TypeCounts gerber;
    TypeCounts other;

    // The precise "nothing to do" condition (Q8/Q1): no pair to inspect and
    // nothing missing on either side, in either kind. Deliberately not
    // "identicalClasses > 0 and nothing else" - two empty archives are
    // identical too.
    bool fullyIdentical() const;
};

// Always summarizes the WHOLE result, never a filtered view - the summary is
// the fixed truth the filters are applied against, so it must not move when
// the user hides rows. Entries' `kind` fields must already be resolved.
Summary summarize(const ArchiveComparisonResult &result);

} // namespace ComparisonSummary
