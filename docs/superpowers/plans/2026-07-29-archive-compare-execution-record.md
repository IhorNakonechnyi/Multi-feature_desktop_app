# Archive Compare content-first — execution record

**What this is**: the durable findings from executing
`2026-07-28-archive-compare-content-first-consolidated.md` (19 tasks, 2026-07-28/29).

**Why it exists**: the working ledger lived at
`.superpowers/sdd/2026-07-28-archive-compare-content-first-consolidated/progress.md`, which is
gitignored by its own `.gitignore` containing `*`. It is in no commit anywhere and dies with
the worktree. This file carries over the parts worth keeping; the ledger keeps the
task-by-task narrative.

Build/environment facts that were also in the ledger now live in `CLAUDE.md` under Commands.

## Final verification (2026-07-29, before merge)

- **17/17 ctest targets green.** No MAX_PATH failures — that known issue belongs to the
  supplier-advisor worktree, not this branch.
- **Grep gates, all clean**: `QMessageBox` in `src/archivecompare/` (Q7); `selfDuplicate`
  (Q1); `"only new"`/`"only old"` (A/B symmetry). `isRecognized`/`addUserExtension`/
  `userRecognizedExtensions` returns 3 hits, **all comment lines in `gerberfiletype.h`
  documenting the removal** — no surviving code.
- **Every interactive gate confirmed by hand** by Igor: folder tints, drag-drop refusals,
  summary line, filter bar, panel split.

### End-to-end over the real archives

Run through the actual `compareArchives → resolveKinds → summarize` pipeline (temporary
harness, since removed). `Test_data/` is gitignored real supplier data.

| Pair | Result |
|---|---|
| `test_pair_3` | 24 identical classes, exactly 1 pair, 0 red |
| `test_pair_2` | 14 identical, 2 pairs, 1 red (rows A=17 vs B=16) |
| same archive both sides | `fullyIdentical = TRUE`, 25 classes, 0 pairs |

Two things worth noting:

- `test_pair_3`'s 24-of-25 **matches the Q15 measurement taken during the gap review**
  ("pair_3 matches 24/25 classes byte-for-byte, its one differing file differs in real
  coordinate records") — independent corroboration from a different code path.
- In both pairs the differing files sit at **different paths on each side**
  (`5435/347128-C_EX.TXT` ↔ `347128-C_EX.TXT`), so content-first matching across a
  folder relocation is confirmed on real data, not just in fixtures.

`test_pair_1` is Igor's hand-built fixture: 20 rows collapse to 12 classes (10 identical +
2 only-in-A). Duplicate collapse is visible there, but **its behaviour is not real customer
behaviour** — the gap review already recorded this, which is why layer-role matching was cut.

## Process findings worth reusing

**A red run only proves that *something* failed — check *which assertion* moved.** Three times
in this execution a nominally-red test turned out unable to discriminate the rule it named:

- a test asserting `onlyIn* == 0` passed against a stub returning all zeros;
- a "conclusive extension is not read" test passed against a stub that read nothing;
- a directories-first test passed because the *old* rule already did that unconditionally.

Each was rescued by **mutation testing**: break the specific clause, rebuild, confirm only
that test fails, restore. Where a test genuinely cannot fail, say so in the test itself —
`rejectsNonLocalUrl` carries a comment recording that `isLocalFile()` is redundant with the
later checks and that the case is a statement of intent, not proof.

**Design the assertion so defaulting cannot satisfy it.** The kind-resolution tests assert
both the resulting kind *and* exactly which paths the head reader was asked for. Without the
read set, "sniffed and found to be Other" is indistinguishable from "never looked at", because
`OtherData` is also the default.

**Arrange a red that characterises the bug.** For the drag-drop rule the helper was first
written to reproduce *today's* behaviour rather than as an always-reject stub — an
always-reject stub would have made every `rejects*` test pass vacuously. The red run then
failed on exactly the four real bugs.

**Some regressions only a screenshot can catch.** Splitting `ArchivePanel` left the new
`ArchiveHeader` without the card background, so the "Elevated Panels" look survived on only
half of each side. It built clean, every test passed, and no unit test could see it. Capturing
the real window (user32 `GetWindowRect` + `Graphics.CopyFromScreen`) caught it before it
shipped.

**Silent-success failure modes deserve a test even when the count looks right.** The Excellon
tool-modifier bug parsed the holes correctly — the layer reported the right object count —
while every hole referenced a tool with no aperture, so nothing drew. Object counts alone
would never have caught it.

## Review coverage — complete

- Tasks 1–10: each reviewed by a dispatched reviewer during execution.
- Tasks 11–14 (`93d0d28..a214d7c`): ready-to-merge, 0 Critical, 0 Important, 3 Minor — all
  addressed in `94c2d0c`. Findings: a comment describing the wrong mechanism (the code was
  right), a dead field, and a missing regression test for the real comparator output shape.
- Tasks 15–19 plus both post-plan fixes (`a214d7c..0b82b05`): ready-to-merge, 0 Critical,
  1 Important, 5 Minor — addressed in `0e39313`. The reviewer independently mutation-verified
  all four load-bearing claims made during implementation; each broke exactly its named test
  and nothing else.

The Important finding is worth recording, because it is a pattern rather than a one-off:
`formatTypeCounts` had **no coverage at all**, while the counting logic feeding it was
meticulously mutation-tested. The numbers were guarded; the sentence the user actually reads
was not. Display formatting that encodes meaning — per-side labels, omitted-vs-zero
fragments — deserves the same treatment as the arithmetic behind it. It has tests now, and
they were themselves proved load-bearing by swapping the A/B labels.
