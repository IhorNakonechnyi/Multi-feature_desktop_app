#pragma once

#include <QWidget>

class ArchivePanel;

// v1.0's feature page: two side-by-side ArchivePanels, re-compared
// automatically whenever either archive changes. No Gerber diff button
// yet - that's User Story 1b (v1.1).
class ArchiveComparePage : public QWidget {
    Q_OBJECT

public:
    explicit ArchiveComparePage(QWidget *parent = nullptr);

private:
    void runComparison();

    ArchivePanel *panelA_;
    ArchivePanel *panelB_;
};
