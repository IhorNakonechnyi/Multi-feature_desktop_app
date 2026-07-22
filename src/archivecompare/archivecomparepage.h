#pragma once

#include <QRectF>
#include <QVector>
#include <QWidget>

#include "../gerber/gerberobjectmodel.h"
#include "archivecomparisonresult.h"

class ArchivePanel;
class CompositeView;
class QLabel;
class QListWidget;
class QWidget;

// v1.0's feature page: two side-by-side ArchivePanels, re-compared
// automatically whenever either archive changes. Also hosts the Gerber
// overlay-compare area in the page's right-hand column (M4, see
// docs/superpowers/specs/2026-07-18-gerber-render-v2-m4-overlay-design.md):
// every changed Gerber pair between the two archives is listed; selecting
// one renders both revisions in one CompositeView, additively tinted (new =
// green, old = red, unchanged = yellow overlap) - the CAMMaster-style
// compare the whole v2 rendering rewrite was built toward.
class ArchiveComparePage : public QWidget {
    Q_OBJECT

public:
    explicit ArchiveComparePage(QWidget *parent = nullptr);

private:
    void runComparison();
    void rebuildSkippedPairsUi(const QVector<DifferingPair> &unrecognized, const QVector<DifferingPair> &drill);
    void showSelectedPair();
    void renderShownPair(const QRectF &windowMm, double pxPerMm);
    Layer parseGerberOrDrill(const QByteArray &bytes, const QString &relativePath, QString &parseError);

    ArchivePanel *panelA_;
    ArchivePanel *panelB_;

    QListWidget *gerberPairList_;
    QLabel *pairStatusLabel_;
    QLabel *skippedUnrecognizedLabel_;
    QWidget *skippedUnrecognizedButtonRow_;
    QLabel *skippedDrillLabel_;
    QLabel *renderTimeLabel_;
    CompositeView *gerberCanvas_;

    // The pairs behind gerberPairList_'s rows, same order/index.
    QVector<DifferingPair> eligiblePairs_;

    // The two revisions currently on the canvas; either may be a
    // default-constructed empty Layer if that side failed to parse.
    Layer shownLayerA_;
    Layer shownLayerB_;
};
