#include "theme.h"

#include <QStringList>

#include "../common/palette.h"

QString themeStylesheet()
{
    QStringList rules;

    rules << QString("#sidebarPanel { background: %1; }").arg(Palette::SidebarBackground);
    rules << QString("#sidebarWordmark { color: %1; font-size: 14px; font-weight: 700; }")
                 .arg(Palette::SidebarTitleText);
    rules << QString("SidebarNav { background: %1; border: none; outline: none; }")
                 .arg(Palette::SidebarBackground);
    rules << QString("SidebarNav::item { padding: 11px 12px; border-radius: 8px; color: %1; font-size: 13.5px; }")
                 .arg(Palette::NavInactiveText);
    rules << QString("SidebarNav::item:selected { background: %1; color: %2; font-weight: 600; }")
                 .arg(Palette::NavActiveBackground, Palette::NavActiveText);
    rules << QString("StatusLegend { background: %1; border-radius: 8px; }")
                 .arg(Palette::LegendCardBackground);
    rules << QString("QLabel#legendTitleLabel { color: %1; font-size: 10px; }")
                 .arg(Palette::LegendLabelText);
    rules << QString("QLabel#legendItemLabel { color: %1; font-size: 11.5px; }")
                 .arg(Palette::LegendItemText);

    return rules.join('\n');
}
