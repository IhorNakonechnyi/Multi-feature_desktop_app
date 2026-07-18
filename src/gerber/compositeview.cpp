#include "compositeview.h"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QTimer>
#include <QWheelEvent>

namespace {

// One wheel notch zooms 15% - matches the feel of the previous viewer.
constexpr double kZoomFactorPerStep = 1.15;

// Deepest zoom-in allowed: 500 px per mm (2 microns per pixel) - beyond
// any real inspection need, cheap insurance against runaway zoom.
constexpr double kMaxPxPerMm = 500.0;

// How long after the last wheel/drag event the view is considered
// "settled" and a crisp re-render is requested.
constexpr int kSettleMs = 120;

} // namespace

CompositeView::CompositeView(QWidget *parent)
    : QWidget(parent)
{
    setAutoFillBackground(false);
    setMouseTracking(false);
    settleTimer_ = new QTimer(this);
    settleTimer_->setSingleShot(true);
    settleTimer_->setInterval(kSettleMs);
    connect(settleTimer_, &QTimer::timeout, this, [this]() {
        if (!windowMm_.isEmpty()) {
            emit renderRequested(windowMm_, pxPerMm());
        }
    });
}

double CompositeView::pxPerMm() const
{
    return windowMm_.isEmpty() ? 1.0 : width() / windowMm_.width();
}

void CompositeView::fitTo(const QRectF &contentBoundsMm)
{
    if (contentBoundsMm.isEmpty() || width() <= 0 || height() <= 0) {
        return;
    }
    // 5% margin so the board's edge isn't glued to the widget border.
    const QRectF padded = contentBoundsMm.adjusted(-contentBoundsMm.width() * 0.05,
                                                   -contentBoundsMm.height() * 0.05,
                                                   contentBoundsMm.width() * 0.05,
                                                   contentBoundsMm.height() * 0.05);
    const double scale = qMin(width() / padded.width(), height() / padded.height());
    fitPxPerMm_ = scale;

    // The window matches the viewport's aspect ratio at the fitted
    // scale, centered on the content.
    const double windowWidthMm = width() / scale;
    const double windowHeightMm = height() / scale;
    const QPointF center = padded.center();
    windowMm_ = QRectF(center.x() - windowWidthMm / 2.0, center.y() - windowHeightMm / 2.0, windowWidthMm,
                       windowHeightMm);
    emit renderRequested(windowMm_, pxPerMm());
    update();
}

void CompositeView::setComposite(const QImage &image, const QRectF &imageWindowMm)
{
    image_ = image;
    imageWindowMm_ = imageWindowMm;
    update();
}

void CompositeView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (image_.isNull() || windowMm_.isEmpty() || imageWindowMm_.isEmpty()) {
        return;
    }

    // Place the cached image (covering imageWindowMm_) inside the
    // currently-desired window. When they're the same rect - the normal,
    // settled state - this is an exact 1:1 blit. During zoom/pan the
    // image lands scaled/shifted (fast preview) until the next crisp
    // render replaces it. Y note: image row 0 is the window's TOP of
    // board (largest y, QRectF::bottom()), so the image's on-screen top
    // edge sits where the desired window's bottom() maps to.
    const double scale = pxPerMm();
    const double x = (imageWindowMm_.left() - windowMm_.left()) * scale;
    const double y = (windowMm_.bottom() - imageWindowMm_.bottom()) * scale;
    const QRectF target(x, y, imageWindowMm_.width() * scale, imageWindowMm_.height() * scale);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(target, image_);
}

void CompositeView::wheelEvent(QWheelEvent *event)
{
    if (windowMm_.isEmpty()) {
        event->ignore();
        return;
    }
    const double oldScale = pxPerMm();
    double newScale = (event->angleDelta().y() > 0) ? oldScale * kZoomFactorPerStep
                                                    : oldScale / kZoomFactorPerStep;
    // Clamp: no deeper than kMaxPxPerMm, no farther out than half the
    // fitted ("whole board") scale.
    newScale = qBound(fitPxPerMm_ > 0.0 ? fitPxPerMm_ * 0.5 : newScale, newScale, kMaxPxPerMm);
    if (qFuzzyCompare(newScale, oldScale)) {
        event->accept();
        return;
    }

    // Keep the board point under the cursor stationary through the zoom.
    const QPointF pos = event->position();
    const double cursorMmX = windowMm_.left() + pos.x() / oldScale;
    const double cursorMmY = windowMm_.bottom() - pos.y() / oldScale;

    const double newWidthMm = width() / newScale;
    const double newHeightMm = height() / newScale;
    const double newLeft = cursorMmX - pos.x() / newScale;
    const double newBottom = cursorMmY + pos.y() / newScale;
    windowMm_ = QRectF(newLeft, newBottom - newHeightMm, newWidthMm, newHeightMm);

    scheduleRender();
    update();
    event->accept();
}

void CompositeView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        lastDragPos_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void CompositeView::mouseMoveEvent(QMouseEvent *event)
{
    if (!dragging_ || windowMm_.isEmpty()) {
        return;
    }
    const QPoint delta = event->pos() - lastDragPos_;
    lastDragPos_ = event->pos();
    const double scale = pxPerMm();
    // Content follows the cursor, so the window moves the opposite way.
    // Screen-down (positive dy) reveals lower board Y - hence the sign
    // difference between the two axes (the Y flip again, this time in
    // interaction space).
    windowMm_.translate(-delta.x() / scale, delta.y() / scale);
    scheduleRender();
    update();
}

void CompositeView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        dragging_ = false;
        unsetCursor();
        scheduleRender();
    }
}

void CompositeView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (windowMm_.isEmpty()) {
        return;
    }
    // Keep the current scale; grow/shrink the visible window to the new
    // viewport instead of stretching the content. The old scale must be
    // computed from the OLD width - windowMm_ still describes the
    // pre-resize view.
    const double oldScale = (event->oldSize().width() > 0) ? event->oldSize().width() / windowMm_.width()
                                                           : pxPerMm();
    const double newWidthMm = width() / oldScale;
    const double newHeightMm = height() / oldScale;
    const QPointF center = windowMm_.center();
    windowMm_ = QRectF(center.x() - newWidthMm / 2.0, center.y() - newHeightMm / 2.0, newWidthMm, newHeightMm);
    scheduleRender();
}

void CompositeView::scheduleRender()
{
    settleTimer_->start();
}
