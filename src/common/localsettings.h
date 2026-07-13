#pragma once

#include <QString>

// Thin wrapper around QSettings so callers don't sprinkle raw key strings
// everywhere. Only remembers last-used file paths for now - the local,
// single-user equivalent of "recent files".
class LocalSettings {
public:
    QString lastArchiveADir() const;
    void setLastArchiveADir(const QString &dir);

    QString lastArchiveBDir() const;
    void setLastArchiveBDir(const QString &dir);
};
