# Pitfall: round-stroke end-cap placement (curves rendering as dots)

**Symptom:** connected line segments and curves rendered as chains of disconnected dots instead of
a smooth, continuous stroke, even though each individual segment's width/direction/roundness
looked correct in isolation.

**Where:** `GerberObjectPaths::resolveLinearStrokePath` (`src/gerber/gerberobjectpaths.cpp`),
the function that builds the drawn shape of a round-aperture line draw (`D01`).

## Root cause

A Gerber line draw with a round aperture is the Minkowski sum of the segment and the pen circle:
the round end caps are centered **on** the segment's endpoints and bulge half a pen-width
**beyond** them in both directions.

The original code built this with `addRoundedRect(0, -width/2, length, width, ...)` - a rounded
rectangle that starts exactly at the segment's start point and ends exactly at its end point. That
places the cap **centers** half a pen-width *inside* the segment instead of on its endpoints, so
every drawn segment came out half a pen-width short at both ends.

A single long, isolated trace just looked imperceptibly short - invisible in practice. The failure
only becomes visible when consecutive segments are shorter than the pen's own width, which is
exactly how real CAM tools export curves and text: as chains of short straight chords (observed
chord lengths as small as ~0.09mm with a 0.05mm pen, no arc commands at all). With both ends
pulled inward, neighboring segments no longer overlapped in a full round joint - they just barely
touched at a point, so each chord rendered as its own isolated blob. A zero-length draw (the valid
Gerber idiom "stamp one dot of the aperture here") degenerated to nothing at all, for the same
reason.

## Fix

Build the capsule spanning `-width/2 .. length + width/2` instead of `0 .. length`, so the cap
centers land exactly on the endpoints - the true swept shape of a round pen. This also makes the
zero-length case correctly degenerate to the bare aperture circle.

Fixed in commit `4331eb6` (`Center round-stroke caps on segment endpoints`), with two regression
tests in `tests/unit/test_gerberobjectpaths.cpp`:
`linearStrokeCapsAreCenteredOnEndpoints` and `zeroLengthDrawPaintsApertureDot`.

## Why this is worth remembering

This class of geometry bug - correct locally, wrong at the seams between shapes - won't show up in
a unit test unless the test specifically checks endpoint/joint coverage rather than just
width/length/shape. It also won't show up on synthetic test fixtures with long, sparse segments;
it only appears on real CAM output where chords are shorter than the pen width. Any future code
that builds its own swept/capsule stroke shape in this codebase (a GPU/GL renderer behind the
`LayerMask` boundary, or the M5 Excellon parser's routed-slot rendering, which sweeps a drill
diameter along a path the same way) should center caps on endpoints from the start, and should
test joint coverage between short consecutive segments specifically - not just an isolated
segment's own dimensions.
