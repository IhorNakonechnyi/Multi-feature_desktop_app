#pragma once

#include <QWidget>

#include "archivecomparisonresult.h"
#include "archivetreemodel.h" // TreeFilter

class QLabel;
class QStackedLayout;
class QTreeView;

// The tree half of one side of the comparison. The header (filename, the
// Change... button, the inline error) lives in ArchiveHeader, so the page can
// put one filter bar between the two headers and the two trees (Q8).
//
// Still accepts drops in its own right: dropping a zip onto the file list is
// at least as natural as dropping it onto the header strip. The path itself
// belongs to the header, so a drop here is forwarded up as a signal rather
// than stored.
class ArchivePanel : public QWidget {
    Q_OBJECT

public:
    explicit ArchivePanel(QWidget *parent = nullptr);

    // Rebuilds the visible tree from a freshly classified entry list.
    void showEntries(const QVector<ArchiveEntry> &entries);

    void setFilter(const TreeFilter &filter);
    bool isEmptyAfterFilter() const;

    // Non-empty message replaces the tree with centered text; an empty
    // string shows the tree again. An empty pane with no explanation reads
    // as a bug, especially when the user has just switched a filter on.
    void showEmptyMessage(const QString &message);

signals:
    // A valid archive was dropped on the tree; the page hands it to this
    // side's header, which owns the path.
    void archiveDropped(const QString &path);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QTreeView *treeView_;
    ArchiveTreeModel *model_;
    QStackedLayout *stack_;
    QLabel *emptyLabel_;
};
