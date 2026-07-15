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

// Persists via QSettings (LocalSettings' storage) so it survives restarts.
void addUserExtension(const QString &extension);

} // namespace GerberFileType
