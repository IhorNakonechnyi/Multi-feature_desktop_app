#include "quotecomparepage.h"

#include <QLabel>
#include <QVBoxLayout>

QuoteComparePage::QuoteComparePage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Quote compare - coming in a later release.")));
    layout->addStretch();
}
