#pragma once

#include <QVector>

#include "archivecomparisonresult.h"
#include "archivereader.h"

// Classifies each file found in archive A and/or B, keyed by its path
// within the archive (FR-003/FR-004): Identical (same path + CRC in both),
// Different (same path, differing CRC), or OnlyInThisArchive (path exists
// in only one archive). Matching is by (path, CRC), not a path-independent
// content match across arbitrarily different folder structures - see
// tasks.md's notes for why that's out of scope for v1.0.
namespace CrcComparator {

ArchiveComparisonResult compare(const QVector<ArchiveReader::RawFileEntry> &entriesA,
                                 const QVector<ArchiveReader::RawFileEntry> &entriesB);

} // namespace CrcComparator
