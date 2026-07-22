#pragma once

#include <QColor>

// One source of truth for the green/yellow/red status scheme, reused by
// the archive comparison tree, supplier fit cards, and quote mismatch
// highlighting (data-model.md), so the same three colors don't drift
// out of sync across features.
//
// The app is dark-mode-only (main.cpp forces Qt::ColorScheme::Dark), so
// these are solid "chip" colors designed to sit on a dark background, not
// pale washes - each background is picked to have decent contrast with its
// paired foreground text color, not just to "look like green/yellow/red".
namespace Palette {

// Classic/deep green, white text - passes normal-text contrast (~5.9:1).
constexpr auto SameBackground = "#2E7D32";
constexpr auto SameForeground = "#FFFFFF";

// Amber, near-black text - yellow backgrounds pair better with dark text
// than white (same reasoning as black-on-yellow hazard signage).
constexpr auto ChangedBackground = "#F9A825";
constexpr auto ChangedForeground = "#1A1A1A";

// Classic red, white text - passes normal-text contrast (~4.5:1).
constexpr auto MissingBackground = "#C62828";
constexpr auto MissingForeground = "#FFFFFF";

inline QColor sameBackground() { return QColor(SameBackground); }
inline QColor sameForeground() { return QColor(SameForeground); }
inline QColor changedBackground() { return QColor(ChangedBackground); }
inline QColor changedForeground() { return QColor(ChangedForeground); }
inline QColor missingBackground() { return QColor(MissingBackground); }
inline QColor missingForeground() { return QColor(MissingForeground); }

// Neutral/card tokens for the 1b "Elevated Panels" visual redesign (see
// docs/superpowers/specs/2026-07-22-archive-compare-1b-visual-redesign-design.md).
// Additive only - the Same/Changed/Missing tokens above are unchanged and
// remain the only colors used for comparison-result meaning. These are
// pure UI-chrome colors (backgrounds, dividers, inactive/active states).
constexpr auto FrameBackground = "#090a0c";
constexpr auto SidebarBackground = "#0c0d0f";
constexpr auto SidebarTitleText = "#e2e5e8";
constexpr auto NavActiveBackground = "#1d1f23";
constexpr auto NavActiveText = "#f0f2f5";
constexpr auto NavInactiveText = "#7d8085";
constexpr auto LegendCardBackground = "#141619";
constexpr auto LegendLabelText = "#6f7276";
constexpr auto LegendItemText = "#bbbec3";
constexpr auto CardBackground = "#101214";
constexpr auto CardHeaderBorder = "#1e1f22";
constexpr auto PairActiveBackground = "#212a33";
constexpr auto PairInactiveBackground = "#141619";
constexpr auto DetailText = "#c0c4cb";

} // namespace Palette
