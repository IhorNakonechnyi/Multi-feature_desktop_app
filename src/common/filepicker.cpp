#include "filepicker.h"

#include <QFileDialog>
#include <QStandardPaths>

QString FilePicker::pickFile(QWidget *parent, const QString &caption,
                              const QString &nameFilter, const QString &startDir)
{
    QString dir = startDir;
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    }
    return QFileDialog::getOpenFileName(parent, caption, dir, nameFilter);
}
