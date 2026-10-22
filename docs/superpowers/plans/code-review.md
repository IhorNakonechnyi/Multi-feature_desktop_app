# Code review: same-data check (branch `issue_resaving`)

**Status**: **Review done and data-checked, decisions answered, plan written. Nothing fixed yet.**
Plan: `docs/superpowers/plans/2026-10-22-same-data-check-review-fixes.md`.
**Reviewed**: 2026-10-22. **Last updated**: 2026-10-22 (real-data findings Q1-Q4 added).
**Scope**: commit `ac1ac2a` ("Same-data check for re-exported layers, bump to v1.8.6")
against `main` (`555f3ad`). That is the whole same-data check, including the G04 / `%TO` /
KiCad-drill-banner changes. v1.8.6 test package: `build-dist/PcbSupplierWorkflow-1.8.6-win64.zip`.
**Related**: `docs/superpowers/specs/idea_multi-threading.md` (point 10).

The check's one promise: **it may wrongly leave a file yellow, but must never wrongly make it
green.** Points 1-4 break that promise for unusual content (false greens). Points 9, 11 and 12
are false yellows that real Altium re-exports hit.

**How data was gathered**: customer files never leave the user's PC. The user asks Copilot
structure-only questions (counts, line numbers, command names, no coordinates) and passes on the
answers. See section 5.

---

## 1. Summary table

| # | Kind | Problem | Fix (revised after data check) | Cons |
|---|---|---|---|---|
| 1 | **False green** | Drill `G85` slots and `R` repeats not understood by the parser; 6a drops them as "geometry" | Covered by the strict geometry rule (4): `G85` lines stay in 6a as **text that must match** (unchanged slots → green). `R` lines → refuse | Files with `R` repeats stay yellow (0 in real Altium data) |
| 2 | **False green** | Region with a **real** second outline: later `D02` start points are lost | Refuse only a `D02` **followed by at least one `D01`** before `G37`. Altium's trailing `D02` (see Q1) is not an outline and must not trigger it | Real multi-outline files stay yellow; the overlay drawing of those stays wrong until a parser fix |
| 3 | **False green** | Commands the parser ignores (`%LR %LM %LS %AB %IPNEG %OF %MI %SF %IR %AS`, `G91`) compared only as a list, not by position | **Allow-list** of `%` commands: `FS MO AD AM SR LP`, attributes `TF TA TO TD`, `%IPPOS` (= default), `%LN`, `%IN`. Anything else, or `G91` → refuse | Unknown old commands give yellow until reviewed and added |
| 4 | **Root cause of 1-3** | "Geometry or not" guessed from text (X/Y/I/J/D), separately from the parser | Invert the guess: a line counts as geometry **only** if it has exactly a form the parser fully reads (`X…Y…(I…J…)D01/02/03`, `Dnn`, `G54Dnn`, plain drill `X…Y…`, bare tool select). Everything else stays in 6a as text | The forms must mirror the parser (a forgotten update → yellow, never green). Long term: the parser reports what it didn't consume |
| 5 | Wrong colour | Same-name cross pairs: after A1=B1 and A2=B2 collapse, A1-B2 and A2-B1 remain, so all 4 stay yellow | Once a file is proven same-data, drop its other pairs. A partner left with **no** pair → decision 3 | More complex collapse logic |
| 6 | Wrong text | Summary "identical" counted per collapsed pair; can disagree with the trees | Count after collapsing, per content class that actually turned green | Summary tests change |
| 7 | Latent | Missing lookup defaults to "parseable" here, "not parseable" in the summary | Default to `OtherData` | None |
| 8 | Tidy-up | Duplicated code (kind lookup, "read both files") | Share one helper | Refactor of working code |
| 9 | **False yellow (frequent)** | Macro names compared in 6a. **Confirmed with real Altium data:** Altium names macros after their D-code (`ROUNDEDRECTD37`), so shifted apertures rename macros | **9b:** drop whole `%AM` definitions from 6a, using the parser's own end-of-macro rule. 6b still compares every used macro's recipe | ~1-2 h with tests |
| 10 | Performance | Files re-read per pair, parsed twice, all on the UI thread (25 pairs = 7.3 s, "Not Responding") | `idea_multi-threading.md` | Separate project |
| 11 | **False yellow (new, real data)** | `%TF.FilePolarity,Positive*%` present in only one file (newer Altium writes it, older doesn't) | `Positive` and "missing" count as equal; `Negative` stays different | Strictly, "missing" means "not stated" |
| 12 | **False yellow (new, real data)** | Altium drill header `;Layer_Color=9474304` (display colour) is compared | Add `Layer_Color` to the drill noise words | None (a colour never moves a hole) |
| 13 | Old files (new, real data) | `%IPPOS*%` and `%LNgto.gbr*%` in older exports: `LN` is a name and is compared in 6a | Part of the 3 allow-list: allow `%IPPOS`; allow `%LN` and **skip** it in 6a | None |

---

## 2. Details of the original findings

### 1. Drill slots (`G85`) and repeats (`R`)
`src/gerber/excellonparser.cpp:372`. `nonGeometryLines` drops every line with X or Y. The drill
parser has no `G85` or `R` handling (confirmed). A: `X1.0Y1.0G85X3.0Y1.0`, B: `…G85X5.0Y1.0` →
both parse to one flash at (1,1) → green. Same with `R05X0.5` vs `R10X0.5`.

### 2. Regions with several outlines
`src/gerber/gerberparser.cpp:754`. Inside `G36…G37` only the first `D02` point is recorded;
later `D02` moves are not (confirmed). A **real** second outline (`D02` then `D01`s) with a
different start compares as same data. The overlay also draws such regions as one joined outline
(pre-existing bug). A trailing `D02` with no `D01` after it draws nothing and is handled
correctly today.

### 3. Position-sensitive directives compared only as a list
`src/gerber/gerberparser.cpp:855`. The parser handles only `FS MO AD AM SR LP` (`%` commands) and
skips every other `%` command (line ~575: "anything else not yet handled - ignored"). 6a keeps
them, but only as an ordered list without their position relative to the objects. `%LR90*%`
before vs after pad P1 → green.

### 4. Root cause
`src/gerber/gerberparser.cpp:869`. Text heuristic separate from the parser. Also affects Gerber
bare coordinates before any D-code (skipped at around line 730).

### 5. Same-name cross pairs
`src/archivecompare/samedatacheck.cpp:79`.

### 6. Summary count, display only
`src/archivecompare/comparisonsummary.cpp:214`. The number appears only in the summary text line
(`archivecomparepage.cpp:346`). The "No differences" banner uses `Summary::fullyIdentical()`
(`comparisonsummary.cpp:115`), which ignores the identical count, so it cannot appear by mistake.

### 7. Inconsistent default kind
`src/archivecompare/samedatacheck.cpp:60`. `kindsA.value(path)` defaults to `GerberData`;
`ComparisonSummary` defaults to `OtherData`.

### 8. Duplicated code
`src/archivecompare/samedatacheck.cpp:49` duplicates `ComparisonSummary`'s `kindByPath`. The
page's read-both lambda duplicates `showSelectedPair()` (around `archivecomparepage.cpp:518`).

### 9. Macro names
`src/gerber/layerequivalence.cpp:25` compares macros by body, but 6a keeps `%AM<name>` headers and
bodies in order, so renamed or reordered macros never reach 6b. 9b must drop the **whole**
definition (header and body) from 6a, because renames come with reorders. The macro end must be
found with the parser's rule in `parseMacroDefinition` (`gerberparser.cpp:357`), ideally shared.

**Side finding:** `parseMacroDefinition` ends a macro at the next `FS/MO/AD/LP/AM` or at any
non-`%` statement, but not at `TA/TF/TO/TD`. A `%TA` directly after a macro would be swallowed
into its body. **Real Altium data (Q4): macros are always followed by `G04` or `%ADD`, so Altium
is unaffected.** KiCad is unverified; it appears to end its macro list with a `G04` comment. Add a
unit test with `%TA` directly after a macro and fix the rule if it fails (only digit- or
`$`-led lines belong to a macro body).

### 10. Performance and freeze
`src/archivecompare/archivecomparepage.cpp:336`. See `idea_multi-threading.md`.

---

## 3. Real-data findings (Altium, via Copilot, 2026-10-22)

| Q | Question | Result | Consequence |
|---|---|---|---|
| Q1 | Regions: several `D02` between `G36`/`G37`? | 805 regions, **every** one `G36 → D02 → D01×N → D02 → G37`. The second `D02` is the last line, no `D01` after it, not at the start point | Altium regions are single outlines plus a trailing move. Point 2's rule must require a `D01` after the extra `D02` |
| Q2 | Drill: `G85` / `R` lines? | 0 / 0. Header: `M48`, `;Layer_Color=…`, `;FILE_FORMAT=4:3`, `METRIC,LZ`, `;TYPE=PLATED`, `;TYPE=NON_PLATED`, `TnnF00S00Cd.ddd` tool table, `%` | Refusing `R` costs nothing. New point 12 (`Layer_Color`). `FILE_FORMAT` and `TYPE` must stay compared (they are) |
| Q3 | Which `%` commands and G-codes? | `%`: `FS MO AD AM LP` only. G: `G01 G03 G04 G36 G37 G71 G75`. No `G91`, no transforms, no `AB`, no X2 `%TF` in that file | The allow-list never triggers on Altium |
| Q4 | What follows a macro? | 20 macros, 9 lines each; next line `G04` (14) or `%ADD` (6) | 9b safe with the existing end-of-macro rule |
| - | Pair `test1` (Altium, apertures shifted) | `%AMROUNDEDRECTD37` etc.; same shapes under different D-numbers | Confirms point 9 |
| - | Pair `test3` (Altium 20.0 vs 22.6) | Differing `%` lines: `SameCoordinates`, `GenerationSoftware`, `MD5` (all ignored) and `%TF.FilePolarity,Positive*%` in one file only | Point 11 |
| - | Older non-Altium exports | `%IPPOS*%`, `%LNgto.gbr*%`, `%MOIN`, `G54Dnn`, `G74/G75`, `%SRX1Y1I0.0J0.0*%`, `D9506`, unpadded `G1/G3/D1/D2` | `IPPOS`/`LN` → point 13. The rest the parser already handles |

Altium file shapes seen: `G04 #@! TF...` comment-form attributes in some exports, real `%TF...%`
in others (version-dependent); `G04 Layer_Color=…`, `G04 Board_Origin=…` comments.

---

## 4. Overlaps

- **1, 2, 3 ↔ 4**: same root cause; the strict geometry rule plus the allow-list fixes all.
- **9b ↔ side finding ↔ 4**: all depend on where a macro ends; 9b must share the parser's rule.
- **5 ↔ 6 ↔ 7 ↔ 8**: same collapse function; 6 is computed from what 5 leaves; 7 is a symptom
  of 8.
- **3/4, 9b, 11, 13** all edit `GerberParser::nonGeometryStatements`: do them one after another.
- **10 ↔ everything**: the threading fingerprint packages the final 6a/6b rules, so do it last.

---

## 5. Suggested order

1. **1-4 + 13**: strict geometry rule, allow-list, precise region rule, `R` refuse. Tests include
   Altium's exact region pattern (trailing `D02`) and the old-file commands (`%IPPOS`, `%LN`).
2. **Macro side-finding test** (`%TA` right after `%AM`); fix the end rule if it fails.
3. **9b.**
4. **11** and **12** (small, independent).
5. **5, then 6, with 7.**
6. **1.8.7 test build** → the user tests with the real pairs (`test1`, `test3`, others).
7. **Merge into `main`** after the user's OK.
8. **8** (optional tidy-up).
9. **10** (threading): measure first.

Tests first for every step; stop after each step for the user to review.

---

## 6. Decisions (all answered 2026-10-22)

1. **Point 9**: yes, do 9b.
2. **Point 11**: yes, `FilePolarity,Positive` = missing line; `Negative` stays different.
3. **Point 5**: yes, a cross-pair partner left with no pair becomes **red**.
4. **Point 6**: yes, count per content class that actually turned green.
5. **Point 12**: yes, `Layer_Color` is a drill noise word.
6. **Scope**: work stays in this worktree; include 7; 8 optional at the end; 10 not in this
   plan; finish with a 1.8.7 test build; **do not merge** into `main`.

Optional data check (older non-Altium files): **skipped**. An unknown command only costs a yellow;
add commands to the allow-list when they show up in testing.

Plan: `docs/superpowers/plans/2026-10-22-same-data-check-review-fixes.md`.

### Copilot prompts used (reuse for new files)

- **Q1 regions:** count `G36…G37` blocks; how many have two or more `D02*`; for 3 examples give
  line numbers, the compressed ending pattern (`G36 → D02 → D01 ×N → … → G37`), and whether each
  extra `D02` equals the previous line or the first `D02` (yes/no); other line kinds inside;
  whole file read?
- **Q2 drill:** count `G85` lines and lines starting with `R`+digit; list line-start commands
  with counts (X/Y lines counted as "coordinate lines"); whole file read? (Make sure it is the
  `M48` program, not Altium's `.DRR` report.)
- **Q3 commands:** list every `%` two-letter command with counts; `G91`?; every `Gnn` line code
  with counts; whole file read?
- **Q4 macros:** count `%AM` definitions; lines per definition; first 4 characters of the line
  after each definition, with counts; whole file read?

Always: "Do NOT output any coordinate values", and ask whether the whole file was read.
