#pragma once

#include <QVector>

#include "archivecomparisonresult.h"
#include "archivereader.h"

// Classifies each file found in archive A and/or B (FR-003/FR-004) as
// Identical, Different, or OnlyInThisArchive, via two passes: first an
// exact relative-path match (same as v1.0), then a filename-only fallback
// (ignoring directory) for anything the first pass couldn't place - this is
// what correctly handles files a customer relocated/renamed folders around
// between archives without losing the "same spot, edited content" signal
// the exact-path pass gives. See
// docs/superpowers/specs/2026-07-15-path-tolerant-crc-matching-design.md.
namespace CrcComparator {

ArchiveComparisonResult compare(const QVector<ArchiveReader::RawFileEntry> &entriesA,
                                 const QVector<ArchiveReader::RawFileEntry> &entriesB);

} // namespace CrcComparator
