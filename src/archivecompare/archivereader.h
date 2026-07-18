#pragma once

#include <stdexcept>

#include <QByteArray>
#include <QString>
#include <QVector>

// Thrown when the top-level archive itself can't be opened at all (corrupt,
// password-protected, or not a valid zip). Callers catch this and show a
// clear per-archive error message instead of crashing or blocking the
// other archive's comparison (FR-008).
class ArchiveOpenError : public std::runtime_error {
public:
    explicit ArchiveOpenError(const QString &message)
        : std::runtime_error(message.toStdString()), message_(message)
    {
    }

    QString message() const { return message_; }

private:
    QString message_;
};

namespace ArchiveReader {

// crc32/sizeBytes come straight from the zip central directory - no
// decompression needed, except to read a nested zip archive's own bytes
// so it can be opened and recursed into.
struct RawFileEntry {
    QString relativePath;
    quint32 crc32 = 0;
    quint64 sizeBytes = 0;
};

// Reads a .zip archive into a flat list of files, recursing into any
// nested zip-inside-zip entries so their contents show at their true leaf
// location (FR-002). Throws ArchiveOpenError if the top-level archive
// can't be opened.
QVector<RawFileEntry> readArchive(const QString &filePath);

// Reads one entry's raw bytes directly out of archiveFilePath (which is
// itself a real filesystem path to the top-level archive). entryRelativePath
// is one of the RawFileEntry::relativePath values readArchive produces,
// which may cross into a nested zip (e.g. "wrapper/archive3.zip/top.gtl") -
// see readArchive's own recursion. Returns an empty QByteArray if the entry
// can't be found or read; unlike readArchive, this doesn't throw - a single
// missing/corrupt entry when opening the Gerber diff view isn't as
// catastrophic as failing to open the whole archive.
QByteArray readEntryBytes(const QString &archiveFilePath, const QString &entryRelativePath);

} // namespace ArchiveReader
