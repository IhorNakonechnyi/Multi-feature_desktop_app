#include "layermaskrenderer.h"

#include <cmath>

#include <QPainter>

#include "gerberobjectpaths.h"

LayerMask LayerMaskRenderer::render(const Layer &layer, const QRectF &windowMm, double pxPerMm)
{
    LayerMask mask;
    mask.windowMm = windowMm;
    mask.pxPerMm = pxPerMm;

    const int widthPx = qMax(1, qCeil(windowMm.width() * pxPerMm));
    const int heightPx = qMax(1, qCeil(windowMm.height() * pxPerMm));

    // ARGB32-premultiplied rather than a plain grayscale format: it is
    // the one format where QPainter supports the full set of composition
    // modes (SourceOver to paint, Clear to erase) at full speed, which
    // is exactly how polarity is implemented below.
    mask.image = QImage(widthPx, heightPx, QImage::Format_ARGB32_Premultiplied);
    mask.image.fill(Qt::transparent);

    QPainter painter(&mask.image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    // Map Gerber-space mm (Y-up) onto image pixels (Y-down): shift to
    // the image's bottom edge, flip Y while scaling to pixels, then move
    // the window's own origin to zero. Applied bottom-up as usual with
    // QPainter transform composition, this sends (left, top=minY) to the
    // image's bottom-left and (left, bottom=maxY) to row 0 - the display
    // flip promised by LayerMask's contract, baked in here once so no
    // consumer ever handles Y conventions.
    painter.translate(0.0, heightPx);
    painter.scale(pxPerMm, -pxPerMm);
    painter.translate(-windowMm.left(), -windowMm.top());

    // File order matters and is preserved: a dark object drawn after a
    // clear cutout refills that area, exactly like the Gerber spec's
    // image-accumulation model. Painting coverage (SourceOver) vs
    // erasing it (Clear) IS the whole polarity implementation - the two
    // lines the v2 design promised would replace boolean geometry.
    for (const GraphicObject &object : layer.objects) {
        painter.setCompositionMode(object.polarity == Polarity::Dark ? QPainter::CompositionMode_SourceOver
                                                                     : QPainter::CompositionMode_Clear);
        painter.fillPath(GerberObjectPaths::objectPathMm(object, layer), Qt::white);
    }

    return mask;
}
