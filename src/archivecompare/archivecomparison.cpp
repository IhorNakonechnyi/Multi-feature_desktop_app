#include "archivecomparison.h"

#include <QElapsedTimer>
#include <QSet>

#include "archivereader.h"
#include "crccomparator.h"

namespace {

using ArchiveReader::RawFileEntry;

// Which entries (by index into `entries`) are worth opening THIS round.
//
// A nested-archive candidate is only ever collected while it is still
// "opaque" (isNestedArchiveCandidate is still set - see expandCandidates
// below for when it gets cleared) AND its classification says something
// about it needs to be seen: Different (content changed) or
// OnlyInThisArchive (exists on one side only). An Identical candidate is
// deliberately skipped here - that is the whole "never open an identical
// container" rule (Q4 point 3): if a nested archive is byte-identical on
// both sides, nothing inside it can possibly differ either, so there is
// nothing for the user to see and opening it would be pure wasted work
// (and pure wasted screen space - it would explode into every one of its
// own contained rows for no reason). `classifiedEntries` is CrcComparator's
// per-archive output, which is guaranteed index-aligned with the `entries`
// vector that produced it - that alignment is what makes this a plain
// index-for-index scan rather than needing a path lookup.
QVector<int> collectCandidateIndices(const QVector<RawFileEntry> &entries, const QVector<ArchiveEntry> &classifiedEntries)
{
    QVector<int> indices;
    for (int i = 0; i < entries.size(); ++i) {
        if (entries[i].isNestedArchiveCandidate && classifiedEntries[i].classification != EntryClassification::Identical) {
            indices.append(i);
        }
    }
    return indices;
}

// Opens every collected candidate in `entries` and returns the next
// iteration's flat entry list for one archive (A or B - this function is
// side-agnostic, called once per side).
//
// Each collected index is replaced in place, preserving the surrounding
// entries' relative order:
//   - opened as a real zip: the ONE candidate entry is spliced out and
//     replaced by however many entries its own top level contains (zero,
//     one, or many) - this is the "expand" half of the algorithm.
//   - not actually a zip (content decided it, per Q3 - a "*.zip" name is
//     only ever a filter, never proof): the entry is kept as an ordinary
//     file, but isNestedArchiveCandidate is cleared. Clearing the flag is
//     what stops it from being collected again on the next iteration - a
//     candidate that fails to open must be retried zero more times, not
//     retried forever.
//
// Termination argument (also see compareArchives below): every entry this
// function touches either (a) loses its candidate flag permanently, which
// can happen at most once per entry, or (b) is replaced by content that
// sits one nesting level shallower than it did. A real archive's nesting
// depth is finite, so the number of "some entry still has a live candidate
// flag AND classifies as non-Identical" iterations is bounded by that
// depth - the outer while-loop in compareArchives cannot spin forever.
QVector<RawFileEntry> expandCandidates(const QString &archiveFilePath, const QVector<RawFileEntry> &entries,
                                        const QVector<int> &indicesToExpand)
{
    const QSet<int> toExpand(indicesToExpand.begin(), indicesToExpand.end());

    QVector<RawFileEntry> next;
    next.reserve(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        if (!toExpand.contains(i)) {
            next.append(entries[i]);
            continue;
        }

        const ArchiveReader::NestedExpansion expansion =
            ArchiveReader::expandNested(archiveFilePath, entries[i].relativePath);
        if (expansion.openedAsArchive) {
            // Splice: the one candidate entry is gone, replaced by its own
            // top level's entries (already path-prefixed by expandNested).
            for (const RawFileEntry &spliced : expansion.entries) {
                next.append(spliced);
            }
        } else {
            RawFileEntry keep = entries[i];
            keep.isNestedArchiveCandidate = false;
            next.append(keep);
        }
    }
    return next;
}

} // namespace

ArchiveComparison::Outcome ArchiveComparison::compareArchives(const QString &pathA, const QString &pathB)
{
    Outcome outcome;
    QElapsedTimer timer;
    timer.start();

    // Phase 1 (per side, independent): list just the top level. Each side's
    // ArchiveOpenError is caught on its own so an unreadable A doesn't stop
    // B from being reported, or vice versa (FR-008) - errors are data for
    // the page to display, never a dialog raised from in here (Q7).
    QVector<RawFileEntry> entriesA;
    QVector<RawFileEntry> entriesB;
    try {
        entriesA = ArchiveReader::readTopLevel(pathA);
    } catch (const ArchiveOpenError &error) {
        outcome.errorA = error.message();
    }
    try {
        entriesB = ArchiveReader::readTopLevel(pathB);
    } catch (const ArchiveOpenError &error) {
        outcome.errorB = error.message();
    }

    if (!outcome.errorA.isEmpty() || !outcome.errorB.isEmpty()) {
        // Both sides must be readable to produce a meaningful comparison -
        // result stays default-constructed (empty), matching what the page
        // already does with readA/readB today.
        outcome.compareMs = timer.elapsed();
        return outcome;
    }

    // Phase 2: compare, expand what needs expanding, repeat - level by
    // level, so a nested archive inside a nested archive gets exactly the
    // same treatment as a top-level one (nestedInsideNestedExpands test).
    //
    // Each pass through the loop:
    //   1. Classify the CURRENT flat lists with CrcComparator (index-aligned
    //      with entriesA/entriesB, per its documented contract).
    //   2. Collect which nested-archive candidates, on each side, turned out
    //      to differ or be one-sided - see collectCandidateIndices above for
    //      why an Identical candidate is never collected (Q4 point 3: never
    //      open an identical container).
    //   3. Nothing collected on either side -> the classification just
    //      computed is final; stop.
    //   4. Otherwise expand what was collected (see expandCandidates above)
    //      and go around again with the updated lists.
    //
    // See expandCandidates' doc comment for the termination argument in
    // full; in short, every loop iteration that doesn't exit at step 3 must
    // have expanded at least one entry into strictly-shallower content or
    // permanently cleared a candidate flag, and both of those are bounded
    // by the archive's (finite) nesting depth.
    ArchiveComparisonResult result;
    while (true) {
        result = CrcComparator::compare(entriesA, entriesB);

        const QVector<int> candidatesA = collectCandidateIndices(entriesA, result.entriesA);
        const QVector<int> candidatesB = collectCandidateIndices(entriesB, result.entriesB);
        if (candidatesA.isEmpty() && candidatesB.isEmpty()) {
            break;
        }

        if (!candidatesA.isEmpty()) {
            entriesA = expandCandidates(pathA, entriesA, candidatesA);
        }
        if (!candidatesB.isEmpty()) {
            entriesB = expandCandidates(pathB, entriesB, candidatesB);
        }
    }

    outcome.result = result;
    outcome.compareMs = timer.elapsed();
    return outcome;
}
