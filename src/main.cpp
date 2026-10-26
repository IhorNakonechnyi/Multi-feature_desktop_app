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

    // Fusion is pinned rather than left to Qt's per-OS default. On Windows 10
    // Qt picks the "windowsvista" style, which draws controls through the
    // Windows theme engine - and that engine has no dark variant, so the
    // tree, buttons, checkbox, dropdown and scrollbars came out light on the
    // dark cards. Fusion is drawn by Qt itself, honours the dark scheme, and
    // looks the same on Windows 10 and 11. Setting it here also overrides a
    // "-style" command-line argument.
    QApplication::setStyle(QStringLiteral("Fusion"));

    // Organization/application name kept as-is (not renamed to match the
    // "PCB Gadget" display name below) - QSettings (LocalSettings) derives
    // its INI file location from these, and changing them would silently
    // orphan any user-added Gerber extensions already saved under the old
    // location. applicationDisplayName is the user-visible name; it's
    // independent of these.
    QApplication::setOrganizationName("PcbSupplierWorkflow");
    QApplication::setApplicationName("PcbSupplierWorkflow");
    QApplication::setApplicationDisplayName("PCB Gadget");
    QApplication::setApplicationVersion(QStringLiteral("1.8.9"));
    app.setWindowIcon(QIcon(":/icons/icon.png"));

    app.setStyleSheet(themeStylesheet());

    MainWindow window;
    window.show();

    return app.exec();
}
