# Gerber Render v2 — M5: Excellon Drill Support

Brainstormed and approved by Igor 2026-07-19. Parent design: `2026-07-18-gerber-render-v2-design.md`
(architecture, `LayerMask` boundary, milestone list). M4's doc (`2026-07-18-gerber-render-v2-m4-overlay-design.md`)
sketched this milestone at a high level under "M5 (queued, not designed here)"; this doc details it.

## Goal

Drill-file pairs (`.drl`, `.xln`) currently surface in Archive Compare as an informational
"skipped — drill compare comes in a later version" row, because the app's Gerber parser doesn't
speak Excellon. M5 removes that limitation: drill pairs render and overlay-compare exactly like
Gerber pairs do today, through the same mask/composite pipeline M3/M4 already gated against
CAMMaster.

Scope decisions (Igor):

- **Round plunge holes and routed slots both in scope.** A drilled hole (a bare `X#Y#` in drill
  mode) is a filled circle at the tool's diameter; a routed slot (`G00`/`G01`/`G02`/`G03` moves in
  rout mode) is a round-cap stroke or arc at the tool's diameter — the same primitives copper
  traces already use.
- **Both explicit-decimal and zero-suppressed coordinate formats in scope.** Modern KiCad/Altium/
  Eagle exports write coordinates with an explicit decimal point (`X1.5000`) and need no format
  guessing. Older/vendor dialects omit the decimal point (`X15000`) and rely on a declared or
  assumed digit format — see "Coordinate format resolution" below.
- No new UI beyond what M4 already built: drill pairs simply become eligible pairs. No
  drill-specific controls, no PTH/NPTH distinction (real archives already split those into
  separate files; this pipeline's existing "one layer = one flat color" rule applies unchanged).

## Approach (chosen: new parser front-end, unchanged back end)

Add `excellonparser.h/.cpp` to `src/gerber/`, structurally analogous to `gerberparser.*`: it
tokenizes an Excellon file and emits the *same* `Layer` object model (`gerberobjectmodel.h`,
integer-nm coordinates) that `GerberParser` already produces. `LayerMaskRenderer`,
`MaskCompositor`, and `CompositeView` require **zero changes** — they render Flash/Stroke/Arc
objects with no knowledge of which parser produced them, so the entire back half of the pipeline
gated at M3/M4 carries over untouched.

Rejected alternative: a parallel drill-specific object model and renderer (tool/hole/slot types
with drill-specific semantics). Rejected because the actual geometry needs — filled circles,
round-cap strokes, arcs — are identical to what `LayerMaskRenderer` already draws; a separate
renderer would duplicate proven, gate-tested code for no rendering benefit.

No new third-party dependency: per `CLAUDE.md`, existing Gerber-parsing options are GPL-licensed
and out of scope for this public repo; the same reasoning extends to Excellon, so this is a
hand-rolled parser like `GerberParser`.

## 1. Parsing

### Header

- `M48` optional start-of-header marker.
- Units: `INCH` or `METRIC`, optionally followed by `LZ`/`TZ` (leading/trailing zero suppression)
  and/or an inline digit-format suffix some tools emit (e.g. `METRIC,LZ,000.000`).
- Tool table: `T##C<diameter>` lines (plus any other per-tool parameters present) mapping a tool
  number to a hole/slot diameter, converted to integer nm using the declared units.
  **Correction (2026-07-29, commit `bd329fa`):** "plus any other per-tool parameters present"
  was aspirational — the implementation only read the diameter when `C` came *immediately*
  after the tool number, so a real file writing `T1F00S00C0.200` (optional feed and
  spindle-speed modifiers first) yielded no diameter at all. The failure was silent and
  misleading: the holes still parsed, so the layer reported the right object count while every
  hole referenced a tool with no aperture and nothing drew. `parseToolLine` now walks the
  line's fields looking for `C`. Other modifiers (`B`, `H`, `Z`, …) are skipped as unknown
  fields; the same field walk is where to add one if a file ever needs it.
- `%` or `M95` ends the header.

### Coordinate format resolution

Applied per-file, once, before parsing the body:

1. If any body coordinate contains a decimal point, coordinates are explicit-decimal — parsed
   directly, no format needed. (Expected to cover the large majority of real files, per Igor's
   read on the supplier archives this tool sees.)
2. Otherwise (zero-suppressed), resolve digit format in this order:
   - An explicit format comment if present (vendor convention, e.g. `;FILE_FORMAT=2:4` meaning
     2 integer / 4 fractional digits) — highest priority, since it's an explicit declaration.
   - Else the header's unit line combined with the public Excellon-spec defaults: **2:4** digits
     for `INCH`, **3:3** for `METRIC`, using whichever of `LZ`/`TZ` the header declared (default
     `LZ` if the header omits it, per the same public spec).
   - Else — no format comment, no unit/zero-suppression declaration, and coordinates without a
     decimal point — this is a genuine ambiguity, not a guess: the file fails to parse with a
     clear error (see "Error handling").

### Body

- `T##` (on its own line in the body) selects the current tool.
- A bare `X#Y#` (or `X#` / `Y#` reusing the other axis's last value) while in drill mode (the
  default, or after `G05`) emits a **Flash** at that coordinate with the current tool's diameter.
- `G00` is a **rapid positioning move only** — it updates the current point but never draws
  (matches the real Excellon convention; treating it as a cut would draw a phantom slot line on
  every ordinary rapid reposition between operations, corrupting the render on essentially every
  real file). `G00` also switches to **rout mode**, so the app that reads it knows the coordinate
  that follows is a rout-mode positioning move, not a drill-mode plunge.
- `M15` ("tool down") marks the start of an actual cut: from here until the matching `M16`, each
  `G01`/`G02`/`G03` move is emitted as a **Stroke** (linear or, for `G02`/`G03`, an **Arc** — same
  arc primitive `GerberParser` already emits for circular interpolation, resolved the same way via
  I/J offsets from the move's start point) at the current tool's diameter.
- `M16` ("tool up") ends the cut; subsequent `G01`/`G02`/`G03` moves (if any, before the next `M15`
  or `G05`) draw nothing.
- `G05` returns to drill mode.
- `M30` (end of program), `M00` — stop parsing; anything after is ignored, matching how trailing
  garbage after Gerber's `M02` is already handled.

## 2. Archive Compare integration

- `GerberFileType::isDrillFile()` already exists and is unchanged.
- `GerberDiffTrigger::eligiblePairs()`'s current "exclude if either side is a drill file" rule
  changes to: a pair where **both** sides are drill files is eligible (dispatched through
  `ExcellonParser` instead of `GerberParser`); a pair with exactly one drill side stays
  unrecognized/skipped (comparing a drill file against a copper Gerber layer is meaningless, and
  in practice this only happens if a file was mis-renamed).
- `ArchiveComparePage`'s parse step picks the parser per side via
  `GerberFileType::isDrillFile(path) ? ExcellonParser::parseContentToObjects(...) : GerberParser::parseContentToObjects(...)`.
- The "N drill file pair(s) skipped" label and its visibility branch in `rebuildSkippedPairsUi()`
  become dead for the normal case (both sides drill) but stay in place for the mixed-type case
  described above — same skip UX, updated wording if the mixed case is common enough to need its
  own message (left to implementation judgment, not a design-level decision).

## 3. Error handling

Mirrors M4 §3's per-side independent failure handling:

- A drill file that hits the "genuine ambiguity" case in coordinate-format resolution (§1) fails
  to parse with a clear message identifying the file and the reason ("ambiguous coordinate
  format — no decimal point and no format declaration found"), surfaced the same way an
  unparseable/corrupt Gerber file already is today (per-pair status text, not a dialog, not a
  crash) — reusing whatever mechanism `GerberParser` failures already use; confirmed against the
  actual code during planning, not re-decided here.
- One side failing still renders the other side alone in its color, exactly as M4 already does
  for Gerber pairs.

## 4. Testing

Unit (QtTest, constitution rule 4 — core logic only), mirroring `test_gerberparser.cpp`'s style:

- `ExcellonParser`: explicit-decimal holes; zero-suppressed coordinates for each of
  INCH×LZ, INCH×TZ, METRIC×LZ, METRIC×TZ; an explicit `;FILE_FORMAT=` comment overriding the
  unit-line default; tool table parsing (diameter lookup by tool number); a `G00` rapid move
  between an `M16` and the next `M15` draws nothing (only positions); an `M15`...`G01`...`M16`
  span draws exactly one linear slot; rout-mode arc slots (both directions) between `M15`/`M16`;
  the ambiguous-format error case; empty-body / header-only file.
- `GerberDiffTrigger`: a drill-drill pair is eligible; a drill/non-drill mixed pair is not.
- `GerberFileType::isDrillFile()` coverage already exists (M4) — unchanged.

Manual (UI glue, same pattern as M3/M4): Igor picks a real drill file, judges the single-file
render against CAMMaster; then judges an overlay compare on a real changed drill pair. Both are
milestone gates, same as M3/M4 — implementation is not "done" until both pass.

## 5. Milestone breakdown (small, working steps — constitution principle 2)

- **M5.1** — tool table + explicit-decimal round holes only. `ExcellonParser` + unit tests. No UI
  change yet (drill pairs still show as skipped).
- **M5.2** — zero-suppressed coordinate formats (§1) + the ambiguous-format error path + unit
  tests.
- **M5.3** — rout-mode slots (linear + arc) + unit tests.
- **M5.4** — wire into Archive Compare (`GerberDiffTrigger` eligibility change, parser dispatch);
  single-file visual gate with Igor (mirrors M3).
- **M5.5** — overlay-compare visual gate with Igor on a real drill pair (mirrors M4). M5 is
  complete only once this gate passes.

## Confidentiality guardrails (unchanged)

No real supplier/customer file names, parameter names, or data in code, comments, tests, or
commits. Test fixtures are synthetic Excellon snippets written from the public Excellon format
description only.
