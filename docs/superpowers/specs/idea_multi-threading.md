# Idea: multi-threading for Archive Compare

**Status**: **Idea only. Nothing decided, nothing implemented.** Collected 2026-10-22 from a
discussion after the v1.8.6 same-data check (branch `issue_resaving`, commit `ac1ac2a`).
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

`ArchiveComparePage::runComparison()`:

1. `ArchiveComparison::compareArchives(pathA, pathB)`: read both zip directories, compare
   (CRC32, size), expand differing nested zips. Fast: no file is decompressed for the CRC.
2. `ComparisonSummary::resolveKinds()`: sniffs file types (8 KB head reads).
3. **`SameDataCheck::collapseSameDataPairs()`**: for every yellow Gerber/drill pair,
   reads **both whole files**, parses them and compares. Most likely the main cost.
4. Fill the trees, summary line and pair list.

When the user clicks a pair, `ArchiveComparePage::showSelectedPair()` **reads and parses
both files again** before rendering. So every file of a yellow pair is parsed **twice**:
once in step 3 and once on click.

The overlay render (`renderShownPair`) is also synchronous. A very large layer could freeze
the window there too.

---

## 3. User workflow: idle time we can use

- Between dropping archive A and archive B: **~2 s, sometimes 3-4 s.**
- After the compare, the user reads the trees (if not all green): **~2-3 s** before
  clicking a pair.

---

## 4. Key idea: per-file fingerprint, computed on drop

The same-data check compares **pairs**, but almost all of its work is **per file**:

- parse into objects (`GerberParser::parseContentToObjects` / `ExcellonParser::...`),
- build the sorted object runs (what `LayerEquivalence::sameObjects` compares),
- build the non-geometry list (`nonGeometryStatements` / `nonGeometryLines`), noise removed.

Each file can therefore be reduced to a **fingerprint** of exactly what 6a and 6b compare:
file kind + non-geometry list + sorted object runs + "has objects" flag.
**Two files are same-data exactly when their fingerprints are equal.**

This lets the work start **as soon as one archive is dropped**, without knowing its partner:

- Drop A → background: parse and fingerprint all of A's Gerber/drill files, in parallel.
- Drop B → the same for B, starting immediately.
- Compare → CRC step (fast) finds the yellow pairs; the same-data check becomes "are the
  two fingerprints equal?", which is instant.

### Fingerprint form: CRC vs SHA-256 vs full data

The zip's CRC32 cannot be reused. It covers the raw bytes, which are exactly what differs on
a re-export. The fingerprint covers our cleaned-up form, so the app computes it itself.

| Form | Size | Strictness | Note |
|---|---|---|---|
| CRC32 | 4 bytes | weak: 4 billion values, designed for transmission errors | a chance match = **false green** |
| **SHA-256** (`QCryptographicHash`, in QtCore) | 32 bytes | accidental match treated as impossible | same cost as CRC; tiny next to parsing |
| Full canonical data | can be large for dense layers | 100% strict, as today | memory cost |

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
| A | **Fingerprint on drop**, background, parallel per file. Wait after dropping B ≈ B's parse only (*estimate* ~1 s) | uses idle time; no freeze anywhere; the compare becomes trivial | parses all drawable files, even ones that turn green by CRC (wasted, but in idle time; CPU busy a few seconds); must cancel or ignore jobs when an archive is replaced; nested zips need a decision (below) |
| B | **A + keep the parsed layers** for the overlay | removes the double parse; clicks faster | **memory**: dense layers can be hundreds of thousands of objects; ~50 layers kept could be large. Fallback: keep only yellow-pair layers after the compare |
| C | **B + pre-render** the overlay of yellow pairs while the user reads the trees (top of list first) | first view of a pair instant | depends on canvas size and zoom (resize/zoom/pan re-renders anyway); images cost memory; most code for least gain |
| - | Baseline: round-1 option 3 | simpler: no caching or cancellation | waits for both archives' parsing after the second drop; double parse stays |

**Suggested order:** measure → A → B (if memory allows) → C only if clicks still feel slow.

---

## 6. Things any option must handle

- **Widgets only from the UI thread.** Workers get plain paths and data and return a result
  struct. Today's lambdas call `headerA_->archivePath()` and must stop doing that.
- **Busy state.** Block a second Compare and archive changes, or cancel cleanly. Show a simple
  "Comparing..." state (minimal UI: no progress details unless they change the user's next action).
- **Stale results.** Replacing an archive mid-analysis → discard old results, for example with a
  generation counter checked when a result arrives.
- **One zip reader per thread.** QuaZip objects must not be shared. `ArchiveReader` already
  opens the archive per call, which fits.
- **Parser thread safety.** Audit `GerberParser`, `ExcellonParser`, `LayerEquivalence` and
  `ExportNoise` for shared mutable state. `ExportNoise`'s function-local statics are safe to
  initialise concurrently (C++11), but this should be verified rather than assumed.
- **Nested zips.** Today a nested zip is opened only if it differs (Q4). Pre-analysis can
  parse nested contents speculatively (this does not change what is displayed), or skip
  them and handle them at compare time as now.
- **Drop order.** B may be dropped first; the design must be symmetric.
- **Memory peak.** Several files parsed at once use more RAM than one.
- **"Never a false green" still holds.** Any doubt (unreadable, unparseable, empty layer,
  different kinds) → yellow, exactly as today.

---

## 7. Threading tool

| Tool | Pros | Cons |
|---|---|---|
| **Qt Concurrent** (`QtConcurrent::run`, `QtConcurrent::mapped`, `QFutureWatcher`) | built for "run in background" and "this list in parallel"; result signals arrive on the UI thread | separate module: **`Qt6Concurrent.dll` added to the package** (no network involved; `test_nonetworkdependency` unaffected) |
| **C++ standard library** (`std::async`, `std::thread`) | no new DLL; fits constitution principle 3 | more hand-written code for the parallel loop and for passing results back (`QMetaObject::invokeMethod`) |

Not decided.

---

## 8. Measure before building

1. Split the 7.3 s: CRC/expand vs `resolveKinds` vs same-data check vs UI fill.
2. Per-file parse time for the largest real files.
3. Memory of one parsed `Layer` for a dense layer. This decides option B.
4. Overlay render time for the largest pair. This decides whether rendering needs a background
   thread too.

---

## 9. Open questions

- Hash (SHA-256) or full canonical data for the fingerprint?
- Qt Concurrent or the standard library?
- Nested zips: pre-analyse speculatively, or leave them for compare time?
- Is any visible "analysing..." hint wanted after the first drop, or nothing until the compare?
