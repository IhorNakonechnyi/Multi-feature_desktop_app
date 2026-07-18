#pragma once

#include <QImage>
#include <QRectF>
#include <QWidget>

class QTimer;

// The canvas of the v2 rendering design: displays an already-composited
// image mapped onto a millimeter window, and owns zoom/pan interaction.
// Contains ZERO Gerber knowledge - its whole vocabulary is "an image",
// "the mm window that image covers", and "the mm window the user wants
// to look at". That ignorance is deliberate: it keeps this widget on
// the display side of the LayerMask boundary, swappable for a GL-backed
// implementation without touching parsing or rendering (the design's
// "C-ready" promise).
//
// Interaction model (the standard viewer trick): wheel-zoom and drag-pan
// update the desired window immediately and repaint by scaling the
// cached image (instant, briefly soft when zooming in). A short settle
// timer then emits renderRequested(window, pxPerMm); the owner renders a
// crisp image for exactly that window and hands it back via
// setComposite(), which repaints 1:1 sharp.
class CompositeView : public QWidget {
    Q_OBJECT

public:
    explicit CompositeView(QWidget *parent = nullptr);

    // Frames `contentBoundsMm` (plus a small margin) in the viewport and
    // requests the first crisp render. Also anchors the zoom-out clamp:
    // you can zoom out to half the fitted scale, no further.
    void fitTo(const QRectF &contentBoundsMm);

    // The crisp render answering (usually) the last renderRequested.
    // imageWindowMm says what window the image actually covers - the
    // paint code positions it correctly even if the desired window has
    // already moved on (stale frames just land shifted/scaled until the
    // next crisp one arrives).
    void setComposite(const QImage &image, const QRectF &imageWindowMm);

signals:
    void renderRequested(const QRectF &windowMm, double pxPerMm);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    double pxPerMm() const;
    void scheduleRender();

    QRectF windowMm_;      // the window the user currently wants to see
    QImage image_;         // last crisp composite received
    QRectF imageWindowMm_; // the window that image covers
    double fitPxPerMm_ = 0.0;
    QPoint lastDragPos_;
    bool dragging_ = false;
    QTimer *settleTimer_ = nullptr;
};
