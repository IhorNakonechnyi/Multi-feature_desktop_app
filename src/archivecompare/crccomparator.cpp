#include "crccomparator.h"

#include <QHash>
#include <QPair>
#include <QSet>

namespace {

using ArchiveReader::RawFileEntry;

// The unit of matching is content, not path: two entries with the same
// (crc32, sizeBytes) are the same content class, everywhere. Size joins
// crc32 purely as a cheap guard against CRC32 collisions - both come free
// from the zip central directory, no decompression needed for either.
using ClassKey = QPair<quint32, quint64>;

// All entries (within one archive) sharing a content class. One member -
// the "representative" - stands for the whole class in every later pass:
// it is the path recorded in a pair, and its leaf name is what the name
// pass (step 4) matches on. Every other member is a redundant copy and is
// unconditionally rendered plain green (Identical), regardless of what
// happens to the class as a whole - see design doc step 2.
struct ContentClass {
    QVector<int> memberIndices; // indices into the archive's entry vector
    int representativeIndex = -1;
};

using ClassMap = QHash<ClassKey, ContentClass>;

int pathDepth(const QString &path)
{
    return path.count(QLatin1Char('/'));
}

QString leafName(const QString &path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? path : path.mid(slash + 1);
}

// Group entries by content class and pick each class's representative:
// the member with the shallowest path (fewest '/'), ties broken by
// case-insensitive path comparison. Never archive/central-directory order -
// the representative must be predictable from the paths alone, not from
// whatever order the customer's packaging tool happened to emit entries in.
// Because a nested archive's own name becomes a path segment, a copy sitting
// at the archive root always beats a copy inside a nested archive.
ClassMap buildClasses(const QVector<RawFileEntry> &entries)
{
    ClassMap classes;
    for (int i = 0; i < entries.size(); ++i) {
        const RawFileEntry &entry = entries.at(i);
        classes[ClassKey(entry.crc32, entry.sizeBytes)].memberIndices.append(i);
    }

    for (auto it = classes.begin(); it != classes.end(); ++it) {
        ContentClass &cls = it.value();
        int repIndex = cls.memberIndices.first();
        for (int candidate : cls.memberIndices) {
            if (candidate == repIndex) {
                continue;
            }
            const QString &candidatePath = entries.at(candidate).relativePath;
            const QString &currentRepPath = entries.at(repIndex).relativePath;
            const int candidateDepth = pathDepth(candidatePath);
            const int currentDepth = pathDepth(currentRepPath);
            const bool candidateWins = candidateDepth != currentDepth
                ? candidateDepth < currentDepth
                : QString::compare(candidatePath, currentRepPath, Qt::CaseInsensitive) < 0;
            if (candidateWins) {
                repIndex = candidate;
            }
        }
        cls.representativeIndex = repIndex;
    }
    return classes;
}

} // namespace

// Content-first matching: entries are matched on (crc32, sizeBytes), never
// on path/name, except as a last-resort fallback once content alone can't
// place a class. See
// docs/superpowers/specs/2026-07-27-content-first-archive-matching-design.md
// for the full rationale (this rewrite fixes false greens, arbitrary
// duplicate survivorship, and rename/relocation blindness that the older
// path-keyed algorithm had).
ArchiveComparisonResult CrcComparator::compare(const QVector<RawFileEntry> &entriesA,
                                                const QVector<RawFileEntry> &entriesB)
{
    ArchiveComparisonResult result;
    result.entriesA.resize(entriesA.size());
    result.entriesB.resize(entriesB.size());

    // Copy the fields every entry needs regardless of classification.
    // Classification defaults to Identical (ArchiveEntry's default) and is
    // overwritten below as each pass places (or fails to place) the entry.
    for (int i = 0; i < entriesA.size(); ++i) {
        result.entriesA[i].relativePath = entriesA[i].relativePath;
        result.entriesA[i].sizeBytes = entriesA[i].sizeBytes;
        result.entriesA[i].crc32 = entriesA[i].crc32;
    }
    for (int i = 0; i < entriesB.size(); ++i) {
        result.entriesB[i].relativePath = entriesB[i].relativePath;
        result.entriesB[i].sizeBytes = entriesB[i].sizeBytes;
        result.entriesB[i].crc32 = entriesB[i].crc32;
    }

    // Step 1: content classes, per archive.
    const ClassMap classesA = buildClasses(entriesA);
    const ClassMap classesB = buildClasses(entriesB);

    // Step 2: collapse duplicates within each archive. Every non-
    // representative member is plain green, unconditionally - whatever the
    // class's eventual status turns out to be, a redundant copy is always
    // "nothing for the user to do" (design doc step 2 / Q9).
    for (auto it = classesA.constBegin(); it != classesA.constEnd(); ++it) {
        for (int index : it.value().memberIndices) {
            if (index != it.value().representativeIndex) {
                result.entriesA[index].classification = EntryClassification::Identical;
            }
        }
    }
    for (auto it = classesB.constBegin(); it != classesB.constEnd(); ++it) {
        for (int index : it.value().memberIndices) {
            if (index != it.value().representativeIndex) {
                result.entriesB[index].classification = EntryClassification::Identical;
            }
        }
    }

    // Step 3: content match. A class key present in both archives means
    // the same content exists on both sides - green on both, name-blind,
    // so renames/relocations/re-extensioning are recognised as unchanged
    // for free. Iterating classesA and checking classesB is enough: a
    // shared key is symmetric, so this handles both sides in one pass.
    QSet<ClassKey> placedA;
    QSet<ClassKey> placedB;
    for (auto it = classesA.constBegin(); it != classesA.constEnd(); ++it) {
        const ClassKey &key = it.key();
        const auto itB = classesB.constFind(key);
        if (itB == classesB.constEnd()) {
            continue;
        }
        result.entriesA[it.value().representativeIndex].classification = EntryClassification::Identical;
        result.entriesB[itB.value().representativeIndex].classification = EntryClassification::Identical;
        placedA.insert(key);
        placedB.insert(key);
    }

    // Step 4: name pass over what content matching couldn't place. Group
    // the leftover classes by their REPRESENTATIVE's leaf filename,
    // lowercased - a class whose members have different names is matched
    // under the representative's name only (design doc step 2). A name
    // present on both sides pairs every A-class under it with every
    // B-class under it (a full cross product, not one representative pair
    // per design doc step 4): if one side has two distinct-content files
    // under that name, the user must be able to check the other side's
    // file against each of them.
    QHash<QString, QVector<ClassKey>> leftoverNamesA;
    for (auto it = classesA.constBegin(); it != classesA.constEnd(); ++it) {
        if (placedA.contains(it.key())) {
            continue;
        }
        const QString name = leafName(entriesA.at(it.value().representativeIndex).relativePath).toLower();
        leftoverNamesA[name].append(it.key());
    }
    QHash<QString, QVector<ClassKey>> leftoverNamesB;
    for (auto it = classesB.constBegin(); it != classesB.constEnd(); ++it) {
        if (placedB.contains(it.key())) {
            continue;
        }
        const QString name = leafName(entriesB.at(it.value().representativeIndex).relativePath).toLower();
        leftoverNamesB[name].append(it.key());
    }

    for (auto it = leftoverNamesA.constBegin(); it != leftoverNamesA.constEnd(); ++it) {
        const auto itB = leftoverNamesB.constFind(it.key());
        if (itB == leftoverNamesB.constEnd()) {
            continue;
        }
        const QVector<ClassKey> &aKeys = it.value();
        const QVector<ClassKey> &bKeys = itB.value();

        for (const ClassKey &key : aKeys) {
            result.entriesA[classesA[key].representativeIndex].classification = EntryClassification::Different;
            placedA.insert(key);
        }
        for (const ClassKey &key : bKeys) {
            result.entriesB[classesB[key].representativeIndex].classification = EntryClassification::Different;
            placedB.insert(key);
        }
        for (const ClassKey &aKey : aKeys) {
            const QString &pathA = entriesA.at(classesA[aKey].representativeIndex).relativePath;
            for (const ClassKey &bKey : bKeys) {
                const QString &pathB = entriesB.at(classesB[bKey].representativeIndex).relativePath;
                result.differingPairs.append({pathA, pathB, PairReason::SameName});
            }
        }
    }

    // Step 5 (of this task; the design's step 5 extension pass is a later
    // task): anything still unplaced has no counterpart on the other side
    // by either content or name - red.
    for (auto it = classesA.constBegin(); it != classesA.constEnd(); ++it) {
        if (!placedA.contains(it.key())) {
            result.entriesA[it.value().representativeIndex].classification = EntryClassification::OnlyInThisArchive;
        }
    }
    for (auto it = classesB.constBegin(); it != classesB.constEnd(); ++it) {
        if (!placedB.contains(it.key())) {
            result.entriesB[it.value().representativeIndex].classification = EntryClassification::OnlyInThisArchive;
        }
    }

    return result;
}
