#pragma once

#include <QByteArray>

#include "gerberobjectmodel.h"

// A Layer written out as plain text, every field exactly, objects in file
// order. Used where "did the parser produce exactly the same thing?" must be
// answered against a result from the past: the golden test
// (tests/unit/test_parsergolden.cpp) and the result fingerprint in the timing
// build. Unlike LayerEquivalence's keys it does NOT ignore order or aperture
// numbers - it records what the parser produced, not what the image looks like.
namespace GerberObjectDump {

// Apertures sorted by code, macros sorted by name, the unit scale, then one
// line per object. Doubles are written with 17 significant digits, so two
// different values never print the same.
QByteArray dump(const Layer &layer);

} // namespace GerberObjectDump
