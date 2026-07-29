# Archive Compare — summary, filters, status bands, drag-drop

**Status**: **Implemented** on `main` (merge `6e84790`), Tasks 6–8, 11–18 of
`docs/superpowers/plans/2026-07-28-archive-compare-content-first-consolidated.md`.
**Relates to**: Q4, Q7, Q8, Q9, Q10 of
`docs/superpowers/plans/2026-07-27-archive-compare-open-questions.md`.
**Companion docs**: `2026-07-27-content-first-archive-matching-design.md` (which files pair)
and `2026-07-27-content-based-drawability-design.md` (which pairs can be drawn). This one
covers everything else the gap review changed — what the page *reports*, how rows are
*ordered*, and what the panels *accept*.

Written after the fact, because Q1 and Q2 each got a design doc during the review and these
five decisions did not — they existed only as worksheet verdicts plus task steps. The
reasoning is worth keeping in one place.

## The counting unit is the content class, not the tree row (Q8)

`ComparisonSummary::summarize()` counts distinct `(crc32, sizeBytes)` classes. After the
duplicate collapse in Q1's matching, one file copied three times inside an archive is **one
thing to look at**; reporting "3" would overstate the work.

Pairs are the deliberate exception and are counted as **rows**: each pair is a separate
comparison the user can open, so a name pass that produced a cross product of two really is
two things to inspect.

`onlyInA`/`onlyInB` count only classes whose rows are actually classified
`OnlyInThisArchive`. A *yellow* class is one-sided too — content-first found no key match,
which is exactly why it fell through to the name pass — but it is already reported as a pair,
so counting it here would report the same work twice and colour it wrongly.

`fullyIdentical()` deliberately says nothing about `identicalClasses`: two empty archives are
identical, and so are two archives whose every class matches.

**Implementation note.** `summarize()` reduces each class with "any member", not "the
representative row". The two are equivalent because `CrcComparator` forces every
*non-representative* member of a class to plain `Identical` unconditionally, so the
representative is the only member that can ever carry `OnlyInThisArchive`. This is easy to
get wrong when reading the code — `test_comparisonsummary`'s
`onlyRepresentativeCarriesTheClassification` pins it.

## Kinds: conclusive extensions trusted, ambiguous ones sniffed (Q8)

`resolveKinds()` splits entries into `GerberData` (layers *and* drill — Q2 makes both
previewable) and `OtherData`, using two short suffix tables. A conclusive extension is trusted
and the file is never read; anything else is sniffed through `GerberFileType::classify`.

`txt` is deliberately **not** conclusive. The real drill file in the sample data is a `.TXT`;
trusting its extension would file it under Other and the summary would claim there is nothing
previewable to look at. Cost stays proportional to the genuinely ambiguous files, and accuracy
is highest exactly where extensions lie.

This table types the summary and nothing else. It is **not** a revival of the eligibility list
Q12 deleted, and there is no user-editable version — what a file can be *parsed* as is still
decided purely by content.

## Status bands order the tree by work demanded (Q9)

Rows sort on `(sortBand, !isDirectory, name case-insensitive)`, where the band is
`Different`→0 (yellow), `OnlyInThisArchive`→1 (red), `Identical`→2 (green). What the user must
act on floats to the top.

A **directory inherits the highest-priority band of any descendant** and carries a 4 px bar at
its leading edge in that colour. This reverses the earlier "folders deliberately don't get a
rollup colour" position: a collapsed folder hiding a changed file otherwise looks like there
is nothing to do inside it. The folder treatment is deliberately lighter than a file row — an
edge bar, no background fill, no text recolour — so one changed folder does not turn the pane
into a wall of colour. An all-green folder stays undecorated.

**Size is gone from both the sort and the display.** It remains only as half of the
`(crc32, size)` content-class key. Sorting on it ordered rows by something the user could not
see. Duplicates get **no marker at all** — plain green.

## Filters rebuild the tree; the summary never moves (Q8)

`TreeFilter { changedOnly, optional<DataKind> kind }`, AND-combined, **both off by default** —
hiding rows before the user asks would make the tree disagree with the summary above it.

Filtering **rebuilds** the tree from the stored entry list rather than wrapping it in a
`QSortFilterProxyModel`. That is what makes folder auto-hide free: folders are *inferred* from
surviving paths, so a folder whose children all disappear simply never gets created. There is
no hide-empty-parent rule that could drift out of sync.

The filter survives `buildFrom()`: a fresh comparison must not silently re-show rows the user
chose to hide, since the control bar keeps its state too.

**The summary is computed before filtering and never changes with it.** It describes the
comparison, not the current view, so it stays a fixed reference to filter against.

One bar spans both panels and applies to both at once — a bar inside one panel would imply it
only affected that side. This is what forced the split of `ArchivePanel` into `ArchiveHeader`
(filename, Change… button, inline error) above the bar and `ArchivePanel` (the tree) below it,
in a `QGridLayout`.

A panel emptied by filtering shows what was filtered out ("No changed Gerber files to show")
via a `QStackedLayout`, rather than an unexplained blank pane.

## Errors are data (Q7)

Compute returns results and errors as data; **only the page shows anything**. Each side
reports its own failure inline in its header, so the user can read A's error and B's result at
the same time instead of dismissing a modal first. `runComparison()` previously violated this
with a mid-flow `QMessageBox`. A grep gate for `QMessageBox` in `src/archivecompare/` enforces
it.

Threading was considered and **deferred** — a render has never been observed above ~100 ms.

## Two-phase read (Q4)

A nested archive whose content class already exists in the other archive is **never opened**.
Expansion is a second phase over the top-level read, so identical nested archives cost one
green row rather than a full recursive descent.

`rar`/`7z` are out of scope for now (7z would be easy to add later: the LZMA SDK is public
domain and 7z stores a per-file CRC32). ODB++/tgz/brd are compared as single files.

## Drag-drop accepts exactly one local `.zip` file (Q10)

`singleDroppedZipPath()` in `archivedroputil.h` — exactly one URL, local, name ending `.zip`
case-insensitively, and `QFileInfo::isFile()`, which is what rejects a **directory named
`something.zip`**. Refusing at `dragEnter` is what makes the OS show the no-drop cursor,
rather than accepting the drag and complaining afterwards. `dropEvent` re-validates because
the two events carry separate mime data.

Two real bugs motivated this: a folder named `*.zip` used to be accepted, and a multi-file
drop was silently reduced to its first URL. Two-archives-in-one-drop is a deliberate
exclusion.

Filtering on the *name* here does not contradict Q3's "content decides" rule — the name is
only a cheap candidate filter for the cursor; a `.zip`-named non-zip is still caught at read
time and reported inline. Both the header and the tree accept drops and share this one
definition, which is why it is a header rather than a member function.

## Deliberately excluded

Mixed-revision archives; container grouping; layer-role (`%TF`/`%LN`) matching; synced
scrolling between the two trees; semantic/normalized Gerber comparison (Q15 — byte-exact
`(crc32,size)` measured zero false yellows across the real archives, and byte-exactness can
never produce a false *green*, only a false *yellow*, which the overlay resolves); any warning
when the same archive is loaded on both sides (the two filenames in the headers already show
it); persisting filter state across sessions.
