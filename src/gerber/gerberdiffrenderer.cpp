#include "gerberdiffrenderer.h"

#include <cmath>

#include <QGraphicsScene>
#include <QPainter>
#include <QWheelEvent>

namespace {

constexpr qreal kGridSpacingMm = 0.1;
constexpr qreal kGridDotSizeMm = 0.05;
constexpr qreal kZoomFactorPerStep = 1.15;

} // namespace

GerberDiffRenderer::GerberDiffRenderer(QWidget *parent)
    : QGraphicsView(parent)
{
    setScene(new QGraphicsScene(this));
    setRenderHint(QPainter::Antialiasing);
}

void GerberDiffRenderer::drawBackground(QPainter *painter, const QRectF &rect)
{
    painter->fillRect(rect, Qt::black);

    const qreal startX = std::floor(rect.left() / kGridSpacingMm) * kGridSpacingMm;
    const qreal startY = std::floor(rect.top() / kGridSpacingMm) * kGridSpacingMm;
    const int columns = static_cast<int>((rect.right() - startX) / kGridSpacingMm) + 1;
    const int rows = static_cast<int>((rect.bottom() - startY) / kGridSpacingMm) + 1;

    // Rendering safety net against an unresponsive UI at extreme zoom-out -
    // not the real zoom-bound feature (Phase 3b.2 clamps the zoom itself
    // once there's a geometry bounding box to clamp against).
    constexpr qint64 kMaxDots = 200000;
    if (static_cast<qint64>(columns) * static_cast<qint64>(rows) > kMaxDots) {
        return;
    }

    painter->setPen(Qt::NoPen);
    painter->setBrush(Qt::white);
    for (int row = 0; row < rows; ++row) {
        const qreal y = startY + row * kGridSpacingMm;
        for (int col = 0; col < columns; ++col) {
            const qreal x = startX + col * kGridSpacingMm;
            painter->drawEllipse(QPointF(x, y), kGridDotSizeMm / 2.0, kGridDotSizeMm / 2.0);
        }
    }
}

void GerberDiffRenderer::wheelEvent(QWheelEvent *event)
{
    const qreal factor = (event->angleDelta().y() > 0) ? kZoomFactorPerStep : 1.0 / kZoomFactorPerStep;
    scale(factor, factor);
    event->accept();
}
