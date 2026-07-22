#pragma once

#include <QString>

// The 1b "Elevated Panels" redesign's app-wide QSS, assembled from
// palette.h's tokens so the hex values live in exactly one place
// (palette.h) - this file only arranges them into selectors. Applied once
// in main.cpp via qApp->setStyleSheet().
QString themeStylesheet();
