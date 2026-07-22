#include <QApplication>
#include <QIcon>
#include <QStyleHints>

#include "app/mainwindow.h"
#include "app/theme.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Dark mode is fixed on, not a system-following or user-toggleable
    // preference - deliberately always Qt::ColorScheme::Dark regardless of
    // the Windows theme setting, per the design decision for this app.
    QApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);

    // Organization/application name kept as-is (not renamed to match the
    // "PCB Gadget" display name below) - QSettings (LocalSettings) derives
    // its INI file location from these, and changing them would silently
    // orphan any user-added Gerber extensions already saved under the old
    // location. applicationDisplayName is the user-visible name; it's
    // independent of these.
    QApplication::setOrganizationName("PcbSupplierWorkflow");
    QApplication::setApplicationName("PcbSupplierWorkflow");
    QApplication::setApplicationDisplayName("PCB Gadget");
    app.setWindowIcon(QIcon(":/icons/icon.png"));

    app.setStyleSheet(themeStylesheet());

    MainWindow window;
    window.show();

    return app.exec();
}
