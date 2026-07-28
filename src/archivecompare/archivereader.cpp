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

// Result of resolving one path step (see resolveStep below) against a
// single open zip level.
struct ResolvedStep {
    QString storedName; // raw stored name as setCurrentFile() needs it; empty = no match
    bool    isFinal;    // storedName covers the whole remaining path
    QString rest;       // remainder after storedName + '/', when !isFinal
};

// One step of resolving a '/'-normalized remaining path against whatever
// entries a single zip level actually has stored. Two things make this more
// than a plain lookup:
//
// 1. QuaZip::setCurrentFile() matches against each entry's raw, as-stored
//    name with no separator normalization at all (confirmed by reading
//    QuaZip's own setCurrentFile()/getCurrentFileInfo() in quazip.cpp), but
//    normalizedRemaining always arrives '/'-normalized (see readEntriesFrom
//    above) - so a zip whose writer stored '\'-separated names (e.g.
//    PowerShell's Compress-Archive) would never match by a plain lookup of
//    normalizedRemaining itself. Every stored name is normalized the same
//    way before comparing, then the *actual* stored name is returned, since
//    that's the only string setCurrentFile's exact lookup will accept.
//
// 2. A single stored entry name can itself contain '/' - both because a
//    plain subfolder entry is one stored name ("docs/board.gtl", not
//    separate "docs" and "board.gtl" entries) and because a nested zip's
//    own name can sit under a subfolder ("wrapper/nested.zip" is one
//    entry). So there is no such thing as "the next path segment" in
//    general; the only reliable way to know where one entry's name ends is
//    to check what names actually exist.
//
// That is what the two rules below do, in this exact order:
//
//   1. Exact match: some stored name, normalized, equals the *whole*
//      remaining path. This must be tried first - see the walkthrough
//      below.
//   2. Prefix descent: no exact match, but some stored name S is a proper
//      prefix of the remaining path followed by '/'. The *longest* such S
//      is used, so a deeper nested-zip name never loses to a shallower one
//      that also happens to prefix-match.
//
// Trying (1) before (2) is what makes a plain *folder* named "gerber.zip"
// work correctly. Given a stored entry "gerber.zip/top.gtl" (an ordinary
// subfolder that happens to end in ".zip") and remaining path
// "gerber.zip/top.gtl", rule 1 matches the whole path directly and the walk
// ends there. If prefix descent were tried first (or the old code's
// approach of always treating ".zip/" as a boundary), "gerber.zip" would be
// mistaken for a nested archive to open - but no entry is stored under that
// exact name, so the lookup would fail even though the file plainly exists.
ResolvedStep resolveStep(QuaZip &zip, const QString &normalizedRemaining)
{
    const QStringList storedNames = zip.getFileNameList();

    // Windows (this app's only target platform) treats zip entry names
    // case-insensitively by default - matching QuaZip::setCurrentFile's own
    // csDefault behavior there keeps this lookup consistent with what
    // setCurrentFile would have done had the separators matched.
    for (const QString &storedName : storedNames) {
        const QString normalizedStoredName = QString(storedName).replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (normalizedStoredName.compare(normalizedRemaining, Qt::CaseInsensitive) == 0) {
            return ResolvedStep{storedName, true, QString()};
        }
    }

    QString longestPrefixStoredName;
    QString longestPrefixNormalized;
    for (const QString &storedName : storedNames) {
        const QString normalizedStoredName = QString(storedName).replace(QLatin1Char('\\'), QLatin1Char('/'));
        const QString prefix = normalizedStoredName + QLatin1Char('/');
        if (normalizedRemaining.startsWith(prefix, Qt::CaseInsensitive)
            && normalizedStoredName.length() > longestPrefixNormalized.length()) {
            longestPrefixStoredName = storedName;
            longestPrefixNormalized = normalizedStoredName;
        }
    }
    if (!longestPrefixStoredName.isEmpty()) {
        return ResolvedStep{longestPrefixStoredName, false,
                             normalizedRemaining.mid(longestPrefixNormalized.length() + 1)};
    }

    return ResolvedStep{QString(), false, QString()};
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

QByteArray ArchiveReader::readEntryBytes(const QString &archiveFilePath, const QString &entryRelativePath, bool *found)
{
    if (found) {
        *found = false;
    }
    // entryRelativePath is one of readArchive's flattened paths (see
    // readEntriesFrom above): at each zip level it's built from that
    // level's own *raw* entry name, which can itself contain '/' when the
    // entry sits in a plain subfolder (e.g. a top-level entry literally
    // named "wrapper/archive3.zip"). There's no way to know in advance
    // where one zip level's entry name ends and the next level's path
    // begins - not by splitting on '/', and not (the old approach here) by
    // treating every ".zip/" as a boundary, since a plain folder can
    // legitimately be named "gerber.zip" too. So each step is resolved
    // against whatever entries the current zip level actually has stored
    // (resolveStep, above) - the name is only ever a candidate, content
    // (does it open as a zip?) decides whether to descend further.
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
        const ResolvedStep step = resolveStep(*currentZip, remainingPath);
        if (step.storedName.isEmpty() || !currentZip->setCurrentFile(step.storedName)) {
            return QByteArray(); // no entry at this level matches what's left of the path
        }

        QuaZipFile file(currentZip);
        if (!file.open(QIODevice::ReadOnly)) {
            return QByteArray();
        }
        const QByteArray bytes = file.readAll();
        file.close();

        if (step.isFinal) {
            if (found) {
                *found = true;
            }
            return bytes;
        }

        // Not final - step.storedName matched a path prefix, so it must
        // itself open as a nested zip to keep descending. If it doesn't
        // (this "prefix" was actually the target file's whole content,
        // e.g. a non-zip file that just happens to share a name prefix),
        // the path is bogus - report not found rather than returning
        // unrelated bytes.
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
        remainingPath = step.rest;
    }
}
