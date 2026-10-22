#include "comparisonsummary.h"

#include <map>
#include <utility>

#include <QFileInfo>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>

#include "../gerber/gerberfiletype.h"

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

namespace {

// Extensions that settle the question on their own. Kept deliberately short:
// an extension only earns a place here if being wrong about it is
// implausible. Anything else is cheaper to read than to guess about.
const QSet<QString> &conclusiveGerberSuffixes()
{
    static const QSet<QString> suffixes = {
        // Layer data
        "gtl", "gbl", "gts", "gbs", "gto", "gbo", "gtp", "gbp",
        "gko", "gml", "gbr", "cmp", "sol", "stc", "sts", "plc", "ger",
        // Drill - folded in with Gerber, since Q2 makes both previewable
        "drl", "xln", "drd",
    };
    return suffixes;
}

// Documents, images and containers, which are never layer data.
const QSet<QString> &conclusiveOtherSuffixes()
{
    static const QSet<QString> suffixes = {
        "pdf", "doc", "docx", "xls", "xlsx",
        "png", "jpg", "jpeg", "bmp", "gif",
        "zip", "rar", "7z", "tgz", "tar", "gz", "brd", "html", "htm",
    };
    return suffixes;
}

} // namespace

void resolveKinds(QVector<ArchiveEntry> &entries, const HeadReader &readHead)
{
    for (ArchiveEntry &entry : entries) {
        const QString suffix = QFileInfo(entry.relativePath).suffix().toLower();

        if (conclusiveGerberSuffixes().contains(suffix)) {
            entry.kind = DataKind::GerberData;
            continue;
        }
        if (conclusiveOtherSuffixes().contains(suffix)) {
            entry.kind = DataKind::OtherData;
            continue;
        }

        // Ambiguous - txt, csv, no extension at all, or something nobody
        // anticipated. Ask the bytes. NotDrawable is the honest answer for
        // anything the parsers could not render anyway.
        const GerberFileType::Drawability drawability =
            GerberFileType::classify(readHead(entry.relativePath));
        entry.kind = drawability != GerberFileType::Drawability::NotDrawable ? DataKind::GerberData
                                                                            : DataKind::OtherData;
    }
}

bool Summary::fullyIdentical() const
{
    const auto nothingToDo = [](const TypeCounts &counts) {
        return counts.pairs == 0 && counts.onlyInA == 0 && counts.onlyInB == 0;
    };
    // Deliberately says nothing about identicalClasses: two empty archives
    // are identical, and so are two archives whose every class matches.
    return nothingToDo(gerber) && nothingToDo(other);
}

QString formatTypeCounts(const QString &label, const TypeCounts &counts)
{
    // Identical and pairs are global facts about the comparison; "to check"
    // is per panel, because a missing file exists on one side only by
    // definition. Zero-valued fragments are dropped rather than printed as
    // "0", so the line stays short enough to read at a glance.
    QStringList parts;
    if (counts.identicalClasses > 0) {
        parts << QObject::tr("%n identical", "", counts.identicalClasses);
    }
    if (counts.pairs > 0) {
        parts << QObject::tr("%n pair(s)", "", counts.pairs);
    }
    if (counts.onlyInA > 0) {
        parts << QObject::tr("A: %n to check", "", counts.onlyInA);
    }
    if (counts.onlyInB > 0) {
        parts << QObject::tr("B: %n to check", "", counts.onlyInB);
    }
    if (parts.isEmpty()) {
        return {};
    }
    return QObject::tr("%1: %2").arg(label, parts.join(QStringLiteral(" · ")));
}

QString formatSummaryLine(const Summary &summary)
{
    QStringList kinds;
    const QString gerber = formatTypeCounts(QObject::tr("Gerber"), summary.gerber);
    if (!gerber.isEmpty()) {
        kinds << gerber;
    }
    const QString other = formatTypeCounts(QObject::tr("Other"), summary.other);
    if (!other.isEmpty()) {
        kinds << other;
    }
    return kinds.join(QStringLiteral("    "));
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

    // A pair the same-data check moved back to green is, for the user, one
    // more identical thing - its CRCs differ, so the class walk above never
    // saw it as identical. Only Gerber/drill pairs can get here.
    summary.gerber.identicalClasses += result.sameDataPairs.size();

    return summary;
}

} // namespace ComparisonSummary
