#pragma once

#include <QAbstractItemModel>

#include <memory>
#include <optional>
#include <vector>

#include "archivecomparisonresult.h"

// What the tree is allowed to show. Criteria AND-combine; the defaults show
// everything, which is the state the panel starts in (Q8: changed-only is
// deliberately OFF by default - hiding rows before the user asks would make
// the tree disagree with the summary line above it).
struct TreeFilter {
    bool changedOnly = false;     // keep only files that are not Identical
    std::optional<DataKind> kind; // nullopt = every kind
};

// Backing node for ArchiveTreeModel. Folders are purely structural,
// inferred from splitting each entry's relativePath on '/' - only file
// leaves carry a classification of their own.
//
// Folders used to be left uncoloured on the grounds that the spec only
// asked for per-file colouring. Q9 reverses that: a folder now inherits
// the highest-priority band of its descendants and is tinted accordingly,
// because a collapsed folder that hides a changed file otherwise looks
// like there is nothing to do inside it.
struct ArchiveTreeNode {
    QString name;
    bool isDirectory = false;
    // No sizeBytes here on purpose: Q9 dropped size from both the display
    // and the sort, so the tree has no use for it. ArchiveEntry still
    // carries it, since it is half of the (crc32, size) content-class key.
    EntryClassification classification = EntryClassification::Identical; // unused for directories
    int sortBand = 2; // 0 yellow, 1 red, 2 green - work-demanded order (Q9).
                      // Directories inherit the LOWEST (highest-priority)
                      // band of their descendants.
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
        SortBandRole,
    };

    explicit ArchiveTreeModel(QObject *parent = nullptr);

    // Rebuilds the whole tree from a flat, already-classified entry list
    // (CrcComparator's output for one archive). The full list is kept, so a
    // later setFilter() can widen the view again without re-comparing.
    void buildFrom(const QVector<ArchiveEntry> &entries);

    // Re-applies over the stored entries. Filtering rebuilds the tree rather
    // than wrapping it in a QSortFilterProxyModel, because folders are
    // *inferred* from the surviving paths - so a folder whose children all
    // disappear stops existing for free, with no separate hide-empty-parent
    // rule to keep correct (Q8).
    void setFilter(const TreeFilter &filter);

    // True when nothing survives - the panel shows a message rather than an
    // unexplained blank pane.
    bool isEmptyAfterFilter() const;

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

private:
    std::unique_ptr<ArchiveTreeNode> root_;

    // The unfiltered truth, kept so filters are reversible.
    QVector<ArchiveEntry> allEntries_;
    TreeFilter filter_;

    // Builds root_ from allEntries_ under the current filter.
    void rebuild();
    bool passesFilter(const ArchiveEntry &entry) const;

    // Fills in sortBand for the whole subtree and returns `node`'s own
    // band, so a directory can be scored from its children in the same
    // single bottom-up pass. Must run before sortChildren, which sorts on
    // the result.
    static int assignBands(ArchiveTreeNode *node);

    // Sort rule applied once at build time, not via QTreeView's
    // click-to-sort headers - a fixed deterministic order is what's wanted
    // here, not a user-toggleable one. Key: band, then directories before
    // files, then case-insensitive name (Q9).
    static void sortChildren(ArchiveTreeNode *node);
};
