#pragma once

#include <functional>

#include <QByteArray>
#include <QString>

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

// Supplies the first bytes of an entry, for the files whose extension does
// not settle the question. Injected rather than called directly so this stays
// testable without an archive on disk.
using HeadReader = std::function<QByteArray(const QString &relativePath)>;

// Fills in each entry's `kind`, which summarize() then consumes.
//
// A conclusive extension is trusted and the file is never read; only an
// ambiguous one is sniffed. That keeps the cost proportional to the genuinely
// unclear files while staying accurate exactly where extensions lie - the
// real drill file in the sample data is a .TXT, which is why txt is
// deliberately NOT in the conclusive list.
//
// This table types the summary and nothing else. It is not a revival of the
// eligibility list Q12 deleted, and there is no user-editable version of it:
// what a file can be PARSED as is still decided purely by content.
void resolveKinds(QVector<ArchiveEntry> &entries, const HeadReader &readHead);

// Always summarizes the WHOLE result, never a filtered view - the summary is
// the fixed truth the filters are applied against, so it must not move when
// the user hides rows. Entries' `kind` fields must already be resolved.
Summary summarize(const ArchiveComparisonResult &result);

// One kind's fragment, e.g. "Gerber: 9 identical - 2 pairs - A: 1 to check".
// Empty when the kind has nothing to report, so an absent kind contributes
// nothing to the line rather than a row of zeros.
//
// Lives here rather than in the page because it is pure logic that decides
// what numbers the user reads: a swapped A/B label or a dropped fragment
// would misreport the work with nothing to catch it.
QString formatTypeCounts(const QString &label, const TypeCounts &counts);

// The whole summary line, both kinds joined.
QString formatSummaryLine(const Summary &summary);

} // namespace ComparisonSummary
