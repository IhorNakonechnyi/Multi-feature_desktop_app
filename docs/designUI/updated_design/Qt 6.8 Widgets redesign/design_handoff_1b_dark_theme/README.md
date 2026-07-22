# Handoff: "Elevated Panels" Dark Theme (Direction 1b)

## Overview
Visual redesign direction for the PCB consulting desktop app (sidebar nav, Archive Compare, Supplier Advisor, Gerber Viewer, Quote Compare, status legend, tree/table rows). This is direction **1b — Elevated Panels**: soft rounded card surfaces, icon+label sidebar, dense but comfortable spacing. Dark-theme-only, per the app's existing constraint.

## About the design file
`reference_mockup.dc.html` is an **HTML/CSS design reference**, built to show the intended look — not code to port directly. The task is to **recreate this styling in the existing Qt 6.8 Widgets (C++20) app**, using QSS stylesheets and the app's existing `palette.h` color-definition pattern, applied to the real widgets (`sidebarnav.*`, `mainwindow.*`, `archivetreedelegate.*`, `statuslegend.*`, and the other 3 pages). Open the HTML file in a browser to see it live; only the Archive Compare page and sidebar were mocked in detail — apply the same system to Supplier Advisor, Gerber Viewer, and Quote Compare.

## Fidelity
**High-fidelity.** Colors below are exact (converted from the design tool's OKLCH values to sRGB hex — use the hex values directly in QSS/`palette.h`, since OKLCH isn't usable there). Spacing, radii, and type sizes are exact. Copy/labels are illustrative placeholders, not final strings.

## Design tokens

### Palette (dark theme, neutral monochrome + one muted cool-gray accent)
| Token | Hex | Usage |
|---|---|---|
| `frame_bg` | `#090a0c` | App window background |
| `frame_border` | `#252629` | Outer window border (1px) |
| `titlebar_bg` | `#050607` | Custom title bar background |
| `titlebar_text` | `#7d8085` | Title bar label |
| `traffic_dot` | `#45484c` | Window control dots (if custom-drawn; skip if using native OS chrome) |
| `sidebar_bg` | `#0c0d0f` | Sidebar nav background |
| `sidebar_title_text` | `#e2e5e8` | "PCB Suite" wordmark, page titles |
| `nav_active_bg` | `#1d1f23` | Active nav item pill background |
| `nav_active_text` | `#f0f2f5` | Active nav item label |
| `nav_inactive_text` | `#7d8085` | Inactive nav item label |
| `nav_inactive_icon` | `#303337` | Inactive nav icon placeholder swatch |
| `accent` | `#b6c6d7` | **The one accent color** — active nav icon, "Match" status dot, active pair selector border. Muted cool gray-blue, low chroma. Use sparingly — selection/success state only. |
| `legend_card_bg` | `#141619` | Status legend + pair-list card background |
| `legend_label` | `#6f7276` | Small uppercase section labels (e.g. "STATUS LEGEND") |
| `legend_item_text` | `#bbbec3` | Legend row text |
| `mismatch_dot` | `#6e7278` | "Mismatch" status dot/indicator |
| `missing_outline` | `#45484c` | "Missing" status indicator (outline only, no fill) |
| `card_bg` | `#101214` | Tree panel / canvas panel / detail panel backgrounds |
| `card_header_border` | `#1e1f22` | Divider under tree column header (archive filename) |
| `tree_row_text` | `#c1c4c9` | File tree row text |
| `pair_active_bg` | `#212a33` | Selected mismatch-pair pill background |
| `pair_inactive_bg` | `#141619` | Unselected mismatch-pair pill background |
| `canvas_placeholder_bg` | `#121416` | Canvas/diff render area background |
| `canvas_placeholder_text` | `#4a4d52` | Canvas placeholder label |
| `detail_text` | `#c0c4cb` | Detail panel body text |
| `detail_highlight` | `#ebeff5` | Emphasized values in detail panel (e.g. a delta measurement) |

Only **one accent hue** is used app-wide (`accent` = `#b6c6d7`), reserved for: the active nav icon, the "Match" status dot, and the active/selected state of the mismatched-pairs selector. Everything else is neutral gray-black at varying lightness — do not introduce other hues.

### Spacing & sizing
- App frame: rounded outer corners, radius **10px**, 1px border (`frame_border`), soft drop shadow below (`0 20px 50px -20px rgba(0,0,0,0.6)` — translate to a `QGraphicsDropShadowEffect` if the window chrome supports it, otherwise omit; not critical).
- Custom title bar (optional — only if not using native OS chrome): height **38px**, 3 status dots at 11px diameter, 8px gaps, label at 12.5px/weight 600.
- Sidebar: width **216px**, padding 16px/10px, nav items stacked with **4px** gap.
- Nav item row: padding **11px 12px**, border-radius **8px**, icon slot **24×24px** (radius 6px) — this is the enlarged slot sized to take a real icon; label at 13.5px/weight 600 (active) or 400 (inactive), gap between icon and label **12px**.
- Sidebar bottom: status-legend card, radius **8px**, padding **12px**, legend rows gap **6px**, dot size 9px.
- Content area: page title 15px/weight 700; two file-tree cards each **210px** wide, radius **8px**; tree header row padding **8px 10px**, divider below it; tree rows padding **6px 8px**, radius 6px, status dot 8px.
- Mismatched-pairs card: radius 8px, padding **10px 12px**, label 10px uppercase/letter-spacing 0.05em; pills: radius **16px** (pill shape), padding **5px 12px**, font 11.5px.
- Canvas/diff panel: flexes to fill remaining height, radius 8px.
- Detail panel: fixed height **96px**, radius 8px, padding 10px 14px; label 11px, body 12.5px.
- Overall gap between major panels: **12px**. Card-internal padding: **8–14px** depending on density.

### Typography
- UI font: system sans (Inter used in the mock; map to whatever the app's default Qt font is, or switch to Inter/SF/Segoe if available).
- Monospace (file names, technical values): JetBrains Mono or the nearest available monospace — used for archive/file names and detail-panel measurements only, not for nav or headings.
- Scale used: 10px (micro-labels, uppercase, letter-spacing ~0.05–0.06em) / 11–12px (body/rows) / 13–13.5px (nav) / 15px (page title).

## Screens / Views
Only **Archive Compare** was mocked in full detail (two file trees + pair selector + canvas + detail panel). Apply the same card language to the other 3 pages:
- **Supplier Advisor**: a dropzone card (dashed border, `card_bg`, radius 8px) above a data table styled like the tree rows (same row height/text color/status dots).
- **Gerber Viewer**: same canvas-panel treatment as Archive Compare but larger (canvas panel gets most of the width), with a single file list card at fixed width to its side.
- **Quote Compare**: same two-tree-plus-detail layout as Archive Compare, but comparing PDF/DOC files instead of Gerbers; the detail panel below shows structural differences instead of trace deltas.

### Sidebar nav
Persistent left rail, all 4 pages listed with icon + label (icon slots are placeholders now — 24×24px, ready for real icon assets). Active page: `nav_active_bg` pill + `accent`-colored icon + brightest text. Inactive: dim icon swatch + `nav_inactive_text`. Status legend pinned to sidebar bottom in its own card.

### Status legend
3 states shown as a dot + label list: **Match** (filled `accent` dot), **Mismatch** (filled `mismatch_dot`), **Missing** (outline-only, no fill, `missing_outline`). Reused wherever comparison results are shown (also drives the mismatched-pairs selector styling).

### Mismatched-pairs selector (Archive Compare / Quote Compare)
New interaction: above the canvas, a row of pill-shaped chips, one per mismatched file pair between the two archives (e.g. "silkscreen_top.gbr ⇄ silkscreen_top.gbr"). Clicking a pill loads that pair's diff into the canvas below. Selected pill = `pair_active_bg` background; others = `pair_inactive_bg`. This replaces having no way to choose which mismatch to inspect when more than one exists.

## Interactions & Behavior
- Nav item click → navigate to that page (existing behavior, unchanged).
- Mismatched-pair pill click → sets the active pair, re-renders canvas + detail panel for that pair (new; wire to whatever diff-loading call currently exists for a single pair).
- No new animations specified; a simple background/opacity transition on hover/active states for nav items and pills is reasonable (150ms).

## Assets
No real icons/images used yet — nav icon slots and canvas areas are placeholders. Real Gerber/PDF icon set and canvas rendering are out of scope for this visual pass.

## Files
- `reference_mockup.dc.html` — contains 3 explored directions (1a hairline, 1b elevated panels — **this is the selected one**, 1c technical blueprint). Only 1b should be implemented.
