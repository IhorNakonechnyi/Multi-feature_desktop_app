# Open Questions

Working file for questions that come up while generating `tasks.md` / during implementation. Answer inline under each question (e.g. replace `_(unanswered)_` with your decision), and flag me to make the corresponding change.

## Q1 — docs/designUI conflicts with already-finalized spec.md/plan.md

`docs/designUI/pcb-advisor-design.md` and `pcb-advisor-qt-ui-design.md` appear to have been written without this project's finalized decisions and conflict on a few points. `tasks.md` will be generated following the **already-finalized `plan.md`/`spec.md`** (i.e. resolving these conflicts in favor of what we already locked in), unless you say otherwise here:

- **RAR support**: `spec.md` says drop `.rar` for Phase 1 (`.zip` only). The design doc reintroduces it (shell out to 7z/unrar, or libarchive). → Defaulting to: **no RAR**, per `spec.md`.
- **Static vs dynamic Qt linking**: `plan.md` chose dynamic linking + `windeployqt` (avoids LGPL relink obligations for Qt and QuaZip). The design doc suggests static Qt "to avoid DLL-hell." → Defaulting to: **dynamic linking**, per `plan.md`.
- **Repo structure**: design doc proposes two separate repos (public app + private config repo). We already implemented single-repo with `specs/` gitignored locally. → Defaulting to: **single repo** (already done).
- **Module naming**: design doc uses `core/archive/`, `core/rules/`, `core/gerber/`, `core/quotes/`, `ui/`; `plan.md` uses `archivecompare/`, `gerber/`, `supplieradvisor/`, `quotecompare/`, `common/`, `app/`. → Defaulting to: **`plan.md`'s naming**, but the design doc's "core has zero Qt/domain knowledge, UI layer converts" separation principle is worth adopting regardless of naming.
- **Zip library**: design doc suggests libzip/KArchive/miniz; `plan.md`/`research.md` chose **QuaZip** (LGPL, dynamically linked) with reasoning already recorded. → Defaulting to: **QuaZip**.
- **Quote PDF library**: design doc mentions Poppler, which is **GPL** — already flagged elsewhere as something to avoid. → Defaulting to: whatever Feature 2 settles on (`QPdfDocument`/PDFium), not Poppler.

**Answered (2026-07-12): no changes — all defaults above confirmed as final.**

## Q2 — Excellon hole-count bucket boundary

Confirmed: single number, holes with diameter in (0mm, 3mm]. Is the upper bound **inclusive** (a hole at exactly 3.00mm counts) or **exclusive** (must be <3mm)?

**Answered (2026-07-12): inclusive — a hole at exactly 3.00mm counts.**

## Q3 — Unmapped-parameter behavior scope

Confirmed: unmapped supplier fields are silently ignored. Does the same "silently ignore" rule apply to an unmapped **spec** parameter (one in the spec PDF with no entry in the alias-mapping file), or should that case behave differently (e.g. block the recommendation, since a required parameter can't be matched against any supplier)?

**Answered (2026-07-12): same rule — silently ignore.** An unmapped spec parameter is excluded from matching entirely (not checked against any supplier), same as an unmapped supplier field. It does not block the recommendation.
