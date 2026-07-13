#include "localsettings.h"

#include <QSettings>

namespace {

// Explicit IniFormat (rather than the platform default, which is the
// registry on Windows) because plan.md calls for a plain INI file under
// %APPDATA% - easier to inspect/delete by hand while learning/debugging.
QSettings makeSettings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope,
                      "PcbSupplierWorkflow", "PcbSupplierWorkflow");
}

constexpr auto kLastArchiveADirKey = "archiveCompare/lastArchiveADir";
constexpr auto kLastArchiveBDirKey = "archiveCompare/lastArchiveBDir";

} // namespace

QString LocalSettings::lastArchiveADir() const
{
    return makeSettings().value(kLastArchiveADirKey).toString();
}

void LocalSettings::setLastArchiveADir(const QString &dir)
{
    QSettings settings = makeSettings();
    settings.setValue(kLastArchiveADirKey, dir);
}

QString LocalSettings::lastArchiveBDir() const
{
    return makeSettings().value(kLastArchiveBDirKey).toString();
}

void LocalSettings::setLastArchiveBDir(const QString &dir)
{
    QSettings settings = makeSettings();
    settings.setValue(kLastArchiveBDirKey, dir);
}
