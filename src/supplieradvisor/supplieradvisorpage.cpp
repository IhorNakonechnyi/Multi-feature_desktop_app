#include "supplieradvisorpage.h"

#include <QLabel>
#include <QVBoxLayout>

SupplierAdvisorPage::SupplierAdvisorPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Supplier advisor - coming in a later release.")));
    layout->addStretch();
}
