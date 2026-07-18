#pragma once

#include <QRectF>

#include "gerberobjectmodel.h"
#include "layermask.h"

// Renders a parsed Layer's objects into a LayerMask coverage image -
// the heart of the v2 fidelity fix (see the 2026-07-18 design doc).
// True shapes only: each object is painted from its exact QPainterPath
// (GerberObjectPaths), so circles rasterize as circles and arcs as
// arcs, at whatever pxPerMm the caller asks for. Polarity is a paint
// mode, not geometry: dark objects paint coverage, clear (%LPC) objects
// erase it, in file order - no boolean geometry anywhere.
//
// Pure function of its inputs (no widgets, no global state), so it can
// be pixel-asserted in unit tests and later moved to a worker thread
// unchanged if real-file timings ever demand it.
namespace LayerMaskRenderer {

// windowMm: the Gerber-space (mm, Y-up) window to render; pxPerMm: the
// resolution. Objects (or parts of objects) outside the window are
// simply clipped by the image bounds. See LayerMask for the output's
// coordinate/orientation contract.
LayerMask render(const Layer &layer, const QRectF &windowMm, double pxPerMm);

} // namespace LayerMaskRenderer
