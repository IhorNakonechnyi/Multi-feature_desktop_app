#pragma once

#include "gerberobjectmodel.h"

// Decides whether two parsed layers draw exactly the same objects, so a
// re-exported file whose bytes changed (new date, new job path) can still be
// recognised as carrying the same data. Exact, not approximate: positions are
// the parser's integer nanometres, aperture sizes its parsed numbers. There is
// no tolerance anywhere, so this can wrongly say "different" but never wrongly
// say "same".
namespace LayerEquivalence {

// True when both layers contain the same objects, under two rules:
//
// 1. Apertures are compared by what they ARE, not by their number. A flash
//    with D10 = "circle 0.2 mm" equals a flash with D23 = "circle 0.2 mm".
//
// 2. Order only matters where it changes the picture. The objects are split
//    into runs at every dark/clear polarity switch. Runs are compared in
//    order, because a clear object erases what was drawn BEFORE it - moving
//    it across a dark object changes the board. Inside one run the order is
//    irrelevant (painting pad 1 then pad 2 gives the same copper as the
//    reverse), so each run is compared as an unordered collection. Duplicates
//    still count: two identical pads are not equal to one.
//
// Everything the object model does not hold - format directives, attributes,
// comments - is NOT looked at here; SameDataCheck compares that separately.
bool sameObjects(const Layer &a, const Layer &b);

} // namespace LayerEquivalence
