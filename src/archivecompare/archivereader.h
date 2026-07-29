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
// decompression needed. isNestedArchiveCandidate is a cheap NAME-based
// filter (".zip" suffix) only - it is never proof the entry actually is a
// nested archive; expandNested() below is what makes that call by trying
// to open the bytes, per the "name filters, content decides" rule this
// module follows throughout.
struct RawFileEntry {
    QString relativePath;
    quint32 crc32 = 0;
    quint64 sizeBytes = 0;
    bool isNestedArchiveCandidate = false;
};

// Lists ONLY the top level of the archive - a nested-archive candidate
// (anything named "*.zip") appears here as a single opaque entry with its
// own central-directory crc/size, not expanded. Whether it's worth
// expanding at all is a decision that needs both archives being compared
// in hand (a nested archive whose content is unchanged on the other side
// has nothing new to show), so that decision belongs to the comparison
// layer, not here - see expandNested() below for the on-demand second
// phase. Throws ArchiveOpenError if the top-level archive can't be opened.
QVector<RawFileEntry> readTopLevel(const QString &filePath);

// Result of trying to expand one nested-archive candidate (see
// expandNested below). openedAsArchive=false means the candidate's bytes
// didn't actually open as a zip (e.g. a plain-text file that merely
// happens to be named "notes.zip") - the caller should keep the original
// RawFileEntry as an ordinary file rather than treat it as a container.
struct NestedExpansion {
    bool openedAsArchive = false;
    QVector<RawFileEntry> entries;
};

// Opens the single nested-archive candidate at containerRelativePath (one
// of readTopLevel's RawFileEntry::relativePath values with
// isNestedArchiveCandidate=true) and lists ITS top level, with each
// resulting entry's relativePath prefixed "containerRelativePath/" so it
// reads the same as readArchive's old flattened paths did.
// containerRelativePath may itself lie inside an already-expanded
// container (e.g. "inner.zip/deep.zip") - this reuses the same
// resolveStep()-based descent as readEntryBytes/readEntryHead to walk
// through as many levels as the path requires, rather than assuming a
// single level. A candidate one level inside the freshly-opened container
// is listed as its own opaque entry (isNestedArchiveCandidate=true) and is
// NOT recursed into further - expansion is always exactly one level per
// call, driven by the caller, same as the top level itself.
NestedExpansion expandNested(const QString &archiveFilePath, const QString &containerRelativePath);

// Reads one entry's raw bytes directly out of archiveFilePath (which is
// itself a real filesystem path to the top-level archive). entryRelativePath
// is one of the RawFileEntry::relativePath values readTopLevel/expandNested
// produce, which may cross into a nested zip (e.g.
// "wrapper/archive3.zip/top.gtl"). The path is resolved one step at a time
// against whatever entries the current zip level actually has (see
// resolveStep() in archivereader.cpp) - it is never guessed from ".zip"
// appearing in the name. Returns an empty QByteArray if the entry can't be
// found or read; unlike readTopLevel, this doesn't throw - a single
// missing/corrupt entry when opening the Gerber diff view isn't as
// catastrophic as failing to open the whole archive.
//
// An empty result is ambiguous on its own (missing entry vs. a genuinely
// zero-byte entry) - pass `found` to tell them apart: set to false when no
// matching entry exists or it couldn't be read, true otherwise. Defaulted
// to nullptr so existing call sites that don't care compile unchanged.
QByteArray readEntryBytes(const QString &archiveFilePath, const QString &entryRelativePath, bool *found = nullptr);

// Same resolution walk as readEntryBytes (including through nested zips -
// see readEntryBytes above), but only reads at most maxBytes of the final
// entry instead of the whole thing. Intermediate nested-zip levels must
// still be read in full to be opened at all - only the *final* entry's read
// is capped. Used by callers (e.g. content-sniffing a candidate drawable
// file) that only need a small prefix and want to avoid pulling a large
// entry entirely into memory just to look at its first few bytes. Same
// `found` semantics as readEntryBytes.
QByteArray readEntryHead(const QString &archiveFilePath, const QString &entryRelativePath, qint64 maxBytes,
                          bool *found = nullptr);

} // namespace ArchiveReader
