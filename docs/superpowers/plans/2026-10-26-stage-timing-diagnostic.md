# Stage-Timing Diagnostic Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the "Render time" / "Compare time" labels, and add a temporary `PCB_TIMING` build that puts a copyable per-stage timing readout in Archive Compare's detail card.

**Architecture:** One CMake option, `PCB_TIMING`, adds a small `StageTiming` module (data structs, memory read, text formatting) and `#ifdef PCB_TIMING` blocks in `ArchiveComparePage` that wrap existing calls in `QElapsedTimer`s. Nothing is reordered or restructured. Everything behind the switch is deleted before the branch merges.

**Tech Stack:** C++20, Qt 6.8.2 Widgets (`QPlainTextEdit`, `QClipboard`, `QElapsedTimer`), Windows `GetProcessMemoryInfo` (psapi), CMake.

**Spec:** `docs/superpowers/specs/2026-10-26-stage-timing-diagnostic-design.md`

## Global Constraints

- Branch `multi-threading`. Never commit to `main`.
- Everything timing-related sits in `#ifdef PCB_TIMING` blocks (or in files built only when `PCB_TIMING` is ON), each marked with the comment `TIMING-DIAG`.
- The readout contains **numbers, sizes and counts only - never a file name, path or file content.**
- No unit tests for the timing code (spec section 2, non-goals). Verification is: normal build + full `ctest` still pass, and manual runs of the timing build.
- Archive Compare's behaviour (colours, pairs, overlay) is identical with the switch on or off. Timers wrap existing calls; no stage is reordered, merged or skipped.
- No new DLL, no new Qt module.
- A value with nothing to measure shows `-`, never `0`.
- Build commands (PowerShell, from repo root; Qt `bin` on `PATH` for tests; close the app before rebuilding; never pipe the build):
  - normal: `cmake --build build --config Debug` then `ctest --test-dir build -C Debug --output-on-failure`
  - timing: `cmake -S . -B build-timing "-DCMAKE_PREFIX_PATH=C:/Qt/6.8.2/msvc2022_64" "-DPCB_TIMING=ON"` then `cmake --build build-timing --config Debug`

## Review Focus

1. **A comparison that stops early** (only one archive set, or one unreadable): the readout must show dashes, never the previous run's numbers. Manual check in Task 3.
2. **No yellow pairs at all** (identical archives, or only real changes): `Parse speed` shows `-`, the check lines show `0 ms`, nothing divides by zero. Manual check in Task 3.
3. **Clicking a pair that cannot be drawn** (read failure, parse failure, nothing to draw): the click section still fills; `Render` shows `-`. Manual check in Task 4.
4. **Zoom, pan and resize after a click** update only `Render` (with the new canvas size) and the click memory line, not read/parse. Manual check in Task 4.
5. **Both switches on** (`PCB_DIAGNOSTICS` + `PCB_TIMING`): the title names both, and `SameDataDiagnosis::explain` stays outside every click timer. Manual check in Task 4.

---

### Task 1: Remove the "Render time" and "Compare time" labels (every build)

**Files:**
- Modify: `src/archivecompare/archivecomparepage.h:77-78`
- Modify: `src/archivecompare/archivecomparepage.cpp` (constructor detail card ~224-251, `runComparison` 300 / 368-380 / 394-398 / 412-416, `renderShownPair` 631-645)
- Modify: `docs/superpowers/specs/idea_multi-threading.md`, `docs/superpowers/specs/2026-07-22-archive-compare-1b-visual-redesign-design.md`, `docs/superpowers/specs/2026-07-18-gerber-render-v2-m4-overlay-design.md`

**Interfaces:**
- Produces: the detail card's `detailCardLayout` holds only the three legend rows (plus the `PCB_DIAGNOSTICS` `sameDataLabel_`). Task 2 appends to this layout.
- `ArchiveComparison::Outcome::compareMs` stays (Task 3 reads it).

- [ ] **Step 1: Delete `renderTimeLabel_` and `compareTimeLabel_`**: members, construction, every `setText`, the `firstLine`/`secondLine` `QHBoxLayout`s (the three legend rows become plain `detailCardLayout->addWidget(makeColorLegendRow(...))` calls in A, B, same order), `sameDataTimer`/`sameDataMs`, and the `QElapsedTimer` in `renderShownPair`. Drop `#include <QElapsedTimer>` if nothing else uses it. Rewrite the comments that called these labels "the measurement before any threading work" (constructor's Detail-card comment, `renderShownPair`'s comment) to say timing now lives in the `PCB_TIMING` build (spec: `2026-10-26-stage-timing-diagnostic-design.md`).

- [ ] **Step 2: Build and run all tests**

Run: `cmake --build build --config Debug` (check `$LASTEXITCODE` is 0), then `ctest --test-dir build -C Debug --output-on-failure`
Expected: build succeeds; all 18 tests pass.

- [ ] **Step 3: Manual check.** Run `build/Debug/PcbSupplierWorkflow.exe`, compare two archives, click a pair: the detail card shows only the three legend rows, and the overlay draws as before.

- [ ] **Step 4: Docs.** In `idea_multi-threading.md`, change the two mentions of the labels (section 2 "The Compare time label shows..." and "Overlay render" paragraph) to past tense plus "removed on this branch; see the stage-timing design". In the 1b redesign and M4 overlay design docs, add one line under the status line: `Note (2026-10-26): the render-time and compare-time labels were removed on branch multi-threading; timing lives in the PCB_TIMING build (2026-10-26-stage-timing-diagnostic-design.md).`

- [ ] **Step 5: Commit**

```powershell
git add src/archivecompare/archivecomparepage.h src/archivecompare/archivecomparepage.cpp docs/superpowers/specs
git commit -m "Remove the render-time and compare-time labels"
```

---

### Task 2: `PCB_TIMING` switch, window title, timing box and Copy button

**Files:**
- Modify: `CMakeLists.txt` (after the `PCB_DIAGNOSTICS` block, ~line 150)
- Create: `src/archivecompare/stagetiming.h`, `src/archivecompare/stagetiming.cpp`
- Modify: `src/app/mainwindow.cpp:61-69`
- Modify: `src/archivecompare/archivecomparepage.h`, `src/archivecompare/archivecomparepage.cpp` (constructor detail card)

**Interfaces:**
- Produces (in `stagetiming.h`, namespace `StageTiming`, built only with `PCB_TIMING`):

```cpp
struct MemoryUse {
    qint64 nowBytes = -1;  // working set; -1 = could not be read
    qint64 peakBytes = -1; // peak working set over the process's whole life
};

struct CompareTimes {
    qint64 totalMs = 0;
    qint64 zipMs = 0;            // outcome.compareMs
    qint64 typesMs = 0;          // both resolveKinds calls
    qint64 checkReadMs = 0;      // readEntryBytes inside the predicate
    qint64 checkParseMs = 0;     // isSameData inside the predicate
    qint64 overlayClassMs = 0;   // classifyPairs
    int pairsChecked = 0;        // predicate calls
    int pairsGreen = 0;          // predicate returned true
    qint64 parsedBytes = 0;      // both files, every checked pair
    qint64 greenParseMs = 0;     // isSameData time, green pairs only
    qint64 greenBytes = 0;       // both files, green pairs only
    MemoryUse memory;
};

struct ClickTimes {
    qint64 readMs = 0;
    qint64 parseMs = 0;
    qint64 renderMs = -1;        // -1 = nothing rendered yet -> "-"
    QSize renderPx;              // size of the composited image
    MemoryUse memory;
};

MemoryUse readMemoryUse();
QString formatReport(const std::optional<CompareTimes> &compare,
                     const std::optional<ClickTimes> &click);
```

- Produces (page, under `#ifdef PCB_TIMING`): members `QPlainTextEdit *timingBox_`, `std::optional<StageTiming::CompareTimes> lastCompare_`, `std::optional<StageTiming::ClickTimes> lastClick_`, and `void showTiming();` which sets `timingBox_`'s text to `StageTiming::formatReport(lastCompare_, lastClick_)`.

- [ ] **Step 1: CMake.** Add, with a comment in the style of the `PCB_DIAGNOSTICS` block (temporary, deleted before merge, own build directory):

```cmake
option(PCB_TIMING "Show per-stage timing in Archive Compare (temporary, multi-threading branch)" OFF)
if(PCB_TIMING)
    target_sources(PcbSupplierWorkflow PRIVATE
        src/archivecompare/stagetiming.h
        src/archivecompare/stagetiming.cpp)
    target_compile_definitions(PcbSupplierWorkflow PRIVATE PCB_TIMING)
    target_link_libraries(PcbSupplierWorkflow PRIVATE psapi)
endif()
```

- [ ] **Step 2: `stagetiming.h/.cpp`.** The interface above. `readMemoryUse()` uses `GetProcessMemoryInfo(GetCurrentProcess(), ...)` (`<windows.h>`, `<psapi.h>`), `WorkingSetSize` / `PeakWorkingSetSize`; on failure both stay -1. `formatReport` produces exactly the spec's text block (section 4, "Text format"), one value per line, with these rules:
  - MB = bytes / 1,048,576, one decimal. Memory as `now/peak MB`, or `-` if -1.
  - `Everything else` = `totalMs - (zipMs + typesMs + checkReadMs + checkParseMs + overlayClassMs)`.
  - `Parse speed` = `greenParseMs / (greenBytes in MB)`, rounded to whole ms/MB; `-` when `greenBytes == 0`.
  - `Render: <ms> ms @ <w>x<h> px`, or `Render: -` when `renderMs < 0`.
  - A missing `compare` → every "Last compare" value is `-`; same for `click`.

- [ ] **Step 3: Window title** in `mainwindow.cpp`: four cases from the two switches: `PCB Gadget` / `Diagnostic build %1` / `Timing build %1` / `Diagnostic + timing build %1` (`%1` = `QCoreApplication::applicationVersion()`).

- [ ] **Step 4: Box and button** in the page constructor, appended to `detailCardLayout` after the legend rows (and after `sameDataLabel_` if present): a row with `timingBox_` (read-only `QPlainTextEdit`, `QFontDatabase::systemFont(QFontDatabase::FixedFont)`, no line wrap, fixed height of 4 text lines + frame from its `fontMetrics()`) and a `QPushButton(tr("Copy"))` that does `QGuiApplication::clipboard()->setText(timingBox_->toPlainText())`. Call `showTiming()` once at the end of the constructor so the box starts with dashes.

- [ ] **Step 5: Build both and run tests**

Run: `cmake --build build --config Debug`, `ctest --test-dir build -C Debug --output-on-failure`, then configure and build `build-timing` (Global Constraints).
Expected: both builds succeed; all tests pass.

- [ ] **Step 6: Manual check.** Normal build: no box, title `PCB Gadget`. Timing build: title `Timing build 1.8.9 - PCB Gadget`, a 4-line box full of dashes under the legend that scrolls, and Copy puts all of it on the clipboard (paste into Notepad: both sections present).

- [ ] **Step 7: Commit**

```powershell
git add CMakeLists.txt src/archivecompare/stagetiming.h src/archivecompare/stagetiming.cpp src/app/mainwindow.cpp src/archivecompare/archivecomparepage.h src/archivecompare/archivecomparepage.cpp
git commit -m "Add the PCB_TIMING build with an empty timing readout"
```

---

### Task 3: Time the comparison

**Files:**
- Modify: `src/archivecompare/archivecomparepage.cpp` (`runComparison`)

**Interfaces:**
- Consumes: `StageTiming::CompareTimes`, `readMemoryUse()`, `lastCompare_`, `lastClick_`, `showTiming()` (Task 2).

- [ ] **Step 1: Wrap the stages** in `runComparison`, each addition in a `#ifdef PCB_TIMING // TIMING-DIAG` block:
  - start a total `QElapsedTimer` on the first line; reset `lastCompare_` and `lastClick_` and call `showTiming()` there, so every early return leaves dashes;
  - `zipMs = outcome.compareMs`;
  - one timer around both `resolveKinds` calls → `typesMs`;
  - inside the same-data predicate: time the two `readEntryBytes` → add to `checkReadMs`; time `isSameData` → add to `checkParseMs`; `++pairsChecked`; add both sizes to `parsedBytes`; if the result is true: `++pairsGreen`, add the `isSameData` time to `greenParseMs` and both sizes to `greenBytes`. The predicate must still return exactly what it returns today (keep `foundA && foundB && isSameData(...)` short-circuit semantics: when a read fails, `isSameData` is not called and not timed);
  - one timer around `classifyPairs` → `overlayClassMs`;
  - after `updateNotDrawableLabel(...)`, the last line: `totalMs`, `memory = readMemoryUse()`, store into `lastCompare_`, `showTiming()`.

- [ ] **Step 2: Build both and run tests** (as Task 2 Step 5). Expected: both builds succeed; all tests pass.

- [ ] **Step 3: Manual check, timing build.**
  - Two archives with re-saved Gerbers: every "Last compare" line has a number; `Pairs checked` ≥ `turned green`; `Everything else` is not negative.
  - Two identical archives: `Pairs checked: 0`, check lines `0 ms`, `Parse speed: -`.
  - Then change A to a non-zip/unreadable file: the box goes back to dashes.
  - Compare colours and pair list with the normal build on the same archives: identical.

- [ ] **Step 4: Commit**

```powershell
git add src/archivecompare/archivecomparepage.cpp
git commit -m "Time each stage of the comparison in the timing build"
```

---

### Task 4: Time the pair click and the render

**Files:**
- Modify: `src/archivecompare/archivecomparepage.cpp` (`showSelectedPair`, `renderShownPair`)

**Interfaces:**
- Consumes: `StageTiming::ClickTimes`, `readMemoryUse()`, `lastClick_`, `showTiming()` (Task 2).

- [ ] **Step 1: `showSelectedPair`.** Time the two `readEntryBytes` → `readMs`; time the two `parseGerberOrDrill` → `parseMs`. The `PCB_DIAGNOSTICS` `SameDataDiagnosis::explain` block sits between them and must stay outside both timers. Store a fresh `ClickTimes` (renderMs = -1, memory = `readMemoryUse()`) into `lastClick_` and call `showTiming()` **before** `fitTo(...)`, so a pair that never renders still shows its read/parse.

- [ ] **Step 2: `renderShownPair`.** Time from the start of the mask rendering to after `MaskCompositor::composite`; keep the composited `QImage` in a local so its `size()` becomes `renderPx`. If `lastClick_` has a value: set `renderMs`, `renderPx`, `memory = readMemoryUse()`, `showTiming()`. The early return (both layers empty) changes nothing.

- [ ] **Step 3: Build both and run tests** (as Task 2 Step 5). Expected: both builds succeed; all tests pass.

- [ ] **Step 4: Manual check, timing build.**
  - Click a pair: Read, Parse, `Render: N ms @ WxH px`, memory all filled.
  - Zoom, pan, resize the window: only Render (with new px size) and click memory change.
  - Click a pair with nothing drawable on either side: Read/Parse filled, `Render: -`.
  - A new comparison resets "Last click" to dashes.
  - Configure a third directory with both `-DPCB_DIAGNOSTICS=ON` and `-DPCB_TIMING=ON`: title `Diagnostic + timing build 1.8.9`, yellow-reason text and timing box both present.

- [ ] **Step 5: Commit**

```powershell
git add src/archivecompare/archivecomparepage.cpp
git commit -m "Time the pair click and the overlay render in the timing build"
```
