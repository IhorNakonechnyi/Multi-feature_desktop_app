#include "gerbermacro.h"

#include <cmath>

#include <QHash>
#include <QStringList>
#include <QTransform>
#include <QtMath>

#include "gerberexpression.h"

namespace {

// Splits a primitive statement's comma-separated fields and evaluates
// each one as an expression (fields can be plain numbers, $-variables,
// or arithmetic like a scaling expression "2.0X$1") - this is the one
// place macro fields turn from text into actual numbers.
QVector<double> evaluateFields(const QStringList &fields, const QHash<int, double> &variables)
{
    QVector<double> values;
    values.reserve(fields.size());
    for (const QString &field : fields) {
        values.append(GerberExpression::evaluate(field, variables));
    }
    return values;
}

// Each buildXxx function below takes the primitive's fields AFTER the
// leading primitive-code number (already evaluated to real numbers) and
// returns that primitive's shape, centered/positioned per its own
// modifiers - see the Gerber Format Specification for each primitive's
// exact field layout, summarized in a comment above each function.

QPainterPath buildCircle(const QVector<double> &v, double unitScale)
{
    // 1,exposure,diameter,centerX,centerY[,rotation] - diameter and the
    // center offset are lengths (scaled); rotation (below) is an angle,
    // never scaled.
    QPainterPath path;
    const double diameter = v.value(1) * unitScale;
    const QPointF center(v.value(2) * unitScale, v.value(3) * unitScale);
    path.addEllipse(center, diameter / 2.0, diameter / 2.0);

    // Rotation is around the macro's origin (0,0), not this primitive's
    // own center - same pattern as buildCenterLine/buildPolygon/
    // buildThermal/buildVectorLine. A circle's own outline is
    // rotationally symmetric, so this only ever matters when the circle
    // is off-origin (rotation then moves its position).
    QTransform rotateAroundOrigin;
    rotateAroundOrigin.rotate(v.value(4, 0.0));
    return rotateAroundOrigin.map(path);
}

QPainterPath buildVectorLine(const QVector<double> &v, double unitScale)
{
    // 20,exposure,width,startX,startY,endX,endY,rotation - width and both
    // endpoints are lengths (scaled); rotation (below) is an angle, never
    // scaled.
    // Built as a rectangle running along +X from 0 to the line's length,
    // then rotated/translated into place - simpler than working out the
    // perpendicular offset by hand for an arbitrary angle.
    const QPointF start(v.value(2) * unitScale, v.value(3) * unitScale);
    const QPointF end(v.value(4) * unitScale, v.value(5) * unitScale);
    const double width = v.value(1) * unitScale;
    const double dx = end.x() - start.x();
    const double dy = end.y() - start.y();
    const double length = std::hypot(dx, dy);
    const double lineAngleDegrees = qRadiansToDegrees(std::atan2(dy, dx));

    QPainterPath path;
    path.addRect(0, -width / 2.0, length, width);

    QTransform placeAlongLine;
    placeAlongLine.translate(start.x(), start.y());
    placeAlongLine.rotate(lineAngleDegrees);
    path = placeAlongLine.map(path);

    QTransform macroRotation;
    macroRotation.rotate(v.value(6));
    return macroRotation.map(path);
}

QPainterPath buildCenterLine(const QVector<double> &v, double unitScale)
{
    // 21,exposure,width,height,centerX,centerY,rotation - width, height,
    // and the center offset are lengths (scaled); rotation (below) is an
    // angle, never scaled.
    const double width = v.value(1) * unitScale;
    const double height = v.value(2) * unitScale;
    QPainterPath path;
    path.addRect(-width / 2.0, -height / 2.0, width, height);

    QTransform placeAtCenter;
    placeAtCenter.translate(v.value(3) * unitScale, v.value(4) * unitScale);
    path = placeAtCenter.map(path);

    // Rotation is around the macro's origin (0,0), not this primitive's
    // own center - a separate, fresh transform applied to the already-
    // positioned path is what achieves that (same pattern as
    // buildVectorLine/buildOutline use).
    QTransform rotateAroundOrigin;
    rotateAroundOrigin.rotate(v.value(5));
    return rotateAroundOrigin.map(path);
}

QPainterPath buildOutline(const QVector<double> &v, double unitScale)
{
    // 4,exposure,numVertices,X1,Y1,...,X(n+1),Y(n+1),rotation
    // (n+1 points because the last one repeats the first, closing the
    // shape - an arbitrary custom polygon, unlike Polygon's regular N-gon)
    // numVertices is a count and rotation is an angle - neither is scaled;
    // every X/Y vertex coordinate is a length and is.
    const int numVertices = static_cast<int>(v.value(1));
    QPolygonF polygon;
    for (int i = 0; i <= numVertices; ++i) {
        polygon << QPointF(v.value(2 + i * 2) * unitScale, v.value(3 + i * 2) * unitScale);
    }
    const double rotation = v.value(2 + (numVertices + 1) * 2);

    QPainterPath path;
    path.addPolygon(polygon);
    path.closeSubpath();
    QTransform transform;
    transform.rotate(rotation);
    return transform.map(path);
}

QPainterPath buildPolygon(const QVector<double> &v, double unitScale)
{
    // 5,exposure,numVertices,centerX,centerY,diameter,rotation -
    // numVertices is a count and rotation is an angle, neither scaled;
    // the center offset and diameter are lengths and are.
    const int vertices = static_cast<int>(v.value(1));
    const QPointF center(v.value(2) * unitScale, v.value(3) * unitScale);
    const double diameter = v.value(4) * unitScale;
    const double rotation = v.value(5);

    QPolygonF polygon;
    for (int i = 0; i < vertices; ++i) {
        const double angle = qDegreesToRadians(360.0 * i / vertices);
        polygon << QPointF(center.x() + (diameter / 2.0) * std::cos(angle),
                            center.y() + (diameter / 2.0) * std::sin(angle));
    }
    QPainterPath path;
    path.addPolygon(polygon);
    path.closeSubpath();

    // Rotation is around the macro's origin (0,0), not this primitive's
    // own center - same reasoning as buildCenterLine above.
    QTransform rotateAroundOrigin;
    rotateAroundOrigin.rotate(rotation);
    return rotateAroundOrigin.map(path);
}

QPainterPath buildThermal(const QVector<double> &v, double unitScale)
{
    // 7,centerX,centerY,outerDiameter,innerDiameter,gapThickness,rotation
    // (no exposure field - a thermal relief is always exactly this "ring
    // with 4 gaps" shape, so it's built directly rather than composited
    // from separately exposure-tagged primitives like the others).
    // Everything except rotation (an angle) is a length and is scaled.
    const QPointF center(v.value(0) * unitScale, v.value(1) * unitScale);
    const double outerDiameter = v.value(2) * unitScale;
    const double innerDiameter = v.value(3) * unitScale;
    const double gap = v.value(4) * unitScale;
    const double rotation = v.value(5);

    QPainterPath outer;
    outer.addEllipse(center, outerDiameter / 2.0, outerDiameter / 2.0);
    QPainterPath inner;
    inner.addEllipse(center, innerDiameter / 2.0, innerDiameter / 2.0);
    QPainterPath ring = outer.subtracted(inner);

    // Four gap wedges, spaced 90 degrees apart, at their baseline angles
    // only - the primitive's own `rotation` is NOT applied here, since
    // rotation must pivot the WHOLE shape (ring position included)
    // around the macro's origin, not just reorient these wedges around
    // the ring's own center.
    for (int i = 0; i < 4; ++i) {
        QPainterPath gapRect;
        gapRect.addRect(-outerDiameter / 2.0, -gap / 2.0, outerDiameter, gap);
        QTransform placeAtCenter;
        placeAtCenter.translate(center.x(), center.y());
        placeAtCenter.rotate(90.0 * i);
        ring = ring.subtracted(placeAtCenter.map(gapRect));
    }

    // Rotation is around the macro's origin (0,0), not this primitive's
    // own center - applied once to the whole already-positioned ring
    // (same pattern as buildCenterLine/buildPolygon/buildVectorLine/
    // buildOutline).
    QTransform rotateAroundOrigin;
    rotateAroundOrigin.rotate(rotation);
    return rotateAroundOrigin.map(ring);
}

// A macro that uses a primitive code this parser doesn't build (the
// rare/legacy ones - old vector-line alias, Moire, old lower-left-line)
// falls back to this, rather than a partially-composited wrong shape or
// a guess at unfamiliar parameter meanings. 1mm is an arbitrary but
// clearly-visible placeholder size.
QPainterPath fallbackMarkerCircle()
{
    QPainterPath path;
    path.addEllipse(QPointF(0, 0), 0.5, 0.5);
    return path;
}

} // namespace

QPainterPath GerberMacroBuilder::buildPath(const GerberMacro &macro, const QVector<double> &parameters,
                                            double unitScale)
{
    QHash<int, double> variables;
    for (int i = 0; i < parameters.size(); ++i) {
        variables.insert(i + 1, parameters.at(i));
    }

    QPainterPath composited;
    for (const QString &statement : macro.statements) {
        const QString trimmed = statement.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        // "$N=..." assigns a variable rather than drawing a primitive.
        if (trimmed.startsWith(QLatin1Char('$'))) {
            const int equalsIndex = trimmed.indexOf(QLatin1Char('='));
            if (equalsIndex > 0) {
                const int variableNumber = trimmed.mid(1, equalsIndex - 1).toInt();
                variables.insert(variableNumber,
                                  GerberExpression::evaluate(trimmed.mid(equalsIndex + 1), variables));
                continue;
            }
        }

        const QStringList fields = trimmed.split(QLatin1Char(','));
        const int primitiveCode = fields.first().toInt();
        if (primitiveCode == 0) {
            continue; // comment primitive - no geometry
        }

        const QVector<double> values = evaluateFields(fields.mid(1), variables);
        QPainterPath primitivePath;
        double exposure = 1.0;

        switch (primitiveCode) {
        case 1:
            primitivePath = buildCircle(values, unitScale);
            exposure = values.value(0, 1.0);
            break;
        case 20:
            primitivePath = buildVectorLine(values, unitScale);
            exposure = values.value(0, 1.0);
            break;
        case 21:
            primitivePath = buildCenterLine(values, unitScale);
            exposure = values.value(0, 1.0);
            break;
        case 4:
            primitivePath = buildOutline(values, unitScale);
            exposure = values.value(0, 1.0);
            break;
        case 5:
            primitivePath = buildPolygon(values, unitScale);
            exposure = values.value(0, 1.0);
            break;
        case 7:
            primitivePath = buildThermal(values, unitScale);
            exposure = 1.0;
            break;
        default:
            return fallbackMarkerCircle();
        }

        composited = (exposure != 0.0) ? composited.united(primitivePath) : composited.subtracted(primitivePath);
    }
    return composited;
}
