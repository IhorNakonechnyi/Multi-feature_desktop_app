# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project status

This is an early-stage project — no application code exists yet (no `CMakeLists.txt`, no `src/`). Development is being driven through GitHub's spec-kit (spec-driven development) workflow: `/speckit.specify` → `/speckit.plan` → `/speckit.tasks` → `/speckit.implement`. Before writing code, check `specs/<feature>/` for the current feature's `spec.md`, `plan.md`, and (once generated) `tasks.md`.

**Confidentiality**: `specs/` is gitignored and exists only locally. It documents the confidential internal PCB spec/supplier-data structure (the structure itself, not just values, is confidential) and must never be committed to this public repo. `.vscode/` is also gitignored (personal editor settings). See `.gitignore`.

## Commands

No build system exists yet. Once the Qt/CMake project is scaffolded, the intended commands (per the feature's `quickstart.md`) are:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Spec-kit workflow commands (PowerShell, run from repo root):

- `.specify/scripts/powershell/create-new-feature.ps1 -ShortName "<name>" "<description>"` — start a new feature spec
- `.specify/scripts/powershell/setup-plan.ps1 -Json` — scaffold `plan.md` for the active feature
- `.specify/scripts/powershell/setup-tasks.ps1 -Json` — scaffold `tasks.md` for the active feature
- `.specify/scripts/powershell/check-prerequisites.ps1` — verify a feature has the docs required for the next spec-kit step

## Spec-kit layout

- `.specify/memory/constitution.md` — project governance/coding principles (see below; read the full file, it's short).
- `.specify/templates/` — spec-kit's document templates (spec, plan, tasks, checklist, constitution).
- `.specify/scripts/powershell/` — the setup scripts above.
- `.github/agents/speckit.*.agent.md` and `.github/prompts/speckit.*.prompt.md` — definitions for the `/speckit.*` slash commands.
- `.specify/feature.json` — points at the currently active feature directory under `specs/`.

## Project constitution

Six governing principles from `.specify/memory/constitution.md`:

1. **Clarity over cleverness** — simple, explicit code over advanced patterns; explain the "why" when something non-obvious is used.
2. **Small, working steps** — incremental, independently testable changes.
3. **Minimal, well-known dependencies** — prefer the standard library; justify any new third-party dependency.
4. **Practical testing, not perfect testing** — unit-test core/calculation logic where a silent bug would produce wrong results; UI/glue code is manually tested.
5. **Local-first, known small future scale** — designed to run locally for a realistic ceiling of ~50 users total, not internet scale; no premature server/infra, but local logic stays decoupled enough that a future update/feedback module could be added later.
6. **Explain as you go** — non-trivial decisions (library choice, pattern, structure) get a short rationale, not a lecture.

## Intended architecture (feature `specs/001-pcb-supplier-workflow/`)

Not yet implemented, but the direction is fixed by that feature's `plan.md`/`research.md` (gitignored, local-only — summarized here since the tech stack itself isn't confidential, only the business data structure is):

- Qt 6.8.2 (MSVC 2022 x64, Qt Widgets, not QML), CMake, C++20. Windows 10/11 only.
- Dependencies via CMake `FetchContent`:
  - **QuaZip — LGPL license.** Build as its own shared DLL and dynamically link; do not statically link (avoids LGPL relink obligations). Used for zip reading/CRC comparison.
  - **QXlsx — MIT license.** Used for reading supplier-capability Excel files.
- **No third-party Gerber-parsing library** — hand-rolled minimal RS-274X (Gerber) / Excellon (drill) parser. Existing options such as gerbv are GPL-licensed, incompatible with this public repo.
- **Qt's own PDF module (`QPdfDocument`)** for extracting text from the Salesforce-exported spec PDF. This module isn't always enabled by default in every Qt installation — verify "Qt PDF" is checked in the Qt Maintenance Tool for this kit before first build.
- **QtTest** (ships with Qt, `ctest`-integrated) for unit tests — core logic only, per constitution principle 4.
- **Distribution**: `windeployqt`-produced folder (exe + Qt/QuaZip DLLs), zipped and shared — not an installer, not a statically-linked single file.
- The app makes **zero outbound network calls**; all input is local files (archives, spec PDF, supplier Excel files) selected via a file picker defaulting to the user's Downloads folder.

## Confidentiality constraint (applies to all code, not just Phase 1)

Real parameter/field names from the internal spec file and from supplier capability files must **never** be hardcoded in source — not even in comments or examples. They are read at runtime from local, gitignored files only. Any parsing, matching, or comparison logic must be schema-driven: it operates on whatever parameter names/structure the local input files contain, not on names known in advance.
