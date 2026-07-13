#include "sidebarnav.h"

#include <QIcon>

SidebarNav::SidebarNav(QWidget *parent)
    : QListWidget(parent)
{
    setFixedWidth(170);

    // Order must match the QStackedWidget page order in MainWindow, since
    // currentRowChanged is wired directly to setCurrentIndex.
    addItem(new QListWidgetItem(QIcon(":/icons/archive-compare.svg"), tr("Archive compare")));
    addItem(new QListWidgetItem(QIcon(":/icons/supplier-advisor.svg"), tr("Supplier advisor")));
    addItem(new QListWidgetItem(QIcon(":/icons/gerber-viewer.svg"), tr("Gerber viewer")));
    addItem(new QListWidgetItem(QIcon(":/icons/quote-compare.svg"), tr("Quote compare")));

    setCurrentRow(0);
}
