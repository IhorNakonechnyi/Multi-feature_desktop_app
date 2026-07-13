#include <QApplication>
#include <QStyleHints>

#include "app/mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Dark mode is fixed on, not a system-following or user-toggleable
    // preference - deliberately always Qt::ColorScheme::Dark regardless of
    // the Windows theme setting, per the design decision for this app.
    QApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);

    // Organization/application name set here so QSettings (LocalSettings)
    // knows where to read/write its INI file without repeating these
    // strings at every call site.
    QApplication::setOrganizationName("PcbSupplierWorkflow");
    QApplication::setApplicationName("PcbSupplierWorkflow");

    MainWindow window;
    window.show();

    return app.exec();
}
