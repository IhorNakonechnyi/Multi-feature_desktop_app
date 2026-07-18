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
// view area in the reserved bottom section: a list of eligible differing
// pairs and a Show button. At the current v2 stage (see the 2026-07-18
// design doc, milestone M3->M4 transition), Show renders ONLY the
// selected pair's archive-A layer through the new mask pipeline - the
// two-layer overlay compare is milestone M4, planned after the
// single-layer rendering passes Igor's visual gate.
class ArchiveComparePage : public QWidget {
    Q_OBJECT

public:
    explicit ArchiveComparePage(QWidget *parent = nullptr);

private:
    void runComparison();
    void showSelectedPair();
    void renderShownLayer(const QRectF &windowMm, double pxPerMm);

    ArchivePanel *panelA_;
    ArchivePanel *panelB_;

    QWidget *gerberDiffContent_;
    QListWidget *gerberPairList_;
    QPushButton *showButton_;
    QLabel *renderTimeLabel_;
    CompositeView *gerberCanvas_;
    QVector<DifferingPair> eligibleGerberPairs_;

    // The layer currently on the canvas (parsed from archive A's side of
    // the selected pair); empty until Show is first clicked.
    Layer shownLayer_;
};
