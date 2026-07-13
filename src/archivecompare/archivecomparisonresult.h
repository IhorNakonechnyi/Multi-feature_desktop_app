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

// One classified file as it will appear in ONE archive's tree pane.
// relativePath is the file's full path within its own archive (a nested
// zip's own name becomes a path segment, so a file inside a nested zip
// still shows at its true leaf location - FR-002).
struct ArchiveEntry {
    QString relativePath;
    quint64 sizeBytes = 0;
    EntryClassification classification = EntryClassification::Identical;
};

// Two independent, flat, per-archive file lists (data-model.md's
// ArchiveComparisonResult) - each list only contains entries that actually
// exist in that archive, so archive A's list never carries a placeholder
// for a file that only exists in B (spec.md acceptance scenario 7).
// ArchiveTreeModel turns each flat list into the nested folder tree used
// for display.
struct ArchiveComparisonResult {
    QVector<ArchiveEntry> entriesA;
    QVector<ArchiveEntry> entriesB;
};

Q_DECLARE_METATYPE(EntryClassification)
