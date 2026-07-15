#pragma once

#include <QWidget>

#include "archivecomparisonresult.h"

class ArchivePanel;
class GerberDiffRenderer;
class QListWidget;
class QWidget;

// v1.0's feature page: two side-by-side ArchivePanels, re-compared
// automatically whenever either archive changes. Also hosts the Phase 3b.1
// Gerber diff UI shell in the reserved bottom area: hidden until at least
// one Different pair is a recognized Gerber type on both sides (FR-006),
// then shows an explanation, a list of eligible pairs, and an (empty for
// now - Phase 3b.2 draws real geometry) zoomable canvas.
class ArchiveComparePage : public QWidget {
    Q_OBJECT

public:
    explicit ArchiveComparePage(QWidget *parent = nullptr);

private:
    void runComparison();

    ArchivePanel *panelA_;
    ArchivePanel *panelB_;

    QWidget *gerberDiffContent_;
    QListWidget *gerberPairList_;
    GerberDiffRenderer *gerberCanvas_;
    QVector<DifferingPair> eligibleGerberPairs_;
};
