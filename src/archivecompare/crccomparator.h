#pragma once

#include <QVector>

#include "archivecomparisonresult.h"
#include "archivereader.h"

// Classifies each file found in archive A and/or B (FR-003/FR-004) as
// Identical, Different, or OnlyInThisArchive. Content-first: matching is
// keyed on (crc32, sizeBytes) content classes, never on path/name, except
// as a last-resort name-based fallback once content alone can't place a
// class. This is what lets a byte-identical file survive a rename or a
// folder relocation between the two archives without losing the "still the
// same file" signal a path-keyed comparison would miss. See
// docs/superpowers/specs/2026-07-27-content-first-archive-matching-design.md.
namespace CrcComparator {

ArchiveComparisonResult compare(const QVector<ArchiveReader::RawFileEntry> &entriesA,
                                 const QVector<ArchiveReader::RawFileEntry> &entriesB);

} // namespace CrcComparator
