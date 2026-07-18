#include "gerberobjectpaths.h"

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

// A drawn line's "pen" is the current aperture's own shape - for the
// overwhelmingly common circular-aperture case, a stroke is just a
// capsule (rectangle plus two round end caps) between from/to.
QPainterPath resolveLinearStrokePath(const GerberAperture &aperture, const QPointF &from, const QPointF &to)
{
    const double width = aperture.modifiers.value(0, 0.1);
    QPainterPath stroke;
    const double dx = to.x() - from.x();
    const double dy = to.y() - from.y();
    const double length = std::hypot(dx, dy);
    const double angleDegrees = qRadiansToDegrees(std::atan2(dy, dx));
    stroke.addRoundedRect(0, -width / 2.0, length, width, width / 2.0, width / 2.0);
    QTransform transform;
    transform.translate(from.x(), from.y());
    transform.rotate(angleDegrees);
    return transform.map(stroke);
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

    const double startAngle = std::atan2(from.y() - center.y(), from.x() - center.x());
    double endAngle = std::atan2(to.y() - center.y(), to.x() - center.x());
    const double radius = std::hypot(from.x() - center.x(), from.y() - center.y());

    if (counterClockwise && endAngle < startAngle) {
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
    QPainterPath centerline;
    centerline.moveTo(from);
    centerline.arcTo(circle, -qRadiansToDegrees(startAngle), -qRadiansToDegrees(endAngle - startAngle));

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

// Same closed polygon the parser's old G37 block built: the collected
// contour vertices, closed. The parser only emits a Region object when
// it has at least 3 vertices, so no empty-region check is needed here.
QPainterPath resolveRegionPath(const QVector<PointNm> &regionPoints)
{
    QPolygonF polygon;
    polygon.reserve(regionPoints.size());
    for (const PointNm &vertex : regionPoints) {
        polygon << toMm(vertex);
    }
    QPainterPath region;
    region.addPolygon(polygon);
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
        return resolveRegionPath(object.regionPoints);
    }
    return QPainterPath(); // unreachable - all ObjectType values handled above
}
