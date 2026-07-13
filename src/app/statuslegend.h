#pragma once

#include <QWidget>

// Small colored-swatch legend explaining the green/yellow/red scheme, so
// the meaning of the archive tree's colors doesn't have to be guessed.
class StatusLegend : public QWidget {
    Q_OBJECT

public:
    explicit StatusLegend(QWidget *parent = nullptr);
};
