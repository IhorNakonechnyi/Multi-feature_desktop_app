#pragma once

#include <stdexcept>

#include <QString>
#include <QStringList>

#include "gerberobjectmodel.h"

// Parses an Excellon (.drl/.xln) drill file into the same integer-nm object
// model GerberParser produces (see gerberobjectmodel.h), so drill layers
// flow through the identical LayerMaskRenderer/MaskCompositor pipeline used
// for copper Gerber layers. See
// docs/superpowers/specs/2026-07-19-gerber-render-v2-m5-drill-design.md.
// A drilled hole becomes a Flash using a Circle aperture at the tool's
// diameter (aperture code = tool number); a routed slot (an M15..M16 span)
// becomes a Stroke, linear or arc, at the same diameter.
namespace ExcellonParser {

// Thrown when a coordinate has no decimal point and no resolvable
// zero-suppression format (no ";FILE_FORMAT=" comment, no usable
// INCH/METRIC header line) - see the design doc's "Coordinate format
// resolution". Distinct from returning an empty Layer so the caller can
// show a specific reason, mirroring ArchiveOpenError's role for unreadable
// archives.
class AmbiguousFormatError : public std::runtime_error {
public:
    explicit AmbiguousFormatError(const QString &message)
        : std::runtime_error(message.toStdString()), message_(message)
    {
    }

    QString message() const { return message_; }

private:
    QString message_;
};

Layer parseContentToObjects(const QString &content);

// Every line NOT captured by the object model, in file order, minus export
// noise (see exportnoise.h) - the drill counterpart of
// GerberParser::nonGeometryStatements, with the same purpose. Left out:
// lines with X/Y coordinates (holes and rout moves) and bare tool selects
// ("T03"), whose effect is already in each object's aperture. Kept: the
// header - unit/format lines, the tool table, comments such as FILE_FORMAT
// or a plated/non-plated note - and any command the parser ignores. Stops
// after M30/M00, exactly where the parser stops.
QStringList nonGeometryLines(const QString &content);

} // namespace ExcellonParser
