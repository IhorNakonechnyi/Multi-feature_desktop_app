#pragma once

#include <QWidget>

#include "../gerber/gerberobjectmodel.h"
#include "archivecomparisonresult.h"

class ArchivePanel;
class CompositeView;
class QLabel;
class QListWidget;
class QPushButton;
class QWidget;

// v1.0's feature page: two side-by-side ArchivePanels, re-compared
// automatically whenever either archive changes. Also hosts the Gerber
// view area in the reserved bottom section. At the current v2 debugging
// stage (per Igor, 2026-07-18): the list shows EVERY file in archive A -
// deliberately unfiltered, because unrecognized Gerber extensions in real
// supplier archives were a confirmed past problem and filtering would
// hide exactly the interesting files - and Show renders whichever file is
// selected through the new mask pipeline, reporting the render time after
// each push. The differing-pair gating and two-layer overlay compare
// return in milestone M4, after the rendering passes the visual gate.
class ArchiveComparePage : public QWidget {
    Q_OBJECT

public:
    explicit ArchiveComparePage(QWidget *parent = nullptr);

private:
    void populateFileList();
    void runComparison();
    void showSelectedFile();
    void renderShownLayer(const QRectF &windowMm, double pxPerMm);

    ArchivePanel *panelA_;
    ArchivePanel *panelB_;

    QWidget *gerberDiffContent_;
    QListWidget *fileListA_;
    QPushButton *showButton_;
    QLabel *renderTimeLabel_;
    CompositeView *gerberCanvas_;

    // relativePath of every file in archive A, same order as fileListA_'s
    // rows (the list displays these verbatim).
    QStringList filePathsA_;

    // The layer currently on the canvas; empty until Show is first clicked.
    Layer shownLayer_;
};
