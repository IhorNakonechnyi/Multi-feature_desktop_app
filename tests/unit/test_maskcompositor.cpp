#include <QtTest>

#include "gerber/maskcompositor.h"

// Pixel asserts for the additive color compositor - the piece that
// produces the CAMMaster-style overlay colors (pure A, pure B, blended
// overlap) from monochrome coverage masks.
class TestMaskCompositor : public QObject {
    Q_OBJECT

private slots:
    void overlapAddsColors();
};

namespace {

// A LayerMask covering the given x range (full height) of a 10x10px
// image - handmade, no renderer involved, to keep this test independent.
LayerMask bandMask(int xFrom, int xTo)
{
    LayerMask mask;
    mask.image = QImage(10, 10, QImage::Format_ARGB32_Premultiplied);
    mask.image.fill(Qt::transparent);
    for (int y = 0; y < 10; ++y) {
        for (int x = xFrom; x <= xTo; ++x) {
            mask.image.setPixel(x, y, qRgba(255, 255, 255, 255));
        }
    }
    mask.windowMm = QRectF(0, 0, 10, 10);
    mask.pxPerMm = 1.0;
    return mask;
}

} // namespace

void TestMaskCompositor::overlapAddsColors()
{
    // Red band covering x 0..4, green band covering x 3..7: the overlap
    // (x 3..4) must be yellow (red + green light), the exclusive parts
    // pure red / pure green, and uncovered pixels (x 8..9) black. That
    // color arithmetic IS the compare feature's visual signal.
    const QVector<MaskCompositor::TintedMask> layers = {
        {bandMask(0, 4), QColor(255, 0, 0)},
        {bandMask(3, 7), QColor(0, 255, 0)},
    };

    const QImage result = MaskCompositor::composite(layers);

    QCOMPARE(result.size(), QSize(10, 10));
    QCOMPARE(result.pixelColor(1, 5), QColor(255, 0, 0));   // red only
    QCOMPARE(result.pixelColor(6, 5), QColor(0, 255, 0));   // green only
    QCOMPARE(result.pixelColor(4, 5), QColor(255, 255, 0)); // overlap: yellow
    QCOMPARE(result.pixelColor(9, 5), QColor(0, 0, 0));     // no material: black
}

QTEST_MAIN(TestMaskCompositor)
#include "test_maskcompositor.moc"
