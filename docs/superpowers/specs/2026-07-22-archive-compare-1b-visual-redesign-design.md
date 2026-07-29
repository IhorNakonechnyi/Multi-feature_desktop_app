# Archive Compare — "Elevated Panels" (1b) Visual Redesign

Approved by Igor 2026-07-22, after reviewing
`docs/designUI/updated_design/Qt 6.8 Widgets redesign/design_handoff_1b_dark_theme/`
(`README.md` + `reference_mockup.dc.html`, direction **1b — Elevated Panels**).

## Goal

Recreate the 1b visual direction — soft rounded card panels, icon+label sidebar, dense-but-comfortable
spacing, dark neutral palette with one muted accent — in the existing Qt 6.8 Widgets app. This is a
**styling and layout pass**, not a feature rebuild: comparison logic (`CrcComparator`, `GerberParser`,
`ExcellonParser`, `MaskCompositor`, `LayerMaskRenderer`) is untouched. Scope is the shared shell
(sidebar nav + status legend, visible on all 4 pages) plus the Archive Compare page's internals only.
Supplier Advisor, Gerber Viewer, and Quote Compare get the new shell "for free" but no page-content
redesign in this pass.

Scope decisions (Igor), each rejecting one respect in which this pass could have gone further than
he wanted:

- **Native Windows title bar kept.** The mockup's custom-drawn dark title bar (3 dot placeholders,
  centered label) is not built — recreating it needs a frameless window and hand-rolled
  move/resize/snap, real risk for a cosmetic detail. Everything below the title bar still gets the
  1b treatment.
- **Existing green/amber/red status colors kept.** The mockup's monochrome + single accent scheme
  (`Match`/`Mismatch`/`Missing`) is not adopted for comparison status — `palette.h`'s
  Same/Changed/Missing colors are unchanged. The one accent color (`#b6c6d7`) is still introduced,
  but only for the active-nav-icon and selected-pill highlight, never for comparison status.
- **Tree rows keep today's full-row color fill.** The mockup's small-dot-per-row style is not
  adopted; `ArchiveTreeDelegate` is unchanged.
- **File trees drop their resizable `QSplitter` for fixed-width (~210px) cards**, matching the
  mockup. Confirmed acceptable: each tree's nested folder display, expand/collapse, and scrolling
  (`QTreeView` + `ArchiveTreeModel`) is unaffected by the column becoming fixed-width — only the
  ability to drag the column wider/narrower is lost.

  **Superseded twice.** The width became 252 px (+20%) during implementation, and as of
  2026-07-29 (commit `0b82b05`) it is **no longer fixed at all**: `ArchiveComparePage::
  updatePanelWidths()` scales the columns with the window, interpolating between 252 px at a
  1100 px window and 384 px at a maximized 1920 px one (= 40% of window width), floored at
  252. A flat width meant every extra pixel from maximizing went to the overlay canvas, so the
  columns' share *fell* from ~46% to ~26% — long archive filenames were hardest to read
  exactly when the screen had the most room. The columns are still not user-draggable, which
  is the part of this decision that stands.
- **Mismatched-pairs list becomes click-to-preview.** Today nothing is selected by default after a
  comparison runs (`gerberPairList_` is populated via `clear()`/`addItem()` with no
  `setCurrentRow()`), and `showButton_` starts disabled until a row is selected. Removing the button
  and wiring selection directly to the existing render path is therefore a strict simplification, not
  a behavior loss — clicking any pair pill is already the natural first trigger.
- **Detail area stays two separate elements**, visually unified inside one card. `pairStatusLabel_`
  and `renderTimeLabel_` are not merged into one string — they become two stacked `QLabel` rows inside
  a single Detail-styled container.
- **Status legend becomes dynamic**, and is wired up for the first time. `StatusLegend` exists today
  but is never instantiated anywhere. This pass gives it a real `setEntries(...)`-style API and has
  `MainWindow` swap its rows based on the active page: Archive Compare supplies its existing 3 rows
  (same colors its tree already uses); Supplier Advisor / Gerber Viewer / Quote Compare are still stub
  "coming later" pages with no comparison concept, so the legend simply hides on those pages until
  they have real content.

## 1. Token additions (`palette.h`)

Additive only — existing `Same`/`Changed`/`Missing` background/foreground constants are untouched.
New neutral/card/accent tokens (hex values are exact, taken from the 1b design doc):

| Token | Hex | Usage |
|---|---|---|
| `frame_bg` | `#090a0c` | Page/content background |
| `sidebar_bg` | `#0c0d0f` | Sidebar background |
| `sidebar_title_text` | `#e2e5e8` | "PCB Suite" wordmark |
| `nav_active_bg` | `#1d1f23` | Active nav item pill background |
| `nav_active_text` | `#f0f2f5` | Active nav item label |
| `nav_inactive_text` | `#7d8085` | Inactive nav item label |
| `nav_inactive_icon` | `#303337` | Inactive nav icon swatch |
| `accent` | `#b6c6d7` | Active nav icon + selected-pill highlight **only** — never comparison status |
| `legend_card_bg` | `#141619` | Status legend card background |
| `legend_label` | `#6f7276` | Legend section micro-label |
| `legend_item_text` | `#bbbec3` | Legend row text |
| `card_bg` | `#101214` | Tree/pills/canvas/detail card backgrounds |
| `card_header_border` | `#1e1f22` | Divider under a tree card's filename header |
| `pair_active_bg` | `#212a33` | Selected pair pill |
| `pair_inactive_bg` | `#141619` | Unselected pair pill |
| `canvas_placeholder_bg` / `canvas_placeholder_text` | `#121416` / `#4a4d52` | Canvas idle state |
| `detail_text` | `#c0c4cb` | Detail card body text |

## 2. Shell: `MainWindow` / `SidebarNav` / `StatusLegend`

- One new QSS stylesheet (e.g. `src/app/theme.qss`, loaded via Qt resource and applied with
  `qApp->setStyleSheet(...)` in `main.cpp`), targeting widgets by `objectName`, built from the tokens
  above.
- `SidebarNav`: add a "PCB Suite" wordmark label above the nav list; QSS gives the active row
  `nav_active_bg` + `nav_active_text`, inactive rows `nav_inactive_text`; icon slots resized to the
  24×24 treatment.
- `StatusLegend`: replace its hardcoded 3-row constructor body with a small `Entry{color, label}` list
  API (e.g. `setEntries(const QVector<Entry>&)`); card styling via `legend_card_bg`.
- **Wiring detail:** `SidebarNav` is a `QListWidget` itself, not a container, so it can't directly hold
  a `StatusLegend` child at its bottom. `MainWindow` introduces one small wrapper widget — `nav_`
  (stretch) stacked above `statusLegend_` (fixed) in a `QVBoxLayout` — and it's this wrapper, not
  `nav_` directly, that goes into the outer splitter.
- `MainWindow`'s existing `nav_` → `pages_` index connection also updates `statusLegend_`: index 0
  (Archive Compare) gets its 3 entries (sourced from the same `Palette` calls `ArchiveTreeDelegate`
  already uses); indices 1–3 hide the legend.

## 3. `ArchiveComparePage` layout restructure

Replace the current stacked layout (top: `QSplitter(panelA_, panelB_)` at 70% height; bottom:
`gerberDiffContent_` at 30% height) with one full-height row (`QHBoxLayout`):

`panelA_` (card) — `panelB_` (card) — right column (stretch, `QVBoxLayout`):

> Both the width and this two-widget layout have since changed: the columns scale with the
> window (see above), and Q8's filter bar split each panel into an `ArchiveHeader` above the
> bar and an `ArchivePanel` tree below it, arranged in a `QGridLayout`. See
> `2026-07-29-archive-compare-summary-filters-and-bands-design.md`.

1. **Pairs card** — the pill row (`gerberPairList_`, restyled but still the same `QListWidget`) plus,
   below it, the existing skipped-pairs messaging (`skippedUnrecognizedLabel_` + its "Add .ext"
   buttons, `skippedDrillLabel_`) — same show/hide logic as today, moved into this container.
   Explanation label copy changes from "Select a pair and click Show..." to reflect the button's
   removal (e.g. "Select a pair to preview the diff.").
2. **Canvas card** — `gerberCanvas_`, stretches to fill remaining height.
3. **Detail card** — fixed height, holding `pairStatusLabel_` and `renderTimeLabel_` as two stacked
   rows, plus the existing overlay-tint legend ("green = only new · red = only old · yellow = same")
   as a third static line.

Interaction change: `gerberPairList_`'s `currentRowChanged` connects directly to
`showSelectedPair()` (already guards `row < 0`/out-of-range internally). `showButton_` — the member,
its construction, and its connect — is removed entirely.

`ArchivePanel`'s own header (filename + "Change..." button) is unchanged functionally, just restyled
as a card with `card_header_border` under the header row.

## 4. File impact list

- `src/common/palette.h` — additive tokens only.
- New: `src/app/theme.qss` (or equivalent resource) + a few lines in `main.cpp`/`mainwindow.cpp` to
  load and apply it.
- `src/app/sidebarnav.{h,cpp}` — wordmark label; QSS object names.
- `src/app/statuslegend.{h,cpp}` — `Entry` struct + `setEntries(...)`, drop hardcoded ctor rows.
- `src/app/mainwindow.{h,cpp}` — sidebar+legend wrapper widget; page-change handler also updates
  `statusLegend_`.
- `src/archivecompare/archivecomparepage.{h,cpp}` — 3-column layout restructure; remove
  `showButton_`; consolidate `pairStatusLabel_`/`renderTimeLabel_`/overlay-legend into one Detail
  card; pairs list restyled as a pill card.
- `src/archivecompare/archivepanel.{h,cpp}` — fixed width instead of splitter-hosted; card styling.
- `src/archivecompare/archivetreedelegate.cpp` — unchanged.

## 5. Explicitly out of scope

- Custom frameless title bar.
- Recoloring comparison status to the mockup's monochrome scheme.
- Supplier Advisor / Gerber Viewer / Quote Compare page-content redesign.
- Dot-style tree row indicators.
- Any change to comparison/parsing/rendering business logic.

## 6. Testing

Per constitution principle 4, this is UI/glue code — manually tested, no new unit tests. Manual
verification: build and run; load two archives with at least one Identical/Different/Only-in-this-
archive file each and 2+ differing Gerber pairs; confirm fixed-width tree cards keep full-row color
fill and nested folder expand/collapse; confirm the sidebar shows the wordmark and the legend card;
confirm the legend hides on the other 3 pages; confirm clicking a pair pill renders immediately with
no Show button; confirm the Detail card shows status text, render time, and the overlay-tint legend;
confirm skipped-pairs messaging still appears/hides correctly inside the pairs card.
