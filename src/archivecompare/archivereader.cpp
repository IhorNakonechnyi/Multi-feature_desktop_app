#include "archivereader.h"

#include <QBuffer>
#include <QFileInfo>
#include <QObject>
#include <QScopedPointer>

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

// QuaZip::setCurrentFile() looks a name up against each entry's raw,
// as-stored name with no separator normalization at all (confirmed by
// reading QuaZip's own setCurrentFile()/getCurrentFileInfo() in
// quazip.cpp) - but entryRelativePath always arrives '/'-normalized (see
// readEntriesFrom above), so a zip whose writer stored '\'-separated
// names (again, PowerShell's Compress-Archive) would never match by
// plain setCurrentFile(entryName). Scan the zip's own stored name list,
// normalize each one the same way readEntriesFrom does, and compare
// against the already-normalized entryName - then hand setCurrentFile
// the *actual* stored name (not the normalized one) since that's the
// only string its exact lookup will accept. Returns an empty string if
// no entry's normalized name matches.
QString findStoredEntryName(QuaZip &zip, const QString &normalizedName)
{
    const QStringList storedNames = zip.getFileNameList();
    for (const QString &storedName : storedNames) {
        const QString normalizedStoredName = QString(storedName).replace(QLatin1Char('\\'), QLatin1Char('/'));
        // Windows (this app's only target platform) treats zip entry names
        // case-insensitively by default - matching QuaZip::setCurrentFile's
        // own csDefault behavior there keeps this lookup consistent with
        // what setCurrentFile would have done had the separators matched.
        if (normalizedStoredName.compare(normalizedName, Qt::CaseInsensitive) == 0) {
            return storedName;
        }
    }
    return QString();
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

QByteArray ArchiveReader::readEntryBytes(const QString &archiveFilePath, const QString &entryRelativePath)
{
    // entryRelativePath is one of readArchive's flattened paths (see
    // readEntriesFrom above): at each zip level it's built from that
    // level's own *raw* entry name, which can itself contain '/' when the
    // entry sits in a plain subfolder (e.g. a top-level entry literally
    // named "wrapper/archive3.zip"). So this can't just split on every
    // '/' and look each piece up individually - QuaZip::setCurrentFile
    // needs the *exact* stored entry name, slash and all. Instead, scan
    // forward for the next ".zip/" boundary: everything up to and
    // including ".zip" is one zip level's entry name; if that's a nested
    // zip, descend into its bytes and repeat with what's left. Once no
    // further ".zip/" boundary remains, whatever's left (still possibly
    // containing '/' for a plain subfolder) is the final entry to read.
    QuaZip zip(archiveFilePath);
    if (!zip.open(QuaZip::mdUnzip)) {
        return QByteArray();
    }

    QuaZip *currentZip = &zip;
    QScopedPointer<QBuffer> nestedBuffer;
    QScopedPointer<QuaZip> nestedZip;
    QByteArray currentArchiveBytes;
    QString remainingPath = entryRelativePath;

    while (true) {
        const int zipSuffixPos = remainingPath.indexOf(QLatin1String(".zip/"), 0, Qt::CaseInsensitive);
        const bool isLastSegment = (zipSuffixPos < 0);
        const QString entryName = isLastSegment ? remainingPath : remainingPath.left(zipSuffixPos + 4); // +4: include ".zip"

        // entryName is always '/'-normalized; resolve it to whatever this
        // zip level actually stored (see findStoredEntryName above) before
        // handing it to setCurrentFile, which needs the exact raw name.
        const QString storedEntryName = findStoredEntryName(*currentZip, entryName);
        if (storedEntryName.isEmpty() || !currentZip->setCurrentFile(storedEntryName)) {
            return QByteArray();
        }
        QuaZipFile file(currentZip);
        if (!file.open(QIODevice::ReadOnly)) {
            return QByteArray();
        }
        const QByteArray bytes = file.readAll();
        file.close();

        if (isLastSegment) {
            return bytes;
        }

        // Not the last segment - this must itself be a nested zip to
        // descend into.
        currentArchiveBytes = bytes;
        nestedBuffer.reset(new QBuffer(&currentArchiveBytes));
        if (!nestedBuffer->open(QIODevice::ReadOnly)) {
            return QByteArray();
        }
        nestedZip.reset(new QuaZip(nestedBuffer.data()));
        if (!nestedZip->open(QuaZip::mdUnzip)) {
            return QByteArray();
        }
        currentZip = nestedZip.data();
        remainingPath = remainingPath.mid(zipSuffixPos + 5); // +5: skip past ".zip/"
    }
}
