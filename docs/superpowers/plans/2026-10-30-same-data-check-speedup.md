# Same-data check speed-up (Stage 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Archive Compare's same-data check fast enough that re-saved archives no longer freeze the window, without ever turning a real change green.

**Architecture:** A positional *fast path* recognises files that differ only in noise the parser ignores (comments, export labels) and answers "same" without parsing; everything else falls back to today's check unchanged. A golden test and a real-data fingerprint guard the parser, then three small parser fixes (A–C) speed up the check for the pairs that still need it.

**Tech Stack:** Qt 6.8.2 (QtCore/QtGui only), C++20, MSVC 2022, CMake, QtTest.

**Spec:** [docs/superpowers/specs/2026-10-28-parser-speedup-analysis.md](../specs/2026-10-28-parser-speedup-analysis.md) (decisions of 2026-10-30 inside).

## Where we are (2026-10-30, end of session)

**Plan complete (2026-10-30).** Final real-data run passed: identical fingerprints in all 4 cases. Whole-branch review: no Critical; the timing build was then removed (`b1df31e`) and the review's small points fixed (`7cc62c0`, `4a964ad`). Next: Igor decides the merge to main, and whether Case 2 (~3.6 s check in the normal build) needs Stage 3 (threading).

| Task | State | Commit |
|---|---|---|
| 1 Golden test | ✅ done | `f965187` |
| 2 Result fingerprint + baseline zip | ✅ done | `83f81d4` |
| 3 Statement cursor | ✅ done | `581e79c` |
| 4 Fast-path functions | ✅ done | `38a8d6a` |
| 5 Fast path wired in | ✅ done | `006c274` |
| 6 Fast-path counters | ✅ done | `015cd74` |
| 6½ Why-missed readout + estimate fix (not in the original plan) | ✅ done | `18f04b0` |
| — Real-data checkpoints 1 and 2 (Igor, unplugged) | ✅ done: fingerprints identical; spec §7 | — |
| 7 Fix A | ✅ done | `96ce87d` |
| 8 Fix B | ✅ done | `a87194e` |
| 9 Fix C + final zip | ✅ done (zip named `…-timing-fixesABC-…`, not `…-fastpath-…`, which is the checkpoint-1 zip) | `350aeb9` |
| — Final real-data run (Igor, unplugged) | ✅ passed: fingerprints identical | — |
| — Whole-branch review + fixes | ✅ done | `b1df31e`, `7cc62c0`, `4a964ad` |

How to resume:
- Working mode agreed with Igor: one task at a time; after each, build + all tests +
  self-review (up to 3 review/fix rounds), then a short report.
- The ledger with every ruling made so far: `.superpowers/sdd/2026-10-30-same-data-check-speedup/progress.md`
  (gitignored). Start the next task with the executing-plans `task-start` script.
- Task 9's final zip is compared against `build-dist/…-timing-missreasons-win64.zip` (or the
  baseline zip): the fingerprint per case must stay `fd7f569b1305373e`, `fe23dd71de3af1e5`,
  `e9ed283c4a4ecd92`, `62770098066bbd94`. **Igor runs it unplugged**, like the checkpoints.
- After Task 9: re-measure; if Case 2 still freezes, Stage 3 (threading). Parked for Case 4:
  the side-by-side early exit (spec §3).

## Global Constraints

- Branch `multi-threading`; commit there only, never on `main`.
- No new dependency: QtCore/QtGui only (`QCryptographicHash` is QtCore). No Qt Concurrent, no networking.
- Never hardcode real parameter, supplier or customer names; all test data is made up. The leak hooks must be active (`git config core.hooksPath` = `.githooks`).
- The check's main rule: **a real change must never be reported as "same"**. A false yellow is acceptable; a false green never is.
- Timing-only code is marked `// TIMING-DIAG` and sits behind `#ifdef PCB_TIMING`, like the existing timing code.
- Build: `cmake --build build --config Debug > build.log 2>&1; $LASTEXITCODE` (never pipe). Tests: `$env:PATH = "C:\Qt\6.8.2\msvc2022_64\bin;$env:PATH"; ctest --test-dir build -C Debug --output-on-failure`.
- Close the running app before building (LNK1168).
- Comments follow the codebase: explain *why*; for parser logic, more explanation than usual (memory: complex code style).

## Review Focus

1. **Noise at the same position but of a different kind** (a comment in A, an attribute in B; `.CreationDate` in A, `.ProjectId` in B) → must fall back, not be called same. Test: Task 4 `differentNoiseKindsAtOnePositionAreNotSame`.
2. **Cut-off files whose dropped tails differ.** `tokenize` drops an unterminated tail, so the fast path would never see it → must fall back. Test: Task 4 `cutOffFilesAreNeverSameByStatements`.
3. **Invalid UTF-8 in a statement that isn't noise.** Two different bad bytes both decode to U+FFFD → text-equal → must fall back. Test: Task 4 `replacementCharacterFallsBack`.
4. **A drill "volatile" comment that also carries `FILE_FORMAT=`.** The drill parser reads that from any comment → must fall back. Test: Task 4 `drillNoiseCommentWithFileFormatIsNotSame`.
5. **The same text, once inside `%…%` and once plain** (`%MOMM*%` vs `MOMM*`). The parser treats them differently → must fall back. Test: Task 4 `sameTextWithDifferentOriginIsNotSame`.

---

### Task 1: Golden test for both parsers — ✅ done

Protects every later parser change: today's exact output on a fixed made-up corpus, stored as SHA-256 per file. (SHA-256 is used here because we compare against results from the *past*; storing a 64-character fingerprint instead of megabytes of expected output.)

**Files:**
- Create: `src/gerber/gerberobjectdump.h`, `src/gerber/gerberobjectdump.cpp`
- Create: `tests/unit/test_parsergolden.cpp`, `tests/unit/golden/parser-golden.txt`
- Modify: `tests/unit/CMakeLists.txt` (new target `test_parsergolden`, sources as `test_samedatacheck` plus `gerberobjectdump.cpp`; `target_compile_definitions(... PRIVATE GOLDEN_FILE="${CMAKE_CURRENT_SOURCE_DIR}/golden/parser-golden.txt")`)

**Interfaces:**
- Produces: `QByteArray GerberObjectDump::dump(const Layer &layer)`: one text line per aperture (sorted by code: code, shape, modifiers, macro name), per macro (sorted by name: name, statements), the unit scale, then one line per object **in file order**, with every field `layerequivalence.cpp`'s `objectKey` uses plus `apertureCode`. Deterministic, value-exact (integers as integers, doubles via `QString::number(v, 'g', 17)`).
- Produces: `QVector<QByteArray> goldenCorpus()` (static in the test): 24 Gerber + 6 drill files from `QRandomGenerator(20261030)`, ~1,500 statements each.

- [ ] **Step 1: Write the dump helper and the corpus generator.** The Gerber generator mixes, by weighted random choice: region blocks (`G36`, a `D02` start, 3–300 `D01` points, some modal `X..Y..*`, `G37`); aperture selects `D10*`, `D012*`, `G54D12*`; flashes `D03`; strokes after `G01*` or `G1*`; arcs after `G75*` (and in 3 files `G74*`) with signed and `+`-prefixed I/J; bare `D01*`/`D02*`; polarity `%LPC*%`/`%LPD*%`; odd forms (`Y..X..D01*`, `X..D10*`, `D05*`, `G02I5J5*`, a 13-digit coordinate, `X..I..J..D01*` with no Y, `G04 X1Y2D01 text*`). Headers rotate between `%FSLAX46Y46*%%MOMM*%` and `%FSTAX24Y24*%%MOIN*%` (trailing-zero format, needed for Fix C), with `C`/`R`/`O`/`P` apertures, one `%AM` macro used by an `%AD`, and one `%SRX2Y3I1.0J2.0*%…%SR*%` block. Two files contain `G91*` (refusal), one ends cut off. Drill files: `M48`, `METRIC,TZ` or `INCH,LZ`, 3 tools, holes, `G85` slots, a rout block (`G00`/`G01`/`G02` with I/J, `M15`/`M16`).
- [ ] **Step 2: Write `goldenOutputIsUnchanged`.** For each file: Gerber → `nonGeometryStatements(text, &refusal, &drawLeftOut)` (the list, or `REFUSED <rule> @<position>`, plus `drawLeftOut`) and `GerberObjectDump::dump(parseContentToObjects(text))`; drill → `nonGeometryLines` and `ExcellonParser::parseContentToObjects` (or `AMBIGUOUS` on the exception). SHA-256 of that text → compare with line *i* of `GOLDEN_FILE`; on mismatch `QFAIL` names the file index. If env `PCB_GOLDEN_RECORD` names a folder, write the hashes file there plus one dump per file, and `QSKIP`.
- [ ] **Step 3: Record.** Run with `$env:PCB_GOLDEN_RECORD = "<scratchpad>\golden"`, copy the hashes file to `tests/unit/golden/parser-golden.txt`.
- [ ] **Step 4: Run without the variable** → `test_parsergolden` PASS; all 18 other tests PASS.
- [ ] **Step 5: Prove it bites.** Temporarily change `pow10` to start at `result = 2`, rebuild, expect FAIL, revert.
- [ ] **Step 6: Commit** `Add a golden test that pins both parsers' output`.

### Task 2: Result fingerprint in the timing build — ✅ done

Proves on real files that later changes give the same answers. Reveals nothing about the files.

**Files:**
- Modify: `src/archivecompare/stagetiming.h` (`CheckStages`: add `QByteArray fingerprint;` and `qint64 fingerprintNs = 0;`), `src/archivecompare/stagetiming.cpp` (`formatReport`: line `Result fingerprint: <first 16 hex chars, or ->`; add the name to the dash list)
- Modify: `src/archivecompare/samedatacheck.cpp` (feed the fingerprint inside `isSameData`, `#ifdef PCB_TIMING`)
- Modify: `CMakeLists.txt:158-162` (add `src/gerber/gerberobjectdump.cpp` to the `PCB_TIMING` sources)

**Interfaces:**
- Consumes: `GerberObjectDump::dump` (Task 1).
- Produces: `StageTiming::checkStages().fingerprint`, a chained hash: after each checked pair, `fingerprint = SHA-256(fingerprint + SHA-256(pairText))`, where `pairText` = kind, both settings lists or refusals, both dumps if the full parse ran, and the verdict. Pairs arrive sorted (collapseSameDataPairs), so the result is deterministic. Hashing is timed into `fingerprintNs` only, never into the other stages. Reset wherever `checkStages()` is reset today.

- [ ] **Step 1: Implement** as above; the hash work starts after the existing `lap(...)` calls so the stage split stays honest.
- [ ] **Step 2: Build the timing configuration** (`cmake -S . -B build-timing "-DCMAKE_PREFIX_PATH=C:/Qt/6.8.2/msvc2022_64" "-DPCB_TIMING=ON"`, Release) and run the app on a made-up archive pair twice → the same fingerprint both times; change one coordinate in one file → a different fingerprint.
- [ ] **Step 3: Commit** `Add a result fingerprint to the timing readout`.
- [ ] **Step 4: Build the baseline zip** `build-dist/PcbSupplierWorkflow-1.8.9-timing-baseline-win64.zip` with the `docs/releasing.md` recipe. Igor does not run it yet; it is run together with Task 9's zip.

### Task 3: Statement cursor under `tokenize` — ✅ done

One splitter that produces statements as views (no copies, can stop early). `tokenize` is rebuilt on it, so the parser and the fast path can never disagree on where a statement starts and ends.

**Files:**
- Modify: `src/gerber/gerberparser.cpp:34-100` (anonymous namespace)

**Interfaces:**
- Produces (internal to `gerberparser.cpp`): `struct StatementView { QStringView text; bool isParameterStatement = false; };` and `class StatementCursor { public: explicit StatementCursor(const QString &content); bool next(StatementView &out); bool truncated() const; };`. Views point into `content` only, never into a temporary (comment says so).
- `tokenize(content, truncated)` keeps its signature and output: loop `next`, append `{view.text.toString(), view.isParameterStatement}`.

- [ ] **Step 1: Implement**, matching today's splitting exactly: skip whitespace between statements; a `%…%` block yields one statement per non-empty `*` part, **trimmed, and a whitespace-only part yields an empty statement** (today's `split(SkipEmptyParts)` + `trimmed()` keeps those, and the settings pass relies on it); a plain command runs to `*`, trimmed, empty ones skipped; an unterminated block or final command sets `truncated` and ends.
- [ ] **Step 2: Run all tests** → `test_parsergolden` PASS unchanged; `test_gerberparser`, `test_samedatacheck` PASS.
- [ ] **Step 3: Commit** `Split Gerber statements with a cursor that yields views`.

### Task 4: Fast-path functions in both parsers — ✅ done

**Files:**
- Modify: `src/gerber/gerberparser.h`, `src/gerber/gerberparser.cpp`
- Modify: `src/gerber/excellonparser.h`, `src/gerber/excellonparser.cpp`
- Test: `tests/unit/test_samedatacheck.cpp`

**Interfaces:**
- Consumes: `StatementCursor` (Task 3), `ExportNoise::isVolatileAttribute`, `ExportNoise::isVolatileComment`.
- Produces: `bool GerberParser::sameStatementsApartFromNoise(const QString &contentA, const QString &contentB);`
- Produces: `bool ExcellonParser::sameLinesApartFromNoise(const QString &contentA, const QString &contentB);`

Rules (Gerber): walk both cursors together; stop with `false` at the first pair that is neither (same `isParameterStatement` **and** same text) nor noise of the same kind; `false` if one side ends first, if either cursor is `truncated()`, or if either text contains U+FFFD. Noise kind of a statement: plain starting `G04` → `"G04"`; parameter with `isVolatileAttribute` → its attribute name (text up to the first `,`); parameter starting `TO`/`TD`/`LN`/`IN` → those two letters; anything else → not noise. Drill: the same over lines split at `\n` and trimmed, empty lines skipped; noise = a `;` line with `isVolatileComment(line.mid(1))` that does **not** contain `FILE_FORMAT=` (case-insensitive, as `tryParseFileFormatComment` reads it). A header comment above each function says why each noise kind is safe: the parse loop ignores it whatever it says (`tryParseDirective` only reads `%FS`/`%MO`/`G70`/`G71`; `isMacroBodyStatement` only looks at a leading digit or `$`). A comment in the parse loop at the `G04` and "anything else ignored" branches points back to the fast path.

- [ ] **Step 1: Write the failing tests** (all on the `gerber()`/`drill()` helpers):
  - `noiseOnlyDifferencesAreSameByStatements`: `kExportA` vs `kExportB` → true
  - `volatileAttributeDifferenceIsSameByStatements`: `%TF.CreationDate,2026-10-01*%` vs `…2026-10-20*%` → true
  - `objectLabelDifferenceIsSameByStatements`: `%TO.C,R12*%` vs `%TO.C,R15*%` → true
  - `windowsLineEndingsAreSameByStatements`: CRLF vs LF → true
  - `differentNoiseKindsAtOnePositionAreNotSame`: `G04 x*` vs `%TF.CreationDate,x*%`; `.CreationDate` vs `.ProjectId`; `%TO…` vs `%TD…` → all false
  - `changedCoordinateIsNotSameByStatements`, `extraCommentLineIsNotSameByStatements`, `changedFileFunctionIsNotSameByStatements` → false
  - `cutOffFilesAreNeverSameByStatements`: `gerber(kExportA) + "%FSLAX"` vs `gerber(kExportB) + "%FSLAY"`, and the identical cut-off text on both sides → false
  - `replacementCharacterFallsBack`: `X1\xFFY2D01*` vs `X1\xFEY2D01*` in the body → false
  - `sameTextWithDifferentOriginIsNotSame`: `%MOMM*%` vs `MOMM*` → false
  - `noiseStatementsDoNotChangeTheParsedObjects`: `parseContentToObjects` of the `kExportA` and `kExportB` variants with extra `%TF.CreationDate`/`%TO`/`%LN` lines → `LayerEquivalence::sameObjects` true and equal `GerberObjectDump::dump` (add `gerberobjectdump.cpp` to the `test_samedatacheck` target)
  - `drillNoiseCommentDifferenceIsSameByLines`: `kDrillExportA` vs `kDrillExportB` → true
  - `drillOrdinaryCommentDifferenceIsNotSame`: `;updated pads` vs `;other pads` → false
  - `drillNoiseCommentWithFileFormatIsNotSame`: `;date: 1 FILE_FORMAT=3:3` vs `;date: 2 FILE_FORMAT=2:4` → false
  - `drillMovedHoleIsNotSameByLines` → false
- [ ] **Step 2: Run** `test_samedatacheck` → the new tests FAIL (functions missing → build error first, then add stubs returning false → the "true" cases fail).
- [ ] **Step 3: Implement** both functions per the rules above.
- [ ] **Step 4: Run all tests** → PASS.
- [ ] **Step 5: Commit** `Add the same-data fast path: same statements apart from noise`.

### Task 5: Wire the fast path into the check — ✅ done

**Files:**
- Modify: `src/archivecompare/samedatacheck.h`, `src/archivecompare/samedatacheck.cpp:32-117`
- Modify: `src/archivecompare/samedatadiagnosis.cpp:305` (`explain`), `src/archivecompare/samedatadiagnosis.h` (header comment)
- Modify: `tests/unit/test_samedatacheck.cpp`
- Modify: `docs/superpowers/specs/2026-10-28-parser-speedup-analysis.md` (the decision line: a cut-off file is *not* among the extra greens; status line)

**Interfaces:**
- Consumes: Task 4's two functions.
- Produces: `bool SameDataCheck::isSameDataByParsing(const QByteArray &bytesA, const QByteArray &bytesB);` = today's `isSameData` body unchanged. `isSameData` = the same kind checks, then fast path (`sameStatementsApartFromNoise` for Gerber, `sameLinesApartFromNoise` for drill) → `true`, else the parsing check. Share the decoded text; don't decode twice.

- [ ] **Step 1: Switch the existing tests to the parsing check:** replace every `SameDataCheck::isSameData(` in `test_samedatacheck.cpp` with `SameDataCheck::isSameDataByParsing(`. They pin the parsing rules, which still decide every pair the fast path passes on.
- [ ] **Step 2: Write the failing tests:**
  - `refusedFeatureWithNoiseOnlyDifferenceIsSameData` (decision A): `gerber(kExportA + "G91*\n")` vs `gerber(kExportB + "G91*\n")` → `isSameData` true, `isSameDataByParsing` false
  - `realChangeIsStillDifferentThroughBothPaths`: moved pad → `isSameData` false
  - `gerberAndDrillAreNeverSameDataThroughFastPath`: same as the existing case, via `isSameData` → false
  - `diagnosisNamesTheFastPath`: `SameDataDiagnosis::explain(gerber(kExportA), gerber(kExportB))` contains `"apart from comments and export labels"`
- [ ] **Step 3: Implement**; `explain` checks the fast path first and returns `Same-data check: same statements apart from comments and export labels - same data`.
- [ ] **Step 4: Run all tests** → PASS.
- [ ] **Step 5: Commit** `Use the fast path first in the same-data check`.

### Task 6: Fast-path counters in the timing build — ✅ done

**Files:**
- Modify: `src/archivecompare/stagetiming.h` (`CheckStages`: `qint64 fastNs`, `int fastHits`, `qint64 slowOnHitsNs`, `int disagreements`, `int greensMissed`), `src/archivecompare/stagetiming.cpp` (one line + dash list)
- Modify: `src/archivecompare/samedatacheck.cpp` (`#ifdef PCB_TIMING` only)

**Interfaces:**
- In the timing build `isSameData` runs the fast path (timed into `fastNs`) **and** the parsing check on every pair; it returns exactly what the normal build returns (`fast || slow`). `disagreements` = fast true and slow false; `greensMissed` = fast false and slow true; `slowOnHitsNs` = parsing time on fast hits (the time the normal build saves). The fingerprint (Task 2) still covers the parsing check of every pair.
- Report line: `Fast path: hits %1 of %2, %3 ms; parsing on hits %4 ms (saved in normal build); disagreements %5; greens missed %6`.

- [ ] **Step 1: Implement.**
- [ ] **Step 2: Timing build, run on a made-up re-saved pair** → hits 1, disagreements 0; on a `G91` noise-only pair → disagreements 1.
- [ ] **Step 3: Commit** `Count the fast path's hits and disagreements in the timing readout`.

### Task 7: Fix A — fewer, cheaper pattern runs — ⏭ next

**Files:**
- Modify: `src/gerber/gerberparser.cpp:1072` (`classifyPlain`) and the three `match` calls in it

- [ ] **Step 1: Implement:** try `kApertureSelectForm` only when `plain` starts with `D` or `G` (comment: the pattern is anchored on `(G54)?D`); pass `QRegularExpression::DontCheckSubjectStringMatchOption` to the three `match` calls (comment: every caller decodes with `fromUtf8` or `QTextStream`, so the text is valid UTF-16, the option's precondition).
- [ ] **Step 2: Run all tests** → `test_parsergolden` unchanged, all PASS.
- [ ] **Step 3: Benchmark** `build-parsebench` (`parsebench.exe`) → settings pass faster than before; note the numbers in the commit body.
- [ ] **Step 4: Commit** `Run the settings pass's patterns less often`.

### Task 8: Fix B — X/Y lane in both passes

**Files:**
- Modify: `src/gerber/gerberparser.cpp:663-910` (parse loop), `:1026-1202` (`classifyPlain`), `:1491-1524` (settings loop)

**Interfaces:**
- Produces (internal): `void applyCoordinateCommand(const QString &text, ParserState &state);`, today's coordinate section (`:823-909`) moved out unchanged. Parse loop: a plain statement starting with `X` or `Y` calls it right after the `isParameterStatement` checks and `continue`s; the existing fall-through calls it too.
- Produces (internal): `PlainVerdict classifyOperation(const QString &plain, const QString &view, GeometryWalk &walk);`, `classifyPlain` from "Work out the statement's operation" on. Settings loop: a plain statement starting with `X` or `Y` still runs the `G91` check, then calls `classifyOperation(plain, plain, walk)` directly.
- A comment at both lanes lists the checks they skip and why none applies to X/Y, and warns that a new check added above them must also be considered for X/Y statements.

- [ ] **Step 1: Implement.**
- [ ] **Step 2: Run all tests** → `test_parsergolden` unchanged, all PASS.
- [ ] **Step 3: Benchmark**, numbers in the commit body.
- [ ] **Step 4: Commit** `Send X/Y statements straight to coordinate handling`.

### Task 9: Fix C — decode coordinates without copies

**Files:**
- Modify: `src/gerber/gerberformat.h:26-35`, `src/gerber/gerberformat.cpp:18-95`, `src/gerber/gerberparser.cpp:453-485` (`applyCoordinateToken`)
- Test: `tests/unit/test_gerberformat.cpp`

**Interfaces:**
- Produces: `double GerberFormat::decodeCoordinateMm(QStringView token) const;` and `qint64 decodeCoordinateNm(QStringView token) const;` as new overloads. The `const QString &` versions stay and forward, so `decodeCoordinateMm("021363")` in tests and the drill parser keep compiling. `extractDigits` takes `QStringView`; the sign is skipped with `mid(1)`. The trailing-zero (`T`) padding branch keeps building a `QString` as today (rare format; exact overflow behaviour preserved). `applyCoordinateToken` passes `QStringView(token).mid(...)`.

- [ ] **Step 1: Write the failing test** `viewAndStringDecodeAgree`: for `"021363"`, `"-021363"`, `"+5"`, `"0"`, `""`, `"1234567890123"`, `"99999999999999999999"`, in `L` and `T` formats, mm and inch: `decodeCoordinateMm(QStringView(s)) == decodeCoordinateMm(s)` and the same for `Nm`. (Fails to build until the overloads exist.)
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Run all tests** → `test_parsergolden` unchanged, all PASS.
- [ ] **Step 4: Benchmark**, numbers in the commit body.
- [ ] **Step 5: Commit** `Decode coordinates from views instead of copies`.
- [ ] **Step 6: Build the final timing zip** `build-dist/PcbSupplierWorkflow-1.8.9-timing-fastpath-win64.zip`. Handoff to Igor: run the **baseline zip (Task 2) and this zip** on the same 4 cases in one sitting, and paste both readouts. Pass = identical `Result fingerprint` per case, disagreements only for refused-feature files, and lower times. A changed fingerprint → per-fix zips (Tasks 3, 7, 8, 9) to find the cause.
