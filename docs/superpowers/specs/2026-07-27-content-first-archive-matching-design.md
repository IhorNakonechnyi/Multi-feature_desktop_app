# Content-first archive matching

**Status**: **Implemented** on branch `archive-compare-content-first` (Tasks 3–5 of
`docs/superpowers/plans/2026-07-28-archive-compare-content-first-consolidated.md`),
verified 2026-07-29. Not merged to `main`.
Two amendments made during implementation, both recorded in that plan: duplicates
get **no marker at all** (plain green, per Q9, not the `duplicate` badge this
document originally promised), and a nested archive whose content class already
exists in the other archive is never opened (per Q4's two-phase read).
**Supersedes**: the matching half of `2026-07-15-path-tolerant-crc-matching-design.md`
(that document's Pass 1 / Pass 2 ordering and its ambiguity rule), plus the
self-duplicate suppression added later in commits `30ec7e0` and `6502548`.
**Relates to**: Q1 of `docs/superpowers/plans/2026-07-27-archive-compare-open-questions.md`.
Closes Q5 (size ignored in matching) as a side effect.

## Purpose of the feature, restated

Archive Compare is a **support** tool. Its job is to tell the user whether two
archives are technically the same or contain changes, and to point at exactly
which files to look at. It does not decide what a change means — production has
review steps, some of which legitimately correct Gerber data, and the user reads
the change and makes that call. The tool must therefore save time on average
cases without ever asserting more confidence than it has.

## Problem with the current algorithm

Three defects, all rooted in the same choice — matching is keyed on the *file
path/name*, while content (CRC) is only used to compare files that the name
already paired up.

1. **False green.** `selfDuplicateIndices()` keys on filename alone, archive
   wide, first-seen-wins. The 2nd+ occurrence of any filename in an archive is
   forced `Identical` and never matched. Two legitimately different files that
   share a name in different folders — routine in PCB archives — make the second
   one green regardless of whether it is missing from or changed in the other
   archive. Green is the one colour that means "safe to skip", so this is wrong
   in the dangerous direction.
2. **Which copy survives is arbitrary.** "First occurrence" means zip central
   directory order, i.e. whatever the customer's packaging tool emitted. The row
   the user must act on can land inside a nested archive while an equivalent copy
   sits at the root showing green.
3. **Renames and relocations defeat matching.** A file that is byte-identical but
   renamed cannot be recognised as unchanged, because the name is the key.

## Colour semantics

These are the contract; the algorithm exists to serve them.

| colour | meaning | produced when |
| --- | --- | --- |
| **green** | nothing for you to do | the same content exists in the other archive, **or** this row is a redundant copy of another row in this same archive |
| **yellow** | the app *assumes* these two files correspond, and their content differs — compare them | a cross-archive pair was formed by name or by extension |
| **red** | no counterpart found — open it, it may carry new information | everything else |

Two invariants follow:

- **Yellow always comes in cross-archive pairs.** A pair is one file in A and one
  in B, never two files inside the same archive. An orphan yellow is
  unconstructible, because every yellow-producing pass requires an unmatched file
  on *each* side.
- **A and B are symmetric slots**, not "new" and "old". The user may load either
  archive on either side. The Gerber overlay legend therefore changes from
  `only new` / `only old` to `only in A` / `only in B`.

## Algorithm

Input: a flat file list per archive, with nested archives flattened so a nested
entry appears at its true leaf path.

**Amended by Q4 — the list is built in two phases, not one.** Phase 1 lists the
top level only, with each nested archive appearing as a single opaque entry
carrying its own `(crc32, size)`. Steps 1–3 below then run on that. Any nested
archive whose content class is **present in the other archive** is identical by
definition, so it is never opened: it stays one green row and its contents never
enter the comparison. Phase 2 recurses only into containers that differ or exist
on one side only, splices their entries in place of the opaque entry, and matching
re-runs. Applied level by level, so a nested archive inside a nested archive gets
the same treatment.

Consequence, accepted: an unchanged nested archive shows as a single **file** row,
while a changed one shows as a **folder** node with its contents inside. Same
archive, different shape depending on whether anything changed — which is itself
a useful signal.

### Step 1 — Content classes

Within each archive, group entries by `(crc32, sizeBytes)`. Both come free from
the zip central directory; no decompression. Size joins CRC purely as a cheap
guard against checksum collision.

A *class* is the unit of all subsequent matching. This makes "files with equal
content always share a status" an invariant of the data structure rather than a
rule that has to be enforced.

### Step 2 — Collapse duplicates within an archive

A class with several members is one item for matching purposes.

- The **representative** is the member with the **shallowest path** (fewest `/`
  separators), ties broken alphabetically. Never archive order — the actionable
  row must land at the most accessible location, predictably. Because a nested
  archive's own name becomes a path segment, a copy at the archive root always
  wins over a copy inside a nested archive.
- **The representative stands for its class everywhere downstream**: it is the
  path recorded in a pair, and its leaf name and extension are the ones steps 4
  and 5 match on. A class whose members have differing names is therefore matched
  under the representative's name only.
- Every non-representative member is rendered **plain green**, with no marker of
  any kind, regardless of what status the class ends up with.

Rationale: two identical files carry one piece of information. Making the user
open both wastes time. Green means "nothing to do", and a redundant copy is
exactly that — whether it is green because the other archive has the same content
or because a sibling row already covers it makes no difference to the user's next
action, so the distinction is not worth row space (decided in Q9). Any summary
counts must still count distinct classes, not file rows.

### Step 3 — Content match

A class present in both archives (same `(crc32, size)`) → **green on both
sides**. Name-blind, so a file that was renamed, relocated, or re-saved under a
different extension without being edited is recognised as unchanged for free.

### Step 4 — Name pass over leftovers

Over classes that step 3 could not place, on both sides. Group by leaf filename,
case-insensitively (Windows-only target; the reader already normalises `\` to
`/`).

For a filename present on both sides: every class under it goes **yellow**, and
**every A-class × B-class combination becomes a pair**. Not one representative
pair — if one side has two distinct-content files under that name, the user needs
to be able to open the other side's file against each of them.

This replaces the old "multiple distinct-CRC candidates → ambiguous → leave
unmatched" rule, which discarded exactly the information the user wanted.

### Step 5 — Extension pass over what remains

Group remaining leftovers by lowercase extension. **Fires only when exactly one
class remains on each side with that extension** → pair them, both yellow.

This is an elimination argument, not evidence, so the guard is deliberately
stricter than step 4's: no cross product. When two or more candidates remain on
either side, leave them red rather than guess. Extension is a weak key in
practice — in real archives an entire layer set frequently shares one generic
extension — so it may only be used to break a genuine one-against-one.

### Step 6 — Everything else is red

### Pair provenance

Every pair records **why** it was formed — `same name` or `only .<ext> left on
each side` — and the pair list displays it. Yellow means the app assumed
something; the user must be able to weigh that assumption. A pair formed by
elimination deserves less trust than one formed by name, and the UI must not
flatten that difference.

## Explicitly out of scope

Recorded so these are understood as decisions, not oversights.

- **Mixed-revision archives.** An archive containing the current layer set plus a
  nested archive holding a *previous* revision is a real possibility, but the
  customer flags it in the covering letter and the user does not reach for this
  app in that case. The tool will report the extra files as red rather than
  attempting to align sets. Consequence, accepted: in that shape the other
  archive's superseded layers can show green, because their content genuinely
  does exist in the other archive — inside the stale copy.
- **Container-aware grouping** (comparing folder-set against folder-set, merging
  results worst-status-wins). Only needed for the mixed-revision case above.
- **Layer-role matching** via `%TF.FileFunction` / `%LN` attributes. Technically
  attractive — real Gerber files do declare their role, and it is the only key
  that survives a layer renumber — but no genuinely-supplied archive exhibits
  renumbering. The one archive that appeared to was constructed by hand as a test
  fixture. Revisit only if a real customer starts renumbering between revisions.
- **Type as a matching constraint.** With exact-filename matching, restricting
  matches to same-type files changes no outcome. Data type survives only to
  decide what can be rendered on the canvas, which belongs to Q2.
- **ODB++ / `.brd` / `.tgz` content comparison.** These are compared as single
  files by content like any document. The eventual approach, if ever needed, is
  exporting Gerber from them and comparing that — not parsing them directly.

## Worked examples

Generic names; no supplier data.

**1 — Duplicate drill, single variant.** A holds one drill file. B holds the same
drill file twice — once at root, once inside a nested archive — both identical to
each other and differing from A's. Step 1 gives B one class with two members;
step 2 makes the root copy the representative and the nested copy goes green;
step 4 pairs the two remaining classes by name. Result: **1 yellow in A, 1 yellow
+ 1 green in B, 1 pair.**

**2 — Duplicate drill, two variants.** As above, but B's two drill files differ
from each other. Step 1 gives B two classes; step 2 collapses nothing; step 4
pairs A's single class against both of B's. Result: **1 yellow in A, 2 yellow in
B, 2 pairs** — the user can open A's file against each of B's variants.

**3 — Renamed and edited, single candidate.** A holds one Gerber file; B holds a
differently-named Gerber file with different content, plus a duplicate of it in a
nested archive. Steps 3 and 4 find nothing; step 5 sees exactly one `.gtl` class
on each side and pairs them. Result: **1 yellow pair, 1 green duplicate.**

**4 — Panelised set in a nested archive.** B carries a nested archive holding a
panelised version of the layer set; A has nothing like it. A's files are all
matched by content in step 3, so A has no leftovers; the panelised files find
nothing to pair with in steps 4 and 5. Result: **red** — "these exist nowhere in
the other archive, open them." If the other archive also carried a panelised set,
they would match on content and go green instead.

**5 — Ordinary revision.** Names stable, two layers edited, one extra document in
one archive. Step 3 greens everything unchanged; step 4 pairs the two edited
layers by name; the extra document goes red. Unchanged from today's good
behaviour.

## Scope of change

- `src/archivecompare/crccomparator.cpp` — rewritten around content classes.
  `selfDuplicateIndices()` deleted.
- `src/archivecompare/archivecomparisonresult.h` — `DifferingPair` gains a
  pair-reason field. No duplicate marker on `ArchiveEntry`: duplicates are plain
  green rows (Q9).
- `src/archivecompare/archivecomparepage.cpp` — pair list shows the pair reason;
  overlay legend text changes to `only in A` / `only in B`.
- `ArchiveReader` unchanged — no decompression is added by this design.

## Testing

Unit tests in `tests/unit/test_archivecomparator.cpp`, extending the existing
suite. The existing cases for unmoved identical / different / only-in-one-archive
must continue to pass.

- content match across a rename — green, no pair
- content match across a relocation — green, no pair
- duplicate collapse: representative is the shallowest path, not the first entry
- duplicate collapse: representative chosen alphabetically on equal depth
- duplicate collapse: a root copy beats a copy inside a nested archive
- a class whose members have different names is matched under the
  representative's name, and the pair records the representative's path
- duplicate of a red class is green, and the representative stays red
- duplicate of a yellow class is green, and the representative stays yellow
- name pass: cross product, one class each side → one pair
- name pass: cross product, one class against two → two pairs
- extension pass fires on exactly one class per side
- extension pass does **not** fire with two candidates on either side — both red
- pair reason recorded as name vs extension
- no orphan yellow: every yellow entry belongs to at least one pair
- previously ambiguous same-name-different-content case now yields pairs rather
  than being discarded

## Open follow-ups

- **Q6** (representative pair per group) is resolved here by the cross product in
  step 4.
- **Q2** (drill detection timing) is untouched and still open; it decides what the
  canvas will accept, not what matches.
- **Q8** summary counts must count distinct classes, not rows — see step 2.
- An optional safety net for the excluded mixed-revision case was discussed and
  not adopted: a one-line notice when a single folder or nested archive in one
  side holds content identical to the entire other archive. Cheap, no matching
  changes. Left for the user to decide later.
