#pragma once

#include <QByteArray>
#include <QString>

// Whether a filename looks like a Gerber/Excellon layer file, for gating
// the diff-render feature (FR-006). Scoped to this app's real inputs, not
// the whole Gerber ecosystem (ADR 0002) - file-naming convention itself
// isn't confidential (research.md), just narrow by design. Extension list
// is data-driven: a sane built-in default plus any extensions the user has
// added via addUserExtension, so an unrecognized convention doesn't need a
// rebuild to fix.
namespace GerberFileType {

bool isRecognized(const QString &fileName);

// Whether fileName's extension identifies it as an Excellon drill file
// (drl, xln) rather than a Gerber (RS-274X) layer. Extension-only, so this
// overload is what callers use when no file content is available yet -
// e.g. GerberDiffTrigger's pair classification, which runs on archive
// metadata (filenames only) before any entry bytes are read. Both
// extensions are already in isRecognized()'s default set (real archives
// mix Gerber and drill files under one naming convention), so callers
// that must not treat a drill file as parseable Gerber check this
// alongside isRecognized, not instead of it - this app's Gerber parser
// doesn't speak Excellon yet.
bool isDrillFile(const QString &fileName);

// Same question, but for callers that already have the file's bytes in
// hand (ArchiveComparePage::parseGerberOrDrill, GerberViewerPage::openFile)
// and are about to pick a parser. A real supplier drill file has been seen
// with a .txt extension, which the extension-only overload above can't
// catch - here, any extension other than the unambiguous drl/xln falls
// back to sniffing content for an Excellon M48 header rather than
// silently mis-parsing genuine Excellon syntax as Gerber (which just
// produces an empty, unexplained render - see gerber-render-v2 progress
// notes on the .txt gap).
bool isDrillFile(const QString &fileName, const QByteArray &content);

// Persists via QSettings (LocalSettings' storage) so it survives restarts.
void addUserExtension(const QString &extension);

} // namespace GerberFileType
