#pragma once

#include <QVector>

#include "archivecomparisonresult.h"

// Filters a comparison's DifferingPairs down to the ones eligible for the
// Gerber diff-render UI (FR-006): both sides' filenames must be a
// recognized Gerber type. An empty result means the diff UI stays hidden.
namespace GerberDiffTrigger {

QVector<DifferingPair> eligiblePairs(const QVector<DifferingPair> &differingPairs);

} // namespace GerberDiffTrigger
