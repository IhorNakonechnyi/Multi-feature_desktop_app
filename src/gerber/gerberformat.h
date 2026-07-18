#pragma once

#include <QString>

// Gerber's coordinate format varies per file - %FS declares how many
// integer/decimal digits each coordinate has and whether omitted zeros
// are the leading or trailing ones; %MO (or the legacy G70/G71 commands)
// declares whether raw numbers are inches or millimeters. GerberFormat
// captures both, so a raw coordinate token from anywhere in the file can
// be turned into real millimeters - the unit GerberDiffRenderer's canvas
// already uses for its grid.
struct GerberFormat {
    int integerDigits = 2;
    int decimalDigits = 4;
    bool leadingZeroOmitted = true; // 'L' after %FS - by far the common case
    bool unitsAreInches = true;     // legacy Gerber's default unit

    // Nanometers per file-native unit - both exact integers, no rounding:
    // 1mm = 1,000,000nm; 1 inch = 25.4mm = 25,400,000nm exactly.
    static constexpr qint64 kNanometersPerMillimeter = 1'000'000;
    static constexpr qint64 kNanometersPerInch = 25'400'000;

    // Turns a raw coordinate token (e.g. "021363", optionally signed) into
    // millimeters, honoring the digit counts/zero-suppression mode and
    // converting from inches if needed.
    double decodeCoordinateMm(const QString &token) const;

    // Same as decodeCoordinateMm, but decodes straight to integer
    // nanometers via integer arithmetic instead of a double division -
    // exact for mm-native files, and for inch-native files rounds to the
    // nearest nanometer (see gerberformat.cpp for why inches can't always
    // divide out evenly). Nothing in the app calls this yet; it's the
    // foundation for the upcoming integer-coordinate object model, which
    // will replace the current float-based QPainterPath pipeline.
    qint64 decodeCoordinateNm(const QString &token) const;

    // Converts a millimeter value (as already produced by
    // decodeCoordinateMm, or any other mm-space value in this app) to
    // integer nanometers, rounding to the nearest nanometer.
    static qint64 mmToNm(double mm);

    // The inverse of mmToNm - integer nanometers back to millimeters.
    static double nmToMm(qint64 nm);
};

namespace GerberFormatParser {

// Updates `format` in place if `line` is a recognized format/unit
// directive (%FS...*%, %MOIN*%/%MOMM*%, or the legacy G70/G71 unit
// commands). Returns true if the line was consumed this way, so the
// caller (GerberParser) knows not to also try parsing it as a draw
// command.
bool tryParseDirective(const QString &line, GerberFormat &format);

} // namespace GerberFormatParser
