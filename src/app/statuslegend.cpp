#include "statuslegend.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "../common/palette.h"

namespace {

QWidget *makeRow(const QColor &color, const QString &label)
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *swatch = new QLabel;
    swatch->setFixedSize(10, 10);
    swatch->setStyleSheet(QString("background:%1; border-radius:2px;").arg(color.name()));

    layout->addWidget(swatch);
    layout->addWidget(new QLabel(label));
    layout->addStretch();
    return row;
}

} // namespace

StatusLegend::StatusLegend(QWidget *parent)
    : QWidget(parent)
{
    // Swatches show each category's background color (the color that
    // actually appears in the tree), not the foreground/text color - that's
    // now just a plain white/black chosen for contrast, not identity.
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(makeRow(Palette::sameBackground(), tr("Identical")));
    layout->addWidget(makeRow(Palette::changedBackground(), tr("Different")));
    layout->addWidget(makeRow(Palette::missingBackground(), tr("Only in this archive")));
}
