#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

// The header half of what used to be one ArchivePanel: which archive this
// slot holds, a button to change it, and the inline error line. Split out
// (Q8) so a single filter bar can span both sides, sitting between the
// headers and the trees - a bar that acts on both panels cannot live inside
// either one without implying it only affects that side.
class ArchiveHeader : public QWidget {
    Q_OBJECT

public:
    explicit ArchiveHeader(const QString &roleLabel, QWidget *parent = nullptr);

    QString archivePath() const { return archivePath_; }

    // Inline failure display (Q7: errors are data; only display code shows
    // anything, never a modal dialog).
    void showError(const QString &message);
    void clearError();

    // A drop that landed on this side's TREE rather than on the header.
    // Both accept drops, but only the header owns the path.
    void setArchivePathFromDrop(const QString &path) { setArchivePath(path); }

signals:
    // Emitted whenever the user picks or drops a new archive - the page
    // re-runs the comparison once both slots have a path.
    void archiveChanged(const QString &path);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void pickArchive();
    void setArchivePath(const QString &path);

    QString roleLabel_;
    QString archivePath_;
    QLabel *fileNameLabel_;
    QPushButton *changeButton_;
    QLabel *errorLabel_;
};
