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

} // namespace Palette
