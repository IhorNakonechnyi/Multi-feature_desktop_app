#pragma once

#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>

class QVBoxLayout;

// Small card explaining what a comparison result's colors mean. Not fixed
// content: setEntries() replaces its rows entirely, so the same card can
// show a different legend per active feature page (MainWindow drives
// this - see mainwindow.cpp). An empty entry list hides the whole card,
// which is the initial/default state until setEntries() is called.
class StatusLegend : public QWidget {
    Q_OBJECT

public:
    struct Entry {
        QColor color;
        QString label;
    };

    explicit StatusLegend(QWidget *parent = nullptr);

    void setEntries(const QVector<Entry> &entries);

private:
    QVBoxLayout *rowsLayout_;
};
