#pragma once

#include <QStyledItemDelegate>

// Paints the green/yellow/red background for file rows (ArchiveTreeModel's
// ClassificationRole). Directories are left uncolored - see
// archivetreemodel.h for why folders don't get a rollup color in v1.0.
class ArchiveTreeDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};
