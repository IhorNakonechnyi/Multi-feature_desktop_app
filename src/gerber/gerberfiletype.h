#pragma once

#include <QByteArray>

// Whether a file's bytes are drawable, and by which parser - content-only,
// no filename/extension involved (Q2/Q12 of the 2026-07-27 archive-compare
// open-questions review; design rationale in
// docs/superpowers/specs/2026-07-27-content-based-drawability-design.md).
//
// The extension-based mechanism this replaced (isRecognized(), the two
// isDrillFile() overloads, addUserExtension(), the built-in extension list,
// and the QSettings-persisted user extension list) is gone outright, not
// just demoted to a fallback. The reason is stronger than "content is more
// accurate": every Gerber file must declare its coordinate format (%FS) near
// the top, or the parser has no scale to apply and cannot render the file
// however its extension reads. An "Add .<ext>" button therefore can no
// longer do anything useful - it would only force an unparseable file
// through to a blank canvas - so keeping it around would mislead rather
// than help. Previously-saved user extensions become orphaned QSettings
// entries under the old "gerber/userRecognizedExtensions" key; harmless,
// and not worth a migration for a ~50-user internal tool.
namespace GerberFileType {

// How many bytes of a file's head classify() needs. 8 KB: with no extension
// fallback behind it, the sniff window is the only thing standing between a
// drawable file and rejection, so it must survive a long G04 comment banner
// pushing %FS down the file. Real sample files run 19-92 KB, so 8 KB covers
// any plausible header at a fraction of a full decompression.
constexpr qint64 kClassifySniffBytes = 8192;

enum class Drawability {
    ExcellonDrill, // parse with ExcellonParser
    Gerber,        // parse with GerberParser
    NotDrawable,   // no %FS/%MO and no M48 - the parser would have no
                   // coordinate format to apply; extension is irrelevant
};

// Content-only. Pass the first kClassifySniffBytes of the file (passing more
// is harmless - only the head is scanned). The filename plays no part.
Drawability classify(const QByteArray &head);

} // namespace GerberFileType
