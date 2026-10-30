# Stage-timing diagnostic for Archive Compare

**Status**: **Designed, not implemented.** Branch `multi-threading`. Temporary: everything
behind the new build switch is **deleted before the branch merges**.

Context: [idea_multi-threading.md](idea_multi-threading.md), sections 2 and 8.

---

## 1. Why

The multi-threading work has several candidate approaches (idea doc, section 5), and the
right one depends on where the time actually goes. Today the app shows only two numbers:
"Compare time" (zip compare + same-data check, summed) and "Render time". Three steps of the
comparison run on the UI thread with no timer at all, so the frozen time the user feels is
longer than the label says.

This diagnostic shows each stage's time on screen, in a build made for it, so the user can
run real archives on real PCs and paste the numbers back. It stays for the whole feature,
because the same numbers then show whether each change actually helped. Expect the list of
timings to change as the code changes (e.g. once work moves off the UI thread).

Target PCs (from the user): weakest is an Intel i5 8th gen / AMD Ryzen 5 4600, minimum 8 GB
RAM.

## 2. Goals, non-goals, anti-goals

**Goals**

- Every timing shown decides something in the idea doc (table in section 4). Nothing else.
- Easy to delete before merge: one build switch, one marker, no logic spread through the
  parser or check code.
- The output can be pasted straight into a conversation: **numbers, sizes and counts only,
  never a file name or path** (file names can carry customer part numbers).

**Non-goals**

- No unit tests. It is temporary glue code (constitution principle 4).
- No timing of individual parser stages inside `isSameData` (first pass vs full parse).
  That would mean instrumenting `samedatacheck.cpp`. Add it later only if the numbers ask
  for it.
- No logging to file, no history of runs. The text box shows the last compare and last click.

**Anti-goals**

- The timing changing what it measures. The timers wrap existing calls; no stage is
  reordered, merged or skipped to make it measurable.
- Anything of it reaching a release build.

## 3. Permanent change: the two old labels go

Decided by the user: **"Render time" and "Compare time" are removed from the app in every
build**, not only in the timing build. The detail card keeps only the colour legend (and the
`PCB_DIAGNOSTICS` yellow-reason text, unchanged). Whether some timing returns to the release
UI is decided at merge time, not now.

Removal: `renderTimeLabel_`, `compareTimeLabel_`, their construction and layout rows, every
`setText` on them, and the timers that only fed them (`sameDataTimer` in `runComparison`, the
`QElapsedTimer` in `renderShownPair`). `ArchiveComparison::Outcome::compareMs` stays: the
timing readout uses it. Comments that point at the old labels as "the measurement before any
threading work" are updated to point at the timing build.

Docs: the idea doc's mentions of the two labels are updated. The 1b visual-redesign and M4
overlay design docs each get a one-line note that the labels were removed on this branch. Old plan documents
(`docs/superpowers/plans/`) are history and stay as they are. No test references the labels.

## 4. The timing build

### Switch

New CMake option **`PCB_TIMING`** (default OFF), defining the compile definition
`PCB_TIMING`. Independent of `PCB_DIAGNOSTICS`: the yellow-reason diagnostic stays after
merge, this does not. Configure into its own build directory, like the diagnostic build
(`docs/releasing.md`), e.g. `build-timing`.

With it on, the window title says **"Timing build <version>"** so it is never mistaken for a
release. With both switches on: "Diagnostic + timing build <version>" (*builder's call on
exact wording*).

All code behind it sits in `#ifdef PCB_TIMING` blocks. Every such block carries the marker
comment `TIMING-DIAG` so removal before merge is: delete every `#ifdef PCB_TIMING` block and
the CMake option, then search for `TIMING-DIAG` and `PCB_TIMING` to confirm none are left.

### Layout

The user does not read the numbers in the app: they copy them and paste them into the
conversation. So the readout only has to be copyable, not readable at a glance, and **no UI
block changes size or place**. Pair list (6 rows), canvas, trees and headers stay as they are.

All changes are inside the detail card (the bottom card with the colour legend):

- The old "Render time" / "Compare time" labels are gone (section 3, every build).
- **Timing build only**: under the legend, a small **read-only, scrolling plain-text box**
  (`QPlainTextEdit`, read-only), about 3-4 lines tall, monospaced. It holds the whole
  readout; anything that does not fit scrolls. Adding timings later only makes it scroll
  further.
- **Timing build only**: a **Copy** button beside it, copying the box's full text to the
  clipboard (including the part scrolled out of view). Ctrl+A, Ctrl+C in the box works too.

The canvas gets shorter in the timing build only by the box's height. Render time depends
on canvas size, so the render line prints the canvas size in pixels; render numbers can then
be scaled to the normal build's canvas or to any monitor.

Text format: plain lines, one value per line, so it pastes cleanly:

```
Last compare
Total: 9120 ms
Zip compare: 410 ms
File types: 180 ms
Check reads: 600 ms
Check parse: 6700 ms
Parse speed: 410 ms/MB
Overlay classification: 900 ms
Everything else: 160 ms
Memory now/peak: 412/530 MB
Pairs checked: 25, turned green: 25, MB parsed: 16.3
Last click
Read: 45 ms
Parse: 520 ms
Render: 130 ms @ 640x360 px
Memory now/peak: 540/560 MB
```

(Numbers invented, shape only.)

### What each value is, and what it decides

All times are wall-clock, `QElapsedTimer`, in ms.

| Value | Measured as | Decides |
|---|---|---|
| **Total** | `runComparison()` from its first line to its last, both archives set | how long the window really freezes |
| **Zip compare** | `outcome.compareMs` (already measured inside `compareArchives`) | whether zip listing/nested expansion needs work |
| **File types** | both `ComparisonSummary::resolveKinds` calls | same, for the head sniffing |
| **Check: reads** | sum, over every pair offered to the check, of the two `readEntryBytes` calls inside the same-data predicate | whether zip reading (esp. nested zips) is a real cost |
| **Check: parse** | sum of the `isSameData` calls inside that predicate | whether the parse is the cost threading must hide |
| **Parse speed** | `isSameData` time ÷ MB of both files, **counting only pairs that turned green** (a real change stops the check early and would make parsing look fast) | whether option A (fingerprint on drop) fits the ~2 s idle time: archive B's drawable MB × this speed |
| **Overlay class.** | `GerberDiffTrigger::classifyPairs` | whether reusing the earlier head reads matters |
| **Everything else** | Total − the five above | tree/list filling; split it only if it turns out large |
| **Memory now/peak** | process working set and peak working set (Windows `GetProcessMemoryInfo`), read at the end of `runComparison` | whether keeping parsed layers (option B) fits 8 GB |
| **Context line** | pairs offered to the check, pairs turned green (`sameDataPairs.size()`), MB of files parsed by the check | makes the numbers comparable across runs |
| **Click: Read** | the two `readEntryBytes` in `showSelectedPair` | click cost split |
| **Click: Parse** | the two `parseGerberOrDrill` in `showSelectedPair` | whether option B (keep layers) is worth it |
| **Click: Render** | `renderShownPair`, the **latest** call (it runs again on zoom, pan, resize) | whether rendering needs its own thread (option C) |
| **Click: Memory** | as above, read after the render | memory of two parsed layers + images |

Notes:

- **"Pairs offered" is counted by the predicate itself**, so it matches what was actually
  checked (the greedy walk skips pairs whose files are already proven).
- **"Green" is the pairs that went green in the check**, so pairs green by CRC are excluded.
- With `PCB_DIAGNOSTICS` also on, the click's `SameDataDiagnosis::explain` call is **not**
  inside any click timer. The predicate's `offeredPairs_` insert is negligible and may stay
  inside. Recommended practice: measure with the timing switch alone.
- **Peak working set is process-lifetime**, not per comparison. To compare runs, restart the
  app between them, or read "now". Stated here so the numbers are not misread.
- A run that stops early (one archive missing, or an archive unreadable) clears the readout to
  dashes rather than leaving the previous run's numbers.
- A value with nothing to measure shows "-", not 0: Parse speed when no pair turned green,
  Render when neither side has anything to draw.

### Reset rules

- New comparison starts → both sections show "-".
- Comparison finishes → "Last compare" filled; "Last click" stays "-".
- Pair clicked → "Last click" filled; zoom/pan/resize update only its Render and Memory.

## 5. Constraints

- C++20 / Qt 6.8.2 Widgets, no new dependency. `GetProcessMemoryInfo` comes with Windows
  (`psapi.h`; linked as `psapi` or through kernel32's `K32GetProcessMemoryInfo`). It is used
  only in the timing build.
- Does not affect `test_nonetworkdependency` (no new DLL).
- Archive Compare's behaviour (colours, pairs, overlay) must be identical with the switch on
  or off.
- Confidentiality: no file names, paths or file content in the readout.

## 6. Left to the builder

- Where the timing helpers live: inline in `archivecomparepage.cpp`, or a small
  `stagetiming.h/.cpp` added to the build only under `PCB_TIMING` (makes deletion one file
  plus a few `#ifdef`s).
- Text box height and styling (reuse the theme's existing card
  look; no new theme colours).
- Number formatting (MB with one decimal is enough).

---

## 7. Second round of values (2026-10-27)

The first run on four real archive pairs showed "Everything else" at 1.0-1.6 s in every
case, the largest share in three of four - so it is split, as section 4 said it would be.
Added because each one decides something:

| Value | Measured as | Decides |
|---|---|---|
| **Until painted** | compare start -> end of the first paint of either tree (zero timer from the paint) | the freeze after `runComparison` returns (layout + paint) |
| **Check other** | whole `collapseSameDataPairs` − reads − parse | whether the walk itself (sort, hashes) matters |
| **Summary** | `summarize` + summary line + banner | completes the split |
| **Trees build / expand / column width** | `ArchiveTreeModel` rebuild, `expandAll`, `resizeColumnToContents`, summed over both panels and every rebuild | which tree step to fix - none of it can move off the UI thread |
| **Tree rebuilds** | count of those rebuilds | confirms the triple rebuild per compare |
| **Pair list** | pair list fill + not-drawable label | completes the split |
| **Slowest pair** | read + parse of the slowest single checked pair, with its MB | the floor a parallel pair check cannot go below |
| **Pairs differing / drawable** | pairs before the check; pairs in the overlay list | scale |
| **Archive A/B** | files, tree rows shown, Gerber/drill files and their uncompressed MB | scale; Gerber MB × parse speed = option A's background work per archive |
| **Since previous pick** | time since the previous `archiveChanged` | option A's idle window (the compare starts on the second pick) |
| **Peak this run** | peak working set if it grew during this run, else "<= earlier peak" | per-run peak without restarting the app |
| **Private start/after check/end** | `PrivateUsage` at those points | what a compare allocates and keeps |
| **Click: slowest render since click** | max render time over zoom/pan/resize | whether panning needs option C |
| **Click: files, parsed objects, private before/after** | file MB, object count of both layers, `PrivateUsage` | memory per MB of parsed file, for option B |
| **PC** | CPU name (registry), thread count, total RAM | tells runs on different PCs apart; thread count scales a parallel check |

The tree timers live in `archivepanel.cpp` (behind `PCB_TIMING`, marked `TIMING-DIAG`) and add
into a process-wide `StageTiming::treeTimes()` that the page resets at each compare. The PC
line is hardware only, never a user or machine name.
