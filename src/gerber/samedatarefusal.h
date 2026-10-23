#pragma once

#include <QString>

// Why GerberParser::nonGeometryStatements or ExcellonParser::nonGeometryLines
// gave no answer (std::nullopt). Filled only when the caller passes one in -
// the same-data check itself never does; only the diagnostic line under the
// canvas does (SameDataDiagnosis, 1.8.7 test round).
//
// `statement` is the RAW text that refused, so it can hold real data.
// SameDataDiagnosis turns it into a value-free shape before showing it -
// never display it as it is.
struct SameDataRefusal {
    QString rule;      // which rule refused, in words
    QString statement; // the statement or line that refused; empty for a whole-file rule
    int position = 0;  // 1-based statement (Gerber) or line (drill) number; 0 = the whole file
};
