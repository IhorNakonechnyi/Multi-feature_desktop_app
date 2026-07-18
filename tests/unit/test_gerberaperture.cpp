#include <QtTest/QtTest>

#include "gerber/gerberaperture.h"

class TestGerberAperture : public QObject {
    Q_OBJECT

private slots:
    void circleHasExpectedBoundingBox();
    void rectangleHasExpectedBoundingBox();
    void obroundHasExpectedBoundingBox();
    void polygonHasExpectedVertexCount();
    void macroDelegatesToLookup();
};

namespace {
QPainterPath noMacroExpected(const QString &, const QVector<double> &)
{
    Q_ASSERT_X(false, "test", "macro lookup should not be called for non-macro shapes");
    return QPainterPath();
}
}

void TestGerberAperture::circleHasExpectedBoundingBox()
{
    GerberAperture aperture;
    aperture.shape = GerberAperture::Shape::Circle;
    aperture.modifiers = {2.0}; // diameter 2mm
    const QPainterPath path = GerberApertureGeometry::buildFlashPath(aperture, noMacroExpected);
    const QRectF bounds = path.boundingRect();
    QVERIFY(qAbs(bounds.width() - 2.0) < 0.01);
    QVERIFY(qAbs(bounds.height() - 2.0) < 0.01);
}

void TestGerberAperture::rectangleHasExpectedBoundingBox()
{
    GerberAperture aperture;
    aperture.shape = GerberAperture::Shape::Rectangle;
    aperture.modifiers = {3.0, 1.0}; // width 3mm, height 1mm
    const QPainterPath path = GerberApertureGeometry::buildFlashPath(aperture, noMacroExpected);
    const QRectF bounds = path.boundingRect();
    QVERIFY(qAbs(bounds.width() - 3.0) < 0.01);
    QVERIFY(qAbs(bounds.height() - 1.0) < 0.01);
}

void TestGerberAperture::obroundHasExpectedBoundingBox()
{
    GerberAperture aperture;
    aperture.shape = GerberAperture::Shape::Obround;
    aperture.modifiers = {3.0, 1.0};
    const QPainterPath path = GerberApertureGeometry::buildFlashPath(aperture, noMacroExpected);
    const QRectF bounds = path.boundingRect();
    QVERIFY(qAbs(bounds.width() - 3.0) < 0.01);
    QVERIFY(qAbs(bounds.height() - 1.0) < 0.01);
}

void TestGerberAperture::polygonHasExpectedVertexCount()
{
    GerberAperture aperture;
    aperture.shape = GerberAperture::Shape::Polygon;
    aperture.modifiers = {2.0, 8.0, 0.0}; // diameter 2mm, 8 vertices (octagon), no rotation
    const QPainterPath path = GerberApertureGeometry::buildFlashPath(aperture, noMacroExpected);
    QCOMPARE(path.elementCount(), 9); // 8 vertices + Qt's implicit close-back-to-start element
}

void TestGerberAperture::macroDelegatesToLookup()
{
    GerberAperture aperture;
    aperture.shape = GerberAperture::Shape::Macro;
    aperture.macroName = "TESTMACRO";
    aperture.modifiers = {0.5};

    bool lookupCalled = false;
    QString capturedName;
    int capturedParamsSize = 0;
    std::function<QPainterPath(const QString &, const QVector<double> &)> lookup =
        [&](const QString &name, const QVector<double> &params) -> QPainterPath {
            lookupCalled = true;
            capturedName = name;
            capturedParamsSize = params.size();
            QPainterPath path;
            path.addEllipse(QPointF(0, 0), 1, 1);
            return path;
        };

    GerberApertureGeometry::buildFlashPath(aperture, lookup);
    QVERIFY(lookupCalled);
    QCOMPARE(capturedName, QString("TESTMACRO"));
    QCOMPARE(capturedParamsSize, 1);
}

QTEST_MAIN(TestGerberAperture)
#include "test_gerberaperture.moc"
