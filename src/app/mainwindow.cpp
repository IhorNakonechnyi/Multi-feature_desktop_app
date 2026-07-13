#include "mainwindow.h"

#include <QSplitter>
#include <QStackedWidget>

#include "sidebarnav.h"
#include "../archivecompare/archivecomparepage.h"
#include "../gerber/gerberviewerpage.h"
#include "../quotecompare/quotecomparepage.h"
#include "../supplieradvisor/supplieradvisorpage.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    nav_ = new SidebarNav(splitter);
    pages_ = new QStackedWidget(splitter);

    // Order must match SidebarNav's item order - currentRowChanged drives
    // setCurrentIndex directly, no separate mapping needed.
    pages_->addWidget(new ArchiveComparePage);
    pages_->addWidget(new SupplierAdvisorPage);
    pages_->addWidget(new GerberViewerPage);
    pages_->addWidget(new QuoteComparePage);

    splitter->setStretchFactor(0, 0); // sidebar: fixed
    splitter->setStretchFactor(1, 1); // content: grows

    setCentralWidget(splitter);
    setWindowTitle(tr("PCB Supplier Workflow"));
    resize(1100, 700);

    connect(nav_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
}
