#pragma once

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
// (drl, xln) rather than a Gerber (RS-274X) layer. Both extensions are
// already in isRecognized()'s default set (real archives mix Gerber and
// drill files under one naming convention), so callers that must not treat
// a drill file as parseable Gerber (GerberDiffTrigger, M4's overlay
// compare) check this alongside isRecognized, not instead of it - this
// app's Gerber parser doesn't speak Excellon yet.
bool isDrillFile(const QString &fileName);

// Persists via QSettings (LocalSettings' storage) so it survives restarts.
void addUserExtension(const QString &extension);

} // namespace GerberFileType
