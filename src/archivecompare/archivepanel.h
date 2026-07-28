#pragma once

#include <QWidget>

#include "archivecomparisonresult.h"

class QLabel;
class QPushButton;
class QTreeView;
class ArchiveTreeModel;

// One side of the two-pane archive comparison: a header bar (current
// filename + "Change..." button) plus a tree of that archive's classified
// files. Two of these sit side-by-side in ArchiveComparePage.
class ArchivePanel : public QWidget {
    Q_OBJECT

public:
    explicit ArchivePanel(const QString &roleLabel, QWidget *parent = nullptr);

    QString archivePath() const { return archivePath_; }

    // Rebuilds the visible tree from a freshly classified entry list.
    void showEntries(const QVector<ArchiveEntry> &entries);

    // Inline failure display (Q7: errors are data, only the page/panel
    // shows anything - never a modal dialog). Also clears the tree, since a
    // stale tree next to a fresh error message would misleadingly suggest
    // the error is about something else.
    void showError(const QString &message);
    void clearError();

signals:
    // Emitted whenever the user picks or drops a new archive - the page
    // re-runs the comparison once both panels have a path set.
    void archiveChanged(const QString &path);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void pickArchive();
    void setArchivePath(const QString &path);

    QString roleLabel_;
    QString archivePath_;
    QLabel *fileNameLabel_;
    QPushButton *changeButton_;
    QLabel *errorLabel_;
    QTreeView *treeView_;
    ArchiveTreeModel *model_;
};
