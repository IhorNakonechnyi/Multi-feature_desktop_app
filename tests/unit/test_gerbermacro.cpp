#include <QtTest/QtTest>

#include "gerber/gerbermacro.h"

class TestGerberMacro : public QObject {
    Q_OBJECT

private slots:
    void circlePrimitiveProducesExpectedBounds();
    void polygonPrimitiveMatchesExpressionSubstitution();
    void variableAssignmentIsUsableByLaterStatement();
    void subtractiveExposureRemovesArea();
    void unsupportedPrimitiveFallsBackToMarkerCircle();
    void vectorLinePrimitiveProducesExpectedBounds();
    void centerLinePrimitiveProducesExpectedBounds();
    void outlinePrimitiveProducesExpectedTriangle();
    void thermalPrimitiveProducesRingWithGaps();
    void rotationIsAppliedAroundMacroOriginNotPrimitiveCenter();
    void thermalRotationIsAppliedAroundMacroOriginNotPrimitiveCenter();
    void circleRotationIsAppliedAroundMacroOriginNotPrimitiveCenter();
    void unitScaleConvertsLengthFieldsButNotRotationOrCount();
};

void TestGerberMacro::circlePrimitiveProducesExpectedBounds()
{
    GerberMacro macro;
    macro.statements = {"1,1,2,0,0"}; // circle, exposure on, diameter 2, center (0,0)
    const QPainterPath path = GerberMacroBuilder::buildPath(macro, {});
    const QRectF bounds = path.boundingRect();
    QVERIFY(qAbs(bounds.width() - 2.0) < 0.01);
}

void TestGerberMacro::polygonPrimitiveMatchesExpressionSubstitution()
{
    // Shaped like the real octagon macro that motivated this whole design
    // (primitive 5/polygon, 8 vertices, diameter computed from a $-scaled
    // expression), but with synthetic values, not the real sample's own
    // macro name/modifier numbers - see the plan's confidentiality
    // constraint (no real customer file content in the test suite).
    GerberMacro macro;
    macro.statements = {"5,1,8,0,0,2.0X$1,0"};
    const QPainterPath path = GerberMacroBuilder::buildPath(macro, {0.5});
    const double expectedDiameter = 2.0 * 0.5;
    const QRectF bounds = path.boundingRect();
    QVERIFY(qAbs(bounds.width() - expectedDiameter) < 0.001);
}

void TestGerberMacro::variableAssignmentIsUsableByLaterStatement()
{
    GerberMacro macro;
    macro.statements = {"$2=$1X2", "1,1,$2,0,0"}; // diameter = 2 * $1
    const QPainterPath path = GerberMacroBuilder::buildPath(macro, {1.5});
    const QRectF bounds = path.boundingRect();
    QVERIFY(qAbs(bounds.width() - 3.0) < 0.01); // 2 * 1.5
}

void TestGerberMacro::subtractiveExposureRemovesArea()
{
    GerberMacro macro;
    // Big circle (exposure on) then a smaller circle (exposure off) cut
    // out of its middle - the result must have a hole, i.e. its area
    // must be smaller than the big circle's alone.
    macro.statements = {"1,1,4,0,0", "1,0,2,0,0"};
    const QPainterPath path = GerberMacroBuilder::buildPath(macro, {});

    QPainterPath bigCircleOnly;
    bigCircleOnly.addEllipse(QPointF(0, 0), 2.0, 2.0);

    // A path with a hole cut out has less area than the outer shape
    // alone - approximate "area" via bounding-rect-relative fill testing
    // at the center point, which should now be OUTSIDE the composited
    // path (the hole), even though it's inside the plain big circle.
    QVERIFY(bigCircleOnly.contains(QPointF(0, 0)));
    QVERIFY(!path.contains(QPointF(0, 0)));
}

void TestGerberMacro::unsupportedPrimitiveFallsBackToMarkerCircle()
{
    GerberMacro macro;
    macro.statements = {"6,0,0,1,1,1,0"}; // primitive 6 (Moire) - deliberately unimplemented
    const QPainterPath path = GerberMacroBuilder::buildPath(macro, {});
    QVERIFY(!path.isEmpty());
    const QRectF bounds = path.boundingRect();
    QVERIFY(qAbs(bounds.width() - 1.0) < 0.01); // the fixed 1mm-diameter fallback marker
}

void TestGerberMacro::vectorLinePrimitiveProducesExpectedBounds()
{
    GerberMacro macro;
    // vector line, exposure on, width 1, from (0,0) to (4,0), no rotation.
    macro.statements = {"20,1,1,0,0,4,0,0"};
    const QPainterPath path = GerberMacroBuilder::buildPath(macro, {});
    const QRectF bounds = path.boundingRect();
    QVERIFY(qAbs(bounds.width() - 4.0) < 0.01);
    QVERIFY(qAbs(bounds.height() - 1.0) < 0.01);
}

void TestGerberMacro::centerLinePrimitiveProducesExpectedBounds()
{
    GerberMacro macro;
    // center line, exposure on, width 4, height 2, center (0,0), no rotation.
    macro.statements = {"21,1,4,2,0,0,0"};
    const QPainterPath path = GerberMacroBuilder::buildPath(macro, {});
    const QRectF bounds = path.boundingRect();
    QVERIFY(qAbs(bounds.width() - 4.0) < 0.01);
    QVERIFY(qAbs(bounds.height() - 2.0) < 0.01);
}

void TestGerberMacro::outlinePrimitiveProducesExpectedTriangle()
{
    GerberMacro macro;
    // outline, exposure on, 3 vertices, triangle (0,0)-(4,0)-(4,4), closed
    // back to (0,0), no rotation.
    macro.statements = {"4,1,3,0,0,4,0,4,4,0,0,0"};
    const QPainterPath path = GerberMacroBuilder::buildPath(macro, {});
    const QRectF bounds = path.boundingRect();
    QVERIFY(qAbs(bounds.width() - 4.0) < 0.01);
    QVERIFY(qAbs(bounds.height() - 4.0) < 0.01);
    QVERIFY(path.contains(QPointF(3, 1))); // below the hypotenuse - inside
    QVERIFY(!path.contains(QPointF(1, 3))); // above the hypotenuse - outside
}

void TestGerberMacro::thermalPrimitiveProducesRingWithGaps()
{
    GerberMacro macro;
    // thermal, center (0,0), outer diameter 4, inner diameter 2, gap 0.5,
    // no rotation - gaps land on the x/y axes.
    macro.statements = {"7,0,0,4,2,0.5,0"};
    const QPainterPath path = GerberMacroBuilder::buildPath(macro, {});
    const QRectF bounds = path.boundingRect();
    // The gap bands cut through the outer circle's extreme axis points,
    // and Qt's boolean path subtraction tessellates the circle into an
    // inscribed polygon first, so the tight bounding box ends up
    // noticeably under the nominal outer diameter (4.0) rather than
    // exactly equal to it. Use a sanity range instead of a tight
    // tolerance: clearly outer-diameter-sized (not shrunk to the inner
    // diameter or collapsed), but never larger than the outer diameter.
    QVERIFY(bounds.width() <= 4.01);
    QVERIFY(bounds.width() > 3.0);
    QVERIFY(bounds.height() <= 4.01);
    QVERIFY(bounds.height() > 3.0);
    QVERIFY(!path.contains(QPointF(0, 0))); // inner hole
    QVERIFY(!path.contains(QPointF(1.5, 0))); // sits in the horizontal gap
    QVERIFY(path.contains(QPointF(1.0606, 1.0606))); // off-axis - solid ring
}

void TestGerberMacro::rotationIsAppliedAroundMacroOriginNotPrimitiveCenter()
{
    // Regression test for the rotation-pivot bug: per the Gerber Format
    // Specification, primitive rotation is always around the macro's
    // origin (0,0), not the primitive's own center. A center line of
    // width 4, height 2, centered at (5,0) and rotated 90 degrees should
    // end up with its bounding box centered near (0,5) - the position
    // (5,0) rotated 90 degrees around the origin - not still near (5,0)
    // (which is what rotating around the primitive's own center would
    // incorrectly produce).
    GerberMacro macro;
    macro.statements = {"21,1,4,2,5,0,90"};
    const QPainterPath path = GerberMacroBuilder::buildPath(macro, {});
    const QRectF bounds = path.boundingRect();
    const QPointF boundsCenter = bounds.center();
    QVERIFY(qAbs(boundsCenter.x() - 0.0) < 0.01);
    QVERIFY(qAbs(boundsCenter.y() - 5.0) < 0.01);
}

void TestGerberMacro::thermalRotationIsAppliedAroundMacroOriginNotPrimitiveCenter()
{
    // Regression test for the same rotation-pivot bug as above, but for
    // Thermal (primitive 7), which the prior fix did not touch: a thermal
    // centered at (5,0), outer diameter 4, inner diameter 2, gap 0.5,
    // rotated 90 degrees should end up with its ring near (0,5) - the
    // position (5,0) rotated 90 degrees around the macro's origin - not
    // still near (5,0) (rotating around the primitive's own center, which
    // is what the pre-fix code incorrectly did, and even then only
    // reoriented the gap wedges without moving the ring at all).
    GerberMacro macro;
    macro.statements = {"7,5,0,4,2,0.5,90"};
    const QPainterPath path = GerberMacroBuilder::buildPath(macro, {});
    const QRectF bounds = path.boundingRect();
    const QPointF boundsCenter = bounds.center();
    QVERIFY(qAbs(boundsCenter.x() - 0.0) < 0.01);
    QVERIFY(qAbs(boundsCenter.y() - 5.0) < 0.01);

    // The whole rigid body (ring position, hole, and gap orientation)
    // rotates together around the origin: the point that was the
    // primitive's own center (5,0) maps to (0,5) and must now be the
    // (still-empty) inner hole, the horizontal gap band that was at
    // world (6.5,0) maps to (0,6.5), and the off-axis solid-ring point
    // that was at world (6.0606,1.0606) maps to (-1.0606,6.0606).
    QVERIFY(!path.contains(QPointF(0, 5))); // inner hole, now at the rotated center
    QVERIFY(!path.contains(QPointF(0, 6.5))); // gap band, rotated along with everything else
    QVERIFY(path.contains(QPointF(-1.0606, 6.0606))); // off-axis - solid ring
}

void TestGerberMacro::circleRotationIsAppliedAroundMacroOriginNotPrimitiveCenter()
{
    // Regression test for the same rotation-pivot gap as above, but for
    // Circle (primitive 1): buildCircle never read its optional rotation
    // field at all. A circle of diameter 2, centered at (5,0) and rotated
    // 90 degrees around the macro's origin should end up centered near
    // (0,5) - not still near (5,0), which is what happens if rotation is
    // silently ignored.
    GerberMacro macro;
    macro.statements = {"1,1,2,5,0,90"};
    const QPainterPath path = GerberMacroBuilder::buildPath(macro, {});
    const QRectF bounds = path.boundingRect();
    const QPointF boundsCenter = bounds.center();
    QVERIFY(qAbs(boundsCenter.x() - 0.0) < 0.01);
    QVERIFY(qAbs(boundsCenter.y() - 5.0) < 0.01);
}

void TestGerberMacro::unitScaleConvertsLengthFieldsButNotRotationOrCount()
{
    // Real-world regression: a real inches-based archive's custom pad
    // shapes rendered ~25x too small, because macro primitive length
    // fields (diameter, center offset, width) were never converted from
    // the file's unit into millimeters, unlike ordinary coordinates (which
    // always were). unitScale (25.4 for an inches file) must multiply
    // every length field - but NOT rotation (an angle) or vertex count (a
    // count), which stay meaningless if scaled.
    //
    // A polygon (primitive 5): exposure on, 4 vertices, center (1,0) [in
    // file units], diameter 2 [in file units], rotation 0. At unitScale =
    // 25.4 (inches), the real center/diameter must come out 25.4x bigger;
    // rotation and vertex count must not change the shape's fundamental
    // 4-sided-ness.
    GerberMacro macro;
    macro.statements = {"5,1,4,1,0,2,0"};

    const QPainterPath mmPath = GerberMacroBuilder::buildPath(macro, {}, 1.0);
    const QPainterPath inchPath = GerberMacroBuilder::buildPath(macro, {}, 25.4);

    const QRectF mmBounds = mmPath.boundingRect();
    const QRectF inchBounds = inchPath.boundingRect();

    // Diameter (and therefore bounding box size) scales by exactly 25.4.
    QVERIFY(qAbs(inchBounds.width() - mmBounds.width() * 25.4) < 0.01);
    QVERIFY(qAbs(inchBounds.height() - mmBounds.height() * 25.4) < 0.01);
    // Center offset scales too - the shape moves out to 25.4x the distance.
    QVERIFY(qAbs(inchBounds.center().x() - mmBounds.center().x() * 25.4) < 0.01);
}

QTEST_MAIN(TestGerberMacro)
#include "test_gerbermacro.moc"
