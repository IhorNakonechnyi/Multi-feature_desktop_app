#pragma once

#include <QString>

class QWidget;

// Shared "Browse..." file picker helper. Always defaults to the user's
// Downloads folder unless a specific last-used directory is passed in -
// research.md: a picker is required for all file types since supplier/quote
// filenames aren't predictable, so it's used uniformly rather than only for
// files that need it.
namespace FilePicker {

// Opens a native "pick one file" dialog. If startDir is empty, defaults to
// the user's Downloads folder. Returns an empty string if the user cancels.
QString pickFile(QWidget *parent, const QString &caption,
                  const QString &nameFilter, const QString &startDir = QString());

} // namespace FilePicker
