#include "comparisonsummary.h"

#include <map>
#include <utility>

#include <QHash>

namespace ComparisonSummary {

namespace {

// A content class is identified by exactly the key the whole comparison is
// built on: the bytes' CRC together with their length.
using ClassKey = std::pair<quint32, quint64>;

// What one archive knows about one content class. std::map (not QHash) so
// the walk order is deterministic - the counts would be the same either way,
// but a stable order makes a failing test reproduce identically.
struct ClassInfo {
    bool anyGerber = false;
    bool anyMissingCounterpart = false;
};

std::map<ClassKey, ClassInfo> indexByClass(const QVector<ArchiveEntry> &entries)
{
    std::map<ClassKey, ClassInfo> classes;
    for (const ArchiveEntry &entry : entries) {
        ClassInfo &info = classes[ClassKey(entry.crc32, entry.sizeBytes)];

        // "any" rather than "the representative row", and the two are
        // equivalent here: CrcComparator forces every NON-representative
        // member of a class to plain Identical unconditionally (see its
        // step 2), so the representative is the only member that can ever
        // carry OnlyInThisArchive. ORing over all members therefore reduces
        // to reading the representative, without this code having to
        // re-derive which row Q1 picked as one.
        info.anyGerber = info.anyGerber || entry.kind == DataKind::GerberData;
        info.anyMissingCounterpart = info.anyMissingCounterpart
                                     || entry.classification == EntryClassification::OnlyInThisArchive;
    }
    return classes;
}

QHash<QString, DataKind> kindByPath(const QVector<ArchiveEntry> &entries)
{
    QHash<QString, DataKind> kinds;
    for (const ArchiveEntry &entry : entries) {
        kinds.insert(entry.relativePath, entry.kind);
    }
    return kinds;
}

} // namespace

bool Summary::fullyIdentical() const
{
    const auto nothingToDo = [](const TypeCounts &counts) {
        return counts.pairs == 0 && counts.onlyInA == 0 && counts.onlyInB == 0;
    };
    // Deliberately says nothing about identicalClasses: two empty archives
    // are identical, and so are two archives whose every class matches.
    return nothingToDo(gerber) && nothingToDo(other);
}

Summary summarize(const ArchiveComparisonResult &result)
{
    const std::map<ClassKey, ClassInfo> classesA = indexByClass(result.entriesA);
    const std::map<ClassKey, ClassInfo> classesB = indexByClass(result.entriesB);

    Summary summary;
    const auto bucket = [&summary](bool isGerber) -> TypeCounts & {
        return isGerber ? summary.gerber : summary.other;
    };

    for (const auto &[key, infoA] : classesA) {
        const auto matchInB = classesB.find(key);
        if (matchInB != classesB.end()) {
            // Present on both sides: one identical thing, counted once here
            // and skipped entirely by B's loop below. If the two sides
            // disagree about the kind (only possible through a sniff edge
            // case) prefer Gerber, so the summary agrees with the pair list.
            bucket(infoA.anyGerber || matchInB->second.anyGerber).identicalClasses++;
        } else if (infoA.anyMissingCounterpart) {
            // One-sided AND actually classified as missing. A *yellow* class
            // is also one-sided by construction - content-first found no key
            // match, which is why it fell through to the name pass - but it
            // is already reported as a pair, so counting it here would
            // report the same work twice.
            bucket(infoA.anyGerber).onlyInA++;
        }
    }

    for (const auto &[key, infoB] : classesB) {
        if (classesA.find(key) == classesA.end() && infoB.anyMissingCounterpart) {
            bucket(infoB.anyGerber).onlyInB++;
        }
    }

    // Pairs are the one thing counted as ROWS, not classes: each pair is a
    // separate comparison the user can open, so a name pass that produced a
    // cross product of two really is two things to look at.
    const QHash<QString, DataKind> kindsA = kindByPath(result.entriesA);
    const QHash<QString, DataKind> kindsB = kindByPath(result.entriesB);

    for (const DifferingPair &pair : result.differingPairs) {
        const bool isGerber =
            kindsA.value(pair.relativePathA, DataKind::OtherData) == DataKind::GerberData
            || kindsB.value(pair.relativePathB, DataKind::OtherData) == DataKind::GerberData;
        bucket(isGerber).pairs++;
    }

    return summary;
}

} // namespace ComparisonSummary
