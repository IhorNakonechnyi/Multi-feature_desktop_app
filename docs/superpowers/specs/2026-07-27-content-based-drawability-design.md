# Content-based drawability

**Status**: **Implemented** on branch `archive-compare-content-first` (Tasks 9–10 of
`docs/superpowers/plans/2026-07-28-archive-compare-content-first-consolidated.md`),
verified 2026-07-29. Not merged to `main`.
Q12 is fully resolved, not just "substantially": the entire user-extension
mechanism was deleted rather than demoted, and the sniff window widened to 8 KB.
**Relates to**: Q2 of `docs/superpowers/plans/2026-07-27-archive-compare-open-questions.md`.
Substantially resolves Q12 (user-added extension list) as a side effect.
Independent of `2026-07-27-content-first-archive-matching-design.md` — that one
decides which files pair, this one decides which pairs can be drawn.

## Problem

The app answers "which parser does this file need?" by looking at content, but
answers "can I draw this file at all?" by looking at the extension. The second
question is asked first, so files the renderer would have handled correctly never
reach it.

- `ArchiveComparePage::parseGerberOrDrill` calls
  `GerberFileType::isDrillFile(fileName, content)`, which sniffs for an Excellon
  `M48` header when the extension isn't `drl`/`xln` (commit `09e0586`). Rendering
  is already content-aware and works.
- `GerberDiffTrigger::eligiblePairs` calls `GerberFileType::isRecognized(path)`,
  which only checks the extension against a fixed list plus a user-added list. A
  pair whose extension isn't listed is diverted to "unrecognized extension" with
  an `Add .<ext>` button, and never reaches the canvas.

Real files in the sample archives that are excluded by this and would render
correctly:

| file | actual content | why excluded today |
| --- | --- | --- |
| a supplier drill file with a `.TXT` extension | Excellon (`M48`, `INCH,LZ`) | `txt` not in the extension list |
| a drill file with a `.drd` extension | Excellon (`M48`) | `drd` deliberately not in the list |
| a milling layer, and two component layers | Gerber (`%FSLAX35Y35`, `%TF.FileFunction`) | their extensions aren't in the list |

The `.TXT` case is the damaging one. That file is genuinely *changed* between two
revisions, so it is exactly a file the user wants to open — and the only offered
remedy, `Add .txt`, permanently makes every other `.txt` in the archive eligible
too. In the same folder sit a pick-and-place list and a tool list. One click
turns both into fake Gerber layers, for every future comparison.

## Decision

**Drawability is decided by content, with the extension list demoted to a
fallback for inconclusive files.**

### Classification

A single function replaces the two overlapping ones. Given a file's **first 8 KB**
(or the whole file, if smaller):

1. A bare `M48` token line → **Excellon**. (Existing sniff, unchanged.)
2. Otherwise a line containing `%FS` or `%MO` → **Gerber**.
3. Otherwise **not drawable**.

Excellon is tested first because Excellon files may legitimately contain a bare
`%` line, while no Gerber file carries a bare `M48` (Gerber's M-codes are
`M00`/`M01`/`M02`).

**The filename plays no part.** There is no extension fallback — see "Q12" below
for why one would be actively misleading rather than merely redundant.

**Why 8 KB and not the ~40 lines originally specified** (revised by Q12,
2026-07-28): with no extension fallback behind it, the sniff window is the *only*
thing standing between a drawable file and rejection, so it must not be
defeatable by a long `G04` comment banner pushing `%FS` past the cut-off. Real
sample files run 19–92 KB, so 8 KB comfortably covers any plausible header while
still being far less than a full decompression.

### Where the bytes come from

Classification needs file content, which needs decompression. Scope it tightly:
**only files that appear in a differing pair**, and **only their first ~40
lines**. A typical comparison has a handful of changed pairs, so this is a few
small reads. The archive-wide comparison itself stays central-directory-only.

### Eligibility

A pair is previewable when **both sides classify as drawable**. Nothing else is
checked.

## Removals

**The mixed drill/Gerber special case goes.** `isMixedDrillPair()`,
`drillPairs()`, and the "N mixed drill/Gerber pair(s) skipped" label are deleted.

Two reasons. First, the code is currently **unreachable**: every pair is formed
from identical filenames (or identical full paths), so both sides always share an
extension, so an extension-based drill test always returns the same answer for
both — the mixed condition can never be true.

Second, and the reason not to reinstate it once Q1's extension pass makes mixed
pairs possible: a drill file is a Gerber-family file that needs a different
parser, and the feature should treat it the same as any other. The app does not
police layer semantics anywhere else — it will happily overlay a top copper
against a silkscreen — so singling out drill is an inconsistency, not a
safeguard. If a pair is meaningless the user sees that immediately on the canvas,
which is the same feedback they get for any other mismatched pair.

The specialness of drill survives in exactly one place: choosing `ExcellonParser`
over `GerberParser` for that file.

## The extension mechanism is removed entirely (Q12, decided 2026-07-28)

`GerberFileType::isRecognized()`, `addUserExtension()`, the QSettings key
`gerber/userRecognizedExtensions`, the `Add .<ext>` buttons, and
`distinctUnrecognizedExtensions()` are all **deleted**.

The reason is stronger than "rarely needed". **The button can no longer do
anything useful.** Every Gerber file must declare its coordinate format (`%FS`)
near the top — that is what tells the parser how to read every number in the
file. If the content sniff finds no `%FS`/`%MO` and no `M48`, the parser has no
scale to apply and cannot render the file whatever the extension says. Adding the
extension would only force an unparseable file through and produce a blank
canvas: a button that misleads rather than helps.

The "unrecognized" bucket itself survives — a pair with a non-drawable side is
still listed with an explanation, just with no button beside it.

Previously saved user extensions become orphaned INI entries. Harmless, and not
worth a migration for a ~50-user internal tool.

## Scope of change

- `src/gerber/gerberfiletype.{h,cpp}` — new content-based classification;
  `isRecognized()`, `addUserExtension()`, the built-in extension list and the
  QSettings key all deleted.
- `src/archivecompare/gerberdifftrigger.{h,cpp}` — eligibility asks for
  classification of both sides' bytes; mixed-drill helpers deleted.
- `src/archivecompare/archivecomparepage.cpp` — reads header bytes for pairs
  before bucketing them; the skipped-drill label, the `Add .<ext>` buttons and
  `distinctUnrecognizedExtensions()` removed.
- `src/archivecompare/archivereader.{h,cpp}` — needs a "read first N bytes of an
  entry" helper so classification does not decompress whole files (also landed by
  Q3's plan).
- `src/gerber/gerberviewerpage.cpp` — check for `isDrillFile`/`isRecognized`
  callers before deleting; the viewer's own open path must move to the same
  classifier.

## Testing

Unit tests in `tests/unit/test_gerberfiletype.cpp` and
`tests/unit/test_gerberdifftrigger.cpp`.

- Excellon detected from content when the extension is `.txt`
- Excellon detected from content when the extension is `.drd`
- Gerber detected from content when the extension is unlisted
- a plain text report (no `M48`, no `%FS`/`%MO`) classifies as not drawable
- a file with a `.gtl` extension but no `%FS`/`%MO`/`M48` classifies as **not
  drawable** — the extension no longer rescues it
- `%FS` preceded by a long `G04` comment banner is still found (the 8 KB window)
- `%FS` beyond 8 KB is not found — documented limit, asserted so a future change
  to the window size is a deliberate one
- Excellon wins over Gerber for a file containing both a bare `%` line and `M48`
- a pair is eligible when both sides classify drawable, regardless of extension
- a pair with one non-drawable side is bucketed as unrecognized, not silently
  dropped
