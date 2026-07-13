#include "gerberviewerpage.h"

#include <QLabel>
#include <QVBoxLayout>

GerberViewerPage::GerberViewerPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Gerber viewer - coming in a later release.")));
    layout->addStretch();
}
