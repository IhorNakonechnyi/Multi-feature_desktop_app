#include "theme.h"

#include <QStringList>

#include "../common/palette.h"

QString themeStylesheet()
{
    QStringList rules;

    rules << QString("#sidebarPanel { background: %1; }").arg(Palette::SidebarBackground);
    rules << QString("#sidebarWordmark { color: %1; font-size: 19px; font-weight: 700; }")
                 .arg(Palette::SidebarTitleText);
    rules << QString("SidebarNav { background: %1; border: none; outline: none; }")
                 .arg(Palette::SidebarBackground);
    rules << QString("SidebarNav::item { padding: 11px 12px; border-radius: 8px; color: %1; font-size: 13.5px; }")
                 .arg(Palette::NavInactiveText);
    rules << QString("SidebarNav::item:selected { background: %1; color: %2; font-weight: 600; }")
                 .arg(Palette::NavActiveBackground, Palette::NavActiveText);
    rules << QString("SidebarNav::item:disabled { color: %1; }").arg(Palette::NavDisabledText);
    rules << QString("StatusLegend { background: %1; border-radius: 8px; }")
                 .arg(Palette::LegendCardBackground);
    rules << QString("QLabel#legendTitleLabel { color: %1; font-size: 10px; }")
                 .arg(Palette::LegendLabelText);
    rules << QString("QLabel#legendItemLabel { color: %1; font-size: 11.5px; }")
                 .arg(Palette::LegendItemText);

    rules << QString("ArchiveComparePage { background: %1; }").arg(Palette::FrameBackground);
    rules << QString("QLabel#pageTitle { color: %1; font-size: 15px; font-weight: 700; }")
                 .arg(Palette::SidebarTitleText);
    rules << QString("ArchivePanel { background: %1; border-radius: 8px; }").arg(Palette::CardBackground);
    rules << QString("#treeCardHeader { border-bottom: 1px solid %1; }").arg(Palette::CardHeaderBorder);
    rules << QString("#pairsCard { background: %1; border-radius: 8px; }").arg(Palette::CardBackground);
    rules << QString("QListWidget#pairsList { background: transparent; border: none; outline: none; }");
    rules << QString("QListWidget#pairsList::item { border-radius: 16px; padding: 5px 12px; margin: 2px 0; "
                      "background: %1; color: %2; font-size: 11.5px; }")
                 .arg(Palette::PairInactiveBackground, Palette::DetailText);
    rules << QString("QListWidget#pairsList::item:selected { background: %1; }")
                 .arg(Palette::PairActiveBackground);
    rules << QString("#canvasCard { background: %1; border-radius: 8px; }").arg(Palette::CardBackground);
    rules << QString("#detailCard { background: %1; border-radius: 8px; }").arg(Palette::CardBackground);
    rules << QString("QLabel#detailLabel { color: %1; font-size: 12px; }").arg(Palette::DetailText);

    return rules.join('\n');
}
