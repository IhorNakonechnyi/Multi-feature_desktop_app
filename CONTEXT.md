# PCB Supplier Workflow Desktop App

A single-context repo. This glossary defines terms specific to the supplier-matching domain — generic programming concepts are excluded. Real parameter/field names and file structures are confidential and never appear here (see `CLAUDE.md`); only generic, structural vocabulary is defined.

## Language

**Fit level**:
The verdict of matching one supplier against one spec item: `Good fit`, `Fit with changes`, or `No fit`.

**Severity**:
A per-parameter property (`Recoverable` or `Disqualifying`) deciding what a failed check on that parameter contributes to a supplier's fit level — `Recoverable` pushes toward `Fit with changes`, `Disqualifying` forces `No fit`. Deliberately a config value per parameter, not something derived from the parameter's comparison shape (see ADR 0001).
_Avoid_: Priority, weight

**Unmapped parameter**:
A spec or supplier parameter with no corresponding entry in the alias mapping. Excluded from matching (not an error), but its count is surfaced to the user so a stale mapping is noticeable.
_Avoid_: Missing field, unknown column

**Parameter conflict**:
A board parameter available from two sources (Gerber extraction and the parsed spec) whose values disagree. Neither source silently wins — both values are shown side-by-side.
_Avoid_: Mismatch (too generic — this specifically means two-source disagreement on one parameter's value)

**Recognized Gerber layer type**:
A file whose name matches the archive-naming convention the app knows how to classify into a layer role (copper, solder mask, silkscreen, drill, outline). Gates whether a differing file pair is offered for visual diff rendering. Scoped to the conventions actually seen in this app's real inputs, not a general-purpose Gerber-ecosystem detector (see ADR 0002).
