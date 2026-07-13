#pragma once

#include <QAbstractItemModel>

#include <memory>
#include <vector>

#include "archivecomparisonresult.h"

// Backing node for ArchiveTreeModel. Folders are purely structural,
// inferred from splitting each entry's relativePath on '/' - only file
// leaves carry a classification/color. The spec only asks for per-file
// coloring, so folders deliberately don't get a rollup color of their own
// (constitution Principle 1: don't invent a rule the spec never asked for).
struct ArchiveTreeNode {
    QString name;
    bool isDirectory = false;
    quint64 sizeBytes = 0;
    EntryClassification classification = EntryClassification::Identical; // unused for directories
    std::vector<std::unique_ptr<ArchiveTreeNode>> children;
    ArchiveTreeNode *parent = nullptr;
};

// QFileSystemModel doesn't work here - it's tied to the real filesystem and
// can't attach a per-node classification or control sort order the way this
// view needs. This subclasses QAbstractItemModel directly instead, backed
// by the plain ArchiveTreeNode tree above.
class ArchiveTreeModel : public QAbstractItemModel {
    Q_OBJECT

public:
    enum Role {
        ClassificationRole = Qt::UserRole + 1,
        IsDirectoryRole,
    };

    explicit ArchiveTreeModel(QObject *parent = nullptr);

    // Rebuilds the whole tree from a flat, already-classified entry list
    // (CrcComparator's output for one archive).
    void buildFrom(const QVector<ArchiveEntry> &entries);

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

private:
    std::unique_ptr<ArchiveTreeNode> root_;

    // Sort rule applied once at build time, not via QTreeView's
    // click-to-sort headers - a fixed deterministic order (folders first,
    // then alphabetical/by size) is what's wanted here, not a
    // user-toggleable one.
    static void sortChildren(ArchiveTreeNode *node);
};
