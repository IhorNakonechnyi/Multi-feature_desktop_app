#pragma once

#include <QGraphicsView>

// Canvas for the Gerber diff overlay (FR-015). For now (Phase 3b.1) this is
// an empty grid shell - real old-vs-new geometry rendering is Phase 3b.2.
// Scene coordinates are millimeters, matching Gerber's native unit scale.
// Zoom is mouse-wheel driven and intentionally unclamped for now: real
// min/max bounds need a parsed geometry bounding box, which doesn't exist
// until Phase 3b.2's parser lands.
class GerberDiffRenderer : public QGraphicsView {
    Q_OBJECT

public:
    explicit GerberDiffRenderer(QWidget *parent = nullptr);

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void wheelEvent(QWheelEvent *event) override;
};
