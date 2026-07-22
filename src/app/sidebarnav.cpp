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

    // Only Archive Compare has real functionality today - the other 3 are
    // still stub "coming later" pages (see e.g. supplieradvisorpage.cpp).
    // Disabled rather than removed: they stay visible so it's clear more
    // is planned, just not selectable yet.
    for (int row = 1; row < count(); ++row) {
        item(row)->setFlags(item(row)->flags() & ~Qt::ItemIsEnabled);
    }

    setCurrentRow(0);
}
