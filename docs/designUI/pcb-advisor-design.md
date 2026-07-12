# PCB Supplier Decision & Analysis App — Design Doc

## 1. Guiding principle

Everything that is *company-specific* (parameter names, spec structure, supplier
capability layout, business rules) lives in **data**, not **code**. The public
repo ships an engine that can evaluate *any* well-formed spec/capability file
against *any* schema — it just happens that your schema is confidential.

This isn't a bolt-on constraint, it's the actual architecture: think of the
app as a small rules engine + file-diffing toolkit with a Qt front end, and
your company's spec format as just one possible plugin/config for it.

---

## 2. Repository layout

Two repos, separated from day one, never sharing git history:

```
pcb-advisor/                    (PUBLIC — GitHub)
├── src/
│   ├── core/                   generic engines (no domain knowledge)
│   ├── ui/                     Qt widgets/QML
│   └── plugins/                archive backends, parser backends
├── schemas/
│   └── sample/                 fake schema + dummy spec/supplier files
├── tests/                      unit tests against dummy data only
├── CMakeLists.txt
└── .gitignore                  excludes *.local.json, /config/, /data/

pcb-advisor-config/             (PRIVATE — company file server or private git)
├── spec_schema.json            real parameter list, types, rules
├── suppliers/*.json            real capability tables
└── company.settings.json       paths, feature flags, etc.
```

- The public app **never reads its schema from source code** — only from a
  config file path the user selects or that's set in a local settings file
  (which itself is gitignored and never leaves your machine / the work PCs).
- On day one, add `/config/`, `/data/`, `*.local.*` to `.gitignore` and set up
  a **pre-commit hook** (or just `git status` discipline) that greps staged
  files for your real parameter names as a tripwire. Cheap insurance against
  a future "oops, committed the real supplier file" moment.
- When you leave: hand IT the public repo URL + the private config folder
  (or its repo) as two separate handoffs. No history to scrub, because
  confidential data was never in history.

---

## 3. High-level architecture

```
┌─────────────────────────────────────────────────────────┐
│                         Qt UI Layer                       │
│   (MainWindow, per-feature tabs/pages, QGraphicsView)      │
└───────────────▲───────────────────────────────▲───────────┘
                 │                               │
        ┌────────┴────────┐             ┌────────┴────────┐
        │  Core Engines    │             │  Data/Parsing    │
        │  (pure logic,    │◄────────────┤  Layer           │
        │  no Qt widgets)  │             │  (readers)       │
        └────────┬────────┘             └────────┬────────┘
                 │                                │
        ┌────────┴──────────────────────────────┴────────┐
        │        Local filesystem only (Phase 1)           │
        │   archives, spec files, supplier files, quotes    │
        └───────────────────────────────────────────────────┘
```

Key rule: **Core engines never import Qt widget classes** and never know
what a "parameter" means. They operate on generic types (`ParameterSet`,
`ComparisonResult`, `FileTree`). This is what keeps the public repo honest —
if an engine class needs to know your spec structure to compile, you've
leaked the schema into code.

Suggested tech: CMake, Qt Widgets (simpler than QML for this kind of
data-heavy, form-heavy tool), C++20.

---

## 4. Feature 1 — Archive Comparison

**Module:** `core/archive/`

- `IArchiveReader` interface, implementations:
  - `ZipReader` (libzip or `KArchive`/`miniz` — permissive licenses, fine for
    public repo)
  - `RarReader` — **do not bundle unrar/UnRAR source** (non-free-for-forks
    license). Two clean options:
    - Shell out to a user-installed `unrar` / `7z` binary if present, detect
      at runtime, degrade gracefully if absent (show "RAR support requires
      7-Zip on PATH").
    - Or use `libarchive` which has RAR *read* support in some builds,
      license-compatible (BSD) — check build flags.
  - This keeps the licensing question out of your source tree entirely.

- `ArchiveTree` — recursive extraction into a temp dir, handling nested
  archives by recursing into any entry whose extension matches a known
  archive type before hashing.
- `CrcComparator` — walks both trees, builds a `FileTree` of
  `{path, crc32, size}`, diffs into three buckets: `identical`, `modified`,
  `onlyInA`. This is also what Feature 3's "duplicate item" check reuses
  directly — no separate logic needed there.

- `GerberDiffTrigger` — a thin rule sitting on top of the comparator:
  if `modified.size() ∈ {1, 2}` **and** all modified files match a
  recognized Gerber extension set (configurable list, not hardcoded — ship
  a sane default like `.gbr/.gtl/.gbl/.gts/.gbs/.drl` etc. as a *sample*
  config the user can extend), enable the "Show Difference" button.
  Otherwise fall back to the plain text diff list. This answers your open
  question: gate by extension, but make the extension list itself
  data-driven so it's not a public-repo hardcoding problem either.

---

## 5. Feature 2 — Spec Parsing + Supplier Advisor

**Module:** `core/rules/`

This is the piece where schema-driven design matters most. Suggested config
shape (illustrative, not your real one):

```json
// spec_schema.json (PRIVATE)
{
  "parameters": [
    { "id": "layer_count", "type": "int", "comparator": "max" },
    { "id": "min_trace_width", "type": "float", "unit": "mil", "comparator": "min" },
    { "id": "surface_finish", "type": "enum", "comparator": "set_member" }
  ]
}
```

```json
// suppliers/supplier_a.json (PRIVATE)
{
  "supplier": "Supplier A",
  "capabilities": {
    "layer_count": 12,
    "min_trace_width": 3.0,
    "surface_finish": ["ENIG", "HASL", "OSP"]
  }
}
```

- `SchemaLoader` reads `spec_schema.json` → produces `ParameterDefinition[]`
  with a `type` and a `comparator` strategy (`min`, `max`, `equal`,
  `set_member`, or a custom rule id).
- `SpecParser` reads the item's spec file (parameter/value/remark columns),
  producing a `ParameterSet`. Missing/zero parameters are simply absent from
  the set — the engine treats "absent" as "no constraint," matching your
  actual file convention.
- `SupplierCapabilityLoader` reads each supplier file into the same
  `ParameterSet` shape.
- `FitEvaluator` — for each `(parameter, comparator)` pair, evaluates
  spec vs. supplier capability using a small `Comparator` strategy registry
  (`std::function` or polymorphic classes keyed by the `comparator` string
  from the schema). Output per supplier:
  ```cpp
  struct FitResult {
      Verdict verdict; // Good, FitWithChanges, NoFit
      std::vector<ParameterMismatch> mismatches; // which param, why
  };
  ```
- Because every domain rule (`min`, `max`, `set_member`, etc.) is a generic
  comparator, you can describe fairly complex fit logic in JSON without ever
  touching source code — and the public repo's sample schema can demonstrate
  the exact same mechanism with fake parameters like `"widget_tolerance"`.

---

## 6. Feature 3 — Gerber Reading

Split exactly as you described — two independent consumers of one parser:

**Module:** `core/gerber/`

- `GerberParser` — parses RS-274X into an intermediate geometry model
  (apertures, flashes, traces, regions per layer). Don't hand-roll this if
  you can avoid it: **gerbv**'s parser (GPL) or **gerber-parser.js**-style
  logic ported to C++ are references, but GPL licensing on gerbv means you
  can't statically link it into a permissively-licensed public repo without
  matching the license — check terms; safest is to write a minimal own
  parser (RS-274X is a fully documented open spec) scoped only to what you
  need: outlines, drill hits, pad flashes, layer count metadata.
- Two separate consumers of `GerberParser`'s output:
  1. **`GerberDiffRenderer`** (UI-adjacent): takes the *specific* old/new
     file pair identified by Feature 1, parses both, renders each into a
     `QGraphicsScene` with old in one color and new in another (e.g. red vs.
     green with alpha), overlaid. Next/prev toggles between the 1–2 pairs.
     Scope stays narrow: just the differing layer(s), not a full stackup —
     so you don't need padstack/layer-ordering logic at all yet.
  2. **`GerberParameterExtractor`** (feeds Feature 2): walks the parsed
     geometry to derive `layer_count`, `board_dimensions`, `panel_dimensions`
     (if panelization layer present), `finish_area` (solder mask layer
     bounding/union area), `solder_mask_layer_count`, `silkscreen_layer_count`.
     These become just another `ParameterSet` merged into the one built by
     `SpecParser`, so `FitEvaluator` doesn't care whether a parameter came
     from the text spec or from Gerber analysis.

This separation means you can ship the diff renderer (visually satisfying,
good portfolio material) well before the parameter extractor is fully
correct — they don't block each other.

---

## 7. Feature 4 — Supplier Quotation Comparison

**Module:** `core/quotes/`

- `IQuoteReader` interface:
  - `ExcelQuoteReader` — via QXlsx (Qt-friendly, MIT-style) or OpenXLSX.
    Straightforward since you control the "expected columns" mapping via
    config, same schema-driven approach as Feature 2.
  - `PdfQuoteReader` — the hard one. Two-tier strategy:
    - Tier 1: generic table extraction (Poppler/pdfium + a text-position
      clustering heuristic, or bind to a library like `camelot`-equivalent
      logic) — good enough when suppliers use simple tabular PDFs.
    - Tier 2: per-supplier `IQuoteReader` plugins for the handful of
      suppliers whose PDFs are too irregular for generic extraction — small
      registry keyed by supplier name/template fingerprint, each just a
      thin subclass implementing "find these fields at these anchors."
  - Since this is explicitly your lowest-priority feature, it's the best
    candidate to build **last**, once the generic `ParameterSet` +
    `FitEvaluator` machinery from Feature 2 already exists — a quote is just
    another `ParameterSet` to diff against the spec `ParameterSet`, plus
    price/lead-time fields compared supplier-to-supplier.

---

## 8. Deferred networked features — design now, wire in later

Don't skip designing these just because they're deferred — stub the seam now
so adding them doesn't require touching unrelated code:

```cpp
class INetworkService {
public:
    virtual ~INetworkService() = default;
    virtual bool submitFeedback(const std::string& text) = 0;
    virtual std::optional<std::string> fetchMinVersion() = 0;
};

class NullNetworkService : public INetworkService { /* no-ops, phase 1 */ };
class HttpNetworkService : public INetworkService { /* phase 2, Google Sheets/API calls */ };
```

The UI calls `INetworkService` unconditionally; in Phase 1 it's bound to
`NullNetworkService` (feedback button just writes to a local text file for
now, version check is skipped). Flip a factory function once IT approves
outbound calls — zero UI or business-logic changes required.

---

## 9. Distribution

- Build with **static Qt** (or bundle via `windeployqt`) to avoid DLL-hell on
  colleagues' machines.
- Ship as a single `.exe` (+ any plugin DLLs like archive backends) to the
  internal file server. No installer needed for a simple portfolio-grade
  internal tool unless you want Start Menu integration later.
- Since it's zero-network by design, there's no firewall/proxy config to
  worry about — worth stating explicitly in the app's "About" box for
  colleagues' peace of mind.
- Code-signing is optional but would further reduce SmartScreen friction if
  it ever comes up as an issue via the file-server route.

---

## 10. Suggested build order (maps to your difficulty ordering)

1. **Archive Comparison** — validates the whole plugin-backend pattern
   (`IArchiveReader`) and CRC diff engine; immediately useful standalone.
2. **Spec + Supplier Advisor** — validates the schema-driven config pattern
   end-to-end (`SchemaLoader`, `ParameterSet`, `FitEvaluator`); this is the
   architectural core, get it right before building more feeders into it.
3. **Gerber diff renderer** — visually the most impressive for a portfolio,
   but scoped narrowly (1–2 file overlay) so it doesn't balloon.
4. **Gerber parameter extraction** — reuses the same parser, feeds #2.
5. **Quotation comparison** — reuses `ParameterSet`/`FitEvaluator` again;
   Excel first, PDF generic extraction second, per-supplier plugins last
   and only as needed.

Each stage after #2 is mostly "write another `ParameterSet` producer or
consumer" rather than new architecture — which is the payoff of getting the
schema-driven core right early.
