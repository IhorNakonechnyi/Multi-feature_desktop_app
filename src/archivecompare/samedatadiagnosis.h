#pragma once

#include <QByteArray>
#include <QString>

// Diagnostics for test rounds on real files: WHY a pair stayed yellow, shown
// under the overlay canvas. Walks the same steps as SameDataCheck::isSameData and
// reports where the pair stopped - the rule that refused a file, the first
// text line that differs, or how the objects differ.
//
// Meant to be pasted into a conversation about real customer files, so it
// must never show a value: every statement is reduced to its SHAPE (shapeOf)
// - numbers become '#', names and free text are dropped. Only Gerber /
// Excellon syntax and counts remain.
//
// Built into the app only in a diagnostic build (CMake option PCB_DIAGNOSTICS,
// see docs/releasing.md). The unit tests always build it, so it keeps working
// between test rounds.
namespace SameDataDiagnosis {

// A few lines of text explaining the verdict for this pair, or a single line
// when every step passes.
QString explain(const QByteArray &bytesA, const QByteArray &bytesB);

// The value-free shape of one statement as the non-geometry lists hold it:
// a Gerber statement ("%..." for a parameter statement) or, with
// isGerber == false, one Excellon line. "G03X1200Y-50I3J0D01" becomes
// "G03X#Y#I#J#D01"; "%TF.FileFunction,Copper,L1,Top" becomes
// "%TF.FileFunction,...".
QString shapeOf(const QString &statement, bool isGerber);

} // namespace SameDataDiagnosis
