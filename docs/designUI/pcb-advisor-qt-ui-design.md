# Qt UI implementation design — app shell + archive compare trees

Maps the mockups directly onto Qt Widgets classes. C++20, Qt 6 assumed
(Qt 5.15 works the same way, just drop `QRandomGenerator64`-style newer
API if you hit any).

## 1. Widget tree

```
QMainWindow
└── QSplitter (horizontal, non-collapsible sidebar)
    ├── SidebarNav : QListWidget          (fixed ~170px width)
    │     items: Archive compare, Supplier advisor,
    │            Gerber viewer, Quote compare
    └── QStackedWidget
          ├── ArchiveComparePage : QWidget
          ├── SupplierAdvisorPage : QWidget
          ├── GerberViewerPage : QWidget
          └── QuoteComparePage : QWidget
```

`SidebarNav::currentRowChanged` → `stackedWidget->setCurrentIndex(row)`.
Simplest possible nav pattern, no need for anything fancier.

```cpp
class MainWindow : public QMainWindow {
public:
    MainWindow(QWidget* parent = nullptr) : QMainWindow(parent) {
        auto* splitter = new QSplitter(Qt::Horizontal, this);
        nav_ = new SidebarNav(splitter);
        pages_ = new QStackedWidget(splitter);

        pages_->addWidget(new ArchiveComparePage);
        pages_->addWidget(new SupplierAdvisorPage);
        pages_->addWidget(new GerberViewerPage);
        pages_->addWidget(new QuoteComparePage);

        splitter->setStretchFactor(0, 0); // sidebar fixed
        splitter->setStretchFactor(1, 1); // content grows
        setCentralWidget(splitter);

        connect(nav_, &QListWidget::currentRowChanged,
                pages_, &QStackedWidget::setCurrentIndex);
    }
private:
    SidebarNav* nav_;
    QStackedWidget* pages_;
};
```

---

## 2. Archive compare page layout

```
ArchiveComparePage : QWidget           (QVBoxLayout)
├── QLabel "Archive compare"           (h2-ish, bold)
├── QLabel subtitle                    (muted)
├── QSplitter (horizontal)
│   ├── ArchivePanel  (Archive A / "New")
│   └── ArchivePanel  (Archive B / "Old")
└── QHBoxLayout (right-aligned)
      └── QPushButton "Show diff (…)"  (hidden/disabled unless 1–2 modified)
```

`ArchivePanel` is the reusable widget you saw twice in the mockup — build
it once, instantiate with a label ("New (A)" / "Old (B)") parameter:

```cpp
class ArchivePanel : public QWidget {
    Q_OBJECT
public:
    ArchivePanel(const QString& roleLabel, QWidget* parent = nullptr);
    void setArchive(const QString& path);   // triggers extraction + model build
    ArchiveTreeModel* model() const { return model_; }

signals:
    void archiveChanged(const QString& path);

private:
    QLabel* fileNameLabel_;
    QPushButton* changeButton_;
    QTreeView* treeView_;
    ArchiveTreeModel* model_;
    ArchiveTreeDelegate* delegate_;
};
```

- Header row: `QHBoxLayout` with a zip icon (`QLabel` + `QIcon`), the
  filename, the role text, and the "Change" `QPushButton`, all inside a
  `QFrame` styled as the muted header bar via QSS (`background:
  palette(alternate-base)` or a custom object name + stylesheet rule).
- Below it: a single `QTreeView` in **single-column mode** (name only —
  size rendered inline in the delegate on the right side of the same row,
  exactly like the mockup) or a real two-column tree (`Name`, `Size`) with
  `setColumnWidth` and a right-aligned size column if you'd rather use
  Qt's built-in column layout instead of custom-painting size text. Either
  works; single-column + delegate gets you the exact visual from the
  mockup with less fighting Qt's header widget.

Drag-and-drop straight onto the panel:

```cpp
void ArchivePanel::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}
void ArchivePanel::dropEvent(QDropEvent* e) {
    const auto urls = e->mimeData()->urls();
    if (!urls.isEmpty()) setArchive(urls.first().toLocalFile());
}
```

---

## 3. Tree model — `ArchiveTreeModel`

Don't reach for `QFileSystemModel` — it's tied to the real filesystem and
won't let you attach a `Status` (Same/Changed/Missing) per node or control
sort order the way you want. Subclass `QAbstractItemModel` directly (or
`QStandardItemModel` if you want less boilerplate — see note below) backed
by your own tree of `ArchiveNode`.

```cpp
enum class NodeStatus { Same, Changed, Missing };

struct ArchiveNode {
    QString name;
    bool isDir = false;
    quint64 sizeBytes = 0;      // 0/aggregate for dirs, real size for files
    NodeStatus status = NodeStatus::Same;
    std::vector<std::unique_ptr<ArchiveNode>> children;
    ArchiveNode* parent = nullptr;
};

class ArchiveTreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Role { StatusRole = Qt::UserRole + 1, SizeRole };

    void buildFrom(std::unique_ptr<ArchiveNode> root);  // called after extraction+diff

    QModelIndex index(int row, int col, const QModelIndex& parent) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent) const override;
    int columnCount(const QModelIndex&) const override { return 1; }
    QVariant data(const QModelIndex& index, int role) const override;

private:
    std::unique_ptr<ArchiveNode> root_;
    void sortChildren(ArchiveNode* node); // dirs first (name asc), then files (size asc)
};
```

**Sorting rule, applied once at build time** (not via `QTreeView`'s
click-to-sort headers, since you want a fixed deterministic order, not a
user-toggleable one):

```cpp
void ArchiveTreeModel::sortChildren(ArchiveNode* node) {
    std::sort(node->children.begin(), node->children.end(),
        [](const auto& a, const auto& b) {
            if (a->isDir != b->isDir) return a->isDir; // dirs first
            if (a->isDir) return a->name < b->name;    // dirs: name asc
            return a->sizeBytes < b->sizeBytes;         // files: size asc
        });
    for (auto& c : node->children) sortChildren(c.get());
}
```

If you'd rather avoid writing a full `QAbstractItemModel` (parent/child
index math is the annoying part), `QStandardItemModel` + `QStandardItem`
gets you a working tree much faster — attach `NodeStatus` and size via
`setData(..., StatusRole)` on each `QStandardItem`, and do the same
recursive sort on `QStandardItem::appendRow` order before inserting. Given
this is a portfolio/learning project, writing the real
`QAbstractItemModel` is worth it — it's the "properly learn Qt's model
view pattern" exercise, and it'll pay off again for the Feature 2 supplier
tables and Feature 4 quote comparison grid, which are naturally
table/model-shaped too.

---

## 4. Row coloring — `ArchiveTreeDelegate`

This is where the green/yellow/red mapping actually gets painted. Don't
try to do it via `Qt::BackgroundRole` in `data()` alone if you also want
the folder icon, name, and right-aligned size text laid out exactly like
the mockup — a custom delegate gives you full control:

```cpp
class ArchiveTreeDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override {
        auto status = idx.data(ArchiveTreeModel::StatusRole).value<NodeStatus>();
        QColor bg = backgroundFor(status);
        QColor fg = foregroundFor(status);

        if (bg.isValid()) p->fillRect(opt.rect, bg);

        // icon (folder / file), painted at fixed left offset per depth
        QIcon icon = idx.data(Qt::DecorationRole).value<QIcon>();
        QRect iconRect(opt.rect.left() + 4, opt.rect.top() + 2, 16, 16);
        icon.paint(p, iconRect);

        // name
        p->setPen(fg.isValid() ? fg : opt.palette.text().color());
        QRect textRect = opt.rect.adjusted(iconRect.width() + 10, 0, -70, 0);
        p->drawText(textRect, Qt::AlignVCenter, idx.data(Qt::DisplayRole).toString());

        // size, right-aligned, muted/tinted
        if (!idx.data(Qt::UserRole).toBool() /* i.e. not a directory */) {
            QString sizeText = humanReadableSize(idx.data(ArchiveTreeModel::SizeRole).toULongLong());
            p->drawText(opt.rect.adjusted(0, 0, -8, 0), Qt::AlignVCenter | Qt::AlignRight, sizeText);
        }
    }

private:
    static QColor backgroundFor(NodeStatus s) {
        switch (s) {
            case NodeStatus::Same:    return QColor("#EAF3DE"); // green-50
            case NodeStatus::Changed: return QColor("#FAEEDA"); // amber-50
            case NodeStatus::Missing: return QColor("#FCEBEB"); // red-50
        }
        return {};
    }
    static QColor foregroundFor(NodeStatus s) {
        switch (s) {
            case NodeStatus::Same:    return QColor("#173404"); // green-900
            case NodeStatus::Changed: return QColor("#412402"); // amber-900
            case NodeStatus::Missing: return QColor("#501313"); // red-900
        }
        return {};
    }
};
```

Set it once per tree view: `treeView_->setItemDelegate(new ArchiveTreeDelegate(treeView_));`

Pull the hex values into a small shared `Palette` header/namespace rather
than duplicating them in every delegate — you'll reuse the same
green/yellow/red scheme in Feature 2's fit/no-fit cards and Feature 4's
quote mismatch highlighting, so one source of truth avoids drift.

**Folder-level color dot** (the "which branch has a diff" affordance from
earlier): compute it once at tree-build time — a directory's `status`
becomes `Changed` or `Missing` if *any* descendant has that status
(`Missing`/`Changed` outranks `Same`), propagated bottom-up during
`buildFrom`. Then the delegate just draws a small colored dot next to
collapsed folder icons using that same `StatusRole` — no special-casing in
the paint code beyond "if `isDir` and `status != Same`, draw a dot."

---

## 5. Legend + sidebar styling

The legend (colored squares + labels) is cheap as a tiny custom widget:

```cpp
class StatusLegend : public QWidget {
public:
    StatusLegend(QWidget* parent = nullptr) : QWidget(parent) {
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(makeRow("#639922", "Same"));
        layout->addWidget(makeRow("#BA7517", "Changed"));
        layout->addWidget(makeRow("#A32D2D", "Missing"));
    }
private:
    QWidget* makeRow(const QString& hex, const QString& label) {
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0,0,0,0);
        auto* swatch = new QLabel;
        swatch->setFixedSize(8, 8);
        swatch->setStyleSheet(QString("background:%1; border-radius:2px;").arg(hex));
        h->addWidget(swatch);
        h->addWidget(new QLabel(label));
        h->addStretch();
        return row;
    }
};
```

Drop a `StatusLegend` instance under the sidebar nav (as in the mockup) or
as a small header widget above the two tree panels — either placement is
fine, sidebar keeps it visible regardless of which feature tab is active
later if you reuse the same status colors elsewhere.

Sidebar nav items with icons:

```cpp
auto addNavItem = [&](const QString& text, const QString& iconPath) {
    auto* item = new QListWidgetItem(QIcon(iconPath), text);
    nav_->addItem(item);
};
addNavItem("Archive compare", ":/icons/git-compare.svg");
addNavItem("Supplier advisor", ":/icons/list-check.svg");
addNavItem("Gerber viewer", ":/icons/cpu.svg");
addNavItem("Quote compare", ":/icons/file-invoice.svg");
```

For icons: bundle a small set of outline SVGs (Tabler Icons is MIT
licensed, safe for the public repo — grab just the ~10 you need rather
than the whole set) into a `.qrc` resource file. `QIcon` renders SVGs
natively via the SVG plugin (`Qt::Svg` module — add
`find_package(Qt6 COMPONENTS Svg)` in CMake).

---

## 6. Project structure addition

```
src/ui/
├── main_window.h/.cpp
├── sidebar_nav.h/.cpp
├── status_legend.h/.cpp
├── pages/
│   ├── archive_compare_page.h/.cpp
│   ├── supplier_advisor_page.h/.cpp
│   ├── gerber_viewer_page.h/.cpp
│   └── quote_compare_page.h/.cpp
└── widgets/
    ├── archive_panel.h/.cpp
    ├── archive_tree_model.h/.cpp
    └── archive_tree_delegate.h/.cpp
resources/
├── icons/*.svg
└── resources.qrc
```

`ArchiveTreeModel` and `NodeStatus` are pure Qt/C++ (no domain knowledge
of specs or suppliers) — they belong to the generic UI layer, not
`core/`, since they're specifically about presenting a `FileTree` diff
result, not about deciding anything.

---

## 7. Where the diff engine hands off to the UI

Keep the boundary clean: `core/archive/CrcComparator` (from the earlier
design doc) produces a plain data structure — no Qt types — that
`ArchiveTreeModel::buildFrom` converts into `ArchiveNode`s. That
conversion function is the *only* place that should know both "core"
types and Qt types, e.g.:

```cpp
std::unique_ptr<ArchiveNode> buildNodeTree(const core::FileTree& tree);
```

This keeps `core/` testable without Qt linked in at all (plain CTest/GTest
unit tests on the comparator logic), while the UI layer stays free to
evolve independently — useful since you'll be iterating on the visual
design (like you're doing now) much more than on the comparison logic
itself.
