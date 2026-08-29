#include "sidebarnav.h"

#include <QIcon>

SidebarNav::SidebarNav(QWidget *parent)
    : QListWidget(parent)
{
    setFixedWidth(170);

    // Order must match the QStackedWidget page order in MainWindow, since
    // currentRowChanged is wired directly to setCurrentIndex.
    addItem(new QListWidgetItem(QIcon(":/icons/archive-compare.svg"), tr("Archive compare")));

    setCurrentRow(0);
}
