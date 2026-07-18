#pragma once

#include <QPainterPath>
#include <QRectF>

#include "gerberobjectmodel.h"

// Resolves one GraphicObject into the filled QPainterPath of its drawn
// area, in millimeters, Gerber-native Y-up coordinates. Extracted from
// GerberParser's legacy-render reconstruction (the resolve*Path family)
// so the v2 mask renderer (see the 2026-07-18 design doc) and the legacy
// parseContent() wrapper share one definition of "what does this object
// look like" instead of drifting apart.
//
// Paths here are TRUE shapes - real ellipses/rounded rects/arc curves,
// never pre-tessellated polygons. Whoever consumes them (QPainter
// fill, a stroker, a bounding-rect query) decides how they become
// pixels; nothing is lost before that point. That rule is the core of
// the v2 fidelity fix and should survive any future edit to this file.
namespace GerberObjectPaths {

// The object's drawn area at its final position. Aperture lookups,
// macro resolution, and macro unit scaling all come from `layer`
// (layer.apertures / layer.macros / layer.unitScale).
QPainterPath objectPathMm(const GraphicObject &object, const Layer &layer);

} // namespace GerberObjectPaths
