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
    void equalEndpointArcDrawsFullCircle();
    void rectangleApertureStrokeHasSharpCorners();
    void layerBoundsUnionAllObjects();
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

void TestGerberObjectPaths::equalEndpointArcDrawsFullCircle()
{
    // An arc object whose endpoints coincide is a full 360-degree circle
    // (G75 semantics, see the parser's multi-quadrant test) - the stroke
    // must cover the whole ring, not collapse to a dot at the start
    // point. Circle: center (0,0), radius 10mm, 0.5mm pen.
    GerberAperture roundPen;
    roundPen.shape = GerberAperture::Shape::Circle;
    roundPen.modifiers = {0.5};

    Layer layer;
    layer.apertures.insert(10, roundPen);

    GraphicObject arc;
    arc.type = ObjectType::Stroke;
    arc.apertureCode = 10;
    arc.position = nm(10.0, 0.0);
    arc.strokeTo = nm(10.0, 0.0);
    arc.isArc = true;
    arc.arcCenter = nm(0.0, 0.0);
    arc.arcCounterClockwise = true;

    const QPainterPath path = GerberObjectPaths::objectPathMm(arc, layer);

    QVERIFY2(path.contains(QPointF(10.0, 0.0)), "ring must cover its start point");
    QVERIFY2(path.contains(QPointF(0.0, 10.0)), "ring must cover 90 degrees");
    QVERIFY2(path.contains(QPointF(-10.0, 0.0)), "ring must cover 180 degrees");
    QVERIFY2(path.contains(QPointF(0.0, -10.0)), "ring must cover 270 degrees");
    QVERIFY2(!path.contains(QPointF(0.0, 0.0)), "ring center must stay empty");
}

void TestGerberObjectPaths::rectangleApertureStrokeHasSharpCorners()
{
    // A draw with a RECTANGULAR aperture sweeps the axis-aligned
    // rectangle along the segment (Gerber spec): the result has sharp
    // corners and flat ends - the rectangle does not rotate with the
    // direction and there are no round caps. Rendering it as a
    // round-capped capsule (the circular-pen shape) wrongly rounds off
    // trace corners. Rect 1.0 x 0.5mm, diagonal stroke (0,0) -> (2,2).
    GerberAperture rectPen;
    rectPen.shape = GerberAperture::Shape::Rectangle;
    rectPen.modifiers = {1.0, 0.5};

    Layer layer;
    layer.apertures.insert(10, rectPen);

    GraphicObject stroke;
    stroke.type = ObjectType::Stroke;
    stroke.apertureCode = 10;
    stroke.position = nm(0.0, 0.0);
    stroke.strokeTo = nm(2.0, 2.0);
    layer.objects.append(stroke);

    const QPainterPath path = GerberObjectPaths::objectPathMm(stroke, layer);

    // The end rectangle's outer corner (2.5, 2.25) must be covered right
    // up to its edge - a round cap cuts this corner off.
    QVERIFY2(path.contains(QPointF(2.49, 2.24)), "sharp corner of the swept rectangle must be covered");
    // Directly above the end point, beyond the rectangle's half-height,
    // must be empty - a round cap (radius = half-WIDTH, 0.5) bulges out
    // to y=2.5 there.
    QVERIFY2(!path.contains(QPointF(2.0, 2.4)), "no round-cap bulge beyond the rectangle's half-height");
}

void TestGerberObjectPaths::layerBoundsUnionAllObjects()
{
    // A 4mm circle at (5,5) and a 2mm circle at (20,10): the layer
    // bounds must span both discs - (3,3) to (21,11).
    GerberAperture big;
    big.shape = GerberAperture::Shape::Circle;
    big.modifiers = {4.0};
    GerberAperture small;
    small.shape = GerberAperture::Shape::Circle;
    small.modifiers = {2.0};

    Layer layer;
    layer.apertures.insert(10, big);
    layer.apertures.insert(11, small);

    GraphicObject first;
    first.type = ObjectType::Flash;
    first.apertureCode = 10;
    first.position = nm(5.0, 5.0);
    layer.objects.append(first);

    GraphicObject second;
    second.type = ObjectType::Flash;
    second.apertureCode = 11;
    second.position = nm(20.0, 10.0);
    layer.objects.append(second);

    const QRectF bounds = GerberObjectPaths::layerBoundsMm(layer);

    QVERIFY(std::abs(bounds.left() - 3.0) < 0.01);
    QVERIFY(std::abs(bounds.top() - 3.0) < 0.01);
    QVERIFY(std::abs(bounds.right() - 21.0) < 0.01);
    QVERIFY(std::abs(bounds.bottom() - 11.0) < 0.01);

    QVERIFY2(GerberObjectPaths::layerBoundsMm(Layer{}).isEmpty(), "empty layer must give empty bounds");
}

QTEST_MAIN(TestGerberObjectPaths)
#include "test_gerberobjectpaths.moc"
