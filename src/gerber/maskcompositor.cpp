#include "maskcompositor.h"

#include <QPainter>

QImage MaskCompositor::composite(const QVector<TintedMask> &layers)
{
    if (layers.isEmpty()) {
        return QImage();
    }

    QImage result(layers.first().mask.image.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::black);

    QPainter painter(&result);
    for (const TintedMask &layer : layers) {
        // Tint: recolor the mask's white coverage with this layer's
        // color while keeping its (possibly anti-aliased) alpha -
        // SourceIn replaces color inside the existing alpha footprint.
        QImage tinted = layer.mask.image;
        QPainter tintPainter(&tinted);
        tintPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tintPainter.fillRect(tinted.rect(), layer.color);
        tintPainter.end();

        // Add: light arithmetic per channel (Plus). On the black base,
        // one layer shows its own color; overlapping layers sum to the
        // blended in-between color (red + green = yellow) with no
        // draw-order dependence - the CAMMaster overlap signal.
        painter.setCompositionMode(QPainter::CompositionMode_Plus);
        painter.drawImage(0, 0, tinted);
    }

    return result;
}
