#include <QtTest>

#include <cmath>
#include <numbers>

#include "gerber/gerberformat.h"
#include "gerber/layermaskrenderer.h"

// Pixel-assert suite for LayerMaskRenderer - the v2 design's testing
// backbone (constitution principle 4: this is core calculation logic
// where a silent bug produces wrong results). Every test renders a
// small synthetic Layer (built directly from the object model, no
// Gerber text involved) and asserts on the resulting mask's pixels.
//
// Reading convention used throughout: "covered" means alpha > 127 at a
// pixel, "empty" means alpha < 128 - the midpoint splits anti-aliased
// edge pixels so assertions stay away from exact edge values.
class TestLayerMaskRenderer : public QObject {
    Q_OBJECT

private slots:
    void circleFlashCoversItsDisc();
    void higherYRendersNearerRowZero();
    void clearPolarityErasesHole();
    void darkAfterClearRefills();
    void selfOverlapStaysUniform();
};

namespace {

PointNm nm(double xMm, double yMm)
{
    return PointNm{GerberFormat::mmToNm(xMm), GerberFormat::mmToNm(yMm)};
}

bool covered(const LayerMask &mask, int xPx, int yPx)
{
    return qAlpha(mask.image.pixel(xPx, yPx)) > 127;
}

} // namespace

void TestLayerMaskRenderer::circleFlashCoversItsDisc()
{
    // A 4mm circle flashed at (5,5) in a 10x10mm window at 10 px/mm:
    // the disc's pixel center is (50,50) with a 20px radius.
    GerberAperture circle;
    circle.shape = GerberAperture::Shape::Circle;
    circle.modifiers = {4.0};

    Layer layer;
    layer.apertures.insert(10, circle);

    GraphicObject flash;
    flash.type = ObjectType::Flash;
    flash.apertureCode = 10;
    flash.position = nm(5.0, 5.0);
    layer.objects.append(flash);

    const LayerMask mask = LayerMaskRenderer::render(layer, QRectF(0.0, 0.0, 10.0, 10.0), 10.0);

    QCOMPARE(mask.image.size(), QSize(100, 100));
    QVERIFY2(covered(mask, 50, 50), "disc center must be material");
    QVERIFY2(covered(mask, 50 + 15, 50), "point well inside the 20px radius must be material");
    QVERIFY2(!covered(mask, 50 + 25, 50), "point outside the radius must be empty");
    QVERIFY2(!covered(mask, 5, 5), "window corner far from the disc must be empty");

    // Area sanity: the disc should cover ~pi*r^2 = ~1257 px. A shape
    // rendered as a crude polygon (the bug class v2 exists to prevent)
    // loses several percent of area; allow 2%.
    int coveredCount = 0;
    for (int y = 0; y < mask.image.height(); ++y) {
        for (int x = 0; x < mask.image.width(); ++x) {
            if (covered(mask, x, y)) {
                ++coveredCount;
            }
        }
    }
    const double expectedArea = std::numbers::pi * 20.0 * 20.0;
    QVERIFY2(std::abs(coveredCount - expectedArea) < expectedArea * 0.02,
             qPrintable(QStringLiteral("disc area %1 px deviates >2% from expected %2 px")
                            .arg(coveredCount)
                            .arg(expectedArea)));
}

void TestLayerMaskRenderer::higherYRendersNearerRowZero()
{
    // Gerber Y is up; image row 0 is the top. A flash near the window's
    // TOP in board space (y=9 of 10mm) must land near ROW 0, not row 90.
    // This pins the single-axis flip whose absence was a real, confirmed
    // bug in the previous pipeline (boards rendered mirrored, text
    // backwards).
    GerberAperture circle;
    circle.shape = GerberAperture::Shape::Circle;
    circle.modifiers = {2.0};

    Layer layer;
    layer.apertures.insert(10, circle);

    GraphicObject flash;
    flash.type = ObjectType::Flash;
    flash.apertureCode = 10;
    flash.position = nm(5.0, 9.0);
    layer.objects.append(flash);

    const LayerMask mask = LayerMaskRenderer::render(layer, QRectF(0.0, 0.0, 10.0, 10.0), 10.0);

    QVERIFY2(covered(mask, 50, 10), "high-Y flash must appear near the image top");
    QVERIFY2(!covered(mask, 50, 90), "nothing must appear at the mirrored position near the bottom");
}

void TestLayerMaskRenderer::clearPolarityErasesHole()
{
    // A dark 6mm disc with a clear 2mm disc flashed on top of its center
    // (%LPD then %LPC): the classic anti-pad/cutout pattern. The clear
    // flash must ERASE - leaving a hole - not paint more material. This
    // is gerbv's documented open bug class and a bug this project itself
    // shipped once; it must never regress silently again.
    GerberAperture big;
    big.shape = GerberAperture::Shape::Circle;
    big.modifiers = {6.0};
    GerberAperture small;
    small.shape = GerberAperture::Shape::Circle;
    small.modifiers = {2.0};

    Layer layer;
    layer.apertures.insert(10, big);
    layer.apertures.insert(11, small);

    GraphicObject dark;
    dark.type = ObjectType::Flash;
    dark.apertureCode = 10;
    dark.position = nm(5.0, 5.0);
    layer.objects.append(dark);

    GraphicObject clear;
    clear.type = ObjectType::Flash;
    clear.polarity = Polarity::Clear;
    clear.apertureCode = 11;
    clear.position = nm(5.0, 5.0);
    layer.objects.append(clear);

    const LayerMask mask = LayerMaskRenderer::render(layer, QRectF(0.0, 0.0, 10.0, 10.0), 10.0);

    QVERIFY2(!covered(mask, 50, 50), "clear flash must leave a hole at the center");
    QVERIFY2(covered(mask, 50 + 20, 50), "the dark ring outside the hole must remain material");
}

void TestLayerMaskRenderer::darkAfterClearRefills()
{
    // Gerber's image accumulation is strictly file-ordered: dark, then
    // clear, then dark again re-adds material inside the erased area.
    // (A polarity-grouped boolean pipeline gets this right only by
    // careful ordering; here it must fall out of paint order.)
    GerberAperture big;
    big.shape = GerberAperture::Shape::Circle;
    big.modifiers = {6.0};
    GerberAperture small;
    small.shape = GerberAperture::Shape::Circle;
    small.modifiers = {2.0};

    Layer layer;
    layer.apertures.insert(10, big);
    layer.apertures.insert(11, small);

    GraphicObject dark;
    dark.type = ObjectType::Flash;
    dark.apertureCode = 10;
    dark.position = nm(5.0, 5.0);
    layer.objects.append(dark);

    GraphicObject clear;
    clear.type = ObjectType::Flash;
    clear.polarity = Polarity::Clear;
    clear.apertureCode = 10; // erase the whole disc
    clear.position = nm(5.0, 5.0);
    layer.objects.append(clear);

    GraphicObject refill;
    refill.type = ObjectType::Flash;
    refill.apertureCode = 11;
    refill.position = nm(5.0, 5.0);
    layer.objects.append(refill);

    const LayerMask mask = LayerMaskRenderer::render(layer, QRectF(0.0, 0.0, 10.0, 10.0), 10.0);

    QVERIFY2(covered(mask, 50, 50), "dark flash after a clear erase must refill the center");
    QVERIFY2(!covered(mask, 50 + 20, 50), "area erased and not refilled must stay empty");
}

void TestLayerMaskRenderer::selfOverlapStaysUniform()
{
    // Two dark discs overlapping: the overlap region must be EXACTLY as
    // covered as either disc alone - one flat coverage value per layer,
    // the mask-vs-alpha-stacking argument that picked this architecture.
    // Interior pixels must be fully opaque (255), overlap included.
    GerberAperture circle;
    circle.shape = GerberAperture::Shape::Circle;
    circle.modifiers = {4.0};

    Layer layer;
    layer.apertures.insert(10, circle);

    GraphicObject left;
    left.type = ObjectType::Flash;
    left.apertureCode = 10;
    left.position = nm(4.0, 5.0);
    layer.objects.append(left);

    GraphicObject right;
    right.type = ObjectType::Flash;
    right.apertureCode = 10;
    right.position = nm(6.0, 5.0);
    layer.objects.append(right);

    const LayerMask mask = LayerMaskRenderer::render(layer, QRectF(0.0, 0.0, 10.0, 10.0), 10.0);

    const int soloAlpha = qAlpha(mask.image.pixel(25, 50));    // deep inside left disc only
    const int overlapAlpha = qAlpha(mask.image.pixel(50, 50)); // deep inside the overlap
    QCOMPARE(soloAlpha, 255);
    QCOMPARE(overlapAlpha, 255);
}

QTEST_MAIN(TestLayerMaskRenderer)
#include "test_layermaskrenderer.moc"
