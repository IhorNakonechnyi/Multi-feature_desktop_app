#pragma once

#include <QRectF>
#include <QWidget>

#include "gerberobjectmodel.h"

class QLabel;
class CompositeView;

// Single-file Gerber viewer - the v2 design's M3 test milestone: open
// one Gerber layer via the file picker and render it smoothly and
// correctly (the visual gate judged against CAMMaster). Glue only -
// parse, render and display all live in their own tested modules; this
// page just connects them.
class GerberViewerPage : public QWidget {
    Q_OBJECT

public:
    explicit GerberViewerPage(QWidget *parent = nullptr);

private:
    void openFile();
    void renderWindow(const QRectF &windowMm, double pxPerMm);

    Layer layer_;
    QRectF layerBoundsMm_;
    QLabel *statusLabel_ = nullptr;
    CompositeView *view_ = nullptr;
};
