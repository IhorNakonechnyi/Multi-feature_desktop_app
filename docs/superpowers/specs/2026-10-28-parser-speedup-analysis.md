# Stage 2: speeding up the same-data check — analysis

**Status**: **Decided 2026-10-30; being implemented** on branch `multi-threading` per
[the plan](../plans/2026-10-30-same-data-check-speedup.md) - the fast path is live from
Task 5. Rewritten 2026-10-30 after a second analysis round. Context:
[idea_multi-threading.md](idea_multi-threading.md),
[2026-10-26-stage-timing-diagnostic-design.md](2026-10-26-stage-timing-diagnostic-design.md).

## 1. Where we are

| Stage | What | State |
|---|---|---|
| 1 | File lists (tree expand + column width) | **Done** (`dbeb897`, `cf41209`): 1.0–2.4 s → 0–15 ms |
| 2 | Faster same-data check | **This document** |
| 3 | Threading (QtCore only) | Only if Stage 2 still leaves a noticeable freeze |

Totals after Stage 1 (work PC): Case 1 0.50 s, **Case 2 3.19 s**, Case 3 0.14 s,
Case 4 0.82 s. The check's parse is now the largest item in every case.

## 2. What was measured

Per pair, `SameDataCheck::isSameData` runs the **settings pass** on both files, then — only
if the settings match — the **full parse** of both, then the **shape comparison**.

| | settings pass | full parse | shape compare | Check parse | turned green |
|---|---|---|---|---|---|
| Case 1 | 213 ms (51 %) | 112 ms | 92 ms | 415 ms | 15 |
| Case 2 | **1977 ms (70 %)** | 749 ms | 85 ms | 2823 ms | 15 |
| Case 3 | 33 ms (55 %) | 18 ms | 9 ms | 54 ms | 8 |
| Case 4 | 429 ms (62 %) | 262 ms | 0 ms | 700 ms | 0 |

- Real Gerber files are ~96 % short region-outline lines (`X..Y..D01`), so per-line cost
  dominates.
- Profile (made-up files of the same shape): text copies ~30 % (two per statement in
  `tokenize`), text patterns ~16 %, "starts with" chains ~16–18 %, number decoding ~16 %
  of the full parse.
- The settings pass costs *more* than the full parse on the same file: it is strict (proves
  each line is exactly a form the parser understands), the parse loop is lenient.

## 3. Ideas

### Proposed

| Idea | What | Risk | Helps |
|---|---|---|---|
| **Fast path** | Split both files, pair statements by position. If every pair is text-equal or *both* are noise the parser ignores whatever they say (`G04`, `ExportNoise::isVolatileAttribute`, `%TO`/`%TD`; drill: volatile `;` comments only), the parser gets the same input → same shapes → green, no parse. Otherwise: today's check, unchanged. It never says "different". Checked: `tryParseDirective` and `isMacroBodyStatement` do not depend on that noise content. | low | re-saved pairs (expected: most greens) |
| **Fix A** | Try the aperture-select pattern only on lines starting with `D`/`G`; pass `DontCheckSubjectStringMatchOption`. Matches unchanged. | very low | settings pass |
| **Fix B** | Plain lines starting with `X`/`Y` go straight to coordinate handling in both passes (every earlier check traced: none applies; `G91` check stays). | low | both passes |
| **Fix C** | Decode coordinates from `QStringView`, no copies (`toLongLong` uses a stack buffer). | low | full parse |
| **Side-by-side early exit** | Run the A and B settings passes together, stop at the first differing kept line. Same verdict as today, just sooner. | medium | real-change pairs (Case 4) |
| **Fix D** (only if still needed) | `tokenize` returns views instead of copies. Largest single cost, but ~50 places change and a view into a temporary dangles silently. | medium | both passes |
| **Fix E** (only if measured) | Split each file into statements once, share the result between the passes. | low | full-parse pairs |

**Decided 2026-10-30:**
- The fast path is built switched on (no separate measurement round). The timing build runs
  it *and* today's check on every pair and reports hits (made green, time saved) and
  disagreements (fast "same" where today's check says not same).
- The fast path may give a few more greens than today: files identical apart from noise but
  using a feature the parser refuses (`G91`, non-absolute `%FS`, a misread `%SR`, an unknown
  directive, an empty layer). Identical input is the same data whether or not the parser
  understands it. Those cases are the only expected disagreements. **Not** a cut-off file
  (corrected while planning): the splitter drops a cut-off tail unread, so the fast path
  cannot compare it and passes the pair on.
- Noise = exactly what today's check already ignores, and a differing pair must be noise of
  the *same kind* on both sides. Gerber: any `G04`; `ExportNoise::isVolatileAttribute`
  (`.CreationDate`, `.GenerationSoftware`, `.ProjectId`, `.MD5`, `.SameCoordinates`);
  `%TO`/`%TD`; `%LN`/`%IN`. Drill: `;` comments where `ExportNoise::isVolatileComment` is
  true. Not included: presence-only rules (`FilePolarity,Positive`, `%IPPOS`, ...), which
  shift positions anyway, and other X2 attributes (a `.FileFunction` change stays real).

### Declined (and why)

| Idea | Why not |
|---|---|
| Hand-written coordinate recogniser instead of the patterns | Must reproduce subtle rules exactly (12-digit cap, signs, `D001`, G prefix, modal rules); one mismatch breaks "a change is never reported as same". Fix A gets part of the gain at no risk. |
| Full first-letter `switch` | Check order matters (`%` origin, G prefixes stripped in sequence); Fix B gets most of the gain, since ~96 % of lines start with X/Y. |
| New byte-level parser | Rewrite of ~1,500 lines of tuned rules; not proportionate. |
| Faster shape comparison (incl. counting keys in a hash table instead of sorting) | Only 0–3 % on real data. Revisit if files with many separate pads/tracks show up: there the sort was costly on made-up files. |
| One pass per file (settings list + shapes in one walk) | Gain smaller than it looks: the strict checks must still run per line. Real-change pairs get slower (shapes always built). Mixes the safety rules into the parser that also draws the overlay. |
| Keep parsed shapes per file (cache by CRC) | Each file is in one pair only, so a compare repeats nothing. Memory ~15–20 MB per MB of file (Case 2: ~250–340 MB vs 87 MB peak today). Click is already ≤215 ms. |
| Per-file fingerprint (SHA-256) for parsing on drop | Only useful for threading on drop; the fast path removes most of that work. Revisit with Stage 3 if it happens. |

## 4. Safety net

1. **Fast path check** (timing build): the fast path and today's check both run on every
   pair; the readout shows hits (how many pairs it made green, time saved) and disagreements
   (fast "same" where today's check says not same - expected only for the cases above).
2. **Before any parser change** (Fixes A–E) — **decided 2026-10-30: both parts**: a golden test (today's parser output on made-up
   files, stored, must stay identical) and a result fingerprint in the timing build (a
   checksum over all check output; reveals nothing; baseline recorded before the first fix).

## 5. Proposed order (to be decided)

| # | Step | Risk |
|---|---|---|
| 1+2 | Fast path switched on, with unit tests; timing build counts hits and disagreements | low |
| 3 | Safety net for the parser (golden test + fingerprint + baseline zip) | none |
| 4 | Fixes A–C; side-by-side early exit if Case-4-type pairs still matter | low–medium |
| 5 | Fix D / Fix E / Stage 3 threading — only if still needed | medium |

After each step: build, all tests, a zip for the work PC, re-measure.

**Decided 2026-10-30:** Fixes A–C go in together, in one zip. The golden test runs after each
fix on this PC; separate zips per fix only if the real-data fingerprint changes.

## 6. Tools

- Benchmark: `build-parsebench/bench-src/` (gitignored). `parsebench.exe` prints the
  per-stage split; `parsebench.exe <seconds> settings|shapes` loops one pass for profiling.
- Profiling: `VSDiagnostics.exe` (CpuUsageHigh config, no admin), then
  `xperf -i <etl> -symbols -a profile -detail` with `_NT_SYMBOL_PATH` at the benchmark PDBs.
- Timing zips in `build-dist/`: `…-timing-iconfix-…`, `…-timing-parsesplit-…`.

## Sources

- QStringView dangling: [KDE libksysguard MR 71](https://invent.kde.org/plasma/libksysguard/-/merge_requests/71)
- Hand-written vs regex lexing: [Eli Bendersky](https://eli.thegreenplace.net/2013/07/16/hand-written-lexer-in-javascript-compared-to-the-regex-based-ones)
- Qt: [qstring.cpp 6.8](https://code.qt.io/cgit/qt/qtbase.git/plain/src/corelib/text/qstring.cpp?h=6.8),
  [QRegularExpression](https://doc.qt.io/qt-6.8/qregularexpression.html)
- Differential testing: [freeCodeCamp](https://www.freecodecamp.org/news/differential-testing-legacy-migration/)
