# Path-tolerant CRC matching for Archive Comparison (v1.0.1)

**Status**: Approved, implementing.
**Relates to**: `specs/001-pcb-supplier-workflow/spec.md` FR-003 (already states this requirement; v1.0 shipped without fully implementing it — this closes that gap). Not a new feature or new spec-kit cycle; `plan.md` and `spec.md` are unchanged.

## Problem

v1.0's `CrcComparator` matches files by exact `(relativePath, crc32)`. In practice, customers often restructure the new archive relative to the old one before sending it — wrapping it in an extra folder, moving a subset of files into a new subfolder, or merging it with unrelated project data (including as a nested zip-within-zip). Files that are byte-identical get misreported as red (`OnlyInThisArchive`) purely because their path moved, and restructuring isn't uniform — different files within the same archive can shift by different amounts (confirmed by example: two files moved into a new subfolder while three others stayed put).

## Algorithm

Output shape is unchanged: still `Identical` / `Different` / `OnlyInThisArchive` per entry, per side. Only the matching logic inside `CrcComparator::compare` changes.

**Pass 1 — exact match (today's behavior, unchanged):** match entries between archives by full `relativePath`. Classify `Identical` (CRC equal) or `Different` (CRC differs). This keeps unmoved files working exactly as before.

**Pass 2 — filename fallback**, applied only to entries neither side matched in Pass 1:

1. Within each archive's leftover set, group entries by filename (leaf name only, ignoring directory).
2. If several leftover entries in the *same* archive share both filename **and** CRC (true duplicates — the same file copied into multiple subfolders), treat them as one equivalence class with a single representative for matching purposes.
3. For each filename with exactly one candidate class on each side, match them: `Identical` if CRC equal, `Different` if not. Apply that same classification to every member of a duplicate class (so all copies of a true duplicate get the same color, not just the representative).
4. If a filename has multiple *distinct-content* candidates on either side (different CRCs, same name, different locations — genuinely ambiguous), leave all of them unmatched rather than guess which pairs with which.

**Anything still unmatched after Pass 2** → `OnlyInThisArchive`, same as today. This is what correctly flags genuinely new/missing files *and* unrelated merged-in noise (e.g. an unrelated nested archive pulled in alongside the real data) without any separate "detect the junk" logic — it just never finds a filename match.

## Worked examples (from design discussion)

- **Case 1** (no restructuring): Pass 1 matches everything. Unchanged from today.
- **Case 2** (partial move — 2 of 5 files moved into a new subfolder, 3 stayed put): Pass 1 matches the 3 unmoved files; Pass 2's filename fallback matches the 2 moved files (same name, same CRC → green).
- **Case 3** (whole subtree buried under a wrapper folder + nested archive, plus an unrelated nested archive mixed in as a sibling): Pass 1 finds nothing (paths differ entirely); Pass 2 matches the real files by filename; the unrelated nested archive's contents have no filename match anywhere in the other side's leftovers, so they fall through to `OnlyInThisArchive` — correctly surfaced as noise without special-casing.
- **True duplicates** (same name + same CRC, multiple subfolders in one archive): first duplicate matches via Pass 2; every other duplicate in its equivalence class inherits the same classification.
- **Ambiguous duplicates** (same name, different CRC, multiple locations): left unmatched — the tool doesn't guess a pairing.

## Scope of change

Only `src/archivecompare/crccomparator.cpp` (and its top-of-file comment in `crccomparator.h`, which currently states matching is path-only — needs updating to describe the two-pass approach). `ArchiveEntry`, `ArchiveComparisonResult`, `ArchiveReader`, and the tree/UI display layer are untouched.

## Testing

Unit tests in `tests/unit/test_archivecomparator.cpp` (extends the existing suite), covering:
- Existing cases (unmoved identical/different/only-in-one-archive) continue to pass unchanged.
- Case 2 shape: partial subfolder move, result all green.
- Case 3 shape: buried subtree + unrelated merged-in files, real files matched, unrelated files red.
- True-duplicate propagation: one classification applied to all copies.
- Ambiguous-duplicate: left unmatched (red on both sides) rather than paired incorrectly.

## Process note

Tracked as a `tasks.md` addition under User Story 1a (extends T021), not a new spec-kit feature cycle — FR-003 already specifies this behavior in `spec.md`; this is v1.0 catching up to its own stated requirement, not new scope. Labeled v1.0.1 to avoid colliding with the already-reserved "v1.1" name (Gerber diff overlay, Phase 3b).
