#pragma once

#include <functional>

#include <QPainterPath>
#include <QString>
#include <QVector>

// One aperture as declared by a %AD command: either a standard RS-274X
// shape (Circle/Rectangle/Obround/Polygon) or a reference to a custom
// aperture macro by name, carrying whatever modifier numbers followed it
// (e.g. a circle's diameter, or the values substituted into a macro's
// $1/$2/... variables).
struct GerberAperture {
    enum class Shape { Circle, Rectangle, Obround, Polygon, Macro };

    Shape shape = Shape::Circle;
    QVector<double> modifiers; // meaning depends on shape - see buildFlashPath
    QString macroName;         // only meaningful when shape == Macro
};

namespace GerberApertureGeometry {

// Path representing this aperture "flashed" (stamped) at the origin -
// callers translate it to the actual flash point. `macroLookup` resolves
// Shape::Macro apertures to their macro's composited geometry (built by
// GerberMacroBuilder::buildPath, wired up by GerberParser); it's a
// callback here so this module doesn't need to depend on the macro one.
QPainterPath buildFlashPath(const GerberAperture &aperture,
                            const std::function<QPainterPath(const QString &, const QVector<double> &)> &macroLookup);

} // namespace GerberApertureGeometry
