#include "archivetreedelegate.h"

#include <QPainter>

#include "../common/palette.h"
#include "archivecomparisonresult.h"
#include "archivetreemodel.h"

void ArchiveTreeDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const bool isDirectory = index.data(ArchiveTreeModel::IsDirectoryRole).toBool();

    QStyleOptionViewItem opt(option);

    if (isDirectory) {
        // A folder is sorted by the worst band anywhere below it (Q9), so it
        // has to *look* sorted too - otherwise a collapsed folder at the top
        // of the pane gives no hint why it is there. Deliberately lighter
        // than a file row: just a bar at the leading edge, no background
        // fill and no text recolor, so a folder holding one changed file
        // doesn't turn the whole pane into a wall of color.
        constexpr int kBandBarWidth = 4;

        QColor edge; // left default-constructed (invalid) for band 2 -
                     // an all-green folder stays undecorated, since there
                     // is nothing to do inside it.
        switch (index.data(ArchiveTreeModel::SortBandRole).toInt()) {
        case 0:
            edge = Palette::changedBackground();
            break;
        case 1:
            edge = Palette::missingBackground();
            break;
        }

        if (edge.isValid()) {
            painter->fillRect(QRect(option.rect.left(), option.rect.top(), kBandBarWidth, option.rect.height()),
                              edge);
        }
    } else {
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
