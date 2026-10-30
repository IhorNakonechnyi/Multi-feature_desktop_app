#include "mainwindow.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "sidebarnav.h"
#include "statuslegend.h"
#include "../archivecompare/archivecomparepage.h"
#include "../common/palette.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // Sidebar and page side by side in a plain layout, not a splitter: the
    // sidebar has one fixed width and cannot be dragged wider or narrower.
    auto *central = new QWidget(this);
    auto *centralLayout = new QHBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);

    // SidebarNav is a QListWidget itself, not a container - it can't hold
    // the wordmark label or the status legend card as direct children, so
    // this wrapper stacks all three: wordmark on top, the nav list taking
    // the remaining space, the legend card pinned to the bottom. This
    // wrapper - not nav_ directly - is what goes into the layout.
    auto *sidebarPanel = new QWidget(central);
    sidebarPanel->setObjectName("sidebarPanel");
    sidebarPanel->setAttribute(Qt::WA_StyledBackground, true);
    // Fixed horizontally: always exactly as wide as its contents ask for
    // (the nav list's fixed width plus the margins), however wide the
    // window gets.
    sidebarPanel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    auto *sidebarLayout = new QVBoxLayout(sidebarPanel);
    sidebarLayout->setContentsMargins(10, 16, 10, 16);
    sidebarLayout->setSpacing(4);

    auto *wordmark = new QLabel(tr("What do you want?"));
    wordmark->setObjectName("sidebarWordmark");
    wordmark->setWordWrap(true);
    sidebarLayout->addWidget(wordmark);

    nav_ = new SidebarNav(sidebarPanel);
    sidebarLayout->addWidget(nav_, 1);

    statusLegend_ = new StatusLegend(sidebarPanel);
    sidebarLayout->addWidget(statusLegend_);

    pages_ = new QStackedWidget(central);

    // Order must match SidebarNav's item order - currentRowChanged drives
    // setCurrentIndex directly, no separate mapping needed.
    pages_->addWidget(new ArchiveComparePage);

    centralLayout->addWidget(sidebarPanel);
    centralLayout->addWidget(pages_, 1); // content: takes all remaining width

    setCentralWidget(central);
#ifdef PCB_DIAGNOSTICS
    // A diagnostic build (CMake option PCB_DIAGNOSTICS) carries the same-data
    // diagnostics under the canvas; the title says so, so it is never
    // mistaken for a release. Qt appends the application name itself:
    // "Diagnostic build <version> - PCB Gadget".
    setWindowTitle(tr("Diagnostic build %1").arg(QCoreApplication::applicationVersion()));
#else
    setWindowTitle(tr("PCB Gadget"));
#endif
    resize(1100, 700);

    connect(nav_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
    connect(nav_, &QListWidget::currentRowChanged, this, &MainWindow::updateStatusLegend);

    updateStatusLegend(nav_->currentRow());
}

void MainWindow::updateStatusLegend(int pageIndex)
{
    // Archive Compare (index 0) is the only page today. setEntries({}) hides
    // the card for any other index.
    // Colors are the same Palette calls ArchiveTreeDelegate already uses,
    // so the legend can never drift out of sync with the tree's colors.
    if (pageIndex == 0) {
        statusLegend_->setEntries({
            {Palette::sameBackground(), tr("Identical")},
            {Palette::changedBackground(), tr("Different")},
            {Palette::missingBackground(), tr("Only in this archive")},
        });
    } else {
        statusLegend_->setEntries({});
    }
}
