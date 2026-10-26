#include "gerberobjectpaths.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <QPainterPathStroker>
#include <QPointF>
#include <QPolygonF>
#include <QTransform>
#include <QtMath>

#include "gerberaperture.h"
#include "gerberformat.h"
#include "gerbermacro.h"

namespace {

QPointF toMm(const PointNm &point)
{
    return QPointF(GerberFormat::nmToMm(point.x), GerberFormat::nmToMm(point.y));
}

// Identical to the parser's old inline flash(): build the aperture's
// flash geometry at the origin (resolving a macro aperture through the
// layer's macros, scaled by layer.unitScale), then translate to the
// flash point.
QPainterPath resolveFlashPath(const GerberAperture &aperture, const QPointF &positionMm, const Layer &layer)
{
    QPainterPath flashPath = GerberApertureGeometry::buildFlashPath(
        aperture, [&layer](const QString &name, const QVector<double> &params) {
            GerberMacro macro = layer.macros.value(name);
            return GerberMacroBuilder::buildPath(macro, params, layer.unitScale);
        });
    QTransform translate;
    translate.translate(positionMm.x(), positionMm.y());
    return translate.map(flashPath);
}

// Convex hull of a small point set (Andrew's monotone chain). Used for
// the rectangular-aperture stroke below - Qt has no convex-hull helper,
// and boolean-uniting the two end rectangles with a connecting band is
// exactly the kind of QPainterPath boolean op this codebase has learned
// to avoid (documented fragility incidents; see the design doc).
QPolygonF convexHull(QVector<QPointF> points)
{
    std::sort(points.begin(), points.end(), [](const QPointF &a, const QPointF &b) {
        return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y());
    });
    const auto cross = [](const QPointF &o, const QPointF &a, const QPointF &b) {
        return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
    };
    QVector<QPointF> hull(2 * points.size());
    int k = 0;
    for (int i = 0; i < points.size(); ++i) { // lower hull
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], points[i]) <= 0.0) {
            --k;
        }
        hull[k++] = points[i];
    }
    for (int i = points.size() - 2, lower = k + 1; i >= 0; --i) { // upper hull
        while (k >= lower && cross(hull[k - 2], hull[k - 1], points[i]) <= 0.0) {
            --k;
        }
        hull[k++] = points[i];
    }
    hull.resize(k - 1); // last point repeats the first
    return QPolygonF(hull);
}

// A drawn line's "pen" is the current aperture's own shape. Two cases
// matter in practice (the only shapes the spec allows for strokes):
//
// - Circle (the overwhelmingly common case): a capsule - rectangle plus
//   two round end caps - between from/to. Corners where two such
//   strokes meet look rounded, which is CORRECT: that's the physical
//   shape a round pen produces, and reference viewers show the same.
// - Rectangle: the AXIS-ALIGNED rectangle swept along the segment (it
//   does not rotate with the direction, and there are no round caps) -
//   the Minkowski sum of the segment with the rectangle, i.e. the
//   convex hull of the two end rectangles. Sharp corners, flat ends.
QPainterPath resolveLinearStrokePath(const GerberAperture &aperture, const QPointF &from, const QPointF &to)
{
    if (aperture.shape == GerberAperture::Shape::Rectangle && aperture.modifiers.size() >= 2) {
        const double halfWidth = aperture.modifiers.at(0) / 2.0;
        const double halfHeight = aperture.modifiers.at(1) / 2.0;
        QVector<QPointF> corners;
        corners.reserve(8);
        for (const QPointF &end : {from, to}) {
            corners << QPointF(end.x() - halfWidth, end.y() - halfHeight)
                    << QPointF(end.x() + halfWidth, end.y() - halfHeight)
                    << QPointF(end.x() + halfWidth, end.y() + halfHeight)
                    << QPointF(end.x() - halfWidth, end.y() + halfHeight);
        }
        QPainterPath swept;
        swept.addPolygon(convexHull(corners));
        swept.closeSubpath();
        return swept;
    }

    const double width = aperture.modifiers.value(0, 0.1);
    QPainterPath stroke;
    const double dx = to.x() - from.x();
    const double dy = to.y() - from.y();
    const double length = std::hypot(dx, dy);
    const double angleDegrees = qRadiansToDegrees(std::atan2(dy, dx));
    // The swept circle bulges half a width BEYOND each endpoint, so the
    // capsule spans -w/2 .. length+w/2. A rounded rect starting at 0
    // instead puts the cap centers w/2 inside the segment - every
    // polyline joint then pinches to a point and chord-approximated
    // curves render as chains of dots. As a bonus, this form degenerates
    // to the bare aperture circle when length is 0 (a valid Gerber
    // "stamp a dot here" draw).
    stroke.addRoundedRect(-width / 2.0, -width / 2.0, length + width, width, width / 2.0, width / 2.0);
    QTransform transform;
    transform.translate(from.x(), from.y());
    transform.rotate(angleDegrees);
    return transform.map(stroke);
}

// Adds to `path` (whose current point is `from`) the circular arc from
// `from` to `to` around `center`, as a TRUE curve (QPainterPath::arcTo,
// exact-to-subpixel bezier segments), in the commanded direction. Equal
// end points mean a full circle (G75 semantics). Used for arc strokes and
// for the arc edges of a region.
void appendArc(QPainterPath &path, const QPointF &from, const QPointF &to, const QPointF &center,
               bool counterClockwise)
{
    const double startAngle = std::atan2(from.y() - center.y(), from.x() - center.x());
    double endAngle = std::atan2(to.y() - center.y(), to.x() - center.x());
    const double radius = std::hypot(from.x() - center.x(), from.y() - center.y());

    if (from == to) {
        endAngle = startAngle + (counterClockwise ? 2.0 : -2.0) * std::numbers::pi;
    } else if (counterClockwise && endAngle < startAngle) {
        endAngle += 2.0 * std::numbers::pi;
    } else if (!counterClockwise && endAngle > startAngle) {
        endAngle -= 2.0 * std::numbers::pi;
    }

    // Angle-sign note: our coordinates are Gerber-native Y-up, where
    // atan2 gives the mathematical angle (CCW positive). arcTo's angle
    // convention is defined for Qt's Y-DOWN screen space - its point at
    // angle t is (cx + r*cos t, cy - r*sin t). Negating both the start
    // angle and the sweep maps one convention onto the other exactly
    // (cos(-t) = cos t, -sin(-t) = sin t), so the drawn curve follows
    // our mathematical arc without this file ever leaving Y-up space.
    const QRectF circle(center.x() - radius, center.y() - radius, 2.0 * radius, 2.0 * radius);
    path.arcTo(circle, -qRadiansToDegrees(startAngle), -qRadiansToDegrees(endAngle - startAngle));
}

// Draws a circular arc stroke following the TRUE curve (QPainterPath::
// arcTo, which represents the circle as exact-to-subpixel bezier
// segments) - not a chord polyline. The predecessor of this function
// (GerberParser's old resolveArcStrokePath) used a fixed 32-segment
// polyline; its chord sagitta grows with radius (~0.3mm at r=1000mm),
// which is one of the "badly redrawn" defects the v2 design exists to
// fix. `counterClockwise` selects which of the two arcs between `from`
// and `to` (going around `center`) gets drawn.
QPainterPath resolveArcStrokePath(const GerberAperture &aperture, const QPointF &from, const QPointF &to,
                                  const QPointF &center, bool counterClockwise)
{
    const double width = aperture.modifiers.value(0, 0.1);
    const double radius = std::hypot(from.x() - center.x(), from.y() - center.y());

    // Coinciding endpoints mean a full 360-degree circle (G75 multi-
    // quadrant semantics - how CAM tools draw round cutouts and via
    // rings). Without this the sweep below normalizes to zero and the
    // whole circle collapses into a single dot at the start point.
    if (from == to) {
        QPainterPath ring;
        ring.addEllipse(center, radius, radius);
        QPainterPathStroker fullCircleStroker;
        fullCircleStroker.setWidth(width);
        fullCircleStroker.setCurveThreshold(0.001); // mm units - see below
        return fullCircleStroker.createStroke(ring);
    }

    QPainterPath centerline;
    centerline.moveTo(from);
    appendArc(centerline, from, to, center, counterClockwise);

    QPainterPathStroker stroker;
    stroker.setWidth(width);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    // The stroker's curve-approximation tolerance is in the path's own
    // units - millimeters here, NOT pixels. Its 0.25 default would allow
    // a quarter-millimeter deviation, quietly reintroducing the same
    // class of error the polyline had. 0.001mm (1 micron) keeps the
    // stroke outline exact at any realistic zoom.
    stroker.setCurveThreshold(0.001);
    return stroker.createStroke(centerline);
}

// The region's closed outline: from vertex to vertex, along a straight
// edge or an arc (see GraphicObject::regionEdges), then closed back to the
// start. The parser only emits a Region object that can enclose an area,
// so no empty-region check is needed here.
QPainterPath resolveRegionPath(const QVector<PointNm> &regionPoints, const QVector<RegionEdge> &regionEdges)
{
    QPainterPath region;
    if (regionPoints.isEmpty()) {
        return region;
    }
    region.moveTo(toMm(regionPoints.first()));
    for (int i = 1; i < regionPoints.size(); ++i) {
        const QPointF from = toMm(regionPoints.at(i - 1));
        const QPointF to = toMm(regionPoints.at(i));
        if (i < regionEdges.size() && regionEdges.at(i).isArc) {
            appendArc(region, from, to, toMm(regionEdges.at(i).arcCenter), regionEdges.at(i).arcCounterClockwise);
        }
        // The straight edge - or, after an arc, a snap onto the vertex: the
        // file's end point can sit a hair off the circle the arc was drawn
        // on (its radius comes from the start point), and the next edge
        // must start where the file says.
        region.lineTo(to);
    }
    region.closeSubpath();
    return region;
}

} // namespace

QPainterPath GerberObjectPaths::objectPathMm(const GraphicObject &object, const Layer &layer)
{
    switch (object.type) {
    case ObjectType::Flash:
        return resolveFlashPath(layer.apertures.value(object.apertureCode), toMm(object.position), layer);
    case ObjectType::Stroke:
        if (object.isArc) {
            return resolveArcStrokePath(layer.apertures.value(object.apertureCode), toMm(object.position),
                                        toMm(object.strokeTo), toMm(object.arcCenter),
                                        object.arcCounterClockwise);
        }
        return resolveLinearStrokePath(layer.apertures.value(object.apertureCode), toMm(object.position),
                                       toMm(object.strokeTo));
    case ObjectType::Region:
        return resolveRegionPath(object.regionPoints, object.regionEdges);
    }
    return QPainterPath(); // unreachable - all ObjectType values handled above
}

QRectF GerberObjectPaths::layerBoundsMm(const Layer &layer)
{
    QRectF bounds;
    for (const GraphicObject &object : layer.objects) {
        bounds = bounds.united(objectPathMm(object, layer).boundingRect());
    }
    return bounds;
}
