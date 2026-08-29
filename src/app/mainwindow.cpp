#include "mainwindow.h"

#include <QLabel>
#include <QSplitter>
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
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    // SidebarNav is a QListWidget itself, not a container - it can't hold
    // the wordmark label or the status legend card as direct children, so
    // this wrapper stacks all three: wordmark on top, the nav list taking
    // the remaining space, the legend card pinned to the bottom. This
    // wrapper - not nav_ directly - is what goes into the splitter.
    auto *sidebarPanel = new QWidget(splitter);
    sidebarPanel->setObjectName("sidebarPanel");
    sidebarPanel->setAttribute(Qt::WA_StyledBackground, true);
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

    pages_ = new QStackedWidget(splitter);

    // Order must match SidebarNav's item order - currentRowChanged drives
    // setCurrentIndex directly, no separate mapping needed.
    pages_->addWidget(new ArchiveComparePage);

    splitter->setStretchFactor(0, 0); // sidebar: fixed
    splitter->setStretchFactor(1, 1); // content: grows

    setCentralWidget(splitter);
    setWindowTitle(tr("PCB Gadget"));
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
