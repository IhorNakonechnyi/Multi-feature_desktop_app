# Scope Gerber-layer-type recognition to this app's real inputs, not the whole ecosystem

> **Superseded (2026-07-29, Q2/Q12).** This ADR's premise — that recognition is a
> *naming/extension* question at all — no longer holds. Detection is now purely content-based:
> `GerberFileType::classify()` reads the first 8 KB and looks for `M48` (drill) or `%FS`/`%MO`
> (Gerber), and the filename plays no part. The extension list, the user-added-extension
> mechanism and its QSettings key were deleted outright, not demoted to a fallback. The
> deciding argument was stronger than accuracy: a file with no `%FS` has no coordinate format,
> so the parser cannot render it whatever its extension claims — which makes an extension list
> unable to help even in principle. The conclusion below (don't build broad multi-convention
> detection) still stands; the reasoning it rests on does not.
> See `docs/superpowers/specs/2026-07-27-content-based-drawability-design.md`.

Detecting whether a file is a "recognized Gerber layer type" gates both visual-diff rendering (Feature 1) and layer-role extraction (Feature 3). We considered supporting multiple common industry naming conventions (Gerber X2 embedded attributes, Altium/Protel-style extensions, KiCad-style, generic `.gbr`/`.drl`) so the tool would work against archives from any source. We rejected this: the project constitution scopes this app to a known, small, local use case (Principle 5) with a specific real input source, not a general-purpose Gerber toolchain — building broad multi-convention detection now is speculative generality the actual use case doesn't need (Principle 1). Detection is scoped to the naming/extension convention this app's real archive inputs actually use. This can be revisited if incoming archives from other sources turn out to use different conventions in practice.
