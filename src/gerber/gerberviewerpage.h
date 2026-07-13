#pragma once

#include <QWidget>

// Empty stub - the Gerber visual diff overlay (User Story 1b, v1.1) and
// board-parameter extraction (User Story 3) both land here later. Not part
// of v1.0.
class GerberViewerPage : public QWidget {
    Q_OBJECT

public:
    explicit GerberViewerPage(QWidget *parent = nullptr);
};
