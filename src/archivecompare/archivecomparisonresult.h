#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>

// Same path + CRC in both archives, same path with a differing CRC, or a
// path that only exists in one archive - the three-way split the whole
// green/yellow/red scheme is built on (FR-004).
enum class EntryClassification {
    Identical,
    Different,
    OnlyInThisArchive
};

// Why a pair was formed (Q1 "pair provenance"): yellow means the app assumed
// something, and the assumption must be inspectable in the pair list.
enum class PairReason {
    SameName,               // leaf filenames matched (case-insensitive)
    OnlyExtensionCandidate, // elimination: exactly one class per side with this extension
};

// Which family a file belongs to, for the split summary line (Q8). Only two
// buckets because only one distinction matters to the user: "can this app
// show me the difference?" Drill files are folded into GerberData - Q2 made
// drawability a content question, and a drill file is previewable exactly as
// a Gerber layer is.
enum class DataKind {
    GerberData,
    OtherData,
};

// One classified file as it will appear in ONE archive's tree pane.
// relativePath is the file's full path within its own archive (a nested
// zip's own name becomes a path segment, so a file inside a nested zip
// still shows at its true leaf location - FR-002).
struct ArchiveEntry {
    QString relativePath;
    quint64 sizeBytes = 0;
    quint32 crc32 = 0;   // half of the (crc32, size) content-class key. Internal
                         // plumbing for class counting (Q8 summary) - never displayed (Q9).
    EntryClassification classification = EntryClassification::Identical;
    DataKind kind = DataKind::OtherData; // resolved in Task 15; defaults to the
                                         // conservative "not previewable"
};

// A file classified Different on both sides, naming which A-file pairs
// with which B-file. Needed explicitly (not inferred from equal paths)
// because content-first matching's name pass or extension pass can match
// two files that sit at different paths in each archive (see the
// path-tolerant-matching design doc).
struct DifferingPair {
    QString relativePathA;
    QString relativePathB;
    PairReason reason = PairReason::SameName;
};

// Two independent, flat, per-archive file lists (data-model.md's
// ArchiveComparisonResult) - each list only contains entries that actually
// exist in that archive, so archive A's list never carries a placeholder
// for a file that only exists in B (spec.md acceptance scenario 7).
// ArchiveTreeModel turns each flat list into the nested folder tree used
// for display. differingPairs is the same Different-classified files as
// entriesA/entriesB, but linked A-to-B for consumers (e.g. GerberDiffTrigger)
// that need to know which two files to compare, not just that each changed.
struct ArchiveComparisonResult {
    QVector<ArchiveEntry> entriesA;
    QVector<ArchiveEntry> entriesB;
    QVector<DifferingPair> differingPairs;

    // Pairs whose bytes differ but whose data does not - typically a plain
    // re-export that only rewrote date/user/job lines. Moved here out of
    // differingPairs by SameDataCheck, their files shown green. Kept rather
    // than dropped so the summary can still count them as identical.
    QVector<DifferingPair> sameDataPairs;
};

Q_DECLARE_METATYPE(EntryClassification)
