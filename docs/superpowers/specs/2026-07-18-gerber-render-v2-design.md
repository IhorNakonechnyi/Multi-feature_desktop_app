# Gerber Rendering v2 — Design ("Approach A with C-ready boundary")

Date: 2026-07-18
Status: Approved (brainstormed and decided with Igor; supersedes the tricolor-diff
rendering approach of `2026-07-15-gerber-diff-ui-shell-design.md` /
`2026-07-16-gerber-diff-tricolor-design.md` for everything after the parser)

## Problem

The previous render pipeline (branch `worktree-gerber-parser-3b2`) tessellates every
circle, arc, and stroked trace into polygon chains, then runs boolean geometry ops
(first `QPainterPath` booleans, later Clipper2) to compute an exact three-region diff.
Judged against CAMMaster on real supplier files, the output looked "badly redrawn —
apertures, artifacts": shapes degrade at the tessellation step, before any widget draws
them, and boolean seams add artifacts. Two rounds of fixes patched real bugs but the
crude look is inherent to the method.

## Key insight (from Igor's actual workflow)

The CAMMaster compare workflow this feature replaces is not a geometric diff. The
engineer loads the old and new layer in two different colors; the tool shows overlap
as a blended in-between color; the engineer's eye finds the differences (5–30 layer
pairs in 2–3 minutes). That is a **raster color overlay**. An exact polygon diff solves
a harder problem than the workflow needs — and that hard problem is where all the
artifacts came from.

Reference implementations confirm the standard method: gerbv (Cairo) and GerberVS
(GDI+) paint each layer's **true shapes** into a per-layer buffer — polarity handled as
paint/erase passes, not geometry — then composite layers by color. Commercial tools
(CAMMaster, CAM350) use the same method on OpenGL for display speed.

## Decision

**Approach A (CPU raster masks) with a C-ready boundary (GPU display can slot in later).**

```
.gbr file → GerberParser → Layer (object model, integer nm)
                              ▼
                    LayerMaskRenderer  (QPainter, true shapes, polarity=erase)
                              ▼
                  LayerMask { QImage grayscale, boundsMm, pxPerMm }   ← the boundary
                              ▼
                    CompositeView (plain QWidget canvas)
                    v1: CPU tint + CompositionMode_Plus onto black
                    later, only if interaction feel demands: GL display, same masks
```

Rules:

1. **No tessellation anywhere.** Round apertures via `drawEllipse`, traces via
   round-cap `QPen` strokes, arcs via real arc curves, obrounds/polygons/macros as
   native QPainter shapes. This is what fixes "crude".
2. **Polarity is a paint mode.** Dark paints opaque; `%LPC` clear erases
   (`CompositionMode_Clear`). No boolean geometry. (gerbv has open bugs exactly here;
   regression tests must cover it.)
3. **One layer = one flat color** regardless of self-overlap — automatic when painting
   a single-channel mask.
4. **Overlap color by light arithmetic.** Tinted masks composited with
   `CompositionMode_Plus` on black: red + green = yellow, order-independent — the
   CAMMaster blend, for free.
5. **Masks render at viewport resolution** for the current view window. On zoom, the
   cached composite scales immediately (briefly soft) and a crisp re-render replaces
   it. Synchronous first; background thread only if real files show lag.

Explicitly rejected:
- **Exact tricolor polygon diff (Clipper2)** — fights for exactness the workflow never
  needed; source of the artifacts and complexity. Clipper2 dependency dropped.
- **Pure GL from day one** — GPUs draw triangles, so GPU-first reintroduces
  tessellation as shader-land work; weakly testable; unjustified before CPU feel is
  even measured.
- **Difference-region "pointer" UI** — real changes are often systematic (all pads
  grow, a whole trace rewidens, silkscreen elements deleted); flagging every region
  would drown the signal. The trained eye on a good overlay is faster.

## Components (all `src/gerber/`)

| Unit | Job | Testing |
|---|---|---|
| `layermask.h` | The A↔C boundary struct; dependency-free | — |
| `layermaskrenderer.*` | Object model + view window → mask; pure function | QtTest pixel asserts |
| `maskcompositor.*` | (mask, color) pairs → composited QImage; pure function | QtTest pixel asserts |
| `compositeview.*` | Canvas widget: zoom/pan/cached pixmap; zero Gerber knowledge | manual |
| `gerberviewerpage.*` | Glue: picker → parse → render → display | manual |

Parser front-half (tokenizer → object model, integer nm) is **reused from the
`worktree-gerber-parser-3b2` parts bin** — it survived real-file fixes (inch units,
`%LPC`, extension recognition). The back half (flatten/canonical/diff-geometry/old
renderer) is not carried over.

## Milestones

- **M0** — this worktree/branch (`gerber-render-v2`), this design doc.
- **M1** — parser front-half + its tests ported; build/tests green; no UI.
- **M2** — mask renderer with pixel-assert suite (apertures, arcs, `%LPC` hole,
  self-overlap uniformity, inch scaling).
- **M3 — the gate** — minimal single-file viewer; Igor judges output side-by-side with
  CAMMaster on real files. Outcome decides: proceed / patch / schedule GL display.
- **M4** — overlay compare (two masks, two colors, additive blend), detailed **after**
  the M3 gate. Entry via Archive Compare's existing Show trigger + manual pick.

## Confidentiality

Unchanged: no real supplier/customer names, parameters, or data in code, comments,
tests, or commits. Test fixtures are synthetic RS-274X snippets written from the spec.
