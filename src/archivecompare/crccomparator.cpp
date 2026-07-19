#include "crccomparator.h"

#include <QHash>
#include <QSet>

namespace {

using ArchiveReader::RawFileEntry;

QString filenameOf(const QString &relativePath)
{
    const int slash = relativePath.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? relativePath : relativePath.mid(slash + 1);
}

QHash<QString, quint32> crcByPath(const QVector<RawFileEntry> &entries)
{
    QHash<QString, quint32> map;
    map.reserve(entries.size());
    for (const auto &entry : entries) {
        map.insert(entry.relativePath, entry.crc32);
    }
    return map;
}

// filename -> crc -> indices (into the original entries vector) sharing
// that (filename, crc) pair - lets Pass 2 treat true duplicates (same name
// and content, copied into multiple folders) as one equivalence class
// instead of only ever matching the first copy it happens to see.
using DuplicateGroups = QHash<QString, QHash<quint32, QVector<int>>>;

DuplicateGroups groupByFilenameAndCrc(const QVector<RawFileEntry> &entries, const QVector<int> &leftoverIndices)
{
    DuplicateGroups groups;
    for (int index : leftoverIndices) {
        const RawFileEntry &entry = entries.at(index);
        groups[filenameOf(entry.relativePath)][entry.crc32].append(index);
    }
    return groups;
}

// Indices of entries that are an accidental extra copy of an earlier entry
// in the *same* archive (same filename, same CRC - e.g. a nested zip that
// re-embeds files already present elsewhere in the same archive, "archive
// contains a copy of itself"). First-seen-wins: only the 2nd+ occurrence of
// a given (filename, crc) pair counts as a duplicate, so a file genuinely
// absent from the other archive still surfaces once via its first
// occurrence.
QSet<int> selfDuplicateIndices(const QVector<RawFileEntry> &entries)
{
    QHash<QString, QSet<quint32>> seenCrcsByFilename;
    QSet<int> duplicates;
    for (int i = 0; i < entries.size(); ++i) {
        const RawFileEntry &entry = entries.at(i);
        QSet<quint32> &seenCrcs = seenCrcsByFilename[filenameOf(entry.relativePath)];
        if (seenCrcs.contains(entry.crc32)) {
            duplicates.insert(i);
        } else {
            seenCrcs.insert(entry.crc32);
        }
    }
    return duplicates;
}

} // namespace

ArchiveComparisonResult CrcComparator::compare(const QVector<RawFileEntry> &entriesA,
                                                const QVector<RawFileEntry> &entriesB)
{
    const QHash<QString, quint32> crcByPathA = crcByPath(entriesA);
    const QHash<QString, quint32> crcByPathB = crcByPath(entriesB);

    ArchiveComparisonResult result;
    result.entriesA.resize(entriesA.size());
    result.entriesB.resize(entriesB.size());

    // Entries Pass 1 couldn't place by exact path - candidates for the
    // Pass 2 filename fallback below.
    QVector<int> leftoverA;
    QVector<int> leftoverB;

    const QSet<int> duplicatesA = selfDuplicateIndices(entriesA);
    const QSet<int> duplicatesB = selfDuplicateIndices(entriesB);

    for (int i = 0; i < entriesA.size(); ++i) {
        const RawFileEntry &entry = entriesA[i];
        ArchiveEntry &out = result.entriesA[i];
        out.relativePath = entry.relativePath;
        out.sizeBytes = entry.sizeBytes;

        if (duplicatesA.contains(i)) {
            // An accidental extra copy of a file already accounted for
            // elsewhere in this same archive - not a real difference to
            // compare, so it's forced green unconditionally rather than run
            // through Pass 1/2 matching against the other archive.
            out.classification = EntryClassification::Identical;
            continue;
        }

        const auto it = crcByPathB.constFind(entry.relativePath);
        if (it == crcByPathB.constEnd()) {
            out.classification = EntryClassification::OnlyInThisArchive;
            leftoverA.append(i);
        } else if (it.value() == entry.crc32) {
            out.classification = EntryClassification::Identical;
        } else {
            out.classification = EntryClassification::Different;
            // Same path on both sides, so it doubles as both halves of the
            // pair - added once here rather than also in the B loop below,
            // which would otherwise record the same pair twice.
            result.differingPairs.append({entry.relativePath, entry.relativePath});
        }
    }

    for (int i = 0; i < entriesB.size(); ++i) {
        const RawFileEntry &entry = entriesB[i];
        ArchiveEntry &out = result.entriesB[i];
        out.relativePath = entry.relativePath;
        out.sizeBytes = entry.sizeBytes;

        if (duplicatesB.contains(i)) {
            out.classification = EntryClassification::Identical;
            continue;
        }

        const auto it = crcByPathA.constFind(entry.relativePath);
        if (it == crcByPathA.constEnd()) {
            out.classification = EntryClassification::OnlyInThisArchive;
            leftoverB.append(i);
        } else {
            out.classification =
                (it.value() == entry.crc32) ? EntryClassification::Identical : EntryClassification::Different;
        }
    }

    // Pass 2: filename fallback, for files a customer moved/renested
    // differently between the two archives. Only matches a filename when
    // each side has exactly one distinct-content candidate under that name
    // - if either side has multiple candidates with different CRCs for the
    // same filename, that's genuinely ambiguous and is left unmatched
    // (stays OnlyInThisArchive) rather than guessing a pairing.
    const DuplicateGroups groupsA = groupByFilenameAndCrc(entriesA, leftoverA);
    const DuplicateGroups groupsB = groupByFilenameAndCrc(entriesB, leftoverB);

    for (auto itFilename = groupsA.constBegin(); itFilename != groupsA.constEnd(); ++itFilename) {
        const auto &crcGroupsA = itFilename.value();

        const auto itB = groupsB.constFind(itFilename.key());
        if (itB == groupsB.constEnd()) {
            continue;
        }
        const auto &crcGroupsB = itB.value();

        if (crcGroupsA.size() != 1 || crcGroupsB.size() != 1) {
            continue;
        }

        const quint32 crcA = crcGroupsA.constBegin().key();
        const quint32 crcB = crcGroupsB.constBegin().key();
        const EntryClassification classification =
            (crcA == crcB) ? EntryClassification::Identical : EntryClassification::Different;

        for (int index : crcGroupsA.constBegin().value()) {
            result.entriesA[index].classification = classification;
        }
        for (int index : crcGroupsB.constBegin().value()) {
            result.entriesB[index].classification = classification;
        }

        if (classification == EntryClassification::Different) {
            // One representative pair per matched filename group, even if
            // either side has duplicate copies - see the design doc's note
            // on not over-modeling the duplicate-times-duplicate case.
            const int representativeA = crcGroupsA.constBegin().value().first();
            const int representativeB = crcGroupsB.constBegin().value().first();
            result.differingPairs.append(
                {entriesA[representativeA].relativePath, entriesB[representativeB].relativePath});
        }
    }

    return result;
}
