#include "archivereader.h"

#include <QBuffer>
#include <QFileInfo>
#include <QObject>

#include <quazip.h>
#include <quazipfile.h>
#include <quazipfileinfo.h>

namespace {

bool isNestedArchiveName(const QString &name)
{
    // Only .zip is in scope (research.md: RAR dropped for Phase 1 -
    // licensing, no bundled RAR code).
    return name.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive);
}

// Walks every file in an already-open QuaZip, appending flattened entries
// to `out`. When an entry is itself a nested zip, its bytes are read into
// memory and recursed into with its own name appended to pathPrefix - that
// is what makes a file inside a nested archive show at its true leaf
// location instead of just "some zip file differs".
void readEntriesFrom(QuaZip &zip, const QString &pathPrefix, QVector<ArchiveReader::RawFileEntry> &out)
{
    if (!zip.goToFirstFile()) {
        return;
    }

    do {
        QuaZipFileInfo64 info;
        if (!zip.getCurrentFileInfo(&info)) {
            continue;
        }

        // Zip's own spec says internal paths use '/', but not every writer
        // follows that (e.g. PowerShell's Compress-Archive stores '\')  -
        // normalize so folder nesting in the tree UI doesn't depend on
        // which tool wrote the archive.
        const QString entryName = QString(info.name).replace(QLatin1Char('\\'), QLatin1Char('/'));

        // Explicit directory entries carry no content - folders are
        // inferred later from file paths, so skip them here.
        if (entryName.endsWith(QLatin1Char('/'))) {
            continue;
        }

        const QString fullPath = pathPrefix.isEmpty() ? entryName : pathPrefix + QLatin1Char('/') + entryName;

        if (isNestedArchiveName(entryName)) {
            QuaZipFile nestedFile(&zip);
            if (!nestedFile.open(QIODevice::ReadOnly)) {
                continue; // unreadable nested archive - skip it, don't fail the whole read
            }
            QByteArray nestedBytes = nestedFile.readAll();
            nestedFile.close();

            QBuffer buffer(&nestedBytes);
            if (!buffer.open(QIODevice::ReadOnly)) {
                continue;
            }

            QuaZip nestedZip(&buffer);
            if (nestedZip.open(QuaZip::mdUnzip)) {
                readEntriesFrom(nestedZip, fullPath, out);
                nestedZip.close();
            }
            buffer.close();
        } else {
            ArchiveReader::RawFileEntry entry;
            entry.relativePath = fullPath;
            entry.crc32 = info.crc;
            entry.sizeBytes = info.uncompressedSize;
            out.append(entry);
        }
    } while (zip.goToNextFile());
}

} // namespace

QVector<ArchiveReader::RawFileEntry> ArchiveReader::readArchive(const QString &filePath)
{
    QuaZip zip(filePath);
    if (!zip.open(QuaZip::mdUnzip)) {
        throw ArchiveOpenError(
            QObject::tr("Could not open \"%1\" - it may be corrupt, password-protected, "
                        "or not a valid zip file.")
                .arg(QFileInfo(filePath).fileName()));
    }

    QVector<RawFileEntry> entries;
    readEntriesFrom(zip, QString(), entries);
    zip.close();
    return entries;
}
