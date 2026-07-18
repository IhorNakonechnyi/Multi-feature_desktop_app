#pragma once

#include <QColor>
#include <QImage>
#include <QVector>

#include "layermask.h"

// Composites tinted LayerMasks into the final on-screen image, the
// CAMMaster-style way (see the 2026-07-18 design doc): each layer's
// coverage is tinted with its own color and added onto black by light
// arithmetic (CompositionMode_Plus). Where two layers overlap, their
// colors sum - red + green = yellow - giving the blended "both files
// have material here" signal for free, order-independently, with no
// geometry computed at all.
//
// Pure function of its inputs (no widgets), pixel-asserted in unit
// tests, same reasoning as LayerMaskRenderer.
namespace MaskCompositor {

struct TintedMask {
    LayerMask mask;
    QColor color;
};

// All masks must share the same image size (the caller renders them
// through the same window/resolution - LayerMaskRenderer guarantees
// that when asked identically). The result is opaque, black where no
// layer has material.
QImage composite(const QVector<TintedMask> &layers);

} // namespace MaskCompositor
