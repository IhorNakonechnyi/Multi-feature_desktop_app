#pragma once

#include <QMainWindow>

class QStackedWidget;
class SidebarNav;

// App shell: sidebar nav on the left, the active feature page on the right.
// Splitter (not just a plain layout) so the sidebar width is user-resizable
// like a normal desktop app, per docs/designUI's mockup.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    SidebarNav *nav_;
    QStackedWidget *pages_;
};
