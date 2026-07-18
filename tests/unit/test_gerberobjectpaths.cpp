#include <QtTest>

#include <cmath>
#include <numbers>

#include "gerber/gerberformat.h"
#include "gerber/gerberobjectpaths.h"

// GerberObjectPaths::objectPathMm resolves one GraphicObject into its
// drawn area as a true-shape QPainterPath. These tests pin the fidelity
// rule from the v2 design doc: no pre-tessellation - in particular, arc
// strokes must follow the true circular curve, not a chord polyline
// (whose sagitta error grows with radius and shows up on-screen as the
// "badly redrawn" look on large-radius board outlines).
class TestGerberObjectPaths : public QObject {
    Q_OBJECT

private slots:
    void arcStrokeFollowsTrueCurve();
};

namespace {

PointNm nm(double xMm, double yMm)
{
    return PointNm{GerberFormat::mmToNm(xMm), GerberFormat::mmToNm(yMm)};
}

} // namespace

void TestGerberObjectPaths::arcStrokeFollowsTrueCurve()
{
    // A quarter-circle arc of radius 1000mm drawn with a 0.2mm round
    // aperture. With a 32-segment chord approximation, the chord midway
    // through one segment sags ~0.3mm inside the true circle - three
    // times the stroke's half-width of 0.1mm - so a point ON the true
    // arc centerline mid-segment falls entirely outside the approximated
    // stroke. A true-curve stroke must contain it.
    GerberAperture roundPen;
    roundPen.shape = GerberAperture::Shape::Circle;
    roundPen.modifiers = {0.2};

    Layer layer;
    layer.apertures.insert(10, roundPen);

    GraphicObject arc;
    arc.type = ObjectType::Stroke;
    arc.apertureCode = 10;
    arc.position = nm(1000.0, 0.0);  // arc start, on the circle
    arc.strokeTo = nm(0.0, 1000.0);  // arc end, quarter turn CCW
    arc.isArc = true;
    arc.arcCenter = nm(0.0, 0.0);
    arc.arcCounterClockwise = true;

    const QPainterPath path = GerberObjectPaths::objectPathMm(arc, layer);

    // Point on the true centerline halfway through the first 1/32nd of
    // the sweep (the spot where a chord approximation deviates most).
    const double midSegmentAngle = (std::numbers::pi / 2.0) / 32.0 / 2.0;
    const QPointF onTrueArc(1000.0 * std::cos(midSegmentAngle), 1000.0 * std::sin(midSegmentAngle));

    QVERIFY2(path.contains(onTrueArc),
             "arc stroke must cover the true circular curve, not a chord polyline");

    // Sanity: both endpoints are covered regardless of representation.
    QVERIFY(path.contains(QPointF(1000.0, 0.0)));
    QVERIFY(path.contains(QPointF(0.0, 1000.0)));
}

QTEST_MAIN(TestGerberObjectPaths)
#include "test_gerberobjectpaths.moc"
