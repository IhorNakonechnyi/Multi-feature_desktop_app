# Same-data check review fixes (v1.8.7) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the false greens (points 1-4) and the real-data false yellows (9, 11, 12, 13) found
in the review of the same-data check, fix the cross-pair colours and summary count (5, 6, 7), and
ship a 1.8.7 test build from the `issue_resaving` worktree.

**Architecture:** The "text left over after geometry" lists (6a) stop guessing what geometry is.
A statement only counts as geometry if it has exactly a form the parser fully reads; everything
else stays as text that must match. Content the parser cannot vouch for at all makes the list
function return "no answer" (`std::nullopt`), and the pair stays yellow. The macro end rule is
shared between the parser and 6a. The pair collapse becomes a one-to-one matching.

**Tech Stack:** Qt 6.8.2 (Core, `QRegularExpression`), C++20 (`std::optional`), QtTest, CMake/MSVC.

**Spec:** `docs/superpowers/plans/code-review.md` (findings, real-data results, decisions in
section 6). Read it first; point numbers below refer to it.

## Global Constraints

- The one promise: **may wrongly leave a file yellow, must never wrongly make it green.** When a
  rule is unsure, it keeps the line as text or refuses.
- No real customer, supplier or spec names anywhere (code, tests, comments, commit messages).
  Test data uses generic names (`BOX`, `ROUNDEDRECTD37` is a generic Altium naming pattern and fine).
- Work only in `.claude/worktrees/issue_resaving` on branch `issue_resaving`. **Do not merge into
  `main`.** The user tests 1.8.7 first.
- Build: `cmake --build build --config Debug > build\build.log; $LASTEXITCODE` (never pipe; `0` =
  success). Close the running app first (`LNK1168`).
- Tests: `$env:PATH = "C:\Qt\6.8.2\msvc2022_64\bin;$env:PATH"; ctest --test-dir build -C Debug -R <target> --output-on-failure`.
  Full run: same without `-R`; expect **19/19 passed**.
- Tests first for every task; stop after each task for the user to review.
- Comment density: these are parser rules, so explain each rule's *why* in plain words (see the
  "complex code style" preference: simpler structures, more explanation).

## Found while planning (covered, no new decision)

The region code (`gerberparser.cpp:739-756`) records only each draw's end point, so an **arc
inside a region** (`G03` + `I/J` between `G36`/`G37`) is parsed as a straight chord and its `I/J`
are lost. Two files whose regions differ only in arc bulge compare as same data today: another
false green. The strict geometry rule (Task 4) covers it: `I/J` only count as geometry on an arc
draw *outside* a region, so in-region arc lines stay in 6a as text. Unchanged re-exports still
turn green (the text is equal); changed bulges stay yellow.

Same rule covers `I/J` on a linear draw or a flash (the parser ignores them there too).

## Small extensions of decision 2 (flag in review)

- `%IPPOS` (image polarity positive) is the default, exactly like `.FilePolarity,Positive`, so it
  is **skipped** in 6a rather than kept. `%IPNEG` refuses.
- `%IN` (image name) is a name exactly like `%LN`, so it is **skipped** too.

If the user prefers the review doc's literal wording (keep `%IPPOS` and `%IN`), it is a one-line
change in Task 3.

## Execution: subagents, strictly one at a time

Several tasks edit the same code, so **only one implementer runs at any moment**, in task order,
all in this worktree. Each task starts from the previous task's commit.

| Shared code | Tasks, in order |
|---|---|
| `GerberParser::nonGeometryStatements` | 1 (return type) → 3 (allow-list, `inMacro`, `G91`) → 4 (geometry walk) → 5 (drop macros) → 6 (`FilePolarity`) |
| `parseMacroDefinition` / `isMacroBodyStatement` | 2 creates → 3 and 5 use |
| `normalizedGCode` + parse loop line ~623 | 3 creates → 4 uses |
| `samedatacheck.cpp` | 1 (`isSameData`) → 7 (`collapseSameDataPairs`) |
| `tests/unit/test_samedatacheck.cpp` | 1, 3, 4, 5, 6, 7 each **add** tests; only 7 replaces one (`fileStillInAnotherPairStaysYellow`) |
| `excellonparser.*` / `exportnoise.h` / `comparisonsummary.cpp` | 1 only / 6 only / 7 only |

Rules for every implementer:
- Change only the files listed in your task. Don't refactor or rename code another task names.
- **Never delete or loosen an existing test.** The only test you may replace is the one your task names.
- After a task, the controller checks `git diff --stat HEAD~1` against the task's file list and
  runs the **full** `ctest`, not just the task's target, so a later task can't silently break an
  earlier one.
- Rule order inside the `%` branch of `nonGeometryStatements` (later tasks insert, never reorder):
  skips (`AD`, `TO`, `TD`, volatile `TF`, default `FilePolarity`, `LN`, `IN`, `IPPOS`, macro
  definitions) → keeps (`FS MO SR LP TF TA`, plus `AM` until Task 5) → refuse everything else.
- Kept plain statements are stored **after** G-code normalization (`G1` and `G01` compare equal,
  since the parser treats them the same).

## File structure

| File | Change |
|---|---|
| `src/gerber/gerberparser.h/.cpp` | `nonGeometryStatements` returns `std::optional<QStringList>`; allow-list, strict geometry forms, region rule, macro skip; shared `isMacroBodyStatement` used by `parseMacroDefinition` |
| `src/gerber/excellonparser.h/.cpp` | `nonGeometryLines` returns `std::optional<QStringList>`; strict drill geometry forms; `R` refuses |
| `src/gerber/exportnoise.h` | `layer_color` drill noise word; `isDefaultFilePolarity` |
| `src/archivecompare/samedatacheck.h/.cpp` | handle `nullopt`; one-to-one collapse; unpaired partner → red; default kind `OtherData` |
| `src/archivecompare/comparisonsummary.cpp` | comment only (point 6 is fixed by the one-to-one collapse) |
| `tests/unit/test_samedatacheck.cpp` | most new tests (end-to-end through `isSameData` with the existing `gerber()`/`drill()` helpers) |
| `tests/unit/test_gerberparser.cpp` | macro end-rule test |
| `CMakeLists.txt`, `resources/app.rc`, `docs/releasing.md`, `docs/privacy-verification.md` | 1.8.7 bump |

## Review Focus

1. **Altium region with arcs** (`G36 … G03 … X..Y..I..J..D01 … G37`), unchanged re-export with
   shifted apertures: must still be green. Test in Task 4 (`unchangedRegionArcIsSameData`).
2. **Unpadded and padded codes** (`G1`, `D1`, `D001`, `G54D11`) in older exports: same data must
   stay green. Test in Task 4 (`oldStyleCodesAreSameData`).
3. **A coordinate line with an aperture code** (`X..Y..D10*`, moves without drawing): the parser
   handles it oddly, so it must stay text. Test in Task 4 (`coordinateWithApertureCodeIsText`).
4. **`%TA` directly after `%AM`**: the attribute must not become part of the macro. Test in Task 2.
5. **A file in three or more same-name pairs** (`top.gtl` vs `v1/`, `v2/`, `v3/`): exactly one
   green partner, the rest red. Test in Task 7 (`oneProvenPairLeavesOtherPartnersRed`).

---

### Task 1: "No answer" plumbing, drill strict rule, `R` refuses (point 1, drill half of 4)

**Files:**
- Modify: `src/gerber/excellonparser.h:41-49`, `src/gerber/excellonparser.cpp:349-382`
- Modify: `src/gerber/gerberparser.h:33-44`, `src/gerber/gerberparser.cpp:837-877` (signature only)
- Modify: `src/archivecompare/samedatacheck.cpp:12-45`, `samedatacheck.h:20-30` (comment)
- Test: `tests/unit/test_samedatacheck.cpp`

**Interfaces:**
- Produces: `std::optional<QStringList> ExcellonParser::nonGeometryLines(const QString &content)`
  and `std::optional<QStringList> GerberParser::nonGeometryStatements(const QString &content)`.
  `std::nullopt` = "this file contains something the parser cannot vouch for; never same data".
  Later tasks add refusals by returning `std::nullopt` from inside these functions.

- [ ] **Step 1: Write the failing tests** (drill section of `test_samedatacheck.cpp`, add slot declarations)

```cpp
void TestSameDataCheck::drillChangedSlotEndIsDifferent()
{
    // G85 slots are not understood by the drill parser: both parse to one
    // hole at the start point. The line must stay compared as text.
    QVERIFY(!SameDataCheck::isSameData(drill(kDrillExportA, "T01C0.300\n", "X1.0Y1.0G85X3.0Y1.0\n"),
                                       drill(kDrillExportB, "T01C0.300\n", "X1.0Y1.0G85X5.0Y1.0\n")));
}

void TestSameDataCheck::drillUnchangedSlotIsSameData()
{
    QVERIFY(SameDataCheck::isSameData(drill(kDrillExportA, "T01C0.300\n", "X1.0Y1.0G85X3.0Y1.0\n"),
                                      drill(kDrillExportB, "T01C0.300\n", "X1.0Y1.0G85X3.0Y1.0\n")));
}

void TestSameDataCheck::drillRepeatCommandIsNeverSameData()
{
    // R repeats are not parsed at all, so even identical lines prove nothing.
    QVERIFY(!SameDataCheck::isSameData(drill(kDrillExportA, "T01C0.300\n", "X1.0Y1.0\nR05X0.5\n"),
                                       drill(kDrillExportB, "T01C0.300\n", "X1.0Y1.0\nR05X0.5\n")));
}

void TestSameDataCheck::drillRoutWithOffsetOnLinearMoveIsText()
{
    // G01 ignores I/J in the parser, so a G01 line with I/J stays text.
    const QString routA = "G00X1.0Y1.0\nM15\nG01X2.0Y1.0I0.5J0.0\nM16\nG05\n";
    const QString routB = "G00X1.0Y1.0\nM15\nG01X2.0Y1.0I0.7J0.0\nM16\nG05\n";
    QVERIFY(!SameDataCheck::isSameData(drill(kDrillExportA, "T01C0.300\n", routA),
                                       drill(kDrillExportB, "T01C0.300\n", routB)));
}
```

- [ ] **Step 2: Run, verify they fail**

Build, then `ctest ... -R test_samedatacheck`. Expected: the four new tests FAIL (the first,
third and fourth return "same"; the second may already pass, which is fine).

- [ ] **Step 3: Implement**

- Change both return types to `std::optional<QStringList>`; Gerber body unchanged for now except
  `return kept;`. Update both header comments: what `nullopt` means.
- `SameDataCheck::isSameData`: `if (!restA || !restB || *restA != *restB) return false;` for
  both kinds (compute once, compare). Update the header comment's condition 2.
- `nonGeometryLines`, in order, after the comment and tool-line branches:
  1. A line matching `^R\d` → `return std::nullopt;` with a comment naming why (repeat not parsed).
  2. Geometry (left out) only when the whole line matches one of:
     - `^(X[+-]?[\d.]+)?(Y[+-]?[\d.]+)?$` with at least one of X/Y (hole or modal rout move),
     - `^G0[0-3](X[+-]?[\d.]+)?(Y[+-]?[\d.]+)?$`,
     - `^G0[23](X[+-]?[\d.]+)?(Y[+-]?[\d.]+)?(I[+-]?[\d.]+)?(J[+-]?[\d.]+)?$` (arc offsets only on arcs).

     Use one `static const QRegularExpression` per form. Everything else falls through to
     `kept << line` (so `G85` lines and any unknown coordinate form are compared as text).
     Remove the old "contains X or Y" test.

- [ ] **Step 4: Run, verify all pass**

`ctest ... -R test_samedatacheck` and `-R test_excellonparser`: all PASS (existing drill tests
included).

- [ ] **Step 5: Commit**

```
git add src/gerber/excellonparser.* src/gerber/gerberparser.* src/archivecompare/samedatacheck.* tests/unit/test_samedatacheck.cpp
git commit -m "Same-data check: drill geometry only in forms the parser reads, refuse R repeats"
```

---

### Task 2: Macro body ends at the first non-primitive statement (side finding of 9)

**Files:**
- Modify: `src/gerber/gerberparser.cpp:352-378`
- Test: `tests/unit/test_gerberparser.cpp`

**Interfaces:**
- Produces (anonymous namespace in `gerberparser.cpp`, above `parseMacroDefinition`):
  `bool isMacroBodyStatement(const GerberStatement &statement)`: true for a parameter
  statement whose first character is a digit or `$`. Task 3 and Task 5 use it.

- [ ] **Step 1: Write the failing test**

```cpp
void TestGerberParser::macroBodyStopsAtApertureAttribute()
{
    const QString content = "%FSLAX46Y46*%\n%MOMM*%\n"
                            "%AMBOX*\n21,1,$1,$2,0,0,0*%\n"
                            "%TA.AperFunction,SMDPad*%\n"
                            "%ADD10BOX,1X2*%\n"
                            "D10*\nX0Y0D03*\nM02*\n";
    const Layer layer = GerberParser::parseContentToObjects(content);
    QVERIFY(layer.macros.contains("BOX"));
    QCOMPARE(layer.macros.value("BOX").statements, QStringList({"21,1,$1,$2,0,0,0"}));
}
```

- [ ] **Step 2: Run, verify it fails** (`-R test_gerberparser`): FAIL, the `TA...` statement is in the body.

- [ ] **Step 3: Implement.** `parseMacroDefinition`'s loop continues while
  `index < size && isMacroBodyStatement(statements.at(index))`; drop the `FS/MO/AD/LP/AM` list.
  Comment: why only digit/`$` lines (primitive codes and `$n=` assignments, Gerber spec 4.5).

- [ ] **Step 4: Run, verify all pass**: `test_gerberparser`, `test_gerbermacro`, `test_layermaskrenderer`, `test_samedatacheck`.

- [ ] **Step 5: Commit** `"Gerber parser: a macro body ends at the first non-primitive statement"`

---

### Task 3: Allow-list of `%` commands, `G91` refuses, `%LN`/`%IN`/`%IPPOS` skipped (points 3, 13)

**Files:**
- Modify: `src/gerber/gerberparser.cpp` (`nonGeometryStatements`), `src/gerber/gerberparser.h` (comment)
- Test: `tests/unit/test_samedatacheck.cpp`

**Interfaces:**
- Consumes: `isMacroBodyStatement` (Task 2), `std::optional` return (Task 1).

- [ ] **Step 1: Write the failing tests**

```cpp
void TestSameDataCheck::ignoredTransformDirectiveIsNeverSameData()
{
    // %LR is ignored by the parser, so even in both files it proves nothing:
    // where it sits relative to the objects is not compared.
    const QString body = "D10*\nX1000000Y1000000D03*\n%LR90*%\nX2000000Y1000000D03*\n";
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA, body), gerber(kExportB, body)));
    for (const QString &directive : {"%LMX*%\n", "%SFA2B2*%\n", "%OFA1B0*%\n", "%MIA1B0*%\n",
                                     "%ASAYBX*%\n", "%IPNEG*%\n", "%IR90*%\n", "%ABD12*%\n"}) {
        QVERIFY2(!SameDataCheck::isSameData(gerber(kExportA + directive), gerber(kExportB + directive)),
                 qPrintable(directive));
    }
}

void TestSameDataCheck::incrementalCoordinatesAreNeverSameData()
{
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA + "G91*\n"), gerber(kExportB + "G91*\n")));
}

void TestSameDataCheck::oldFileHeaderDirectivesAreSameData()
{
    // Older exports: image polarity positive (the default), and layer/image
    // names that differ per export.
    QVERIFY(SameDataCheck::isSameData(gerber(kExportA + "%IPPOS*%\n%LNfirst.gbr*%\n%INjob1*%\n"),
                                      gerber(kExportB + "%LNsecond.gbr*%\n%INjob2*%\n")));
}

void TestSameDataCheck::strayMacroPrimitiveIsNeverSameData()
{
    // A digit-led parameter statement outside any %AM is not something the
    // parser reads.
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA + "%1,1,0.5,0,0*%\n"),
                                       gerber(kExportB + "%1,1,0.5,0,0*%\n")));
}
```

Also keep `changedDirectiveTheParserIgnoresIsDifferent` (`%LR0` vs `%LR90`): must still pass.

- [ ] **Step 2: Run, verify they fail**: `ignoredTransform…`, `incremental…`, `oldFileHeader…`, `strayMacro…` FAIL.

- [ ] **Step 3: Implement** in `nonGeometryStatements`, for parameter statements:
  - Track `bool inMacro`: set after an `AM` statement, cleared at the first statement for which
    `isMacroBodyStatement` is false. Body statements while `inMacro` are kept as today (Task 5 drops them).
  - Skipped (allowed, not kept): `AD`, `TO`, `TD`, volatile `TF` (existing), `LN…`, `IN…`, exactly `IPPOS`.
  - Kept (allowed): statements starting with `FS`, `MO`, `AM`, `SR`, `LP`, `TF`, `TA`.
  - Anything else, including a digit/`$` statement outside a macro → `return std::nullopt;`.

  For plain statements: after the existing `G04` skip, normalize a one-digit G-code the same way
  the parse loop does (`G1` → `G01`; share it as a small helper `normalizedGCode(QString)` in the
  anonymous namespace and call it from both places). Then a statement starting with `G91` →
  `return std::nullopt;`.

  Update the header comment: list the allow-list and say why unknown means "no answer" (position
  relative to the objects is not compared, so equal text proves nothing).

- [ ] **Step 4: Run, verify all pass**: `test_samedatacheck`, `test_gerberparser`.

- [ ] **Step 5: Commit** `"Same-data check: allow-list of Gerber directives, refuse what the parser ignores"`

---

### Task 4: Strict Gerber geometry forms and the region outline rule (points 2, 4)

**Files:**
- Modify: `src/gerber/gerberparser.cpp` (`nonGeometryStatements`)
- Test: `tests/unit/test_samedatacheck.cpp`

**Interfaces:**
- Consumes: `normalizedGCode` (Task 3), `std::optional` return (Task 1).

- [ ] **Step 1: Write the failing tests**

```cpp
void TestSameDataCheck::secondRegionOutlineIsNeverSameData()
{
    // A real second outline: D02 to a new start, then D01 draws. The parser
    // drops the second start point, so the region cannot be vouched for.
    const QString region = "G36*\nX0Y0D02*\nX1000000Y0D01*\nX1000000Y1000000D01*\n"
                           "X2000000Y2000000D02*\nX3000000Y2000000D01*\nX3000000Y3000000D01*\nG37*\n";
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA, region), gerber(kExportB, region)));
}

void TestSameDataCheck::altiumTrailingRegionMoveIsSameData()
{
    // Altium ends every region with a D02 and no D01 after it (real data Q1).
    const QString region = "G36*\nX0Y0D02*\nX1000000Y0D01*\nX1000000Y1000000D01*\nX0Y0D01*\n"
                           "X0Y0D02*\nG37*\n";
    QVERIFY(SameDataCheck::isSameData(gerber(kExportA, region), gerber(kExportB, region)));
}

void TestSameDataCheck::unchangedRegionArcIsSameData()
{
    const QString region = "G75*\nG36*\nX0Y0D02*\nG01X1000000Y0D01*\n"
                           "G03X1000000Y1000000I0J500000D01*\nG01X0Y0D01*\nG37*\n";
    QVERIFY(SameDataCheck::isSameData(gerber(kExportA, region), gerber(kExportB, region)));
}

void TestSameDataCheck::changedRegionArcBulgeIsDifferent()
{
    const QString a = "G75*\nG36*\nX0Y0D02*\nG01X1000000Y0D01*\n"
                      "G03X1000000Y1000000I0J500000D01*\nG01X0Y0D01*\nG37*\n";
    QString b = a;
    b.replace("I0J500000", "I-500000J500000");
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA, a), gerber(kExportB, b)));
}

void TestSameDataCheck::bareCoordinateBeforeAnyOperationIsText()
{
    // The parser ignores coordinates before the first D01/D02/D03.
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA, "D10*\nX1000000Y1000000*\nX0Y0D03*\n"),
                                       gerber(kExportB, "D10*\nX3000000Y1000000*\nX0Y0D03*\n")));
}

void TestSameDataCheck::coordinateWithApertureCodeIsText()
{
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA, "D10*\nX1000000Y0D10*\nX0Y0D03*\n"),
                                       gerber(kExportB, "D10*\nX2000000Y0D10*\nX0Y0D03*\n")));
}

void TestSameDataCheck::oldStyleCodesAreSameData()
{
    // Unpadded/padded codes and the G54 prefix, in a file with shifted apertures.
    const QString a = "G54D10*\nX1000000Y1000000D3*\nG1X0Y0D2*\nD11*\nX5000000Y0D001*\n";
    QByteArray b = gerber(kExportB, a);
    b.replace("%ADD10C,0.200*%", "%ADD12C,0.200*%");
    b.replace("G54D10*", "G54D12*");
    QVERIFY(SameDataCheck::isSameData(gerber(kExportA, a), b));
}
```

- [ ] **Step 2: Run, verify they fail**: `secondRegionOutline…`, `changedRegionArcBulge…`,
  `bareCoordinateBefore…`, `coordinateWithApertureCode…` FAIL (the others may pass already; they
  guard against the new rule being too strict).

- [ ] **Step 3: Implement.** Replace the "contains X/Y/I/J/D" test with a small state walk,
  in file order, over the plain statements (after `G04` skip, G-code normalization, `G91` refuse):

  State: `interpolation` (Linear / Arc, set by a leading `G01` / `G02`/`G03`, default Linear),
  `bool operationSeen` (an explicit D01/D02/D03 has been met), `int modalOperation`,
  `bool insideRegion`, `bool regionDrawn` (a D01 since the region's current start),
  `bool regionMovedAfterDraw`.

  A statement is **geometry** (left out) only if it fully matches, with `C = [+-]?\d+`:
  - **aperture select**: `^(G54)?D0*([1-9]\d+)$` with the code ≥ 10;
  - **operation**: `^(G0[123])?(XC)?(YC)?(IC)?(JC)?D0*([123])$`;
  - **modal coordinates**: `^(G0[123])?(XC)?(YC)?(IC)?(JC)?$` with at least one field, and only
    when `operationSeen` (the operation is `modalOperation`).

  And, for an operation or modal-coordinates match: `I`/`J` present is only allowed when the
  operation is D01, `interpolation` is Arc (after applying this statement's own G-code) and not
  `insideRegion`. Otherwise the statement is **not** geometry and is kept as text.

  `G36` sets `insideRegion`, clears the two region flags; `G37` clears `insideRegion`. Both stay
  kept as text, as today. Inside a region: on D01 → if `regionMovedAfterDraw` →
  `return std::nullopt;` (a real second outline), else `regionDrawn = true`; on D02 → if
  `regionDrawn` set `regionMovedAfterDraw`. Comment this with Altium's pattern
  (`G36 → D02 → D01×N → D02 → G37`) and why only a D01 *after* the extra D02 refuses.

  A standalone `G01`/`G02`/`G03` (kept as text, as today) updates `interpolation` too. Everything
  not matched stays `kept << text`.

  Comment that the forms mirror the parse loop and that a missing form only costs a yellow.

- [ ] **Step 4: Run, verify all pass**: `test_samedatacheck` (old and new), `test_gerberparser`.

- [ ] **Step 5: Commit** `"Same-data check: Gerber geometry only in forms the parser fully reads"`

---

### Task 5: Leave macro definitions out of 6a (point 9b)

**Files:**
- Modify: `src/gerber/gerberparser.cpp` (`nonGeometryStatements`), `gerberparser.h` (comment)
- Test: `tests/unit/test_samedatacheck.cpp`

**Interfaces:**
- Consumes: the `inMacro` tracking from Task 3.

- [ ] **Step 1: Write the failing tests**

```cpp
void TestSameDataCheck::macroRenamedWithShiftedApertureIsSameData()
{
    // Altium names macros after their D-code, so a shifted aperture renames
    // the macro and reorders the definitions (real data, pair test1).
    const QString a = "%AMROUNDEDRECTD37*\n21,1,$1,$2,0,0,0*%\n%AMROUNDEDRECTD38*\n1,1,$1,0,0*%\n"
                      "%ADD37ROUNDEDRECTD37,1X2*%\n%ADD38ROUNDEDRECTD38,0.5*%\n";
    const QString b = "%AMROUNDEDRECTD40*\n1,1,$1,0,0*%\n%AMROUNDEDRECTD41*\n21,1,$1,$2,0,0,0*%\n"
                      "%ADD40ROUNDEDRECTD40,0.5*%\n%ADD41ROUNDEDRECTD41,1X2*%\n";
    QVERIFY(SameDataCheck::isSameData(gerber(kExportA + a, "D37*\nX0Y0D03*\nD38*\nX1000000Y0D03*\n"),
                                      gerber(kExportB + b, "D41*\nX0Y0D03*\nD40*\nX1000000Y0D03*\n")));
}

void TestSameDataCheck::changedMacroRecipeIsDifferent()
{
    const QString a = "%AMBOX*\n21,1,$1,$2,0,0,0*%\n%ADD37BOX,1X2*%\n";
    const QString b = "%AMBOX*\n21,1,$1,$2,0,0,45*%\n%ADD37BOX,1X2*%\n";
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA + a, "D37*\nX0Y0D03*\n"),
                                       gerber(kExportB + b, "D37*\nX0Y0D03*\n")));
}
```

- [ ] **Step 2: Run, verify**: `macroRenamed…` FAILS; `changedMacroRecipe…` passes (guard).

- [ ] **Step 3: Implement.** Skip the `AM` header and its body statements (`inMacro`) instead of
  keeping them. Comment: 6b compares each *used* macro's recipe through the aperture key
  (`layerequivalence.cpp` `apertureKey`); names and definition order never change the image; an
  unused macro draws nothing.

- [ ] **Step 4: Run, verify all pass**: `test_samedatacheck`.

- [ ] **Step 5: Commit** `"Same-data check: compare macros by recipe only, not by name or order"`

---

### Task 6: `FilePolarity,Positive` equals missing; drill `Layer_Color` is noise (points 11, 12)

**Files:**
- Modify: `src/gerber/exportnoise.h`, `src/gerber/gerberparser.cpp` (`nonGeometryStatements`)
- Test: `tests/unit/test_samedatacheck.cpp`

**Interfaces:**
- Produces: `bool ExportNoise::isDefaultFilePolarity(const QString &statement)`: true for
  `TF.FilePolarity,Positive` (case-insensitive on the value), false for `…,Negative`.

- [ ] **Step 1: Write the failing tests**

```cpp
void TestSameDataCheck::positiveFilePolarityEqualsMissing()
{
    // Newer Altium writes it, older does not (real data, pair test3).
    QVERIFY(SameDataCheck::isSameData(gerber(kExportA + "%TF.FilePolarity,Positive*%\n"), gerber(kExportB)));
    QVERIFY(!SameDataCheck::isSameData(gerber(kExportA + "%TF.FilePolarity,Negative*%\n"), gerber(kExportB)));
}

void TestSameDataCheck::drillLayerColorIsNoise()
{
    QVERIFY(SameDataCheck::isSameData(drill(kDrillExportA + ";Layer_Color=9474304\n"),
                                      drill(kDrillExportB + ";Layer_Color=32768\n")));
}
```

- [ ] **Step 2: Run, verify both fail.**

- [ ] **Step 3: Implement.** Add `QStringLiteral("layer_color")` to `volatileCommentWords()` with a
  comment (display colour, never moves a hole; Altium drill header). Add `isDefaultFilePolarity`
  next to `isVolatileAttribute` with a comment (Positive is the Gerber default, so stating it and
  leaving it out mean the same image). Skip it in `nonGeometryStatements` alongside the volatile
  `TF` check. Update the file-header comment of `exportnoise.h` if it says the list is only
  "re-export noise".

- [ ] **Step 4: Run, verify all pass**: `test_samedatacheck`.

- [ ] **Step 5: Commit** `"Same-data check: positive file polarity equals none, drill layer colour is noise"`

---

### Task 7: One-to-one collapse, red unpaired partner, default kind, summary count (points 5, 6, 7)

**Files:**
- Modify: `src/archivecompare/samedatacheck.cpp:47-95`, `samedatacheck.h:35-42` (comment)
- Modify: `src/archivecompare/comparisonsummary.cpp:209-214` (comment only)
- Test: `tests/unit/test_samedatacheck.cpp` (replace `fileStillInAnotherPairStaysYellow`)

Point 6 needs no code of its own. `CrcComparator` only ever pairs class *representatives*
(`crccomparator.cpp:119-205`), so every path in a pair is a distinct content class. Once this task
makes the collapse one-to-one, `sameDataPairs.size()` is exactly the number of classes that turned
green on each side. The collapse was what went wrong, not the count.

- [ ] **Step 1: Write the failing tests**

```cpp
void TestSameDataCheck::provenPairDropsItsOtherPairs()   // replaces fileStillInAnotherPairStaysYellow
{
    // A's top.gtl matches v1; its pair with v2 goes, and v2 is left with no
    // counterpart at all -> red.
    ArchiveComparisonResult result;
    result.entriesA = {entry("top.gtl", EntryClassification::Different)};
    result.entriesB = {entry("v1/top.gtl", EntryClassification::Different),
                       entry("v2/top.gtl", EntryClassification::Different)};
    result.differingPairs = {{"top.gtl", "v1/top.gtl", PairReason::SameName},
                             {"top.gtl", "v2/top.gtl", PairReason::SameName}};
    SameDataCheck::collapseSameDataPairs(result, [](const DifferingPair &p) { return p.relativePathB == "v1/top.gtl"; });

    QVERIFY(result.differingPairs.isEmpty());
    QCOMPARE(result.sameDataPairs.size(), 1);
    QCOMPARE(classificationOf(result.entriesA, "top.gtl"), EntryClassification::Identical);
    QCOMPARE(classificationOf(result.entriesB, "v1/top.gtl"), EntryClassification::Identical);
    QCOMPARE(classificationOf(result.entriesB, "v2/top.gtl"), EntryClassification::OnlyInThisArchive);
}

void TestSameDataCheck::crossPairsCollapseToTwoGreens()
{
    // a/top + b/top on both sides: the name pass makes 4 pairs. a=a and b=b
    // are proven; the two cross pairs go and nothing is left yellow or red.
    ...entries a/top.gtl, b/top.gtl on both sides, 4 SameName pairs in order aa, ab, ba, bb;
    predicate: pathA's folder == pathB's folder.
    QVERIFY(result.differingPairs.isEmpty());
    QCOMPARE(result.sameDataPairs.size(), 2);
    all four entries Identical.
}

void TestSameDataCheck::oneProvenPairLeavesOtherPartnersRed()
{
    // top.gtl vs v1/, v2/, v3/; only v2 matches -> v2 green, v1 and v3 red.
}

void TestSameDataCheck::partnerStillPairedElsewhereStaysYellow()
{
    // A: x.gtl, y.gtl; B: x.gtl. Pairs (A x, B x) proven; (A y, B x) dropped.
    // A y.gtl also pairs with B other/y.gtl (unproven) -> A y stays yellow.
}

void TestSameDataCheck::provenFileIsNotOfferedAgain()
{
    // Count predicate calls: after top.gtl-v1 is proven, top.gtl-v2 is not tested.
    QCOMPARE(calls, 1);
}

void TestSameDataCheck::summaryAfterCollapseMatchesTheTrees()
{
    // The provenPairDropsItsOtherPairs scenario with a distinct crc32 per
    // entry, then ComparisonSummary::summarize.
    QCOMPARE(summary.gerber.identicalClasses, 1);
    QCOMPARE(summary.gerber.pairs, 0);
    QCOMPARE(summary.gerber.onlyInB, 1);
    QVERIFY(!summary.fullyIdentical());
}

void TestSameDataCheck::pairWithUnknownPathIsNeverOffered()
{
    // A pair whose path is missing from entriesA (point 7): kind defaults to OtherData.
    QCOMPARE(calls, 0);
}
```

Fill the elided bodies in the same shape as `provenPairDropsItsOtherPairs` (entries, pairs,
predicate, `QCOMPARE` per entry).

- [ ] **Step 2: Run, verify they fail.**

- [ ] **Step 3: Implement** `collapseSameDataPairs`:
  1. Walk `differingPairs` in order with two `QSet<QString> provenA, provenB`. Skip (do not offer)
     a pair if its A path is in `provenA` or its B path in `provenB`. Otherwise, if parseable
     (both kinds `GerberData`, looked up with default `DataKind::OtherData`) and `isSame`, append
     to `sameDataPairs` and insert both paths.
  2. `differingPairs` = the pairs touching no proven file.
  3. Proven files: `Different` → `Identical`.
  4. A file that was in a pair before step 2, is not proven, and is in no remaining pair:
     `Different` → `OnlyInThisArchive` (its only possible partners already matched other files).
  Comment the greedy order (first proven pair wins, the name pass's order) and why red is right.
  Update the header comment ("unless still part of another pair" no longer applies).
  In `comparisonsummary.cpp`, extend the comment above `+= sameDataPairs.size()`: pairs join
  class representatives and the collapse is one-to-one, so this counts green classes.

- [ ] **Step 4: Run, verify all pass**: `test_samedatacheck`, `test_archivecomparison`, `test_comparisonsummary`.

- [ ] **Step 5: Commit** `"Same-data check: a proven pair takes its files out of all other pairs"`

---

### Task 8: 1.8.7 test build

**Files:** `CMakeLists.txt:2`, `resources/app.rc` (FILEVERSION, PRODUCTVERSION, the two string
versions), `docs/releasing.md`, `docs/privacy-verification.md`: every `1.8.6` → `1.8.7`, same
places as commit `ac1ac2a` touched.

- [ ] **Step 1:** Bump the version strings. `git grep -n "1\.8\.6"` afterwards shows nothing
  outside history-style notes.
- [ ] **Step 2:** Full Debug build + `ctest --test-dir build -C Debug --output-on-failure`: **19/19 passed**.
- [ ] **Step 3:** Follow `docs/releasing.md` (Release build, package folder, windeployqt, step 5
  network check, zip + SHA-256) to produce `build-dist/PcbSupplierWorkflow-1.8.7-win64.zip`.
- [ ] **Step 4: Commit** `"Bump to v1.8.7 for the same-data check test round"`
- [ ] **Step 5:** Hand the zip + hash to the user with the test list: pairs `test1` (shifted
  Altium apertures, expect green), `test3` (Altium 20.0 vs 22.6, expect green), a real changed
  pair (expect yellow), an archive with same-name files in two folders (Task 7 colours).
  **Stop. No merge into `main`.**

---

### Task 9 (optional, after the user's OK on 1.8.7): share the kind lookup (point 8)

- Move `kindByPath` from `comparisonsummary.cpp` to an inline function in
  `archivecomparisonresult.h` (`QHash<QString, DataKind> kindByPath(const QVector<ArchiveEntry> &)`)
  and use it in `samedatacheck.cpp`. Share the page's "read both files" lambda with
  `showSelectedPair()` (`archivecomparepage.cpp` ~336 and ~518) as one private member function.
- No behaviour change: full `ctest` 19/19 before and after. Commit `"Tidy: share kind lookup and pair reading"`.

Point 10 (threading) is **not** in this plan: `docs/superpowers/specs/idea_multi-threading.md`.
