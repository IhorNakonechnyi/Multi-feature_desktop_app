# Idea: multi-threading for Archive Compare

**Status**: **Idea only. Nothing decided, nothing implemented.** Collected 2026-10-22 from a
discussion after the v1.8.6 same-data check (branch `issue_resaving`, commit `ac1ac2a`).
Checked against the code on 2026-10-26 (v1.8.9, branch `multi-threading`, `2836427`);
sections 2, 4, 6, 7 and 8 were corrected or extended from that read.
Numbers marked *estimate* have not been measured yet.

---

## 1. The problem

Two archives of ~25 files each, every file re-saved (re-exported) in the CAD tool. The
CRC step made all 25 pairs yellow, so the same-data check had to read and parse **50
whole files**, one after another, **on the UI thread**. Compare time: **7.3 s**.

Because the UI thread was busy for more than 5 s, Windows showed the window as
**"(Not Responding)"**.

### Why Windows does that ("window ghosting")

- Every Windows app has one **UI thread** that processes clicks, redraws and window moves.
- If it does not take messages for **~5 s**, Windows treats the window as **hung** and swaps
  it for a white, faded "ghost" copy titled "(Not Responding)".
- When the work finishes, the real window returns. The app has not crashed.

### Ways to hide it, both rejected

- **Raise the timeout**: `HKEY_CURRENT_USER\Control Panel\Desktop\HungAppTimeout` (default
  5000 ms). It is system-wide, so it affects every app on the PC. An app must not change it,
  and IT reviewers would object. No per-app version exists.
- **`DisableProcessWindowsGhosting()`**: stops the ghost for our process only. The app
  is still frozen (cannot move, resize or redraw). It hides the symptom without fixing it.

The real fix is to get heavy work **off the UI thread**.

---

## 2. Today's flow (all synchronous, all on the UI thread)

Both `ArchiveHeader`s emit `archiveChanged` on a drop or a pick, and both are connected to
`ArchiveComparePage::runComparison()`. With only one archive set it returns early, so
**nothing happens on the first drop today**. With both set:

1. `ArchiveComparison::compareArchives(pathA, pathB)`: read both zip directories, compare
   (CRC32, size), expand differing nested zips, repeat until nothing is left to expand.
   Timed as `outcome.compareMs`.
2. `ComparisonSummary::resolveKinds()`, once per side: only files whose extension is not
   conclusive are sniffed (8 KB head read each). **Not timed.**
3. **`SameDataCheck::collapseSameDataPairs()`**: walks the yellow pairs in a fixed order
   (sorted by path A, then path B) and calls `isSameData` on each pair whose two files are
   both `GerberData`. Timed as `sameDataMs`. Most likely the main cost.
4. Summary line, banner, both trees, filters.
5. `GerberDiffTrigger::classifyPairs()`: reads the head of **both files of every remaining
   pair again** to decide which pairs the overlay can draw. **Not timed.**
6. Fill the pair list.

The "Compare time" label shows `compareMs + sameDataMs` only. Steps 2, 4, 5 and 6 are part
of the freeze but not of the number, so **7.3 s understates the frozen time**.

### What one yellow pair costs today

`isSameData` (`samedatacheck.cpp`) for a Gerber pair:

- `classify()` on both files,
- `GerberParser::nonGeometryStatements()` on both: one **full pass** over each file. If the
  lists differ (the usual case for a real change), it stops here - a real change is cheap.
- `GerberParser::parseContentToObjects()` on both: a **second full pass**, then
  `LayerEquivalence::sameObjects()` builds and sorts a key per object.

Drill pairs follow the same pattern with `ExcellonParser`. So the expensive case is exactly
the problem case: **a re-saved, unchanged file is passed over twice inside the check**.

When the user clicks a pair, `showSelectedPair()` reads both files again and parses them a
**third time** (`parseGerberOrDrill`). Diagnostic builds (`PCB_DIAGNOSTICS`) add a fourth
pass (`SameDataDiagnosis::explain`).

### Reading from the zip

`ArchiveReader::readEntryBytes` / `readEntryHead` open the archive file anew for **every
single read** (`QuaZip zip(archiveFilePath)` in `readEntryWalk`), and each lookup lists the
zip's file names to resolve the path (`resolveStep`). For a file inside a nested zip, the
**whole nested zip is decompressed into memory on every read** of any entry inside it. With
25 Gerbers inside a nested zip, that container is inflated once per head read in step 2,
twice per pair in step 3, twice per pair in step 5 and twice per click. Not measured; it may
be a real share of the 7.3 s for nested archives.

### Overlay render

`renderShownPair` is synchronous and runs on every `renderRequested` from the canvas: the
first fit, and also **every zoom, pan and resize**. Two masks (`LayerMaskRenderer::render`,
a `QImage` each) plus `MaskCompositor::composite`. A very large layer could freeze there
too. Its time is already shown in the "Render time" label.

---

## 3. User workflow: idle time we can use

- Between dropping archive A and archive B: **~2 s, sometimes 3-4 s.**
- After the compare, the user reads the trees (if not all green): **~2-3 s** before
  clicking a pair.

---

## 4. Key idea: per-file fingerprint, computed on drop

The same-data check compares **pairs**, but almost all of its work is **per file**:

- the content kind (`GerberFileType::classify`),
- the non-geometry list (`nonGeometryStatements` / `nonGeometryLines`), noise removed,
  plus its `drawLeftOut` flag,
- the parsed objects, turned into sorted object runs. This is already a per-layer function:
  `sortedRuns()` in `layerequivalence.cpp`, and `sameObjects(a, b)` is just
  `sortedRuns(a) == sortedRuns(b)`. It sits in an anonymous namespace today and would have
  to be exposed.

Each file can therefore be reduced to a **fingerprint** of exactly what `isSameData`
compares. Drop A → fingerprint A's files in the background; drop B → the same for B;
compare → the CRC step finds the yellow pairs, and the same-data check becomes "are the two
fingerprints equal?", which is instant. A file that sits in several candidate pairs (the
name pass's cross product) is also fingerprinted once instead of once per pair.

### "Equal fingerprints" must mean exactly "isSameData is true"

Reading `isSameData` and `sameObjectsOrBothEmpty`, four cases make a file
**unfingerprintable** - it can never be equal to anything, not even to an identical file:

- `classify()` says `NotDrawable`,
- the non-geometry list comes back `nullopt` (the parser cannot vouch for the file),
- Excellon throws `AmbiguousFormatError`,
- the file has **no objects and `drawLeftOut` is set**. Two such files would have equal
  fingerprints, but today's check says "not same" for them. Without this rule the
  fingerprint would create a **false green**.

Everything else: fingerprint = kind + non-geometry list + sorted runs. `drawLeftOut` only
matters in the both-empty case, so it can stay out once that case is excluded. Unit tests
should run both paths (pair check and fingerprint equality) over the existing same-data test
inputs and require identical answers.

### The greedy walk stays sequential

`collapseSameDataPairs` is order-dependent on purpose: the first accepted pair claims both
files and the other pairs of those files are dropped. Fingerprints only replace the
`isSame(pair)` call inside it; the walk itself stays as it is, single-threaded and cheap.
The same holds for a parallel version without fingerprints: compute the pair answers in
parallel, then run the walk.

### Fingerprint form: CRC vs SHA-256 vs full data

The zip's CRC32 cannot be reused. It covers the raw bytes, which are exactly what differs on
a re-export. The fingerprint covers our cleaned-up form, so the app computes it itself.

| Form | Size | Strictness | Note |
|---|---|---|---|
| CRC32 | 4 bytes | weak: 4 billion values, designed for transmission errors | a chance match = **false green** |
| **SHA-256** (`QCryptographicHash`, in QtCore) | 32 bytes | accidental match treated as impossible | hashes the sorted object keys `sortedRuns` already builds; tiny next to parsing |
| Full canonical data | can be large for dense layers | 100% strict, as today | memory cost: one `QByteArray` key per object |

Leaning: SHA-256. **Open question for the user:** is a hash acceptable, or must the strict
full comparison be kept?

---

## 5. Options

### Round 1 (before the idle-time picture)

| # | Option | Pros | Cons |
|---|---|---|---|
| 1 | Whole compare on **one background thread** | no freeze; no parallel-safety concerns inside | not faster (still 7.3 s); needs busy-state UI; compare code must take paths as strings, not ask widgets |
| 2 | **Parallel pair check**, still on UI thread | much faster (*estimate* 7 s → 1-2 s on 8 cores); smallest change | window still frozen; slower PCs can still pass 5 s; no help for one huge file; parser thread-safety audit; higher peak memory |
| 3 | **1 + 2**: background thread that checks pairs in parallel | no freeze and short wait; buildable in two steps | most work of round 1 |
| 4 | Show CRC result at once, **turn rows green one by one** | fastest feeling | rows recolour and jump (yellow → red → green sort) while being read; live summary updates; against the minimal-UI preference |
| 5 | Single thread + `processEvents()` | tiny change | **not recommended**: re-entrancy bugs (new drop mid-compare), not faster, discouraged by Qt |

### Round 2 (using the idle time): current recommendation

| # | Option | Pros | Cons |
|---|---|---|---|
| A | **Fingerprint on drop**, background, parallel per file. Wait after dropping B ≈ B's parse only (*estimate* ~1 s) | uses idle time; no freeze anywhere; the compare becomes trivial | parses all drawable files, even ones that turn green by CRC (wasted, but in idle time; CPU busy a few seconds); must cancel or ignore jobs when an archive is replaced; nested zips need a decision (below); needs a new "one archive dropped" hook, since today the first drop does nothing |
| B | **A + keep the parsed layers** for the overlay | removes the third parse on click; clicks faster | **memory**: dense layers can be hundreds of thousands of objects; ~50 layers kept could be large. Fallback: keep only yellow-pair layers after the compare |
| C | **B + pre-render** the overlay of yellow pairs while the user reads the trees (top of list first) | first view of a pair instant | depends on canvas size and zoom (resize/zoom/pan re-renders anyway); images cost memory; most code for least gain |
| - | Baseline: round-1 option 3 | simpler: no caching or cancellation | waits for both archives' parsing after the second drop; repeated parses stay |

**Suggested order:** measure → A → B (if memory allows) → C only if clicks still feel slow.

### Savings that need no threads (found in the code read)

Not threading, but they shrink the same 7.3 s and make any threaded version cheaper. Listed
here so they are weighed together, not built as part of this idea:

- **One pass instead of two** in the check for an unchanged file (`nonGeometryStatements`
  then `parseContentToObjects`). Only worth it if measurement shows the parse dominates.
- **Open each archive once per stage** instead of once per read, and decompress a nested zip
  once instead of once per entry inside it.
- **Reuse step 2's head reads in step 5** instead of reading the same heads again.

---

## 6. Things any option must handle

- **Widgets only from the UI thread.** Workers get plain paths and data and return a result
  struct. Today three lambdas in `runComparison` call `headerA_->archivePath()` /
  `headerB_->archivePath()` (the two head readers and the same-data predicate), and
  `showSelectedPair` does the same. `pathA`/`pathB` are already local strings in
  `runComparison`; the lambdas should capture those instead.
- **Busy state.** Block a second Compare and archive changes, or cancel cleanly. Show a simple
  "Comparing..." state (minimal UI: no progress details unless they change the user's next action).
- **Stale results.** Replacing an archive mid-analysis → discard old results, for example with a
  generation counter checked when a result arrives. Today `runComparison` clears the trees,
  layers and labels first; a background version must do the same at start and again refuse
  a late result.
- **One zip reader per thread.** QuaZip objects must not be shared. `ArchiveReader` already
  opens the archive per call (`readEntryWalk`, `readTopLevel`, `expandNested`), which fits.
  If the "open once per stage" saving above is built, each worker needs its own open zip.
- **Parser thread safety.** Audit result so far:
  - No mutable globals found in `src/gerber/`: parser state lives in locals inside each call.
  - Function-local `static const QRegularExpression` objects are shared by all threads:
    `gerberparser.cpp` (offset/scale/mirror/rotation, step-and-repeat) and
    `excellonparser.cpp` (repeat line, hole/move, arc, rout command). Their initialisation is
    thread-safe (C++11). Calling `match()` on one shared object from several threads is
    believed safe in Qt 6 but must be **verified against the Qt docs**, not assumed.
  - `ExportNoise` (`exportnoise.h`) and `LayerEquivalence` use `static const` lists only:
    read-only after initialisation.
  - Rendering uses `QImage` + `QPainter`, which may run off the UI thread (`QPixmap` may not;
    none is used in `src/gerber/`).
- **Nested zips.** Today a nested zip is opened only if it differs (Q4). Pre-analysis can
  parse nested contents speculatively (this does not change what is displayed), or skip
  them and handle them at compare time as now.
- **Which files to pre-analyse.** Today only pairs where both entries are `GerberData` reach
  the check, and that kind comes from `resolveKinds` (extension first, then a sniff). The
  drop-time job must pick its files with the same rule, or it will analyse different files
  than the check would.
- **Drop order.** B may be dropped first; the design must be symmetric.
- **Memory peak.** Several files parsed at once use more RAM than one.
- **"Never a false green" still holds.** Any doubt (unreadable, unparseable, empty layer,
  different kinds) → yellow, exactly as today. See the unfingerprintable cases in section 4.

---

## 7. Threading tool

Today `CMakeLists.txt` uses `Core Gui Widgets Svg Test` (no Concurrent).

| Tool | Pros | Cons |
|---|---|---|
| **QtCore only**: `QThreadPool` + `QPromise` / `QFuture` + `QFutureWatcher` (all in QtCore since Qt 6) | no new DLL; result signals arrive on the UI thread; a thread pool sized to the CPU is built in | the parallel loop over files is hand-written (one pool task per file) |
| **Qt Concurrent** (`QtConcurrent::run`, `QtConcurrent::mapped`) | shortest code for "this list in parallel" | separate module: **`Qt6Concurrent.dll` added to the package**, and IT re-checks on new dependencies (no network involved; `test_nonetworkdependency` unaffected) |
| **C++ standard library** (`std::async`, `std::thread`) | no new DLL | most hand-written code: no pool, results passed back by hand (`QMetaObject::invokeMethod`) |

Leaning: **QtCore only**. Not decided.

---

## 8. Measure before building

Already measured in the app: `compareMs` (step 1), `sameDataMs` (step 3, shown summed with
step 1) and render time. Missing:

1. Split the freeze: step 1 vs step 2 vs step 3 vs step 4 vs step 5 vs step 6. Steps 2, 4, 5
   and 6 have no timer today.
2. Inside step 3: read vs `nonGeometryStatements` vs `parseContentToObjects` vs
   `sameObjects`, for the largest real files.
3. Nested-zip cost: the same files in a nested zip vs at the top level.
4. Memory of one parsed `Layer` for a dense layer. This decides option B.
5. Overlay render time for the largest pair. This decides whether rendering needs a
   background thread too.

---

## 9. Open questions

- Hash (SHA-256) or full canonical data for the fingerprint?
- QtCore only, Qt Concurrent or the standard library?
- Nested zips: pre-analyse speculatively, or leave them for compare time?
- Is any visible "analysing..." hint wanted after the first drop, or nothing until the compare?
- Build the no-thread savings (section 5) first, separately, or not at all?
