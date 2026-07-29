# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project status

The app is built and running: Qt 6.8.2 Widgets, CMake, C++20, MSVC 2022 x64, with 17 `ctest`
targets. `src/` is organised by feature area:

- `src/app/` — shell: `MainWindow`, sidebar nav, status legend, `theme.cpp` (the single
  stylesheet; the app is dark-mode-only and forces it in `main.cpp`).
- `src/archivecompare/` — **Archive Compare, the one complete feature.** Compares two zip
  archives of PCB data and previews the differing Gerber/drill pairs as an overlay.
- `src/gerber/` — hand-rolled RS-274X + Excellon parsers, the integer-nm object model, and
  the mask renderer/compositor that draw the overlay.
- `src/common/` — file picker, local settings.
- `src/supplieradvisor/`, `src/quotecompare/` — **not built into the app yet**; their sidebar
  entries are deliberately disabled. Supplier Advisor's real implementation lives unmerged in
  the archived `.claude/worktrees/old/supplier-advisor-first-slice` worktree.

Design records live in `docs/superpowers/specs/` (how something works and why) and
`docs/superpowers/plans/` (the task breakdown that built it). Read the relevant design doc
before changing a subsystem — several encode decisions that are expensive to rediscover, and
each carries a status line saying whether it is implemented.

Newer feature work is driven through these design/plan docs rather than spec-kit. The
spec-kit scaffolding below is still present and still describes feature `001`, but has not
been the working process for recent features.

**Confidentiality**: `specs/` is gitignored and exists only locally. It documents the confidential internal PCB spec/supplier-data structure (the structure itself, not just values, is confidential) and must never be committed to this public repo. `Test_data/` (real supplier archives) and `.vscode/` are gitignored too. See `.gitignore`.

**Leak tripwire.** Two git hooks in `.githooks/` refuse commits containing real identifiers —
`pre-commit` scans staged additions, `commit-msg` scans the message. Both paths have leaked
before: a customer part number reached a design doc *and* two commit messages, and was pushed.

Enable it once per clone (hooks path is local config, not committed):

```powershell
git config core.hooksPath .githooks
```

The patterns are themselves confidential, so they are not in the repo. They live in
`.git/confidential-patterns` — one per line, matched case-insensitively as fixed strings —
which is inside the git directory and therefore impossible to commit, and shared by every
worktree. **A fresh clone has no such file, and the hooks then stay silent** (they print a
one-line notice so an inactive tripwire is not mistaken for a clean scan). Recreate it before
relying on them. Add real spec parameter names and supplier names to it as they come up.

Note the tripwire only protects *future* commits. Anything already pushed is public
permanently; correcting history needs a rewrite and a force-push.

## Commands

```powershell
cmake -S . -B build "-DCMAKE_PREFIX_PATH=C:/Qt/6.8.2/msvc2022_64"
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Five environment facts, each of which has cost real debugging time here:

- **`CMAKE_PREFIX_PATH` is required** — without it configure cannot find Qt.
- **`ctest` needs `-C Debug`.** This is a multi-config MSVC generator; omit it and every test
  reports "Not Run" rather than failing loudly.
- **`C:\Qt\6.8.2\msvc2022_64\bin` must be on `PATH` when running tests**, or every test exits
  `0xc0000135`. That is a DLL-not-found error, *not* a broken test.
- **Never pipe the build command.** `cmake --build build | tail` reports the *pipe's* exit
  code, so a failed build reads as success. Redirect to a file and check `$LASTEXITCODE`.
- **Close the running app before rebuilding.** It holds a lock on
  `build/Debug/PcbSupplierWorkflow.exe` and the link step fails with `LNK1168`.

In PowerShell, quote `cmake` `-D` arguments (`"-DSRC=$x"`). Unquoted ones get mangled and
surface as a misleading "Cannot find source file".

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

## Architecture

Set by feature `001`'s `plan.md`/`research.md` (gitignored, local-only — summarised here
since the tech stack itself isn't confidential, only the business data structure is). Every
point below is **implemented**, except QXlsx and `QPdfDocument`, which belong to the unbuilt
Supplier Advisor:

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
