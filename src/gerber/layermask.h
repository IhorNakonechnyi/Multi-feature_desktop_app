#pragma once

#include <QImage>
#include <QRectF>

// One rendered Gerber layer as a monochrome coverage mask. This struct is
// THE architectural boundary of the v2 rendering design ("Approach A with
// C-ready boundary", docs/superpowers/specs/2026-07-18-gerber-render-v2-
// design.md): everything upstream (parser, LayerMaskRenderer) produces it,
// everything downstream (MaskCompositor, CompositeView - and a possible
// future GPU display) consumes it. Keep this header dependency-free so
// either side can be swapped without touching the other.
struct LayerMask {
    // ARGB32-premultiplied; material is opaque white, empty space fully
    // transparent. Alpha IS the coverage signal (anti-aliased edges land
    // between 0 and 255); color is constant white so a compositor can
    // tint by multiplication without re-rendering.
    QImage image;

    // The window of Gerber-space (millimeters, Y-up) this image covers.
    // QRectF-normalized as usual (top() is the SMALLER y) - but note the
    // display flip: image row 0 corresponds to y = bottom() (the LARGEST
    // y, the top of the physical board). The renderer bakes that flip in
    // so consumers never reason about Y conventions.
    QRectF windowMm;

    // Resolution this mask was rendered at (image pixels per mm).
    double pxPerMm = 1.0;
};
