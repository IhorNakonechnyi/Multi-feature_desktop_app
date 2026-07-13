#pragma once

#include <QListWidget>

// Left-hand navigation: one row per feature page. Plain QListWidget is
// enough here - no need for a custom model just to show 4 fixed icon+text
// rows (constitution Principle 1: don't reach for a heavier pattern than
// the situation needs).
class SidebarNav : public QListWidget {
    Q_OBJECT

public:
    explicit SidebarNav(QWidget *parent = nullptr);
};
