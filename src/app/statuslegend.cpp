#include "statuslegend.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace {

QWidget *makeRow(const QColor &color, const QString &label)
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);

    // Per-entry color, so this stays a direct inline stylesheet rather
    // than a themeStylesheet() rule (which only knows fixed palette.h
    // tokens, not a caller-supplied QColor).
    auto *swatch = new QLabel;
    swatch->setFixedSize(9, 9);
    swatch->setAttribute(Qt::WA_StyledBackground, true);
    swatch->setStyleSheet(QString("background:%1; border-radius:4px;").arg(color.name()));

    auto *text = new QLabel(label);
    text->setObjectName("legendItemLabel");

    layout->addWidget(swatch);
    layout->addWidget(text);
    layout->addStretch();
    return row;
}

} // namespace

StatusLegend::StatusLegend(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);

    auto *title = new QLabel(tr("STATUS LEGEND"));
    title->setObjectName("legendTitleLabel");
    outer->addWidget(title);

    rowsLayout_ = new QVBoxLayout;
    rowsLayout_->setContentsMargins(0, 8, 0, 0);
    rowsLayout_->setSpacing(6);
    outer->addLayout(rowsLayout_);

    setVisible(false); // nothing to show until setEntries() is called
}

void StatusLegend::setEntries(const QVector<Entry> &entries)
{
    QLayoutItem *item;
    while ((item = rowsLayout_->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    for (const Entry &entry : entries) {
        rowsLayout_->addWidget(makeRow(entry.color, entry.label));
    }
    setVisible(!entries.isEmpty());
}
