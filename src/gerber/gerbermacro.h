#pragma once

#include <QPainterPath>
#include <QString>
#include <QVector>

// One aperture macro (%AM name * statement * statement ... %). Statements
// are kept as raw text and only evaluated when an aperture actually
// instantiates the macro with real parameter values ($1, $2, ...) - the
// same macro can be referenced by many apertures with different
// parameters (that's the whole point of $-substitution), so there's
// nothing useful to precompute at definition time.
struct GerberMacro {
    QString name;
    QVector<QString> statements; // one raw primitive/assignment statement each, no leading/trailing '*'
};

namespace GerberMacroBuilder {

// Builds the composited path for one macro instantiation. `parameters`
// are the aperture's own modifier values, available inside the macro as
// $1, $2, .... Each statement is either a variable assignment ("$3=...")
// or a primitive draw statement (starts with a primitive code number);
// primitive shapes are combined in order using their exposure flag
// (1 = add material, 0 = subtract) via QPainterPath's boolean operations.
//
// `unitScale` converts a primitive field's raw evaluated number into
// millimeters (25.4 if the file is in inches, 1.0 for millimeters) - but
// only for fields that are actually lengths (a diameter, a width, a
// center-offset). Fields that aren't lengths (exposure flags, vertex
// counts, rotation angles) are never scaled, so this is applied inside
// each buildXxx function at the specific fields it knows are lengths,
// not blanket-applied to every field. Defaults to 1.0 (no conversion)
// so existing millimeter-only callers/tests are unaffected.
//
// If a statement uses a primitive code this parser doesn't implement
// (the rare/legacy ones - see the design doc), the WHOLE macro falls
// back to a small fixed-size marker circle rather than compositing a
// partially-wrong shape or guessing at unfamiliar parameter meanings.
QPainterPath buildPath(const GerberMacro &macro, const QVector<double> &parameters, double unitScale = 1.0);

} // namespace GerberMacroBuilder
