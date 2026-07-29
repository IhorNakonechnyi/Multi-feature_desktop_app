#pragma once

#include <QFileInfo>
#include <QList>
#include <QMimeData>
#include <QString>
#include <QUrl>

// Q10: the file picker has always been *.zip-filtered; drag-drop now agrees.
// Exactly one local .zip FILE is accepted - a folder, a multi-select, or any
// other type is refused at dragEnter time so the OS shows the no-drop cursor,
// rather than the app swallowing the drag and then complaining afterwards.
//
// Returns the path to open, or an empty string to refuse. Callers treat
// "empty" as "not a droppable archive"; the path can never legitimately be
// empty, since it came from a URL that named an existing file.
//
// Filtering on the NAME here does not contradict Q3 ("content decides what is
// really an archive"). The name is only a cheap candidate filter for the
// cursor; a .zip-named file that turns out not to be a zip is still caught at
// read time and reported inline on the panel.
inline QString singleDroppedZipPath(const QMimeData *mime)
{
    const QList<QUrl> urls = mime->urls();

    // Exactly one: a multi-file drop used to be silently reduced to its
    // first URL, which quietly discarded whatever else the user dragged.
    if (urls.size() != 1 || !urls.first().isLocalFile()) {
        return QString();
    }

    const QString path = urls.first().toLocalFile();

    // isFile() is what rejects a *directory* named "something.zip" - the
    // name check alone would happily accept one.
    if (!path.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive) || !QFileInfo(path).isFile()) {
        return QString();
    }

    return path;
}
