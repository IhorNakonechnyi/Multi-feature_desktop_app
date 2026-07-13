#include "archivetreemodel.h"

#include <algorithm>

#include <QApplication>
#include <QStringList>
#include <QStyle>

ArchiveTreeModel::ArchiveTreeModel(QObject *parent)
    : QAbstractItemModel(parent)
{
    root_ = std::make_unique<ArchiveTreeNode>();
    root_->isDirectory = true;
}

void ArchiveTreeModel::buildFrom(const QVector<ArchiveEntry> &entries)
{
    beginResetModel();

    root_ = std::make_unique<ArchiveTreeNode>();
    root_->isDirectory = true;

    for (const auto &entry : entries) {
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
                child->sizeBytes = entry.sizeBytes;
                child->classification = entry.classification;
            }
            current = child;
        }
    }

    sortChildren(root_.get());
    endResetModel();
}

void ArchiveTreeModel::sortChildren(ArchiveTreeNode *node)
{
    std::sort(node->children.begin(), node->children.end(),
              [](const std::unique_ptr<ArchiveTreeNode> &a, const std::unique_ptr<ArchiveTreeNode> &b) {
                  if (a->isDirectory != b->isDirectory) {
                      return a->isDirectory; // directories first
                  }
                  if (a->isDirectory) {
                      return a->name < b->name; // directories: name ascending
                  }
                  return a->sizeBytes < b->sizeBytes; // files: size ascending
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
    return 1; // single column - size is shown inline by the delegate, not a real column
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
    default:
        return {};
    }
}
