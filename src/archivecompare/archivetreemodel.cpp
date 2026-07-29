#include "archivetreemodel.h"

#include <algorithm>

#include <QApplication>
#include <QStringList>
#include <QStyle>

QString emptyAfterFilterMessage(const TreeFilter &filter)
{
    // Built as an accumulating qualifier so the criteria read as one phrase
    // ("changed Gerber") instead of a list, and so no criterion contributes
    // anything when it is off.
    QString qualifier;
    if (filter.changedOnly) {
        qualifier += QObject::tr("changed ");
    }
    if (filter.kind == DataKind::GerberData) {
        qualifier += QObject::tr("Gerber ");
    } else if (filter.kind == DataKind::OtherData) {
        qualifier += QObject::tr("other ");
    }
    return QObject::tr("No %1files to show").arg(qualifier);
}

ArchiveTreeModel::ArchiveTreeModel(QObject *parent)
    : QAbstractItemModel(parent)
{
    root_ = std::make_unique<ArchiveTreeNode>();
    root_->isDirectory = true;
}

void ArchiveTreeModel::setFilter(const TreeFilter &filter)
{
    filter_ = filter;
    rebuild();
}

bool ArchiveTreeModel::isEmptyAfterFilter() const
{
    return root_->children.empty();
}

bool ArchiveTreeModel::passesFilter(const ArchiveEntry &entry) const
{
    // AND-combined: every criterion the user switched on has to hold.
    if (filter_.changedOnly && entry.classification == EntryClassification::Identical) {
        return false;
    }
    if (filter_.kind.has_value() && entry.kind != *filter_.kind) {
        return false;
    }
    return true;
}

void ArchiveTreeModel::buildFrom(const QVector<ArchiveEntry> &entries)
{
    // The filter deliberately survives: a fresh comparison must not silently
    // re-show rows the user chose to hide, since the control bar driving the
    // filter keeps its state too.
    allEntries_ = entries;
    rebuild();
}

void ArchiveTreeModel::rebuild()
{
    beginResetModel();

    root_ = std::make_unique<ArchiveTreeNode>();
    root_->isDirectory = true;

    for (const auto &entry : allEntries_) {
        if (!passesFilter(entry)) {
            continue; // folders are inferred below, so this hides emptied
                      // folders too, with no separate rule for them
        }
        const QStringList segments = entry.relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        ArchiveTreeNode *current = root_.get();

        for (int i = 0; i < segments.size(); ++i) {
            const bool isLastSegment = (i == segments.size() - 1);

            ArchiveTreeNode *child = nullptr;
            for (auto &existing : current->children) {
                if (existing->name == segments[i] && existing->isDirectory == !isLastSegment) {
                    child = existing.get();
                    break;
                }
            }

            if (!child) {
                auto newNode = std::make_unique<ArchiveTreeNode>();
                newNode->name = segments[i];
                newNode->isDirectory = !isLastSegment;
                newNode->parent = current;
                child = newNode.get();
                current->children.push_back(std::move(newNode));
            }

            if (isLastSegment) {
                child->classification = entry.classification;
            }
            current = child;
        }
    }

    assignBands(root_.get());
    sortChildren(root_.get());
    endResetModel();
}

int ArchiveTreeModel::assignBands(ArchiveTreeNode *node)
{
    // Written as an explicit switch rather than a cast of the enum, so the
    // band order stays independent of the order the enumerators happen to
    // be declared in.
    auto bandForFile = [](EntryClassification classification) {
        switch (classification) {
        case EntryClassification::Different:
            return 0; // yellow - the app assumed these correspond and they differ
        case EntryClassification::OnlyInThisArchive:
            return 1; // red - no counterpart, check it
        case EntryClassification::Identical:
            return 2; // green - nothing to do
        }
        return 2;
    };

    if (!node->isDirectory) {
        node->sortBand = bandForFile(node->classification);
        return node->sortBand;
    }

    // A directory takes the most urgent band found anywhere below it. The
    // recursion runs over every child first (no early exit on finding a 0)
    // because each child still needs its own band filled in for the view.
    int band = 2;
    for (auto &child : node->children) {
        band = std::min(band, assignBands(child.get()));
    }
    node->sortBand = band;
    return band;
}

void ArchiveTreeModel::sortChildren(ArchiveTreeNode *node)
{
    std::sort(node->children.begin(), node->children.end(),
              [](const std::unique_ptr<ArchiveTreeNode> &a, const std::unique_ptr<ArchiveTreeNode> &b) {
                  if (a->sortBand != b->sortBand) {
                      return a->sortBand < b->sortBand; // yellow, then red, then green
                  }
                  if (a->isDirectory != b->isDirectory) {
                      return a->isDirectory; // directories first within a band
                  }
                  // Case-insensitive: an archive mixing "Gerber/" and
                  // "gerber/" style names should read alphabetically to a
                  // person, not group all capitals ahead of all lowercase.
                  return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
              });

    for (auto &child : node->children) {
        sortChildren(child.get());
    }
}

QModelIndex ArchiveTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent)) {
        return {};
    }

    ArchiveTreeNode *parentNode = parent.isValid() ? static_cast<ArchiveTreeNode *>(parent.internalPointer())
                                                    : root_.get();
    if (row < 0 || static_cast<size_t>(row) >= parentNode->children.size()) {
        return {};
    }

    return createIndex(row, column, parentNode->children[row].get());
}

QModelIndex ArchiveTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid()) {
        return {};
    }

    auto *node = static_cast<ArchiveTreeNode *>(child.internalPointer());
    ArchiveTreeNode *parentNode = node->parent;
    if (parentNode == nullptr || parentNode == root_.get()) {
        return {};
    }

    ArchiveTreeNode *grandparent = parentNode->parent;
    int row = 0;
    for (size_t i = 0; i < grandparent->children.size(); ++i) {
        if (grandparent->children[i].get() == parentNode) {
            row = static_cast<int>(i);
            break;
        }
    }
    return createIndex(row, 0, parentNode);
}

int ArchiveTreeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0) {
        return 0;
    }
    ArchiveTreeNode *parentNode = parent.isValid() ? static_cast<ArchiveTreeNode *>(parent.internalPointer())
                                                    : root_.get();
    return static_cast<int>(parentNode->children.size());
}

int ArchiveTreeModel::columnCount(const QModelIndex &) const
{
    return 1; // single column - the row shows the filename and nothing else;
              // size is internal plumbing (Q9)
}

QVariant ArchiveTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return {};
    }

    auto *node = static_cast<ArchiveTreeNode *>(index.internalPointer());

    switch (role) {
    case Qt::DisplayRole:
        return node->name;
    case Qt::DecorationRole:
        return qApp->style()->standardIcon(node->isDirectory ? QStyle::SP_DirIcon : QStyle::SP_FileIcon);
    case ClassificationRole:
        return QVariant::fromValue(node->classification);
    case IsDirectoryRole:
        return node->isDirectory;
    case SortBandRole:
        return node->sortBand;
    default:
        return {};
    }
}
