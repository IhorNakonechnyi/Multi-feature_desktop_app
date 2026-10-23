#pragma once

#include <QRectF>
#include <QSet>
#include <QString>
#include <QVector>
#include <QWidget>

#include "../gerber/gerberobjectmodel.h"
#include "archivecomparisonresult.h"

class ArchiveHeader;
class ArchivePanel;
class CompositeView;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QWidget;

// The Archive Compare feature page, re-compared automatically whenever
// either archive changes.
//
// Left column is a grid (Q8): the two ArchiveHeaders on top, one filter bar
// spanning both beneath them, and the two ArchivePanel trees below that - the
// bar spans because it acts on both sides, which it could not express from
// inside either one. Above the grid sit the summary line and the
// nothing-differs banner, full width so they describe both sides at once.
//
// Also hosts the Gerber overlay-compare area in the page's right-hand
// column (M4, see
// docs/superpowers/specs/2026-07-18-gerber-render-v2-m4-overlay-design.md):
// every changed Gerber pair between the two archives is listed; selecting
// one renders both revisions in one CompositeView, additively tinted (A =
// green, B = red, unchanged = yellow overlap) - the CAMMaster-style
// compare the whole v2 rendering rewrite was built toward.
class ArchiveComparePage : public QWidget {
    Q_OBJECT

public:
    explicit ArchiveComparePage(QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void runComparison();
    void clearSummary();
    void applyFilters();

    // Widens the archive columns as the window grows, so a long filename has
    // more room exactly when there is room to give it.
    void updatePanelWidths();
    void updateNotDrawableLabel(int count);
    void showSelectedPair();
    void renderShownPair(const QRectF &windowMm, double pxPerMm);
    Layer parseGerberOrDrill(const QByteArray &bytes, QString &parseError);

    ArchiveHeader *headerA_;
    ArchiveHeader *headerB_;
    ArchivePanel *panelA_;
    ArchivePanel *panelB_;

    // One bar for both panels, so it is unambiguous that it acts on both.
    // Filter state lives here for the session only - never persisted.
    QCheckBox *onlyChangesCheck_;
    QComboBox *typeCombo_;

    // Full width under the page title, so they describe both panels at once
    // rather than appearing to belong to either side.
    QLabel *summaryLabel_;
    QLabel *identicalBanner_;

    QListWidget *gerberPairList_;
    QLabel *pairStatusLabel_;
    QLabel *notDrawableLabel_;
    QLabel *renderTimeLabel_;
    QLabel *compareTimeLabel_;
    // Diagnostics (1.8.7 test round, samedatadiagnosis.h): why the selected
    // pair is yellow, and which pairs the same-data check was offered.
    QLabel *sameDataLabel_;
    QSet<QString> offeredPairs_;
    CompositeView *gerberCanvas_;

    // The pairs behind gerberPairList_'s rows, same order/index.
    QVector<DifferingPair> eligiblePairs_;

    // The two revisions currently on the canvas; either may be a
    // default-constructed empty Layer if that side failed to parse.
    Layer shownLayerA_;
    Layer shownLayerB_;
};
