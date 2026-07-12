# Project Constitution: Multi-feature_desktop_app

**Version**: 1.0.1
**Ratified**: 2026-07-12
**Last Amended**: 2026-07-12

## Purpose

This is a personal learning project as well as a real, useful tool for me and a
colleague at work. The constitution below balances two goals: helping me learn
solid programming practices, and keeping the app simple enough that I can
actually finish it and maintain it.

## Core Principles

### Principle 1: Clarity Over Cleverness
**Rule**: Code MUST be written to be understood, first by me, second by anyone
else who reads it.
**Rationale**: I'm learning. Code I can't explain back is code I haven't
actually learned from.
**Application**:
- Prefer simple, explicit code over "smart" one-liners or advanced patterns I
  don't yet understand.
- Add short comments explaining *why*, not *what*, especially for anything
  non-obvious.
- If Claude proposes an advanced technique, it should briefly explain what it
  does and why it's the right choice here.
- **Named design patterns** (GoF or otherwise) MUST only be used where they
  genuinely fit the situation, and MUST carry a one-line comment at their
  point of use stating *why that pattern fits here* — not what the pattern
  is. Don't force a pattern's name onto something that's fundamentally
  simple (e.g. a plain lookup table doesn't need to be dressed up as an
  "Adapter").

### Principle 2: Small, Working Steps
**Rule**: Features MUST be built incrementally, with something runnable after
each step.
**Rationale**: Small steps are easier to learn from, easier to debug, and
avoid the trap of a big pile of untested code.
**Application**:
- Break each feature into small tasks (roughly: one task = one small,
  testable change).
- Prefer getting a basic version working before adding polish.

### Principle 3: Minimal, Well-Known Dependencies
**Rule**: Third-party libraries MUST be kept to a minimum, and only
well-established, widely-documented ones should be used.
**Rationale**: Fewer dependencies means less to learn at once, fewer things
that can silently break, and easier long-term maintenance for a small
internal tool.
**Application**:
- Prefer the language's standard library when reasonably possible.
- Before adding a new dependency, briefly note why it's needed and what
  simpler alternative was considered.

### Principle 4: Practical Testing, Not Perfect Testing
**Rule**: Core logic (the parts that could quietly break and cause wrong
results) SHOULD have basic tests. UI/glue code does not need full coverage.
**Rationale**: Testing is a valuable skill to learn, but 100% coverage on a
personal tool is wasted effort. Focus testing where mistakes actually matter.
**Application**:
- Write tests for calculations, data processing, or anything where a silent
  bug would produce wrong results.
- Manual testing is acceptable for simple UI interactions.

### Principle 5: Local-First, with a Known (Small) Future Scale
**Rule**: The app MUST run locally on each user's PC as the primary mode. It
MUST be designed for a realistic maximum of ~50 users total — not unlimited
scale — with a lightweight server added later only for specific, known
purposes: pushing updates to users, and collecting feedback/bug reports (and
possibly simple chat between users).
**Rationale**: The app's real future is known and bounded, so it doesn't need
generic "scale to anything" architecture — but it does need enough separation
between local logic and future server-facing features that adding the server
later isn't a full rewrite.
**Application**:
- No premature optimization, load balancing, or infrastructure meant for
  large-scale or high-traffic systems.
- Keep local app logic (the actual features) decoupled from anything that
  will eventually talk to a server, so a future update/feedback/chat module
  can be added without reworking the core app.
- Don't build the server, update mechanism, or chat now — just avoid tightly
  coupling local logic in a way that would block adding them later.
- Hardcoding is fine for anything that's genuinely single-user/local
  configuration.
- Revisit this principle only if actual plans change (e.g. user count
  expectations, or new server-side features beyond updates/feedback/chat).

### Principle 6: Explain As You Go
**Rule**: When Claude makes a non-trivial decision (choice of library,
pattern, structure), it MUST briefly explain the reasoning in plain language.
**Rationale**: The point of this project is learning, not just having working
software.
**Application**:
- Short rationale after non-obvious code, not a lecture — a sentence or two
  is enough.

## Governance

### Amendment Procedure
1. Since this is a personal project, amendments just require me updating this
   file directly (via `/speckit.constitution` or by hand) with a short note
   on what changed and why.
2. Bump the version number when principles are added, changed, or removed.

### Versioning Policy
- **MAJOR**: A principle is removed or fundamentally changed.
- **MINOR**: A new principle is added.
- **PATCH**: Wording clarified, no meaning changed.

### Compliance
- Before starting a new feature (`/speckit.plan`), quickly check it doesn't
  conflict with these principles.
- If a principle starts getting in the way in practice, that's a signal to
  amend it — not to silently ignore it.

### Amendment History
- **1.0.1** (2026-07-12): Added the "patterns need a one-line rationale
  comment" rule to Principle 1's Application list, agreed on while designing
  `specs/001-pcb-supplier-workflow` (the confidential-parameter-matching
  design specifically, to avoid mislabeling a plain lookup table as an
  "Adapter pattern").
