# Gerber diff UI shell: pairing, gating, and canvas (Phase 3b sub-project A)

**Status**: Approved, implementing.
**Relates to**: `specs/001-pcb-supplier-workflow/spec.md` FR-006/FR-007/FR-015 (User Story 1b, Phase 3b, v1.1 — deferred). `plan.md`/`spec.md` are unchanged; this refines *how* Phase 3b is delivered (split into two sub-projects) and adds one data-model gap that surfaced during design.

## Scope split

Phase 3b as originally scoped (`tasks.md` T027–T032) bundles the pairing/gating logic, the UI, and the actual hand-rolled Gerber parser + geometry rendering into one phase. That's too much for one working step (constitution Principle 2), so it's split:

- **Sub-project A (this doc)**: the pairing data model, Gerber-type gating, and the full UI shell (reveal/hide, explanation text, selectable list, render-time label, canvas with grid + zoom). The canvas renders an empty grid — no real Gerber geometry yet.
- **Sub-project B (separate future design)**: the hand-rolled RS-274X parser (`gerberparser.h/.cpp`) and drawing real old-vs-new geometry into the canvas built here, plus the zoom min/max clamping (needs a real bounding box to compute against — see below).

Sub-project A is independently testable: drag in archives with 1+ yellow Gerber pairs, confirm the list/canvas/text appear, click a pair, confirm the canvas is visible and zoomable (unclamped) even though it shows only the empty grid.

## Data model gap found during design

`CrcComparator`'s output (`ArchiveComparisonResult`) classifies entries per-side but never recorded which A-file pairs with which B-file for a `Different` match. That was fine for v1.0 (matching was path-only, so "same path" implied the pairing), but since v1.0.1 added filename-fallback matching (see `2026-07-15-path-tolerant-crc-matching-design.md`), a yellow pair's two files can legitimately sit at different paths — so the pairing must be tracked explicitly, not inferred.

**Fix**: add `QVector<DifferingPair>` to `ArchiveComparisonResult`:

```cpp
struct DifferingPair {
    QString relativePathA;
    QString relativePathB;
};
```

`CrcComparator::compare` appends one `DifferingPair` per `Different` classification produced in either pass (Pass 1 exact-path, Pass 2 filename-fallback). This is the only new data `CrcComparator` needs to expose; `ArchiveEntry`'s own fields are unchanged.

## Gating

`GerberDiffTrigger` (in `src/archivecompare/gerberdifftrigger.h/.cpp`) takes `ArchiveComparisonResult::differingPairs` and `GerberFileType`'s recognized-extension check, and returns the subset of pairs where **both** `relativePathA`'s and `relativePathB`'s filenames are recognized Gerber types. This subset is what drives the UI:

- Empty → the reserved bottom-right area stays blank, exactly as it is today (no visible change).
- Non-empty → the three UI elements below become visible.

Note this is a change from `spec.md`'s original FR-006 wording ("exactly 1 or 2 files classified as Different") — that count limit assumed a fixed small number because v1.0's simpler UI concept (an explicit "Show difference" button opening a single render or a 2-item next/prev) couldn't handle more. The list-based UI in this design has no such limit — any number of qualifying pairs can be listed and selected from. `spec.md`'s FR-006/FR-007 will need a wording update to reflect the list-based approach when this ships (tracked as a `tasks.md` note below, not a `spec.md` edit made as part of this design — that's a documentation follow-up, not new scope).

## UI layout

Inside the reserved bottom area (added in the `2026-07-15` archive-compare-page height change), split roughly 30% (left) / 70% (right):

- **Left column**, top to bottom:
  1. A short, static explanation label (plain text, not yet worded — wording is a small implementation detail, not a design decision).
  2. A selectable list (`QListWidget` or similar) of qualifying pairs, one row per pair (label likely the shared filename, since both sides' leaf names are identical for a real pair by construction of the matching logic).
  3. A render-time label, showing a placeholder/blank value until sub-project B wires in real timing.
- **Right side**: the canvas — black background, a dot-style grid (white dots, spaced 0.1mm apart, 0.05mm dot size), mouse-wheel zoom. Empty (just the grid) whether or not a pair is selected, until sub-project B adds real rendering.

**Navigation**: clicking a list row is the only selection mechanism — no separate next/prev buttons. This satisfies FR-007's intent (navigate between rendered pairs) via the list itself.

**Zoom bounds**: deferred entirely to sub-project B, per explicit decision — sub-project A's canvas zoom is unclamped. When sub-project B lands, the bounds are:
- **Max zoom-out**: let `D` = the largest span (`max(xMax−xMin, yMax−yMin)`) of the union bounding box of both files in the selected pair. The view may zoom out until it shows `2×D` in that dimension, no further.
- **Max zoom-in**: the *smaller* of the viewport's two visible world-space dimensions must never drop below 10 grid dots (1mm). Stop zooming in at that point.

## Testing

Unit tests (constitution Principle 4 — core logic only):
- `CrcComparator`'s `DifferingPair` construction: pairs are produced correctly for both exact-path and filename-fallback matches, and only for `Different` classifications (not `Identical`/`OnlyInThisArchive`).
- `GerberDiffTrigger`'s gating: filters correctly by recognized-Gerber-type on both sides; empty when no pairs qualify; correct subset when some do.

UI (list, canvas, grid rendering, zoom, reveal/hide) is manually tested per the constitution — no automated UI tests.

## Process note

Tracked in `tasks.md` as a refinement/split of Phase 3b (T028/T029/T031/T032), not a new spec-kit cycle. `spec.md`'s FR-006/FR-007 wording will need a small update once this ships to reflect "any number of qualifying pairs, list-selected" instead of "exactly 1 or 2, next/prev" — noted here as a follow-up, not done as part of this design.
