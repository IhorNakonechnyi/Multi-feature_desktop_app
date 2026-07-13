#include "archivetreedelegate.h"

#include <QPainter>

#include "../common/palette.h"
#include "archivecomparisonresult.h"
#include "archivetreemodel.h"

void ArchiveTreeDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const bool isDirectory = index.data(ArchiveTreeModel::IsDirectoryRole).toBool();

    QStyleOptionViewItem opt(option);

    if (!isDirectory) {
        const auto classification = index.data(ArchiveTreeModel::ClassificationRole).value<EntryClassification>();
        QColor background;
        QColor foreground;
        switch (classification) {
        case EntryClassification::Identical:
            background = Palette::sameBackground();
            foreground = Palette::sameForeground();
            break;
        case EntryClassification::Different:
            background = Palette::changedBackground();
            foreground = Palette::changedForeground();
            break;
        case EntryClassification::OnlyInThisArchive:
            background = Palette::missingBackground();
            foreground = Palette::missingForeground();
            break;
        }
        painter->fillRect(option.rect, background);

        // Without this, the base paint below draws text in the app's
        // default Text/HighlightedText color - fine in light mode by
        // accident, but unreadable once dark mode is forced (light default
        // text on top of these still-light background fills).
        opt.palette.setColor(QPalette::Text, foreground);
        opt.palette.setColor(QPalette::HighlightedText, foreground);
    }

    // Icon + text still drawn by the base implementation on top of our
    // background fill - only the coloring itself is custom here.
    QStyledItemDelegate::paint(painter, opt, index);
}
