#pragma once

#include <optional>
#include <stdexcept>

#include <QString>
#include <QStringList>

#include "gerberobjectmodel.h"
#include "samedatarefusal.h"

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
// lines that are wholly a hole or rout move in a form the parser reads
// (X/Y only, G00-G03 with X/Y, I/J only on G02/G03) and bare tool selects
// ("T03"), whose effect is already in each object's aperture. Kept: only an
// allow-list - the header (M48, '%'/M95, unit lines, FMAT, the tool table,
// comments such as FILE_FORMAT or a plated/non-plated note), M15/M16/G05/G90,
// an M71/M72 that names the unit the parser is already using, G00-G03 lines in a form not left out, and G85 slot lines (the parser reads
// only their start point). Stops after M30/M00, exactly where the parser
// stops.
//
// std::nullopt means "this file contains something the parser cannot vouch
// for": any line not on the allow-list (G91, pattern or mirror
// commands, ...), an M71/M72 that disagrees with the parser's unit, a unit line after the header, an R repeat command, or a
// tool defined twice. Such a file is never the same data.
//
// `refusal` (optional, diagnostics only) is filled with the rule and the
// line that made the result std::nullopt.
//
// `drawLeftOut` (optional) is set to true when a hole or rout line was left
// out of the list. A file with no objects is only "nothing drawn" when this
// stays false.
std::optional<QStringList> nonGeometryLines(const QString &content, SameDataRefusal *refusal = nullptr,
                                            bool *drawLeftOut = nullptr);

} // namespace ExcellonParser
