#include "gerberformat.h"

#include <cmath>

namespace {

// Shared by decodeCoordinateMm and decodeCoordinateNm - both need the same
// sign-strip + zero-pad-per-format-mode digit extraction, just interpret
// the resulting scaled integer differently afterward (double division vs.
// integer nanometer scaling). Keeping this in one place means the two
// decode paths can never silently drift apart on the parsing half.
struct DecodedDigits {
    qint64 rawValue; // digit string as an integer - i.e. the coordinate
                      // scaled by 10^decimalDigits, in the file's native unit
    bool negative;
};

DecodedDigits extractDigits(const QString &token, const GerberFormat &format)
{
    QString digits = token;
    bool negative = false;
    if (digits.startsWith(QLatin1Char('+'))) {
        digits.remove(0, 1);
    } else if (digits.startsWith(QLatin1Char('-'))) {
        negative = true;
        digits.remove(0, 1);
    }

    const int totalDigits = format.integerDigits + format.decimalDigits;
    // Leading-zero omission ('L', the common case): the digits given are
    // the right-justified tail of the full-width number, so the decimal
    // point always sits decimalDigits from the right, however many
    // leading zeros were dropped - nothing to pad.
    // Trailing-zero omission ('T', rare/legacy): the digits given are the
    // left-justified head, so pad with trailing zeros up to the full
    // width before placing the decimal point.
    if (!format.leadingZeroOmitted && digits.length() < totalDigits) {
        digits += QString(totalDigits - digits.length(), QLatin1Char('0'));
    }

    bool ok = false;
    const qint64 rawValue = digits.toLongLong(&ok);
    return DecodedDigits{ok ? rawValue : 0, negative};
}

// Integer power of 10. decimalDigits is realistically 2-6 per real %FS
// formats seen in this codebase's test fixtures and the real archives
// validated this session, so a simple loop (no overflow guard) is enough -
// no need to defensively handle arbitrary digit counts.
qint64 pow10(int exponent)
{
    qint64 result = 1;
    for (int i = 0; i < exponent; ++i) {
        result *= 10;
    }
    return result;
}

// Round-half-away-from-zero integer division. numerator/denominator are
// always non-negative here (extractDigits strips the sign separately, and
// decodeCoordinateNm re-applies it after rounding), so "away from zero"
// just means "round .5-and-above up".
//
// This matters for inch-native files: kNanometersPerInch (25,400,000)
// doesn't divide evenly by every power of 10 (e.g. 10^6, since 1
// millionth of an inch is 25.4nm, not a whole number of nanometers).
// mm-native files always divide evenly at realistic decimalDigits counts
// (kNanometersPerMillimeter is exactly 10^6), so this is a no-op there.
qint64 roundedDivide(qint64 numerator, qint64 denominator)
{
    return (numerator + denominator / 2) / denominator;
}

} // namespace

double GerberFormat::decodeCoordinateMm(const QString &token) const
{
    const DecodedDigits decoded = extractDigits(token, *this);

    double value = static_cast<double>(decoded.rawValue) / std::pow(10.0, decimalDigits);
    if (decoded.negative) {
        value = -value;
    }

    return unitsAreInches ? value * 25.4 : value;
}

qint64 GerberFormat::decodeCoordinateNm(const QString &token) const
{
    const DecodedDigits decoded = extractDigits(token, *this);

    // How many exact nanometers correspond to one whole unit in the
    // file's native coordinate system (mm or inch).
    const qint64 nmPerFileUnit = unitsAreInches ? kNanometersPerInch : kNanometersPerMillimeter;

    // decoded.rawValue is the coordinate scaled by 10^decimalDigits in the
    // file's native unit (see extractDigits above). Multiplying by
    // nmPerFileUnit and dividing back out by 10^decimalDigits converts to
    // nanometers using only integer arithmetic - no float rounding noise.
    //
    // Overflow: at realistic %FS precision (2-4 integer digits, 4-6
    // decimal digits, per this codebase's real-data validation),
    // rawValue * nmPerFileUnit stays comfortably inside qint64 - worst
    // realistic case is roughly 10^10 * 2.54*10^7 ~= 2.5*10^17, versus
    // qint64's max of ~9.2*10^18. Not defending against unrealistic digit
    // counts (e.g. __int128) per the constitution's no-speculative-
    // generality stance - MSVC (this project's toolchain) doesn't have
    // __int128 anyway.
    const qint64 scaled = roundedDivide(decoded.rawValue * nmPerFileUnit, pow10(decimalDigits));

    return decoded.negative ? -scaled : scaled;
}

qint64 GerberFormat::mmToNm(double mm)
{
    // qRound64 rounds to the nearest integer, ties away from zero - the
    // same tie-breaking rule decodeCoordinateNm uses via roundedDivide.
    return qRound64(mm * static_cast<double>(kNanometersPerMillimeter));
}

double GerberFormat::nmToMm(qint64 nm)
{
    return static_cast<double>(nm) / static_cast<double>(kNanometersPerMillimeter);
}

namespace GerberFormatParser {

bool tryParseDirective(const QString &line, GerberFormat &format)
{
    // %FSLAX24Y24*% - zero-suppression mode (L/T) right after "%FS", then
    // "X" followed by two digits (integer, decimal), then the same for Y
    // (X and Y always match in every real file seen, so only X is read).
    if (line.startsWith(QLatin1String("%FS")) && line.length() > 4) {
        format.leadingZeroOmitted = (line.at(3) == QLatin1Char('L'));
        const int xIndex = line.indexOf(QLatin1Char('X'));
        if (xIndex >= 0 && line.length() > xIndex + 2) {
            format.integerDigits = QStringView(line).mid(xIndex + 1, 1).toInt();
            format.decimalDigits = QStringView(line).mid(xIndex + 2, 1).toInt();
        }
        return true;
    }
    if (line.startsWith(QLatin1String("%MOIN"))) {
        format.unitsAreInches = true;
        return true;
    }
    if (line.startsWith(QLatin1String("%MOMM"))) {
        format.unitsAreInches = false;
        return true;
    }
    if (line.startsWith(QLatin1String("G70"))) {
        format.unitsAreInches = true;
        return true;
    }
    if (line.startsWith(QLatin1String("G71"))) {
        format.unitsAreInches = false;
        return true;
    }
    return false;
}

} // namespace GerberFormatParser
