#include "crccomparator.h"

#include <QFileInfo>
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
// case-insensitive path comparison, and any tie remaining after THAT
// (paths equal except for case, e.g. "Top.gtl" vs "TOP.GTL") is broken by
// earlier original index - guaranteed deterministic because memberIndices
// is always built in ascending index order, so "first candidate seen"
// below is also "lowest index". Never archive/central-directory order for
// the depth/case rules above, though - the representative must be
// predictable from the paths alone, not from whatever order the customer's
// packaging tool happened to emit entries in. Because a nested archive's
// own name becomes a path segment, a copy sitting at the archive root
// always beats a copy inside a nested archive.
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
            result.entriesA[classesA.constFind(key)->representativeIndex].classification =
                EntryClassification::Different;
            placedA.insert(key);
        }
        for (const ClassKey &key : bKeys) {
            result.entriesB[classesB.constFind(key)->representativeIndex].classification =
                EntryClassification::Different;
            placedB.insert(key);
        }
        for (const ClassKey &aKey : aKeys) {
            const QString &pathA = entriesA.at(classesA.constFind(aKey)->representativeIndex).relativePath;
            for (const ClassKey &bKey : bKeys) {
                const QString &pathB = entriesB.at(classesB.constFind(bKey)->representativeIndex).relativePath;
                result.differingPairs.append({pathA, pathB, PairReason::SameName});
            }
        }
    }

    // Step 5: extension elimination pass over whatever content matching and
    // the name pass both failed to place. This is deliberately the last
    // and weakest fallback, and its guard is much stricter than the name
    // pass's for a reason: a shared extension is not evidence that two
    // files correspond, only an elimination argument - "these are the only
    // two files left unaccounted for with this extension, on each side, so
    // they probably correspond." In a real PCB archive an entire layer set
    // routinely shares one generic extension (e.g. every drill file), so
    // the moment there is more than one leftover candidate under an
    // extension on EITHER side, the elimination stops being unambiguous
    // and nothing is paired - unlike the name pass, there is no cross
    // product fallback here. Guessing wrong here would silently point the
    // user at the wrong file to diff, which is worse than leaving both
    // sides red for a human to sort out. Classes whose representative path
    // has no extension are skipped outright - QFileInfo::suffix() returns
    // an empty string for those, and an empty string is not a real key to
    // eliminate on (it would falsely lump together every extension-less
    // file in the archive as if they were one candidate pool).
    QHash<QString, QVector<ClassKey>> leftoverExtensionsA;
    for (auto it = classesA.constBegin(); it != classesA.constEnd(); ++it) {
        if (placedA.contains(it.key())) {
            continue;
        }
        const QString extension =
            QFileInfo(entriesA.at(it.value().representativeIndex).relativePath).suffix().toLower();
        if (extension.isEmpty()) {
            continue;
        }
        leftoverExtensionsA[extension].append(it.key());
    }
    QHash<QString, QVector<ClassKey>> leftoverExtensionsB;
    for (auto it = classesB.constBegin(); it != classesB.constEnd(); ++it) {
        if (placedB.contains(it.key())) {
            continue;
        }
        const QString extension =
            QFileInfo(entriesB.at(it.value().representativeIndex).relativePath).suffix().toLower();
        if (extension.isEmpty()) {
            continue;
        }
        leftoverExtensionsB[extension].append(it.key());
    }

    for (auto it = leftoverExtensionsA.constBegin(); it != leftoverExtensionsA.constEnd(); ++it) {
        if (it.value().size() != 1) {
            // More than one leftover class shares this extension in A -
            // no way to tell which one (if any) corresponds to B's side.
            continue;
        }
        const auto itB = leftoverExtensionsB.constFind(it.key());
        if (itB == leftoverExtensionsB.constEnd() || itB.value().size() != 1) {
            // Nothing under this extension in B, or more than one there
            // too - same "can't tell which" problem, mirrored on B's side.
            continue;
        }
        const ClassKey &aKey = it.value().first();
        const ClassKey &bKey = itB.value().first();
        const int repIndexA = classesA.constFind(aKey)->representativeIndex;
        const int repIndexB = classesB.constFind(bKey)->representativeIndex;
        result.entriesA[repIndexA].classification = EntryClassification::Different;
        result.entriesB[repIndexB].classification = EntryClassification::Different;
        placedA.insert(aKey);
        placedB.insert(bKey);
        result.differingPairs.append({entriesA.at(repIndexA).relativePath,
                                       entriesB.at(repIndexB).relativePath,
                                       PairReason::OnlyExtensionCandidate});
    }

    // Step 6: anything still unplaced has no counterpart on the other side
    // by content, name, or extension elimination - red.
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
