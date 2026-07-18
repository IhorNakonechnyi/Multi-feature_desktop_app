#include "gerberaperture.h"

#include <cmath>

#include <QtMath>

QPainterPath GerberApertureGeometry::buildFlashPath(
    const GerberAperture &aperture,
    const std::function<QPainterPath(const QString &, const QVector<double> &)> &macroLookup)
{
    QPainterPath path;
    const QVector<double> &m = aperture.modifiers;

    switch (aperture.shape) {
    case GerberAperture::Shape::Circle: {
        // %ADDnnC,diameter[,holeDiameter]*% - the optional hole isn't
        // modeled (not seen in real inputs so far; would just be another
        // subtracted ellipse if it ever is).
        const double diameter = m.value(0, 0.0);
        path.addEllipse(QPointF(0, 0), diameter / 2.0, diameter / 2.0);
        break;
    }
    case GerberAperture::Shape::Rectangle: {
        // %ADDnnR,width,height[,holeDiameter]*%
        const double width = m.value(0, 0.0);
        const double height = m.value(1, 0.0);
        path.addRect(-width / 2.0, -height / 2.0, width, height);
        break;
    }
    case GerberAperture::Shape::Obround: {
        // %ADDnnO,width,height*% - a rectangle with fully-rounded short
        // ends; Qt's rounded-rect with radius = half the shorter side
        // draws exactly that.
        const double width = m.value(0, 0.0);
        const double height = m.value(1, 0.0);
        const double radius = std::min(width, height) / 2.0;
        path.addRoundedRect(-width / 2.0, -height / 2.0, width, height, radius, radius);
        break;
    }
    case GerberAperture::Shape::Polygon: {
        // %ADDnnP,diameter,vertices[,rotation]*% - a regular N-gon
        // inscribed in a circle of the given diameter. Same math as
        // macro primitive 5 (GerberMacroBuilder) - both represent the
        // exact same shape, just declared two different ways.
        const double diameter = m.value(0, 0.0);
        const int vertices = static_cast<int>(m.value(1, 3.0));
        const double rotationDegrees = m.value(2, 0.0);
        QPolygonF polygon;
        for (int i = 0; i < vertices; ++i) {
            const double angle = qDegreesToRadians(rotationDegrees + 360.0 * i / vertices);
            polygon << QPointF((diameter / 2.0) * std::cos(angle), (diameter / 2.0) * std::sin(angle));
        }
        path.addPolygon(polygon);
        path.closeSubpath();
        break;
    }
    case GerberAperture::Shape::Macro:
        path = macroLookup(aperture.macroName, m);
        break;
    }
    return path;
}
