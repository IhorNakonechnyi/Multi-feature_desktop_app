#pragma once

#include <QHash>
#include <QString>

// Aperture macros can compute a primitive's parameters from a small
// arithmetic expression over the macro's own variables ($1, $2, ...),
// e.g. a scaling expression "2.0X$1" (Gerber uses 'x'/'X' for
// multiply, since '*' already terminates statements). This evaluates one
// such expression given whatever variable values are currently in scope.
namespace GerberExpression {

double evaluate(const QString &expression, const QHash<int, double> &variables);

} // namespace GerberExpression
