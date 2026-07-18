# Gerber Render v2 — M4: Overlay Compare

Approved by Igor 2026-07-18, after the M3 single-file-viewer gate passed visual comparison
against CAMMaster. Parent design: `2026-07-18-gerber-render-v2-design.md` (architecture,
`LayerMask` boundary, milestone list). This doc details M4 only.

## Goal

CAMMaster-style revision compare inside the Archive Compare page: the old and new version of a
changed Gerber file rendered as two colored layers in one view — new (A) in green, old (B) in
red, unchanged copper adding to yellow. The eye finds the differences; there is no geometric
diff, no difference list, no pointer UI (explicitly rejected in the parent design).

Scope decisions (Igor):

- Entry point: **Archive Compare page only.** The standalone Gerber viewer page stays
  single-file; a loose-file overlay can reuse these pieces later if wanted.
- Controls: **none beyond zoom/pan.** No layer toggles, no blink compare, no color pickers —
  candidate follow-ons once the overlay is in daily use.
- Colors fixed: A = green (60, 220, 60), B = red, overlap = additive yellow
  (`MaskCompositor`'s existing `CompositionMode_Plus` on black).
- Drill (Excellon) files: **not rendered in M4** — they surface as informational skipped rows
  (see below) and get their own milestone (M5).

## Approach (chosen: extend `ArchiveComparePage` in place)

The overlay engine already exists: `MaskCompositor::composite()` takes N `(LayerMask, QColor)`
pairs and is unit-tested. M4 is wiring, and wiring lives in the page, matching the existing
pattern. Rejected alternatives: a new overlay-controller class (would wrap two function calls
today — extract later if the standalone-page entry point ever creates duplication) and
multi-layer support inside `CompositeView` (violates the parent design's no-Gerber-knowledge
canvas rule that keeps the future GL swap cheap).

## 1. Pairs list and gating

- The temporary debugging list (ALL archive-A files) and its
  `kAlwaysShowGerberDiffSection = true` flag are **deleted** — real gating returns.
- After each `runComparison()`, `result.differingPairs` is split by a reworked
  `GerberDiffTrigger` into three buckets:
  - **eligible** — both sides recognized Gerber, neither a drill file → one list row per pair,
    labeled with the A-side path (plus " ↔ B-path" when the paths differ, e.g. the
    filename-fallback match);
  - **drill** — either side is a drill file (`GerberFileType::isDrillFile()`: `drl`, `xln`);
  - **unrecognized** — either side's extension not recognized at all.
- The whole Gerber section is visible iff any bucket is non-empty. Selecting an eligible row
  enables Show. Explanation label: "Changed Gerber files. Select a pair and click Show to
  compare old vs new."

## 2. Overlay rendering and data flow

On Show:

1. Read both entries' bytes from their own archives (`ArchiveReader::readEntryBytes`).
2. Parse each side to a `Layer` (`GerberParser::parseContentToObjects`).
3. `unitedBoundsMm = boundsA ∪ boundsB` — fit and all zoom/pan operate on the union so neither
   revision is cropped when extents differ.
4. `CompositeView::fitTo(unitedBoundsMm)` → every `renderRequested(windowMm, pxPerMm)` renders
   **two masks** (same window, same resolution, one `LayerMaskRenderer::render` call per layer)
   and sets `MaskCompositor::composite({{maskA, kColorA}, {maskB, kColorB}})` on the view.

A static legend label under the canvas: "green = only new · red = only old · yellow = same".
The render-time label stays and now measures the doubled per-view workload — that number is the
gate for any future threading or GL-display work (constitution: measure first).

## 3. Error handling

Each side fails independently (mirroring `runComparison()`'s per-archive reporting):

- One side unreadable from its archive, or parses to no drawable content → the other side still
  renders alone in its color; the status line names the missing side and why ("B side: no
  drawable Gerber content — showing new file only").
- Both sides fail → status message only; canvas cleared to black.
- Parse/read failures are per-pair status text, never dialog boxes.

## 4. Skipped-pairs rows (port from the 3b2 parts bin + drill rows)

Below the pairs list, changed-but-not-eligible pairs are surfaced instead of silently dropped:

- **Unrecognized extension**: "N changed pairs skipped (unrecognized type: .gpt, .gpb)" with one
  **Add ".ext"** button per distinct extension, wired to the existing
  `GerberFileType::addUserExtension()`. Clicking re-runs the split so the pair jumps into the
  eligible list immediately. This is a port of reviewed 3b2 work (`ineligiblePairs` + the
  d02018a lesson: section visibility must key off the FULL skipped set, not a derived list).
- **Drill files**: "N drill file pairs skipped (drill compare comes in a later version)" —
  informational only, no button (adding an extension cannot help; the parser doesn't speak
  Excellon yet).

## 5. Testing

Unit (QtTest, constitution rule 4 — core logic only):

- `GerberDiffTrigger`: three-bucket split — eligible passes through; a pair lands in exactly
  one bucket; drill is checked first, so a pair with one drill side and one unknown side counts
  as drill (an Add-extension button could never make it renderable in M4).
- `GerberFileType::isDrillFile()`: positive (`drl`, `xln`, case-insensitive), negative
  (`gtl`, `ger`, empty).
- `MaskCompositor`: a case pinning two overlapping masks → pure color where one covers,
  additive mix where both do (extend only if existing tests don't already pin this).

Manual (UI glue): load the three real archive pairs —

- pair 1 (byte-identical re-zip) → no differing pairs → Gerber section hidden;
- pair 2 → the changed `.GER` pair listed; overlay shows its known change as pure color on
  yellow background;
- pair 3 → skipped row with Add-extension buttons; after one click the `.GPT`/`.GPB` pair
  appears; its single added trace shows pure green;
- one-side-failure path: pick a pair, corrupt/rename scenario if convenient, otherwise covered
  by code review.

Igor's eye against CAMMaster on a real changed pair is the milestone gate, same as M3.

## M5 (queued, not designed here): Excellon drill support

Hand-rolled Excellon parser (tool table `T#C<dia>`, drill hits, routed slots; own
units/zero-suppression handling) emitting the same `Layer` object model, so drill layers flow
through the identical mask/overlay pipeline and drill pairs move from "skipped" into the normal
compare list. Detailed design after M4 ships. (License note: hand-rolled keeps the no-GPL rule;
no new dependencies.)

## Confidentiality guardrails (unchanged)

No real supplier/customer file names, parameter names, or data in code, comments, tests, or
commits. Test fixtures are synthetic snippets written from the public RS-274X/Excellon format
descriptions only.
