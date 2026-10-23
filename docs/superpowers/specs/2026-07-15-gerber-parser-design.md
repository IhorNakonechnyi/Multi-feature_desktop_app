# Hand-rolled Gerber (RS-274X) parser + real diff rendering (Phase 3b.2)

**Status**: Approved, ready for implementation planning.
**Relates to**: `specs/001-pcb-supplier-workflow/spec.md` FR-015 (visual overlay render), `research.md`'s hand-rolled-parser decision, `tasks.md`'s Phase 3b.2 (T030/T031b/T031c). Builds on Phase 3b.1 (pairing/gating/UI shell, already implemented) - specifically the `GerberDiffRenderer` canvas (`QGraphicsView`/`QGraphicsScene`, black background, 0.1mm dot grid) and the pair-selection list in `ArchiveComparePage`.

## Why not a third-party library

Investigated during this design: `hsiang-lee/gerber-parser` (MIT, Qt-based) was rejected after inspecting its actual source - it bundles a machine-locked license-validation system with libsodium and gRPC/HTTP-based license-server capability, incompatible with this app's zero-outbound-network-calls requirement and a general trust concern for a small project. `integralpro/libpger` and `miltonneal54/GerberVS` are GPL-licensed, ruled out per the existing no-copyleft-dependency stance (`research.md` already rejected gerbv for the same reason). `JulesStringer/GerberParser` (MIT) was inspected directly (cloned, read source) and rejected on its own merits: abandoned since 2017, old-style C++03 with manual memory management and no CMake, and a callback/subclass rendering architecture that would require adopting its whole ~35-file class hierarchy - the audit and integration effort was judged comparable to or greater than hand-rolling the specific scope below, with less confidence in correctness for code touching customers' confidential board data. Proceeding with a hand-rolled parser, as `research.md` already called for.

## Scope, grounded in a real sample archive

A real sample archive (`Gerber_BT30-3b`, from `C:/Igor/Projects/Test_data/...` - not committed, inspected locally only per the project's confidentiality boundary) was used to determine real-world scope rather than guessing:

- Straight-line traces only in the sample (no arcs), but **arc support (G02/G03) is being built for real** per explicit decision - other real jobs are expected to use curved traces even though this sample didn't.
- Standard aperture shapes seen: Circle (`C`), Polygon (`P`, standard regular N-gon). Rectangle (`R`) and Obround (`O`) are the remaining two RS-274X standard shapes - not seen in this sample but trivial additions alongside Circle/Polygon (same aperture-definition mechanism), included for completeness.
- One custom aperture macro using the regular-polygon primitive (code 5) with one variable substitution and one multiplication (a scaling expression shaped like `2.0X$1`, not the real sample's own literal macro name/values), used for octagon pads.
- `G04` comment lines present - skipped, not parsed as geometry.
- Format/units vary **per file, not per convention**: some layers use legacy `G70` (inches) with `%FSLAX24Y24*%` (2.4 decimal format), one uses modern `%MOIN*%` with `%FSLAX25Y25*%` (2.5 format). Parser reads each file's own header rather than assuming a fixed format.
- Only `%LPD*%` (dark polarity) seen - `%LPC*%` (clear/subtractive layer polarity) not observed but included since the compositing infrastructure built for macro exposure (below) covers it for free.
- No `G36`/`G37` (region fill) in the sample. Included anyway: region fill is a simple polygon-from-point-sequence feature (no macro/expression complexity), and omitting it risks the same "silently hides a real difference" problem that motivated full macro support - if disagreed with, this is a small, separable piece to cut.

## Aperture macros: full support, not partial + fallback

Earlier in this design's discussion, a "support only the one confirmed primitive (regular polygon), fall back to a circle approximation for anything else" approach was considered and rejected: a circle-fallback for an unsupported macro (e.g. a thermal relief) would render identically across old/new even if the real shape differs in ways the fallback can't capture (e.g. spoke count) - silently hiding a real difference, which defeats the point of a diff tool. Also: research turned up that thermal reliefs are actually the *most common* real-world use of aperture macros industry-wide, more common than the polygon primitive this one sample happened to use - so the fallback's blind spot wasn't even a rare edge case.

Full support means:

- **A small expression evaluator**: macro bodies can define intermediate variables via assignment (`$3=$1+$2/2*$4`) before using them in primitive statements, with `+`, `-`, `x` (multiply), `/`, and parentheses. This is needed in full, not just `$1` substitution, since real macros chain variables.
- **All active primitive types** per the current Gerber spec: Comment (`0`, skipped), Circle (`1`), Vector Line (`20`), Center Line (`21`), Outline (`4`, arbitrary vertex polygon), Polygon (`5`), Thermal (`7`, ring with spokes). Deprecated/legacy codes (old Vector Line alias `2`, Moiré `6`, old Lower-Left Line `22`) are explicitly **not** implemented - rare in modern output; if encountered, treated as unsupported (see fallback note below), not actively built.
- **Exposure-based compositing**: each primitive within a macro carries an exposure flag (`1` = add material, `0` = subtract). Primitives are composited in sequence using `QGraphicsScene`/`QPainterPath`'s boolean operations (`united()`, `subtracted()`, `intersected()`) - Qt provides this natively, which is what makes full support tractable rather than requiring a from-scratch geometry-boolean implementation. This is the part of "full macro support" that actually matters more than the shape-formula count: a thermal relief is often built by compositing a circle (add) with rotated rectangles (subtract), not by using a single dedicated primitive.
- **Rotation**: primitives carry their own rotation modifier, applied as a local transform.
- **Fallback of last resort**: if a macro uses one of the explicitly-unimplemented deprecated primitive codes, that macro's aperture falls back to a small fixed-size marker circle (not a guessed diameter from unfamiliar parameter semantics - see the rejected partial-support discussion for why guessing per-primitive parameter meaning was rejected as its own risk). This should be rare in practice given the deprecated codes are legacy/uncommon.

## Data flow into the existing canvas

`GerberDiffRenderer` (Phase 3b.1) already provides the `QGraphicsView`/`QGraphicsScene` canvas with the black background and dot grid. This phase adds:

1. A `GerberParser` (`src/gerber/gerberparser.h/.cpp`) that reads one Gerber file's text and produces geometry - either directly as `QGraphicsItem`s/`QPainterPath`s scoped to millimeter scene coordinates (matching the grid's existing mm-based units), or an intermediate primitive-list representation the renderer consumes. (Exact intermediate representation is an implementation-plan decision, not a design-level one.)
2. Wiring `ArchiveComparePage`'s pair-list selection (currently a no-op placeholder, since Phase 3b.1 had no geometry to show) to: parse both files in the selected pair, draw archive A's geometry in one color and archive B's in a contrasting color into the same scene, and record/report elapsed parse+render time into the existing render-time label (currently a static placeholder).
3. Implementing the zoom-bound formulas already specified in the Phase 3b.1 design (deferred there for lack of a real bounding box): max zoom-out = 2x the union bounding box's largest span across both files in the pair; max zoom-in = the smaller viewport dimension never below 10 grid dots (1mm).

## Testing

Per constitution Principle 4 (core logic gets tests, UI/glue doesn't) - this phase is almost entirely core calculation logic, so it gets substantial unit test coverage:

- Format/unit parsing: leading-zero-suppressed coordinate decoding at different `%FS` decimal precisions; legacy `G70`/`G71` vs. modern `%MO` unit handling.
- Standard aperture geometry: circle, rectangle, obround, regular polygon (both the standard `P` shape and macro primitive 5, confirmed to share the same math).
- Arc interpolation: both single- and multi-quadrant modes, at least one known-correct arc (e.g. a quarter circle) verified against expected endpoint/sweep.
- Macro expression evaluator: variable substitution, chained assignment, all four operators, operator precedence.
- Macro compositing: at least one case combining an additive and a subtractive primitive, confirming the subtraction actually removes area rather than just layering visually.
- Region fill: a closed outline from a `G36`/`G37` point sequence. *(Updated in v1.8.8: edges drawn in G02/G03 mode are kept as true arcs, G74 included, and a single full-circle arc is a valid region. Before that, a region arc was reduced to a straight chord.)*
- Unsupported-primitive fallback: confirms a deprecated/unimplemented macro primitive code degrades to the fixed-size fallback circle rather than crashing or being silently skipped entirely.

**Test fixtures must be hand-authored minimal synthetic Gerber snippets**, not copies of the real sample archive used to scope this design - that archive is a customer's confidential board data living outside the repo (`Test_data/`, not committed), and per `CLAUDE.md`'s confidentiality constraint nothing derived from real customer files should end up in the committed test suite, even though the Gerber *format* itself isn't confidential.

## Code style for this phase

Per Igor's explicit request (2026-07-15): this part of the codebase gets more explanatory comments than the project's usual terse default (section-level "what and why," not per-line narration), and should favor simple, flat structures - plain functions, minimal class hierarchies - over "flexible framework" abstractions, even where a more general design might seem natural. Prioritize readability and maintainability by someone still learning this domain over generality.

## Known residual limitation

The deprecated-macro-primitive fallback (fixed-size circle) retains the same "could silently render two different unsupported shapes identically" risk discussed and rejected for the primary primitive set - it's accepted here only because it's scoped to rare/legacy codes, not the common ones, and is a smaller residual than the originally-proposed broader fallback would have been.
