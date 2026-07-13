#include "crccomparator.h"

#include <QHash>

namespace {

ArchiveEntry classifyAgainst(const ArchiveReader::RawFileEntry &entry,
                              const QHash<QString, quint32> &otherArchiveCrcByPath)
{
    ArchiveEntry result;
    result.relativePath = entry.relativePath;
    result.sizeBytes = entry.sizeBytes;

    const auto it = otherArchiveCrcByPath.constFind(entry.relativePath);
    if (it == otherArchiveCrcByPath.constEnd()) {
        result.classification = EntryClassification::OnlyInThisArchive;
    } else if (it.value() == entry.crc32) {
        result.classification = EntryClassification::Identical;
    } else {
        result.classification = EntryClassification::Different;
    }
    return result;
}

QHash<QString, quint32> crcByPath(const QVector<ArchiveReader::RawFileEntry> &entries)
{
    QHash<QString, quint32> map;
    map.reserve(entries.size());
    for (const auto &entry : entries) {
        map.insert(entry.relativePath, entry.crc32);
    }
    return map;
}

} // namespace

ArchiveComparisonResult CrcComparator::compare(const QVector<ArchiveReader::RawFileEntry> &entriesA,
                                                const QVector<ArchiveReader::RawFileEntry> &entriesB)
{
    const QHash<QString, quint32> crcByPathA = crcByPath(entriesA);
    const QHash<QString, quint32> crcByPathB = crcByPath(entriesB);

    ArchiveComparisonResult result;
    result.entriesA.reserve(entriesA.size());
    for (const auto &entry : entriesA) {
        result.entriesA.append(classifyAgainst(entry, crcByPathB));
    }

    result.entriesB.reserve(entriesB.size());
    for (const auto &entry : entriesB) {
        result.entriesB.append(classifyAgainst(entry, crcByPathA));
    }

    return result;
}
