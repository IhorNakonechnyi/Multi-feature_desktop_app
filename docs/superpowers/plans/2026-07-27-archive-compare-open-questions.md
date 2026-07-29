# Archive Compare — gap review decisions (Q1–Q15)

**Started:** 2026-07-27 · **Completed:** 2026-07-28 (two sessions)
**Status:** **ALL QUESTIONS DECIDED AND IMPLEMENTED** — Q1–Q15, on branch
`archive-compare-content-first` (19 tasks of
`docs/superpowers/plans/2026-07-28-archive-compare-content-first-consolidated.md`),
finished and verified 2026-07-29. **Not merged to `main`, not pushed** — integration
is a separate explicit call.
Q5/Q6 were dissolved by Q1; Q11/Q13/Q14/Q15 produced no code by decision.
**Purpose:** every known gap in the Archive Compare feature, worked through one
question at a time, each decided as fix now / fix later / accept and document.

This is the decision record for that review — kept because the *reasoning* behind
each answer, and the measurements taken against the real supplier archives, are
worth more than the one-line verdicts. The implementation plan that follows should
cite these rather than re-litigate them.

**How the review was run** (recorded in case another one is ever needed): one
question at a time, in order; a question closed only when Igor said so; before
opening the next question, re-check whether decisions already made changed it, and
revise it first if so. Several questions were substantially rewritten this way,
and two (Q5, Q6) were dissolved entirely by a later decision. All questions were
decided before any implementation — Igor was explicit about that, and it paid off:
Q1 changed the shape of six later questions.

## Resuming

The question phase is **finished**. Read the "Decisions so far" table below, then
consolidate everything into a single implementation plan.

Which questions actually produce code (the rest are "accept as-is" or are closed
by another question): **Q1, Q2, Q3, Q4, Q7's constraint, Q8, Q9, Q10, Q12.**
Q11, Q13, Q14 and Q15 are decided *not* to change anything, and Q5/Q6 are closed
by Q1.

## Documents

- **This worksheet** — every question, findings, and decisions.
- `docs/superpowers/specs/2026-07-27-content-first-archive-matching-design.md` —
  Q1's full design (amended by Q4's two-phase read, and by Q9 removing the
  duplicate badge).
- `docs/superpowers/specs/2026-07-27-content-based-drawability-design.md` — Q2
  (amended by Q12: extension fallback removed, sniff window widened to 8 KB).
- `docs/superpowers/plans/2026-07-27-archivereader-content-based-nesting.md` —
  Q3's implementation plan (drafted, **not executed**; also lands the shared path
  walk and the `readEntryHead()` helper Q2 needs). Moved into the repo on
  2026-07-28 from a user-global `.claude/plans/` scratch location where it would
  have been lost.

---

## What the feature does today (baseline, so this doc stands alone)

Source: `src/archivecompare/` (+ `src/gerber/gerberfiletype.*`, `src/common/filepicker.*`).

- **Input:** two `.zip` files. Panel A = "New", panel B = "Old". Picked via
  `Change...` (dialog, `*.zip` filter, opens in Downloads) or drag-drop.
  Comparison auto-runs once both sides are set, and on every re-pick.
- **Read** (`archivereader.cpp`): QuaZip, **central directory only** — CRC32 +
  uncompressed size, no decompression. Nested `.zip` entries *are* decompressed
  into memory and recursed into at any depth, so a file inside a nested zip
  appears at its true leaf path (`wrapper/inner.zip/top.gtl`). `\` normalized to `/`.
- **Classify** (`crccomparator.cpp`) → Identical / Different / OnlyInThisArchive:
  1. self-duplicate suppression (2nd+ occurrence of a filename in the same
     archive forced green, never matched),
  2. Pass 1 exact relative-path match on CRC,
  3. Pass 2 filename-only fallback for leftovers, only when each side has
     exactly one distinct CRC under that filename.
- **Display:** two independent trees, folders inferred from paths, dirs first
  then files by ascending size, rows tinted green/yellow/red by the delegate.
- **Gerber preview:** `Different` pairs bucketed by `GerberDiffTrigger` into
  eligible / unrecognized-extension / mixed-drill. Selecting an eligible pair
  reads both files' bytes back out of the archives, parses RS-274X or Excellon,
  and renders an additive overlay — green = only new, red = only old, yellow =
  same — with wheel-zoom / drag-pan and a render-time readout.
  `Add .xxx` button teaches new extensions, persisted in QSettings.

Already handled well (not up for discussion here): corrupt / password-protected
/ non-zip archives reported per side; unreadable nested zip skipped without
failing the read; `\`-separated names from PowerShell `Compress-Archive`; stale
canvas/label cleanup on re-pick; per-side render degradation with specific
status text; `M48` content sniff for drill files (commit 09e0586); mixed
drill-vs-copper pairs excluded with an explanation.

---

## Decisions so far

| Q | Topic | Decision | Follow-up |
|---|-------|----------|-----------|
| 1 | Self-duplicate rule | **DONE** — content-first matching; see design doc | implement |
| 2 | Drill detection timing | **DONE** — content decides drawability; see design doc | implement |
| 3 | `*.zip` directory | **DONE** — content decides what is an archive; name is a candidate filter only | implement |
| 4 | Nested formats / RAR | **DONE** — no rar/7z now; identical nested archive never opened (two-phase read) | implement; amends Q1 spec |
| 5 | CRC vs size | **closed by Q1** — size joins CRC as the class key | |
| 6 | Representative pair | **closed by Q1** — cross-product pairs in the name pass | |
| 7 | Synchronous UI | **DONE** — defer threading; adopt compute/UI separation rule | constraint on Q1-Q4 work |
| 8 | Summary / filter | **DONE** — type-split summary, identical notification, changed-only + type filters in one wide bar | splits ArchivePanel |
| 9 | Size display | **DONE** — size never shown; sort by status bands yellow→red→green; duplicates plain green | implement; amends Q1 spec |
| 10 | Drag-drop validation | **DONE** — dragEnter accepts exactly one `.zip` file; failures inline not modal; no two-at-once | implement |
| 11 | Same file both sides | **DONE** — no warning; filenames in the headers already show it | none |
| 12 | Extension list mgmt | **DONE** — delete the whole extension mechanism; widen content sniff to 8 KB | implement; amends Q2 spec |
| 13 | Non-ASCII names | **DONE** — accept and document; cosmetic-only risk, no code, no fixture | none |
| 14 | Pair list height | **DONE** — keep the fixed 90 px; real archives yield 1–2 pairs | none |
| 15 | Byte-exact vs semantic Gerber compare | **DONE** — byte-exact stays; normalization excluded; revisit if a whole archive goes yellow | none |

**Known couplings** (re-check the target question when the source is decided):
Q1 → Q5, Q6 · Q2 → Q12, Q14 · Q3 → Q4 · Q6 → Q14 · Q8 → Q9

---

# Part A — Correctness risks

## Q1 — Self-duplicate rule forces green on same-filename files in different folders

**Status:** DONE
**Code:** `crccomparator.cpp:54` `selfDuplicateIndices()`

Keys on **filename alone, archive-wide, first-seen-wins**. Any 2nd+ occurrence
of a filename anywhere in the archive is set to `Identical` and skipped
entirely — never goes through Pass 1 or Pass 2.

*Why it exists:* an archive that re-embeds a nested copy of itself, where the
copy is a stale older revision. Keying on `(filename, crc)` would not catch
that, because the stale copy's content genuinely differs. Test
`sameNameDifferentCrcStillTreatedAsSelfDuplicate` locks this in deliberately.

*The cost:* indistinguishable from two legitimately different files sharing a
name in different folders. `docs/readme.txt` vs `gerber/readme.txt` — the second
shows green regardless, even if missing from the other archive or edited.

**Questions:**
1. Do real archives legitimately repeat a filename across folders (per-layer
   `notes.txt`, a `readme` per subfolder, `drill.drl` in root and in `nc/`)?
2. When you have seen "archive contains a stale copy of itself" — was the copy
   always inside a **nested zip**, or also loose in a plain subfolder?

Q2 matters because if it is always nested-zip, the rule can narrow to "suppress
only duplicates whose path crosses a `.zip/` boundary" — killing the false-green
for plain subfolders while keeping the case the rule was written for.

### Findings from exploring the real archives (2026-07-27)

Checked the three real supplier pairs in `Test_data/` (gitignored; described
structurally here, no filenames or supplier data reproduced) and the git history
of the rule. Facts, not guesses:

1. **The rule is not in the path-tolerant design doc.** It arrived afterwards in
   two commits — `30ec7e0` (suppress same-name/same-CRC repeats) then `6502548`
   (re-key on filename alone), both driven by one real supplier pair.
2. **No real archive contains a nested zip at all.** The "self-nested copy" the
   commit messages describe is in fact a **plain subfolder**. So the narrowing
   floated above — suppress only across a `.zip/` boundary — would break the
   exact case the rule exists for. That option is dead.
3. **One `Test_data` archive is synthetic — do not reason from it.** Igor built
   it by hand (renamed and modified files, embedded a copy of the other
   archive) to produce a fixture with two yellow pairs and a self-duplicated
   set. Its layer *renumbering* is therefore **not** real customer behaviour.
   Across all genuinely-supplied archives, **filenames are stable between
   revisions**. This retires the `%TF`/`%LN` layer-role matching idea: no real
   evidence supports it.
4. **Filenames really do repeat across folders** (in that synthetic archive):
   a complete second copy of a ~10-file layer set, once at the root and once in
   a subfolder.
5. **Ground truth for that (synthetic) pair, established by hashing every file:**
   the subfolder copy is **byte-for-byte identical to the old archive**, all
   files, same numbering. The root copy is a **renumbered re-export** of the
   same board in which exactly **two** layers genuinely changed content. So the
   honest answer for this pair is "2 files changed".
6. **What the current pipeline reports for it** (traced by hand through
   `compare()` over those hashes — not yet executed): first-seen-wins makes the
   root copy canonical and suppresses the subfolder copy as green. The root copy
   is then filename-matched number-for-number against the old archive, and
   because the re-export renumbered the layers, almost every number lands on a
   different layer → **~8 files reported changed, 2 identical**. Before
   `6502548` it was worse (ambiguous → everything red, 0 previewable pairs).

**Reframing:** the duplicate rule is entangled with this, but it is not the root
cause. The root cause is that **matching is keyed on filename, and this supplier
renumbers layers between revisions** — so the strong key (CRC/content) is being
used only to *compare* files that the weak key (filename) already paired up.
Q1 should therefore decide the matching strategy, not just the dedup predicate.

**Decision:** matching becomes **content-first**. Full design in
`docs/superpowers/specs/2026-07-27-content-first-archive-matching-design.md`.
Summary: group by `(crc32, size)` into classes → collapse duplicates within an
archive (representative = shallowest path; other copies plain green — originally
a `duplicate` badge, removed by Q9) → content match → name pass over leftovers
with cross-product pairs →
extension pass, one class per side only → red. `selfDuplicateIndices()` deleted.
Colours redefined: green = nothing to do, yellow = app *assumes* these
correspond and they differ, red = no counterpart, check it yourself. A/B are
symmetric slots, so the overlay legend becomes `only in A` / `only in B`.
Deliberately excluded: mixed-revision archives, container grouping, layer-role
(`%TF`/`%LN`) matching. **Also closes Q5 and Q6.**

---

## Q2 — Drill eligibility is extension-only; content sniff runs too late

**Status:** DONE
**Code:** `gerberdifftrigger.cpp:7-20`, `gerberfiletype.cpp:82`, `archivecomparepage.cpp:317`

`GerberDiffTrigger` uses `isDrillFile(path)` (extension only: `.drl`/`.xln`),
while the `M48` content sniff only runs later in `parseGerberOrDrill`.
So a drill file named `.txt` paired against a real `.drl` is **not** bucketed as
a mixed drill pair. It lands in "unrecognized", and if the user clicks
`Add .txt` it becomes eligible and silently renders drill holes over copper
traces as though the comparison were valid.

**Question:** should the trigger read bytes to classify drill-ness (costs one
decompress per differing pair), or is a cheaper guard enough — e.g. block at
parse time when exactly one side parses as Excellon, and show a "mixed
drill/Gerber" message there instead?

### Findings (2026-07-27)

- The real problem is **not** drill detection or rendering — rendering is already
  content-aware and works (commit `09e0586`). The gate is *eligibility*, which is
  extension-only, and it is asked first.
- Real files wrongly excluded today, all in `Test_data`: a supplier drill file
  with a `.TXT` extension (and it is one of the *changed* files in that pair), a
  `.drd` drill file, and a milling layer plus two component layers that are
  genuine Gerber. All would render correctly if allowed through.
- The `Add .txt` remedy is actively harmful: the same folder holds a
  pick-and-place list and a tool list, which that one click would also make
  eligible, permanently and for every future comparison.
- **The mixed drill/Gerber code is unreachable.** Every pair is formed from
  identical filenames, so both sides always share an extension, so an
  extension-based drill test always agrees for both. That skipped-pairs message
  has never been shown and cannot be.

**Decision:** **drawability is decided by content**, and only of files appearing
in a differing pair, so the archive-wide comparison stays central-directory-only.
The mixed drill/Gerber special case is deleted — a drill file is a Gerber-family
file that needs a different parser and is otherwise treated identically; the app
polices layer semantics nowhere else. Full design in
`docs/superpowers/specs/2026-07-27-content-based-drawability-design.md`.

*As decided here* the extension list survived as a fallback for inconclusive
files, with a ~40-line sniff window. **Q12 then went further**: the extension
mechanism is deleted outright (a file with no `%FS`/`%MO`/`M48` cannot be parsed
whatever its extension says) and the sniff window widens to 8 KB to compensate.
The spec is amended accordingly — read it, not this paragraph, as authoritative.

---

## Q3 — A directory literally named `*.zip` silently breaks Gerber preview

**Status:** DONE
**Code:** `archivereader.cpp:127` `readEntryBytes()`

Splits the flattened path at the next `".zip/"` boundary and tries to open that
prefix as a nested archive entry. For a plain **folder** named `panel.zip/`, the
stored entry is `panel.zip/top.gtl` — no entry named `panel.zip` exists, lookup
fails, empty bytes returned. UI then shows "Neither side has drawable content"
with no hint why. Tree/CRC comparison itself is unaffected.

**Question:** is a folder named `*.zip` realistic here? If not, worth fixing at
all, or is it enough to make the failure loud (a specific "could not read entry
from archive" status instead of the misleading "no drawable content")?

**Impact of Q1/Q2:** Q2 adds a *second* caller of this same path-splitting logic
— a "read the first N bytes of an entry" helper used to classify drawability. So
the bug now bites twice, and the visible symptom changes: such a file classifies
as not-drawable and is bucketed as "unrecognized extension" rather than reaching
the canvas and reporting "no drawable content". Both messages are wrong for the
same underlying reason. Fixing the splitter once fixes both, and Q2 is touching
this file anyway.

### Findings (2026-07-27)

A second, worse bug was found while tracing this one. `readEntriesFrom()` treats
any entry whose name ends in `.zip` as a nested archive, decompresses it, and
tries to open it — and on failure `continue`s, so **the entry is never added to
the file list at all**. A corrupt nested archive, or any file that merely happens
to be named `.zip`, is invisible: not in the tree, not compared, not counted.

Both bugs have the same root cause: the reader asks the *filename* whether
something is an archive, in two places that must agree and don't.

**Decision:** **content decides whether an entry is really an archive; the name is
only a cheap candidate filter.** Two changes: (a) resolve paths against the
entries that actually exist — longest existing prefix, exact match winning over
prefix descent — instead of splitting the string on `".zip/"`; (b) when a
`.zip`-named entry does not open as an archive, emit it as an ordinary file entry
rather than dropping it. Also distinguish "entry unreadable" from "entry is
empty" so the UI stops reporting a read failure as "no drawable content".

Implementation plan drafted at
`docs/superpowers/plans/2026-07-27-archivereader-content-based-nesting.md`
(moved into the repo 2026-07-28 from a user-global scratch path); it also lands
the shared path-resolution walk and the `readEntryHead()` helper **Q2** needs, so
Q2 does not duplicate the same logic in the same file. Not executed — all
questions get decided before any implementation.

---

## Q4 — Nested-archive recursion only detects `.zip`; no RAR/7z

**Status:** DONE
**Code:** `archivereader.cpp:14` `isNestedArchiveName()`

Matches only names ending in `.zip`. A nested archive with any other extension
(or none) is one opaque file compared by CRC — you learn "it differs", not what
changed inside. RAR/7z were dropped in Phase 1 for licensing reasons (no bundled
RAR code).

**Question:** do suppliers/customers actually send `.rar`/`.7z`, or nested
archives with odd extensions? If yes — desired behaviour: a clear "nested
archive not supported, compared as a whole file" marker in the tree, or real
support (which reopens the licensing question)?

**Impact of Q1/Q2 — half of this is already answered.** During Q1 Igor decided
that ODB++, `.tgz` and `.brd` are compared **as single files** by content, like a
pdf or doc, with visual comparison deferred to a possible future milestone that
would export Gerber from them first. ODB/tgz are as a rule sent *alongside*
already-exported Gerber in a separate folder, and `.brd` arrives in roughly 1 in
100 cases. Under Q1 such a blob is one content class — green when both archives
carry it identically, red otherwise — and under Q2 it classifies as not drawable,
so it lands in the unrecognized bucket rather than the canvas. **What remains of
Q4 is narrower:** do `.rar`/`.7z` arrive as *containers* holding the real data,
where not recursing means losing per-file comparison entirely?

**Decision:**

1. **`.rar` and `.7z` stay unsupported for now.** RAR's licensing is already
   understood to be the blocker. 7z is deferred by choice, not by constraint —
   and adding it later is cheap: the **LZMA SDK is public domain** and its C
   sources include a full 7z *reader*, so no copyleft obligation at all (unlike
   QuaZip, which is LGPL and had to be a separate DLL). 7z also **stores CRC32
   per file**, so Q1's `(crc32, size)` class key would work unchanged. The one
   real difference is solid compression: files share compressed blocks, so
   reading one file can require decompressing its whole block — cheap for whole
   files, expensive for Q2's header reads unless the block is cached. No
   preparatory work is needed now; just never let `".zip"` become load-bearing in
   the flattened-path convention.
2. **ODB++ / `.tgz` / `.brd` are compared as single files** by content (decided
   during Q1).
3. **A nested archive whose own content class is present in the other archive is
   never opened** — shown as a single green row. Recurse only where the container
   differs, or exists on one side only.
4. **The candidate filter stays `.zip`-only.** Nested archives always arrive with
   a `.zip` extension, so the blind spot (a real archive under an unrelated name)
   is not worth paying for.

Point 3 requires a **two-phase read**, which amends Q1's spec: `ArchiveReader` no
longer produces a finished flat list in one call. Phase 1 lists the top level with
nested archives as opaque entries; matching then decides which containers differ;
phase 2 recurses only into those and splices their contents in. Applied
recursively, level by level.

---

## Q5 — Only CRC32 is compared; size is ignored in matching

**Status:** CLOSED by Q1 — `(crc32, size)` is the content-class key, so size is
now part of file identity rather than a redundant check. No separate decision
needed.
**Code:** `crccomparator.cpp`

Compares `crc32` only. `sizeBytes` is carried for display/sorting but never used
as a match criterion, so same-CRC-different-size classifies as Identical. CRC32
collisions are practically negligible here, but the extra check is free.

**Question:** add size as a second equality condition (cheap, strictly safer),
or leave CRC-only and treat this as a non-issue?

**Decision:** none needed — dissolved by Q1 (see Status above). Size became half
of file identity, which is strictly stronger than the "second equality condition"
this question asked for.

---

## Q6 — Filename-fallback emits one representative pair per group

**Status:** CLOSED by Q1 — the name pass emits the full A-class × B-class cross
product, so every distinct-content variant is reachable from the pair list.
Duplicate *copies* of the same content collapse to one representative instead,
which is the noise reduction this question was worried about.
**Code:** `crccomparator.cpp:174-182`

When a Pass-2 filename group is classified Different, only **one**
representative A/B pair is appended to `differingPairs` (deliberate, per the
path-tolerant-matching design doc). If a changed file exists as multiple copies
on either side, only one is reachable from the Gerber pair list — the others are
yellow in the tree with no way to preview them.

**Question:** acceptable, or should every copy be listed (risking an N×M pair
list)? Related to Q1 — a tightened duplicate rule changes how often this fires.

**Decision:** none needed — dissolved by Q1 (see Status above). The cross product
answers "every variant reachable"; duplicate collapse answers the N×M worry.

---

# Part B — Behavioural / UX limits

## Q7 — Everything runs synchronously on the GUI thread

**Status:** DONE
**Code:** `archivecomparepage.cpp:198` `runComparison()`, `:383` `renderShownPair()`

Archive read, nested-zip decompression into memory, parse and raster render all
on the UI thread. A large archive or big nested zip freezes the window with no
progress indicator and no cancel. The render-time label exists specifically as
the measurement gate before any threading work.

**Question:** what are realistic worst-case archive sizes, and how long does the
app actually take on them today? Threading now, or just a wait cursor / status text?

**Impact of Q1/Q2:** Q1 keeps the archive-wide comparison central-directory-only
(no decompression), so the *comparison* cost is unchanged. Q2 adds a new cost the
original question didn't account for: decompressing the first ~40 lines of every
file in a differing pair, on the GUI thread, before the pair list can be drawn.
Small per file, but it is now work that happens between "user picks archives" and
"user sees results", where previously nothing was decompressed at that point.
(Q12 later widened that read to 8 KB per file — still far below anything the
thresholds below would notice, so this decision is unaffected.)

### Findings (2026-07-27)

- Real sample archives are **19–86 KB**, largest single layer 92 KB. Igor reports
  **never seeing a render time above ~100 ms**, though on simple PCBs.
- Q4 removed the largest cost the old code could incur (always recursing into
  nested archives); Q1 keeps matching central-directory-only. The comparison side
  is in good shape. The render side is untouched and is the slower half.
- All heavy work already lives in **free functions that never touch widgets**:
  `readArchive`, `CrcComparator::compare`, both parsers, `LayerMaskRenderer`,
  `MaskCompositor`. The rasteriser returns **`QImage`, not `QPixmap`**, so
  painting off the main thread is legal. Threading later is mechanical.
- The part that does *not* retrofit cleanly is **cancellation** — it needs stop
  checks threaded through parser loops. Meaningless below multi-second runtimes.

**Decision:** **defer threading.** Keep the render-time label as the trigger;
revisit past ~500 ms on a real heavy board. Thresholds: <200 ms ignore, 200–500 ms
consider caching, ~1 s wait cursor + status line, >3 s threading (and only then
does cancel matter).

**Constraint adopted for the Q1–Q4 implementations:** *compute returns results and
errors as data; only the page shows dialogs.* This preserves the property that
makes deferral safe. Note that `runComparison()` currently violates it —
`archivecomparepage.cpp:224` and `:233` raise `QMessageBox::warning` inside the
read-and-compare flow. New logic added by Q2 and Q4 must stay on the data side of
that line.

Also agreed: time the **comparison** as well as the render, so a future slowdown
can be attributed to the right half.

---

## Q8 — No comparison summary, no filter-to-changed, no synced scrolling

**Status:** DONE

Two independent trees read by eye. No "12 same / 3 changed / 1 only in new"
summary, no explicit "archives are identical" confirmation, no way to hide green
rows, trees scroll independently.

**Question:** which of these is worth adding, in what priority? (summary counts /
filter toggle / synced scroll / "no differences found" message)

**Impact of Q1 — this question changed shape.** Q1 redefined the colours, so a
summary now has to say something different: green means "nothing to do" and
*includes redundant duplicate copies*, so counting rows would inflate it. Any
summary must count **distinct content classes**, not files. Q1 also makes the
"archives are identical" state precise for the first time — zero yellow and zero
red — and gives yellow a much stronger meaning ("the app assumed these
correspond"), which strengthens the case for a filter that shows only yellow and
red. Also: A/B are symmetric slots, so any summary wording must avoid
"new"/"old".

**Decision:**

**Summary line**, full width under the page title, **split by data type**:

```
Gerber: 9 identical · 2 pairs      Other: 5 identical · A: 1 to check
```

- Counts **content classes, never rows** — green covers both "same content in the
  other archive" and "redundant copy in this archive", and under Q4 an unopened
  nested archive is one row standing for everything inside it.
- `identical` and `pairs` are **global** (a shared class is one fact; yellow only
  exists in cross-archive pairs). `to check` is **per panel** — red is inherently
  one-sided.
- Two buckets: **Gerber data** (layers + drill, i.e. everything previewable) and
  **Other**. Drill folds into Gerber per Q2's rule. Splitting drill out later is
  trivial.
- **The summary never reflects the filters** — it always reports the whole
  comparison, or you could hide rows and watch counts shrink with no way to tell
  what was hidden.

**Type determination — bounded, so it does not undo Q1/Q2's fast path.** The
summary needs a type for *every* file, not just paired ones. Rule: **a conclusive
extension is trusted and nothing is read** (known Gerber/drill extensions, known
document/image extensions); **an unknown extension is sniffed**. Cost stays
proportional to the ambiguous files, and it is accurate exactly where extensions
lie. Without this the summary and the pair list would disagree about the same
file — e.g. a `.TXT` drill counted under "Other" while being previewable as
Gerber.

**Fully-identical state:** needs an explicit notification (zero yellow, zero red).
Form — banner, popup, something else — deliberately deferred to planning.
Argument on record against a modal: "the customer resent what we already have" is
the *common* outcome, so a dialog would tax the most frequent result.

**Filters:** a **changed-only toggle, off by default**, and a **type selector**
(All / Gerber / Other, default All).

- Both apply to **both trees at once** — filtering one side would make the panels
  non-comparable.
- A folder whose visible children are all filtered out hides too.
- An emptied tree shows a message (`no changed Gerber files`), never a blank
  panel, which reads as a bug.
- Filter state **persists across comparisons within a session**; both controls
  stay visible so the state is never a mystery.

**Layout — one wide control bar spanning both panels**, sitting between the
archive headers and the trees:

```
┌────────────────────┬────────────────────┐
│ A: name.zip        │ B: name.zip        │
│           [Change] │          [Change]  │
├────────────────────┴────────────────────┤
│  [ Only changes ]        [ All types ▾ ] │
├────────────────────┬────────────────────┤
│ tree A             │ tree B             │
└────────────────────┴────────────────────┘
```

**Structural consequence:** `ArchivePanel` currently owns both its header and its
tree, so it must be split — headers and trees become separate rows of a shared
layout — for the bar to span both. Chosen over one toggle per panel because a
single bar makes it unambiguous that the filter acts on both sides.

**No synced scrolling** — each tree scrolls independently.

---

## Q9 — File sizes drive sorting but are never displayed

**Status:** DONE
**Code:** `archivetreemodel.cpp:59` `sortChildren()`, `:127`; `archivetreedelegate.cpp`

Files sort by ascending size, but no size is ever rendered — the delegate only
tints the row background. The comment at `archivetreemodel.cpp:127` claims "size
is shown inline by the delegate, not a real column", which is stale/wrong.

**Question:** show the size inline (right-aligned, dim, as the comment
intended), or drop size sorting for alphabetical? Sorting by an invisible key is
currently confusing. Either way the stale comment needs correcting.

**Impact of Q1:** size is no longer decorative — it is half the content-class key
`(crc32, size)`, i.e. part of how the tool decides two files are the same file.
That strengthens the case for displaying it. Q1 also adds a second thing the row
must show (the `duplicate of …` badge), so the row layout question is now "size
*and* badge", not "size or nothing".

### Findings (2026-07-28)

- `expandAll()` at `archivepanel.cpp:83` means nothing is ever hidden behind a
  collapsed folder — the user scrolls one fully-expanded list. So folder
  *position*, not folder expansion, is what buries a row.
- `sortChildren()` puts directories first **unconditionally**
  (`archivetreemodel.cpp:63`), so any status-based order over files alone is
  defeated by an all-green folder sitting above a problem file at the root.
- The delegate skips directories entirely (`archivetreedelegate.cpp:15`), so
  folders are currently the one uncoloured thing in the tree.

**Decision:**

1. **Size is never displayed.** It is internal plumbing — half the `(crc32,
   size)` class key — and carries no meaning the user acts on. Consequently
   **size sorting is dropped**: it was ordering by an invisible key, which is what
   made the tree confusing.
2. **Rows sort into status bands: yellow → red → green**, alphabetical within a
   band. Not severity order — *work-demanded* order. Yellow means the app assumed
   two files correspond and they differ, so it needs a human and it leads
   somewhere (click → overlay). Red is one-sided and usually self-explanatory.
   Green is done. Rejected: alphabetical-only (problems land wherever the alphabet
   puts them, and with a long file list the user cannot tell at a glance whether
   there is anything to look at); relying on Q8's changed-only filter instead (a
   filter the user must remember to switch on does not help someone who has not
   yet realised there is something to find).
3. **Folders inherit the highest-priority band among their descendants** and sort
   into those same bands, instead of always leading. Within a band: directories
   first, then alphabetical. Without this, rule 2 does nothing in a nested
   archive.
4. **Folder rows are tinted by that inherited band**, but more lightly than file
   rows — a coloured left edge or coloured text, not a full background fill — so
   one changed folder does not turn the panel into a wall of colour. A folder that
   is *sorted* by status must *look* it, or its position is unexplained.
5. **Duplicates get no indication at all — they are simply green.** This
   **amends Q1's spec**, which had specified a `duplicate of <path>` badge. Green
   already means "nothing to do"; whether a row is green because the other archive
   holds the same content or because a sibling row covers it does not change the
   user's next action, so it is not worth row space, a glyph, or a tooltip.
   Consequence: `ArchiveEntry` gains no duplicate-marker field.
6. The stale `archivetreemodel.cpp:127` comment ("size is shown inline by the
   delegate") is corrected — it was never true.

Net effect on the row: it shows the filename and nothing else. The only visible
change is the order, and the tint.

---

## Q10 — Drag-drop accepts any file type, uses only the first URL

**Status:** DONE
**Code:** `archivepanel.cpp:89-107`

`dragEnterEvent` accepts any mimeData with URLs; `dropEvent` takes `urls.first()`
with no `.zip` check and no directory check. Dropping a PDF, a folder, or
several files at once falls through to the "could not be opened" warning (or
silently uses one of several).

**Question:** reject non-`.zip` at dragEnter (cursor shows "no drop"), validate
at drop with a clearer message, or leave as-is? Also: should dropping two zips
at once fill both panels in one gesture?

**Impact of Q1/Q3.** Q3 rules out the obvious "reject non-`.zip`" fix *if* it is
read as a content question — but see the decision: name-as-candidate-filter and
content-as-truth are the same rule Q3 adopted, applied at two different moments.
Q1 makes a two-at-once drop coherent (A/B are symmetric, so there is no new/old
ordering to get backwards) but also makes a mis-assignment invisible.

**Decision:**

1. **dragEnter accepts exactly one local file whose name ends in `.zip`**;
   anything else — a folder, a multi-file selection, a `.rar`, a PDF — shows the
   no-drop cursor. This makes drag-drop agree with the `Change...` picker, which
   has always been `*.zip`-filtered. Non-zip input is the user's own problem to
   convert; the app does not try to handle it.
2. **This does not contradict Q3.** The rule there and here is identical: *the
   name is a cheap candidate filter, content decides the truth.* At dragEnter only
   the name is available and a hover must not read files, so the name filters. At
   drop, the reader still decides whether the file genuinely opens as an archive.
   Q3's content-first rule governs *entries inside* an archive, where the tool must
   decide whether to recurse; at the top level the user chose the file deliberately.
3. **A `.zip`-named file that does not open reports inline on that panel**, not as
   a modal. `archivecomparepage.cpp:224/:233` currently raise `QMessageBox::warning`
   mid-flow, which violates the Q7 constraint and hides which panel failed behind a
   dialog that must be dismissed first.
4. **No two-archives-in-one-drop.** One archive per drop box, one at a time.
   Windows does not guarantee URL order within a multi-file drop, and because Q1
   made A/B symmetric a backwards assignment looks entirely plausible — the user
   would only notice when "only in A" read the wrong way round. Rule 1 already
   excludes it by construction.

---

## Q11 — Same archive can be selected on both sides with no warning

**Status:** DONE

Nothing prevents picking the identical path for both slots. The comparison runs
and everything comes out green — correct, but it looks like a valid "no changes"
result rather than a mistake.

**Question:** warn when both sides resolve to the same path, or leave silent?

**Impact of Q1:** the confusion gets slightly worse. Under Q1 an all-green result
is the *definitive* "these archives are the same" answer, so accidentally
comparing a file with itself produces the strongest possible reassurance. Ties to
Q8 — if a summary states "identical", it will state it for this mistake too.

**Decision (2026-07-28):** **no warning of any kind.** Not for the same path, not
for identical-content-under-a-different-name. The two archive filenames are
already displayed in the panel headers, so the mistake is visible without the app
saying anything, and it does not change what the user does next. Consistent with
Q9's principle — see the same reasoning there. Rejected on the way: same-canonical-
path detection (cheap, but buys nothing) and identical-content detection (also
cheap, but indistinguishable from the legitimate "customer resent an unchanged
set" result, which is the *common* outcome).

---

## Q12 — User-added extensions are append-only with no removal UI

**Status:** DONE
**Code:** `gerberfiletype.cpp:96` `addUserExtension()`

Appends to a QSettings INI list (`PcbSupplierWorkflow`, key
`gerber/userRecognizedExtensions`). The `Add .xxx` button is the only writer;
no UI reviews or removes the list. A wrongly added extension (e.g. `.txt` from a
CAM report) permanently makes junk files look like previewable Gerber pairs,
across all future sessions.

**Question:** add a small manage/reset UI, or is "delete the INI file" an
acceptable escape hatch for a ~50-user internal tool?

**Impact of Q2 — mostly defused.** Content now decides drawability, so the
extension list is a fallback for files whose content is inconclusive, and the
`Add .<ext>` button will rarely be needed. The specific harm this question was
built on — clicking `Add .txt` to reach a real drill file and thereby promoting
the pick-and-place and tool lists beside it — cannot happen any more, because
that drill file is recognised from its content. What remains is the plain
housekeeping question: an append-only list with no review or removal UI.

**Decision (2026-07-28): delete the whole mechanism.** `isRecognized()`,
`addUserExtension()`, the built-in extension list, the QSettings key, the
`Add .<ext>` buttons (`archivecomparepage.cpp:286`) and
`distinctUnrecognizedExtensions()` (`:78`) all go. Q12 stops existing rather than
acquiring a management UI. Q2's spec is amended to drop its step-3 extension
fallback.

The decisive argument is not "rarely needed" but **"can no longer work"**: every
Gerber file declares its coordinate format (`%FS`) near the top, and that is what
tells the parser how to read every number in the file. A file where the content
sniff finds no `%FS`/`%MO` and no `M48` cannot be rendered whatever its extension
says — so adding the extension only forces an unparseable file through to a blank
canvas. A button that misleads is worse than no button.

**Compensating change:** the sniff window widens from ~40 lines to **the first
8 KB**. With no extension fallback behind it, the window is now the only thing
between a drawable file and rejection, so it must survive a long `G04` comment
banner pushing `%FS` down the file. Real samples are 19–92 KB, so 8 KB covers any
plausible header at a fraction of a full decompression. That — not a permanent
global setting the user has to guess at — is the right remedy for the one genuine
failure mode this question leaves behind.

Rejected: a manage/reset settings screen (builds UI for a list that should now be
empty); leaving it as-is with "delete the INI" as the escape hatch.

Side effects: the "unrecognized" bucket survives, just without buttons.
Previously saved user extensions become orphaned INI entries — harmless, no
migration for a ~50-user internal tool. `gerberviewerpage.cpp` must be checked for
callers before deletion.

---

## Q13 — Non-ASCII zip entry names are untested

**Status:** DONE

Entry names come straight from QuaZip's default name codec. Zips written on
non-UTF-8 Windows locales store names in a legacy codepage without the UTF-8
flag, which can mis-decode. Effects: garbled names in the tree, and — worse —
`findStoredEntryName()` failing its comparison so the Gerber preview returns
empty bytes for that file.

**Question:** do you ever receive archives with non-English filenames (Cyrillic,
umlauts, CJK)? If yes this needs a real test with a sample archive; if no,
deliberately mark out of scope.

**Impact of Q1/Q2 — pulls in both directions.** Q1 makes *matching* far more
robust: green now comes from content, not names, so a mis-decoded filename no
longer misclassifies an unchanged file. And both archives decode through the same
codec, so even mangled names still match each other in the name pass. But Q2 adds
a second consumer of `findStoredEntryName()` — header reads for classification —
so a name that fails to resolve now silently costs *drawability* as well as
preview. Net: lower risk to the comparison result, same or slightly higher risk
to the canvas. (Note for context: at least one supplier's files carry German text
in Gerber attributes, so non-ASCII **content** already occurs; this question is
only about non-ASCII **entry names**.)

### Findings (2026-07-28)

- All six real archives scanned: **123 entries, zero non-ASCII entry names**, and
  zero backslash-separated names (the reader normalises those anyway).
- Igor: they do work with German customers, but umlauts in archive or file names
  are **a genuinely unusual case**.
- **The `findStoredEntryName()` worry does not hold.** The same QuaZip codec
  decodes the name both when the entries are listed and when one is looked up to
  read its bytes, so the two strings always agree even when both are mis-decoded.
  A mis-decode therefore cannot break the byte read, and after Q1 it cannot cause
  a wrong green/yellow/red either, because matching is content-based.

**Decision: accept and document — no code, no test fixture.** The worst realistic
outcome is **garbled text in the tree while the comparison result stays correct**,
which does not justify hand-building a non-UTF-8 archive to test against.

Two cases worth keeping distinct, since only one is even at risk: the **archive's
own filename** goes through Qt and Windows and is never affected; only **entry
names inside** the archive could garble, and only when the zip was written by an
older tool that omitted the UTF-8 flag (modern Windows and 7-Zip both set it).

Revisit only if garbled names are actually observed.

---

## Q14 — Pair list capped at ~3 visible rows

**Status:** DONE
**Code:** `archivecomparepage.cpp:134`

`gerberPairList_->setMaximumHeight(90)` — roughly three rows — by design, to
match the mockup's content-sized pill row. Scrolls internally beyond that. With
a heavily revised archive the user scrolls a tiny box.

**Question:** is 3 rows enough for the number of changed Gerber pairs you
actually see, or should the card grow (up to a cap) with the pair count?

**Impact of Q1/Q2 — this got more pressing, and each row got wider.** Three
changes push against the 90px cap (point 2 survives Q9: the badge Q9 removed was
a *tree-row* marker for duplicates; the pair *reason* on pair-list rows is a
different element and stays):

1. Q1's name pass emits the full A-class × B-class cross product, so one filename
   with two distinct variants on one side now produces two rows where it produced
   one.
2. Q1 requires each row to show **why** the pair was formed (`same name` /
   `only .gtl left on each side`) — yellow means the app assumed something, and
   the assumption has to be inspectable. That is extra width on every row, on top
   of the existing `A-path <-> B-path` label for cross-path pairs.
3. Q2 makes more pairs eligible in the first place — files previously diverted to
   "unrecognized extension" (a real `.TXT` drill file, `.drd`, `.Mill`, `.X3B`,
   `.X3T`) now reach the list.

Q6's original worry (duplicate copies multiplying rows) is gone — duplicates
collapse to one representative.

### Findings (2026-07-28)

Measured on the real archives: **`test_pair_3` produces 1 changed pair,
`test_pair_2` produces 2** (its `.GER` layer and its drill `.TXT`, the latter only
becoming previewable under Q2). Three visible rows is comfortably enough for
real input; the pressure listed above is theoretical.

**Decision: leave `setMaximumHeight(90)` exactly as it is.** Igor: "I like this
design." It matches the mockup's content-sized pill row, and real archives produce
1–2 pairs. Rejected: grow-to-content with a 6-row ceiling (solves a problem the
data does not show, and trades a known-good layout for a conditional one);
removing the cap entirely (would squeeze the preview canvas).

If a heavily-revised archive ever makes the small box genuinely painful, this is a
one-line change to revisit — no design work is being foreclosed.

---

# Part C — Questions added during the review

## Q15 — Does byte-exact comparison make sense for Gerber data at all?

**Status:** DONE (2026-07-28)
**Raised by:** Igor, during Q10. Not in the original fourteen.

Q1 makes `(crc32, size)` the identity of a file. Gerber is a text format, so two
files can describe the same board and still differ in bytes: a re-export that
renumbers apertures, a changed coordinate format, a regenerated comment header,
or CRLF-vs-LF line endings. Does byte-exactness answer the question the user is
actually asking?

### Findings — measured on the two real pairs (2026-07-28)

Both archives hashed and diffed file by file. Reported structurally; no supplier
data reproduced, extracted copies deleted afterwards.

- **`test_pair_3`** — 26 files a side. **Zero** files share a relative path (the
  whole tree sits under a differently-named root folder), yet **24 of 25 content
  classes match byte-for-byte**. The one differing file is a `.G1` layer, 32 bytes
  apart, whose 4 changed lines are `G` and `X` coordinate records — real geometry.
  This pair is also direct evidence for Q1: path matching finds nothing here,
  content matching finds everything.
- **`test_pair_2`** — 14 files byte-identical, 2 differing, 2 only on one side.
  The differing `.GER` has 2082 changed lines of which **2** are `G04` comments;
  the differing drill `.TXT` has 37 changed lines and **no** comment changes.
- `G04` comment headers **are** present (12 of 17 files in that pair) but are
  stable between revisions. **No file carries `%TF.CreationDate`.** No embedded
  timestamps were found anywhere.

**Zero false yellows across both real pairs.**

**Decision:** **keep byte-exact `(crc32, size)` as identity.** Semantic /
normalized comparison (strip comments, canonicalise coordinates, sort apertures,
then hash) is a **deliberate exclusion**.

Reasoning worth keeping:

- Byte-exactness **can never produce a false green.** Byte-identical means
  genuinely unchanged. That is the property that matters, and it is free —
  central-directory CRC, no decompression.
- Its only failure mode is a false **yellow**, which is an alarm, not a wrong
  answer, and the overlay is exactly the second stage that resolves it: render
  both, see an all-yellow composite, conclude nothing moved.
- Normalization would require parsing **every** file in **both** archives up
  front, discarding Q1's central-directory-only fast path, to solve a problem the
  real data does not exhibit.

**Revisit trigger — specific and easy to spot:** *a whole archive turning yellow
at once.* That means a supplier changed CAM tool or re-exported wholesale, and the
user would face ~25 overlays to confirm nothing changed. Below that threshold the
overlay handles it. (CRLF-vs-LF would present the same way.)
