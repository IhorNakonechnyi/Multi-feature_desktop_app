# Archive Compare Content-First Consolidated Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement every code-producing decision of the Archive Compare gap review (Q1–Q15): content-first matching, two-phase archive reading, content-based drawability, status-band tree sorting, strict drag-drop, and the summary/filter bar.

**Architecture:** Pure compute functions (`CrcComparator`, new `ArchiveComparison`, new `ComparisonSummary`, `GerberFileType::classify`) return results and errors as data; only `ArchiveComparePage` shows anything. Matching is keyed on `(crc32, size)` content classes read from the zip central directory — no decompression except (a) expanding nested archives that differ and (b) reading ≤8 KB heads of files in differing pairs.

**Tech Stack:** Qt 6.8.2 (Widgets, MSVC 2022 x64), C++20, CMake, QuaZip (dynamic DLL), QtTest/ctest.

**Decision record:** `docs/superpowers/plans/2026-07-27-archive-compare-open-questions.md` (Q1–Q15, all decided). Design specs: `2026-07-27-content-first-archive-matching-design.md` (Q1, amended by Q4/Q9), `2026-07-27-content-based-drawability-design.md` (Q2, amended by Q12). Cite these; do not re-litigate.

## Global Constraints

- **Compute/UI separation (Q7):** functions below the page never touch widgets or dialogs; errors travel as data. No `QMessageBox` anywhere in the compare flow when this plan is done.
- **Central-directory fast path (Q1):** archive-wide comparison never decompresses entries. Decompression happens only in `expandNested` (differing containers) and `readEntryHead`/`readEntryBytes` (differing pairs).
- **Confidentiality (CLAUDE.md):** no real supplier/spec parameter names in code, comments, or fixtures. Test fixtures use generic names (`top.gtl`, `notes.zip`).
- **No new dependencies.** QuaZip stays dynamically linked (LGPL).
- **Style (user preference):** intricate logic (comparator, orchestrator) uses simple structures + more explanatory comments than terse default.
- **Colours contract (Q1):** green = nothing to do; yellow = app *assumes* correspondence and content differs (always in cross-archive pairs); red = no counterpart. A/B are symmetric slots — no "new"/"old" wording anywhere.
- **Commits stay on branch `archive-compare-content-first`.** Never commit to `main`; never push (user gate).
- Build/test: `cmake --build build` · `ctest --test-dir build --output-on-failure`. First configure: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`.
- Windows note: this worktree path is deep; if a test target hits MAX_PATH, report it — do not silently skip.

## Milestone map

| Milestone | Delivers | Depends on |
|---|---|---|
| A (Tasks 1–2) | Reader resolves paths by content, nothing vanishes, `readEntryHead` | — |
| B (Tasks 3–5) | Content-first comparator (Q1, closes Q5/Q6) | — |
| C (Tasks 6–8) | Two-phase read (Q4), orchestrator, inline errors, A/B symmetry | A, B |
| D (Tasks 9–10) | Content-based drawability (Q2), extension mechanism deleted (Q12) | A, C |
| E (Tasks 11–12) | Status-band tree sorting + folder tint (Q9) | B |
| F (Task 13) | Strict drag-drop (Q10) | — |
| G (Tasks 14–18) | Summary, identical banner, filters, panel split (Q8) | B, C, D |
| Task 19 | Full verification + doc status updates | all |

---

# Milestone A — ArchiveReader: content decides nesting (Q3)

Milestone A executes the pre-existing plan `docs/superpowers/plans/2026-07-27-archivereader-content-based-nesting.md` **as written** — read that document first; it contains the full context, the `resolveStep` design, and all ten test cases. The two tasks below carve it into commit-sized pieces and restate the essentials so the checkboxes live here.

### Task 1: `resolveStep` path resolution + `readEntryBytes` rewrite

**Files:**
- Modify: `src/archivecompare/archivereader.h` (add `bool *found = nullptr` param to `readEntryBytes`)
- Modify: `src/archivecompare/archivereader.cpp` (replace the `".zip/"` split in `readEntryBytes`, absorb `findStoredEntryName` into `resolveStep`)
- Test: `tests/unit/test_archivereader.cpp`

**Interfaces:**
- Produces: `QByteArray ArchiveReader::readEntryBytes(const QString &archiveFilePath, const QString &entryRelativePath, bool *found = nullptr)` — existing call sites compile unchanged.
- Produces (internal): `ResolvedStep resolveStep(QuaZip &zip, const QString &normalizedRemaining)` in the anonymous namespace:

```cpp
struct ResolvedStep {
    QString storedName; // raw stored name as setCurrentFile() needs it; empty = no match
    bool    isFinal;    // storedName covers the whole remaining path
    QString rest;       // remainder after storedName + '/', when !isFinal
};
```

Resolution rules, in order: (1) a stored name whose `\`→`/`-normalized form case-insensitively equals the whole remaining path → `isFinal = true`; (2) otherwise the **longest** stored name `S` where the remaining path starts with normalized `S + "/"` → descend; (3) otherwise no match. Exact-before-prefix is what fixes the `*.zip`-folder bug.

- [ ] **Step 1: Write the failing tests** — add to `tests/unit/test_archivereader.cpp`, using its existing `addEntry(QuaZip&, name, content)` + `QTemporaryDir` machinery:

```cpp
void zipNamedFolderEntryIsReadable();       // archive with entry "gerber.zip/top.gtl" (plain folder named *.zip):
                                            //   readEntryBytes(path, "gerber.zip/top.gtl") returns the content
void zipNamedFolderNestedDeeper();          // entry "wrapper/gerber.zip/sub/top.gtl" → content returned
void exactMatchBeatsPrefixDescent();        // archive holding BOTH a real nested zip stored as "a.zip"
                                            //   AND a direct entry "a.zip/x.gtl" → asking for "a.zip/x.gtl"
                                            //   returns the direct entry's content, not a descent into a.zip
void missingEntrySetsFoundFalse();          // bool found=true; readEntryBytes(path,"nope.gtl",&found)
                                            //   → empty bytes, found == false
void zeroLengthEntrySetsFoundTrue();        // entry "empty.gbr" with "" content
                                            //   → empty bytes, found == true
```

- [ ] **Step 2: Run to verify the new cases fail** — `cmake --build build` then `ctest --test-dir build -R archivereader --output-on-failure`. Expected: the five new cases FAIL (first three return empty bytes today; `found` param doesn't compile until header edited — add header param first, then run).
- [ ] **Step 3: Implement** — `resolveStep` per the rules above; rewrite `readEntryBytes` as: loop { resolve; no match → set `found=false`, return empty; final → read bytes, `found=true`, return; else read bytes, open as nested `QuaZip` via `QBuffer` (failure → bogus path, `found=false`, empty), continue with `rest` }. Delete `findStoredEntryName` (its normalization/case-compare logic moves into `resolveStep`). Rewrite the big `".zip/"`-strategy comments — they describe the old approach.
- [ ] **Step 4: Verify all archivereader tests pass** — including the five pre-existing cases, unchanged.
- [ ] **Step 5: Commit** — `git add -A && git commit -m "ArchiveReader: resolve entry paths against stored entries, not '.zip/' splits"`

### Task 2: stop dropping misnamed entries + `readEntryHead` + honest read-failure message

**Files:**
- Modify: `src/archivecompare/archivereader.h` (add `readEntryHead`), `src/archivecompare/archivereader.cpp` (`readEntriesFrom` fallback), `src/archivecompare/archivecomparepage.cpp` (status text in `showSelectedPair`)
- Test: `tests/unit/test_archivereader.cpp`

**Interfaces:**
- Produces: `QByteArray ArchiveReader::readEntryHead(const QString &archiveFilePath, const QString &entryRelativePath, qint64 maxBytes, bool *found = nullptr)` — same walk as `readEntryBytes`; only the final read differs (`file.read(maxBytes)`); intermediate nested levels still read fully (unavoidable — a nested zip must be opened whole).

- [ ] **Step 1: Write failing tests**:

```cpp
void misnamedZipFileIsListed();     // entry "notes.zip" whose content is plain text ("not a zip"):
                                    //   readArchive() lists it with correct crc32 + sizeBytes (today: absent)
void misnamedZipFileIsReadable();   // readEntryBytes on that same entry returns its bytes
void genuineNestedZipStillFlattens(); // real nested zip → "nested.zip/inner.gtl" listed (guards the fallback)
void readEntryHeadTruncates();      // 10 KB entry, maxBytes=100 → exactly first 100 bytes
void readEntryHeadThroughNestedZip(); // head read of "nested.zip/inner.gtl" works
```

- [ ] **Step 2: Verify they fail** — `ctest --test-dir build -R archivereader --output-on-failure`. (`readEntryHead` calls won't compile until the header declares it — declare first, leave the body returning empty, so the test targets build and FAIL.)
- [ ] **Step 3: Implement** — in `readEntriesFrom`, every nested-open failure path (`nestedFile.open()`, `buffer.open()`, `nestedZip.open()`) appends the entry as an ordinary `RawFileEntry` using the `crc`/`uncompressedSize` already in hand, instead of `continue`. Implement `readEntryHead` sharing the walk (factor the walk into a private helper both readers call, differing only in the final read). In `ArchiveComparePage::showSelectedPair`, capture `found` for both sides and, when a side's read failed, show `tr("Could not read \"%1\" from archive %2.")` in `pairStatusLabel_` instead of falling through to "no drawable content" (that message must now only appear when the file was read and parsed to nothing).
- [ ] **Step 4: Verify** — full `ctest --test-dir build --output-on-failure`; nothing regressed.
- [ ] **Step 5: Commit** — `git commit -am "ArchiveReader: misnamed .zip entries stay listed; add readEntryHead"`

---

# Milestone B — Content-first comparator (Q1; closes Q5/Q6)

Full design: `docs/superpowers/specs/2026-07-27-content-first-archive-matching-design.md`. Read it before Task 4.

### Task 3: data-model changes

**Files:**
- Modify: `src/archivecompare/archivecomparisonresult.h`
- Test: `tests/unit/test_archivecomparator.cpp` (compile-fix only)

**Interfaces (produced — later tasks and Milestones C/G rely on these exact names):**

```cpp
// Why a pair was formed (Q1 "pair provenance"): yellow means the app assumed
// something, and the assumption must be inspectable in the pair list.
enum class PairReason {
    SameName,               // leaf filenames matched (case-insensitive)
    OnlyExtensionCandidate, // elimination: exactly one class per side with this extension
};

struct DifferingPair {
    QString relativePathA;
    QString relativePathB;
    PairReason reason = PairReason::SameName;
};

struct ArchiveEntry {
    QString relativePath;
    quint64 sizeBytes = 0;
    quint32 crc32 = 0;   // half of the (crc32, size) content-class key. Internal
                         // plumbing for class counting (Q8 summary) - never displayed (Q9).
    EntryClassification classification = EntryClassification::Identical;
};
```

`EntryClassification` and `ArchiveComparisonResult` are unchanged.

- [ ] **Step 1: Edit the header** as above (add `PairReason`, `reason`, `crc32`; update the `DifferingPair` doc comment to say pairs come from the name pass or extension pass of content-first matching).
- [ ] **Step 2: Build everything** — `cmake --build build`. Fix any aggregate-initialization sites that now mismatch (`{path, path}` still compiles since `reason` is defaulted; expect no breaks, but verify).
- [ ] **Step 3: Run full ctest** — all green (no behavior changed yet).
- [ ] **Step 4: Commit** — `git commit -am "Data model: PairReason on DifferingPair, crc32 carried on ArchiveEntry"`

### Task 4: comparator rewrite — classes, duplicate collapse, content match

**Files:**
- Modify: `src/archivecompare/crccomparator.cpp` (full rewrite), `src/archivecompare/crccomparator.h` (doc comment: content-first, cite the design doc; signature unchanged)
- Test: `tests/unit/test_archivecomparator.cpp`

**Interfaces:**
- Consumes: `ArchiveReader::RawFileEntry` (`relativePath`, `crc32`, `sizeBytes`).
- Produces: `CrcComparator::compare(entriesA, entriesB)` — **output entry order mirrors input order, index for index** (Milestone C's orchestrator depends on this to map results back to raw entries).

**Algorithm (design doc steps 1–3; steps 4–6 are Task 5):**

1. Per archive, group entry indices by class key `(crc32, sizeBytes)`.
2. Per class, the **representative** is the member with the fewest `/` in its path; ties broken by case-insensitive path compare. Every non-representative member is classified `Identical` (plain green — no marker, per Q9) *unconditionally, whatever happens to the class*.
3. A class whose key exists in both archives → representative (and members) `Identical` on both sides. No pair.
4. (Task 5) leftover classes → name pass → extension pass → red.

- [ ] **Step 1: Replace the old locked-in tests and write the new ones.** In `tests/unit/test_archivecomparator.cpp`: **delete** `sameNameDifferentCrcStillTreatedAsSelfDuplicate` (it locks the old rule the design retires) and any test asserting Pass-2's "ambiguous → unmatched" behavior (grep the file for `ambiguous` / `selfDuplicate` and read each test against the new design; keep plain identical/different/only-in-one cases). Add:

```cpp
// helper: RawFileEntry make(const QString &path, quint32 crc, quint64 size)
void contentMatchAcrossRename();      // A: {"top.gtl",7,100}  B: {"renamed.gtl",7,100}
                                      //   → both Identical, no pairs
void contentMatchAcrossRelocation();  // A: {"top.gtl",7,100}  B: {"backup/top.gtl",7,100}
                                      //   → both Identical, no pairs
void representativeIsShallowestPath();// A: {"deep/a/top.gtl",7,100},{"top.gtl",7,100}; B: {}
                                      //   → "top.gtl" is the actionable row (OnlyInThisArchive),
                                      //     "deep/a/top.gtl" is Identical (collapsed duplicate)
void representativeTieAlphabetical(); // A: {"b.gtl",7,100},{"a.gtl",7,100}; B: {}
                                      //   → "a.gtl" red, "b.gtl" green
void duplicateOfRedClassIsGreen();    // covered by representativeIsShallowestPath asserts; keep explicit:
                                      //   the non-representative is Identical while representative is red
void sameCrcDifferentSizeNotMatched();// A: {"x.gtl",7,100} B: {"x.gtl",7,999} → NOT content-matched
                                      //   (falls to Task 5's name pass; for now assert not Identical-by-content:
                                      //    classification != Identical)
```

- [ ] **Step 2: Verify new tests fail** — `ctest --test-dir build -R archivecomparator --output-on-failure` (rename/relocation cases fail under the old path-keyed algorithm).
- [ ] **Step 3: Rewrite `crccomparator.cpp`.** Structure (simple containers + explanatory comments, per style preference):

```cpp
namespace {
struct ContentClass {
    QVector<int> memberIndices;  // into the archive's entry vector
    int representativeIndex = -1;
};
// Key type: QPair<quint32, quint64> works directly as a QHash key.
using ClassMap = QHash<QPair<quint32, quint64>, ContentClass>;

int pathDepth(const QString &p) { return p.count(QLatin1Char('/')); }

ClassMap buildClasses(const QVector<RawFileEntry> &entries);
// representative: fewest '/', tie -> QString::compare(a, b, Qt::CaseInsensitive) < 0
}
```

`compare()`: size both output vectors, copy `relativePath`/`sizeBytes`/`crc32` for every entry; mark all non-representatives `Identical`; content-match classes across archives (shared key → representatives `Identical`); collect unmatched class keys per side for Task 5 (for now, classify their representatives `OnlyInThisArchive` and emit no pairs — Task 5 replaces that stub with the name/extension passes). Delete `selfDuplicateIndices`, `groupByFilenameAndCrc`, `crcByPath`, and their comments.

- [ ] **Step 4: Run** — Task 4's tests pass. Pre-existing exact-path identical/different cases: identical passes; *different* cases now rely on the Task 5 name pass — mark any such failing pre-existing test with a `QSKIP("re-enabled in name-pass task")` and a comment, listing them in the commit message.
- [ ] **Step 5: Commit** — `git commit -am "CrcComparator: content classes, duplicate collapse, content match (Q1 steps 1-3)"`

### Task 5: name pass, extension pass, pair reasons

**Files:**
- Modify: `src/archivecompare/crccomparator.cpp`
- Test: `tests/unit/test_archivecomparator.cpp`

**Interfaces:**
- Produces: `differingPairs` entries carry representative paths and a correct `PairReason`. Invariant for consumers: *no orphan yellow* — every `Different` entry belongs to ≥1 pair.

**Algorithm (design doc steps 4–6):**

- **Name pass:** group leftover classes (each side) by representative's leaf filename, lowercased. A name present on both sides → every A-class and B-class under it becomes `Different` (representatives; other members stay green), and **every A-class × B-class combination** becomes a pair (`PairReason::SameName`).
- **Extension pass:** group what remains by `QFileInfo(path).suffix().toLower()`; classes with an **empty** suffix are skipped (no key to eliminate on). A suffix with **exactly one** class on each side → pair them, `Different`, `PairReason::OnlyExtensionCandidate`. Two or more candidates on either side → leave red, never guess.
- **Everything else:** `OnlyInThisArchive`.

- [ ] **Step 1: Write failing tests** (the design doc's test list; worked examples 1–5 from its "Worked examples" section give the fixtures):

```cpp
void samePathDifferentContentPairsByName();  // A/B both "drill.txt" different crc → 1 pair, both Different,
                                             //   reason SameName  (re-enable any QSKIPped Task-4 case here)
void namePassCrossProductOneToTwo();         // A: 1 class "x.gtl"; B: 2 distinct-content classes named "x.gtl"
                                             //   → 2 pairs (A-rep × each B-rep), 3 entries Different
void namePassIsCaseInsensitive();            // "TOP.GTL" vs "top.gtl" pairs
void classMatchedUnderRepresentativeName();  // A class members {"sub/x.gtl","other.gtl"} (same content):
                                             //   representative is "other.gtl" (shallower). B: {"other.gtl", differing crc}
                                             //   → pairs under "other.gtl"; "x.gtl"'s name never enters matching
void extensionPassSingleCandidates();        // A: "old_name.gtl"(7) B: "new_name.gtl"(9), no name match
                                             //   → 1 pair, reason OnlyExtensionCandidate
void extensionPassRefusesTwoCandidates();    // A: two leftover .gtl classes, B: one → all three red, no pair
void extensionPassSkipsNoExtension();        // leftover files without a suffix never pair by elimination
void duplicateOfYellowClassIsGreen();        // A: {"x.gtl",7},{"copy/x.gtl",7}; B: {"x.gtl",9}
                                             //   → A rep Different, A copy Identical, 1 pair
void noOrphanYellow();                       // walk every fixture above: each Different entry's path appears
                                             //   in >=1 differingPairs side
void pairReasonRecorded();                   // SameName vs OnlyExtensionCandidate asserted explicitly
```

- [ ] **Step 2: Verify they fail.**
- [ ] **Step 3: Implement** the two passes, replacing Task 4's stub. Then remove every `QSKIP` added in Task 4.
- [ ] **Step 4: Run the full comparator suite** — all pass, none skipped. Run full ctest — nothing else regressed.
- [ ] **Step 5: Commit** — `git commit -am "CrcComparator: name pass with cross-product pairs, extension elimination pass (Q1 complete)"`

---

# Milestone C — Two-phase read + orchestrator + page wiring (Q4, Q7 constraint, A/B symmetry)

### Task 6: `readTopLevel` + `expandNested`; retire `readArchive`

**Files:**
- Modify: `src/archivecompare/archivereader.h`, `src/archivecompare/archivereader.cpp`
- Modify: `src/archivecompare/archivecomparepage.cpp` (temporarily: `readArchive` call sites — see Step 3)
- Test: `tests/unit/test_archivereader.cpp`

**Interfaces (produced):**

```cpp
struct RawFileEntry {
    QString relativePath;
    quint32 crc32 = 0;
    quint64 sizeBytes = 0;
    bool isNestedArchiveCandidate = false; // name ends in ".zip" (cheap filter, Q4.4);
                                           // content decides at expansion time (Q3 rule)
};

// Phase 1 (Q4): lists ONLY the top level of the archive; a nested-archive
// candidate appears as a single opaque entry with its own crc/size.
// Throws ArchiveOpenError like readArchive did.
QVector<RawFileEntry> readTopLevel(const QString &filePath);

// Phase 2: opens one candidate container and lists ITS top level, each entry
// prefixed "containerRelativePath/". openedAsArchive=false means the bytes are
// not a zip - the caller keeps the original entry as an ordinary file (Q3).
// containerRelativePath may itself lie inside an expanded container - the
// resolveStep walk from Task 1 handles the descent.
struct NestedExpansion {
    bool openedAsArchive = false;
    QVector<RawFileEntry> entries;
};
NestedExpansion expandNested(const QString &archiveFilePath, const QString &containerRelativePath);
```

`readArchive` (recursive one-shot) is **deleted** — its only caller is `ArchiveComparePage::runComparison`, replaced in Task 7/8. `readEntryBytes`/`readEntryHead` are untouched.

- [ ] **Step 1: Write failing tests**:

```cpp
void topLevelListsNestedAsOpaque();   // archive with "top.gtl" + real nested "inner.zip" containing "a.gtl"
                                      //   → 2 entries; "inner.zip" has isNestedArchiveCandidate=true,
                                      //     carries its central-directory crc/size; "a.gtl" NOT listed
void topLevelMarksMisnamedCandidate();// plain-text "notes.zip" → listed, candidate=true (content unknown yet)
void expandNestedListsContents();     // expandNested(path,"inner.zip") → openedAsArchive=true,
                                      //   entries=[{"inner.zip/a.gtl", crc, size}]
void expandNestedRejectsNonZip();     // expandNested(path,"notes.zip") → openedAsArchive=false
void expandNestedGrandchildCandidate();// inner.zip itself containing "deep.zip" → entry
                                      //   "inner.zip/deep.zip" listed with candidate=true (not recursed)
void expandNestedInsideExpanded();    // expandNested(path,"inner.zip/deep.zip") descends two levels
```

- [ ] **Step 2: Verify they fail** (new functions: declare in header, stub bodies returning `{}`, so the target compiles and the tests FAIL).
- [ ] **Step 3: Implement.** `readTopLevel`: the existing `readEntriesFrom` loop minus recursion — every entry becomes a `RawFileEntry`, `isNestedArchiveCandidate = name.endsWith(".zip", CaseInsensitive)`. `expandNested`: resolveStep-walk to the container, read its bytes, open as `QuaZip` in a `QBuffer`; on success list its top level with the prefix; on failure `openedAsArchive=false`. Delete `readArchive`, `readEntriesFrom`, `isNestedArchiveName`; delete their tests (`misnamedZipFileIsListed` and `genuineNestedZipStillFlattens` re-target: misnamed-zip listing is now `topLevelMarksMisnamedCandidate` + `expandNestedRejectsNonZip`; flattening is `expandNestedListsContents`). To keep the app compiling until Task 8, change `runComparison`'s two `readArchive` calls to `readTopLevel` (behavior temporarily loses recursion — restored properly in Tasks 7–8; note this in the commit message).
- [ ] **Step 4: Run** — archivereader suite green; full ctest green (comparator tests are reader-independent).
- [ ] **Step 5: Commit** — `git commit -am "ArchiveReader: two-phase API (readTopLevel/expandNested), readArchive retired (Q4)"`

### Task 7: `ArchiveComparison` orchestrator

**Files:**
- Create: `src/archivecompare/archivecomparison.h`, `src/archivecompare/archivecomparison.cpp`
- Modify: the app-target source list (find it: `grep -r "crccomparator.cpp" --include=CMakeLists.txt` from repo root; add the new .cpp beside it)
- Test: `tests/unit/test_archivecomparison.cpp` (new target)
- Modify: `tests/unit/CMakeLists.txt`

**Interfaces (produced):**

```cpp
// The whole read-expand-compare pipeline as one pure call. Errors are data
// (Q7 rule: only the page shows anything).
namespace ArchiveComparison {

struct Outcome {
    ArchiveComparisonResult result;
    QString errorA;   // non-empty = archive A unreadable; result is empty then
    QString errorB;
    qint64 compareMs = 0; // wall time of the whole pipeline (Q7: time compare AND render)
};

Outcome compareArchives(const QString &pathA, const QString &pathB);

} // namespace ArchiveComparison
```

**Algorithm (Q4 point 3 + design-doc amendment):** read both top levels (catch `ArchiveOpenError` per side into `errorA`/`errorB`; both must succeed to proceed). Then loop:

1. `result = CrcComparator::compare(entriesA, entriesB)` (index-aligned with inputs).
2. Collect indices where `isNestedArchiveCandidate && classification != Identical` and the entry wasn't already tried this session of the loop.
3. None on either side → done.
4. For each collected index: `expandNested`. Opened → replace that one entry with the expansion's entries (splice, preserving list order). Not opened → clear the candidate flag, keep the entry (ordinary file, Q3).
5. Repeat (expanded content may contain candidates — level by level).

An identical nested archive is classified green in step 1 and never collected — **never opened** (Q4 point 3). Termination: every iteration either removes a candidate flag or replaces a candidate with strictly-shallower content (finite nesting depth).

- [ ] **Step 1: Add the test target** to `tests/unit/CMakeLists.txt` (model it on `test_archivereader`, adding `crccomparator.cpp` and `archivecomparison.cpp` to the sources and `QuaZip::QuaZip` to the links).
- [ ] **Step 2: Write failing tests** (build real zips with the archivereader-test `addEntry` helper — copy that helper into this file):

```cpp
void identicalNestedArchiveStaysClosed(); // A and B both contain byte-identical "inner.zip"
                                          //   → result has ONE entry per side for it, Identical;
                                          //     no "inner.zip/..." rows exist
void differingNestedArchiveExpands();     // A's inner.zip has {"a.gtl","X"}, B's has {"a.gtl","Y"}
                                          //   → rows "inner.zip/a.gtl" on both sides, Different, 1 pair
void oneSidedNestedArchiveExpands();      // inner.zip only in A → its contents rows appear (red or
                                          //   content-matched green per the mixed-revision consequence)
void misnamedZipComparedAsFile();         // "notes.zip" plain text, differing bytes per side
                                          //   → single row each side (name pass pairs them), never expanded
void unreadableArchiveReportsError();     // pathA = a text file → errorA non-empty, result empty
void nestedInsideNestedExpands();         // differing deep.zip inside differing inner.zip → leaf rows appear
```

- [ ] **Step 3: Verify they fail** (stub `compareArchives` returning `{}`).
- [ ] **Step 4: Implement** per the algorithm block. Wrap the whole pipeline in `QElapsedTimer` → `compareMs`.
- [ ] **Step 5: Run** — new suite green, full ctest green.
- [ ] **Step 6: Commit** — `git commit -am "ArchiveComparison: two-phase orchestrator, errors as data (Q4/Q7)"`

### Task 8: page wiring — inline errors, A/B symmetry, pair reasons, compare time

**Files:**
- Modify: `src/archivecompare/archivecomparepage.cpp`, `src/archivecompare/archivecomparepage.h`
- Modify: `src/archivecompare/archivepanel.h`, `src/archivecompare/archivepanel.cpp`

**Interfaces:**
- Consumes: `ArchiveComparison::compareArchives`, `PairReason`.
- Produces: `ArchivePanel::showError(const QString &message)` and `ArchivePanel::clearError()` — a word-wrapped `QLabel` (objectName `detailLabel`) under the header, hidden by default; `showError` also clears the tree (`model_->buildFrom({})`).

UI/glue — no unit tests (constitution P4); the gate is manual.

- [ ] **Step 1: Panel error label** — add `errorLabel_` to `ArchivePanel`, implement `showError`/`clearError`.
- [ ] **Step 2: Rewire `runComparison`** — replace the try/catch + two `QMessageBox::warning` blocks with one `compareArchives(pathA, pathB)` call; `errorA`/`errorB` → `panelX_->showError(...)`, success → `clearError()` + `showEntries`. Remove the `#include <QMessageBox>` and the `ArchiveOpenError` handling (the header include of `archivereader.h` stays — `readEntryBytes`/`readEntryHead` are still called).
- [ ] **Step 3: A/B symmetry** — panel constructor args `tr("New")`/`tr("Old")` → `tr("A")`/`tr("B")`; legend rows `tr("only new")`/`tr("only old")` → `tr("only in A")`/`tr("only in B")`; update `kColorA`/`kColorB` comments ("A in green, B in red"); in `showSelectedPair`, the two "showing old/new file only" strings become "showing B only"/"showing A only" (check every `tr()` in the file for new/old wording).
- [ ] **Step 4: Pair reasons in the list** — extend `formatPairLabel(pair)`: append the reason to **every** row (under content-first, even a same-path pair is an assumption the user must be able to weigh): `tr(" - same name")` for `PairReason::SameName`, `tr(" - only .%1 left on each side").arg(QFileInfo(pair.relativePathA).suffix().toLower())` for `OnlyExtensionCandidate`.
- [ ] **Step 5: Compare time** — add `compareTimeLabel_` (`detailLabel`) right-aligned on the detail card's second legend line (mirror how `renderTimeLabel_` sits on the first); set from `outcome.compareMs` each run; reset to `tr("Compare time: -")` alongside the render-time reset.
- [ ] **Step 6: Build + manual gate** — `cmake --build build`, run the app (`build/…/pcb-gadget.exe` — find the exe under `build`, or use the project run skill): load `Test_data/test_pair_3` (both sides). Expect: trees populate, legend says "only in A/only in B", compare time shows, exactly 1 pair listed **with " - same name" suffix**. Then pick a non-zip file renamed `.zip` on one side: inline error text on that panel, no dialog, other panel unaffected.
- [ ] **Step 7: Commit** — `git commit -am "Page: orchestrator wiring, inline errors, A/B symmetry, pair reasons, compare timing"`

---

# Milestone D — Content-based drawability (Q2) + extension mechanism deleted (Q12)

Design: `docs/superpowers/specs/2026-07-27-content-based-drawability-design.md` (as amended by Q12 — no extension fallback, 8 KB window).

### Task 9: `GerberFileType::classify`; delete the extension mechanism

**Files:**
- Modify: `src/gerber/gerberfiletype.h`, `src/gerber/gerberfiletype.cpp`
- Modify: `src/gerber/gerberviewerpage.cpp` (call-site switch — `grep -rn "isDrillFile\|isRecognized" src/` first and update every hit)
- Modify: `src/archivecompare/archivecomparepage.cpp` (`parseGerberOrDrill` call site)
- Test: `tests/unit/test_gerberfiletype.cpp` (rewrite)

**Interfaces (produced):**

```cpp
namespace GerberFileType {

// How many bytes of a file's head classify() needs. 8 KB: with no extension
// fallback, the sniff window is the only thing between a drawable file and
// rejection, so it must survive a long G04 comment banner pushing %FS down
// the file (Q12). Real sample files run 19-92 KB.
constexpr qint64 kClassifySniffBytes = 8192;

enum class Drawability {
    ExcellonDrill, // parse with ExcellonParser
    Gerber,        // parse with GerberParser
    NotDrawable,   // no %FS/%MO and no M48 - the parser would have no
                   // coordinate format to apply; extension is irrelevant (Q12)
};

// Content-only. Pass the first kClassifySniffBytes of the file (more is
// harmless - only the head is scanned). The filename plays no part.
Drawability classify(const QByteArray &head);

} // namespace GerberFileType
```

**Deleted:** `isRecognized`, both `isDrillFile` overloads, `addUserExtension`, `defaultExtensions`, `makeSettings`, `kUserExtensionsKey`. Orphaned INI entries under `gerber/userRecognizedExtensions` stay on disk — harmless, no migration (Q12).

**Classification rules (order matters):** (1) any line in the head whose trimmed content is exactly `M48` (case-insensitive) → `ExcellonDrill` — Excellon first because a drill file may contain a bare `%` line while no Gerber file carries a bare `M48`; (2) else head contains `"%FS"` or `"%MO"` (byte search, no line split) → `Gerber`; (3) else `NotDrawable`. Scan at most `kClassifySniffBytes` bytes even if given more.

- [ ] **Step 1: Rewrite `test_gerberfiletype.cpp`** (delete the extension/`addUserExtension` cases wholesale):

```cpp
void excellonDetectedWhateverTheName();   // "M48\nINCH,LZ\n..." → ExcellonDrill
void gerberDetectedByFormatSpec();        // "%FSLAX35Y35*%\n..." → Gerber
void gerberDetectedByModeOnly();          // header with %MOIN but the %FS beyond the given head → Gerber
void gtlNamedTextIsNotDrawable();         // "just a plain report\nline 2\n" → NotDrawable
                                          //   (the extension no longer rescues anything - Q12)
void excellonWinsOverGerberMarkers();     // head containing both a bare "%" line and "M48" → ExcellonDrill
void fsBehindLongCommentBannerFound();    // 6 KB of "G04 banner*\n" lines then "%FSLAX35Y35*%" → Gerber
void fsBeyond8KbNotFound();               // 9 KB of G04 lines then %FS → NotDrawable
                                          //   (documented limit - a future window change must be deliberate)
void m48SubstringInsideLineIgnored();     // "G04 BOM48 note*\n%FSLAX24Y24*%" → Gerber, not drill
```

- [ ] **Step 2: Verify they fail** (declare `classify` + enum, stub returning `NotDrawable`).
- [ ] **Step 3: Implement** `classify` per the rules; delete everything listed above; update `parseGerberOrDrill` in `archivecomparepage.cpp` to `classify(bytes) == Drawability::ExcellonDrill ? ExcellonParser : GerberParser` (drop the `relativePath` parameter — it's unused now; adjust the two callers and the member declaration); update `gerberviewerpage.cpp`'s parser dispatch the same way. **Note:** `gerberdifftrigger.cpp` still calls the deleted functions — Task 10 rewrites it; to keep the build green across the commit boundary, do Tasks 9 and 10 as one build unit if needed, but keep the commits separate only if the tree compiles; otherwise merge into a single commit at Task 10's step 5 and say so.
- [ ] **Step 4: Run** — gerberfiletype suite green.
- [ ] **Step 5: Commit** (if the whole tree builds) — `git commit -am "GerberFileType: content-only classify(); extension mechanism deleted (Q2/Q12)"`

### Task 10: trigger rewrite + page bucketing without buttons

**Files:**
- Modify: `src/archivecompare/gerberdifftrigger.h`, `src/archivecompare/gerberdifftrigger.cpp`
- Modify: `src/archivecompare/archivecomparepage.cpp`, `src/archivecompare/archivecomparepage.h`
- Test: `tests/unit/test_gerberdifftrigger.cpp` (rewrite)

**Interfaces (produced):**

```cpp
// Buckets a comparison's pairs by whether the overlay can draw both sides.
// Content decides (GerberFileType::classify over each side's head bytes);
// the reader callbacks keep this testable without an archive on disk.
namespace GerberDiffTrigger {

using HeadReader = std::function<QByteArray(const QString &relativePath)>;

struct PairBuckets {
    QVector<DifferingPair> drawable;    // both sides classify != NotDrawable
    QVector<DifferingPair> notDrawable; // at least one side NotDrawable
};

PairBuckets classifyPairs(const QVector<DifferingPair> &pairs,
                          const HeadReader &readHeadA, const HeadReader &readHeadB);

} // namespace GerberDiffTrigger
```

**Deleted:** `eligiblePairs`, `unrecognizedPairs`, `drillPairs`, `isMixedDrillPair`, `hasDrillSide`, `bothSidesDrillFiles` (the mixed drill/Gerber special case goes — Q2 "Removals": the app polices layer semantics nowhere else; a drill-vs-copper pair now renders and the user sees the mismatch on canvas). In the page: `skippedDrillLabel_`, `skippedUnrecognizedButtonRow_`, `distinctUnrecognizedExtensions`, and the whole Add-button loop go; `rebuildSkippedPairsUi` shrinks to setting one label.

- [ ] **Step 1: Rewrite `test_gerberdifftrigger.cpp`** — inject readers as lambdas over a `QHash<QString, QByteArray>` fixture map (no QuaZip; drop `gerberfiletype.cpp`? **no** — keep it in the target's sources, `classify` is called):

```cpp
void bothGerberSidesDrawable();          // both heads "%FSLAX35Y35*%" → drawable
void drillTxtAgainstDrlDrawable();       // A head "M48\nINCH\n", B head "M48\n" → drawable
                                         //   (the real .TXT drill case that started Q2)
void mixedDrillAndGerberDrawable();      // one M48 side, one %FS side → DRAWABLE (special case deleted)
void plainTextSideNotDrawable();         // one side "pick and place list\n" → notDrawable bucket
void unreadableSideNotDrawable();        // reader returns empty QByteArray → that side NotDrawable
void bucketsPartitionInput();            // drawable.size() + notDrawable.size() == pairs.size()
```

- [ ] **Step 2: Verify they fail.**
- [ ] **Step 3: Implement** `classifyPairs`; rewrite the page: in `runComparison`, build the buckets with lambdas `[this](const QString &p){ return ArchiveReader::readEntryHead(panelA_->archivePath(), p, GerberFileType::kClassifySniffBytes); }` (and B); fill `gerberPairList_` from `buckets.drawable` (this replaces `eligiblePairs_` content — keep the member, same name); `rebuildSkippedPairsUi` becomes `updateNotDrawableLabel(int count)`: `tr("%n changed pair(s) can't be previewed - not recognized as Gerber or drill data.", "", count)`, hidden at zero. Delete the listed members/functions. (If Task 9 could not commit alone, this is the combined commit.)
- [ ] **Step 4: Run** — full ctest green.
- [ ] **Step 5: Manual gate** — run the app on `Test_data/test_pair_2`: the changed drill `.TXT` pair must now appear in the pair list and render holes on the canvas; the pick-and-place/tool `.TXT` files must not (they're identical — not pairs at all); no Add-extension buttons anywhere.
- [ ] **Step 6: Commit** — `git commit -am "GerberDiffTrigger: content-based drawability buckets; Add-extension UI removed (Q2/Q12)"`

---

# Milestone E — Tree: status bands + folder tint (Q9)

### Task 11: band sorting + folder band inheritance

**Files:**
- Modify: `src/archivecompare/archivetreemodel.h`, `src/archivecompare/archivetreemodel.cpp`
- Test: `tests/unit/test_archivetreemodel.cpp`

**Interfaces (produced):**

```cpp
// In archivetreemodel.h -
struct ArchiveTreeNode {
    QString name;
    bool isDirectory = false;
    quint64 sizeBytes = 0;
    EntryClassification classification = EntryClassification::Identical;
    int sortBand = 2; // 0 yellow, 1 red, 2 green - work-demanded order (Q9).
                      // Directories inherit the LOWEST (highest-priority) band
                      // of their descendants.
    std::vector<std::unique_ptr<ArchiveTreeNode>> children;
    ArchiveTreeNode *parent = nullptr;
};
// New role so the delegate can tint folders:
enum Role { ClassificationRole = Qt::UserRole + 1, IsDirectoryRole, SortBandRole };
```

Band mapping: `Different`→0, `OnlyInThisArchive`→1, `Identical`→2. Sort key within one parent: `(sortBand, !isDirectory, name case-insensitive)` — bands first, directories before files inside a band, then alphabetical. Size sorting is **gone** (size is internal plumbing, never displayed — Q9).

- [ ] **Step 1: Write failing tests** in `test_archivetreemodel.cpp` (its existing fixture style builds entry vectors and inspects the model):

```cpp
void filesSortYellowRedGreen();       // entries green.gbr/red.gbr/yellow.gbr (classifications to match)
                                      //   → row order: yellow.gbr, red.gbr, green.gbr
void alphabeticalWithinBand();        // two yellow files "b.gbr","a.gbr" → a before b
void folderInheritsWorstBand();       // "docs/readme.txt" green + "gerber/x.gbr" yellow + root "z.txt" red
                                      //   → order: gerber (band0), z.txt (band1), docs (band2)
void directoriesFirstWithinBand();    // a yellow folder and a yellow file → folder row first
void sortBandRoleExposed();           // model.data(idx, SortBandRole) returns the int band
void sizeNoLongerOrders();            // two green files sizes 999/1 named "a.gbr","b.gbr" → a first
```

- [ ] **Step 2: Verify they fail.**
- [ ] **Step 3: Implement** — after `buildFrom`'s node construction: recursive pass computing `sortBand` (files from classification; directories = min over children); `sortChildren` uses the new key; `data()` serves `SortBandRole`; **fix the stale comment** at `columnCount` (`// single column - size is shown inline by the delegate, not a real column` → `// single column - the row shows the filename and nothing else; size is internal plumbing (Q9)`); update the header's "folders deliberately don't get a rollup color" comment — that decision is reversed by Q9, say so and cite it.
- [ ] **Step 4: Run** — suite green, full ctest green.
- [ ] **Step 5: Commit** — `git commit -am "ArchiveTreeModel: yellow/red/green band sort, folders inherit worst band (Q9)"`

### Task 12: folder tint in the delegate

**Files:**
- Modify: `src/archivecompare/archivetreedelegate.cpp`

Folders get a **light** treatment — a 4 px colored bar at the row's left edge in the band color, no background fill, default text — so one changed folder doesn't become a wall of color, while a folder *sorted* by status also *looks* it (Q9 rule 4). Band → color source: reuse `Palette::changedBackground()` (band 0), `Palette::missingBackground()` (band 1), nothing for band 2 (an all-green folder stays undecorated).

- [ ] **Step 1: Implement** — in `paint()`, for `isDirectory`: read `SortBandRole`; for bands 0/1 `painter->fillRect(QRect(option.rect.left(), option.rect.top(), 4, option.rect.height()), color)` before the base paint. File rows unchanged.
- [ ] **Step 2: Build + manual gate** — app on `Test_data/test_pair_1` (its synthetic layer subfolder produces mixed bands): yellow-holding folders sort first and carry a yellow edge; fully green folders sink, undecorated; file rows look exactly as before.
- [ ] **Step 3: Commit** — `git commit -am "Tree delegate: light band tint on folder rows (Q9)"`

---

# Milestone F — Drag-drop validation (Q10)

### Task 13: exactly one local `.zip` file

**Files:**
- Modify: `src/archivecompare/archivepanel.cpp`

Rules (Q10): dragEnter accepts iff the mime data has **exactly one** URL, it `isLocalFile()`, the path ends in `.zip` (case-insensitive), and `QFileInfo(path).isFile()` (rejects folders — including folders named `*.zip`). Everything else → no accept → the OS shows the no-drop cursor. Drop re-validates identically (defense in depth) and otherwise behaves as today. Name-as-candidate-filter here does **not** contradict Q3 — content still decides at read time; a `.zip`-named non-zip reports inline via Task 8's error label. No two-archives-in-one-drop (excluded by the single-URL rule, deliberately).

- [ ] **Step 1: Implement** — shared local helper in `archivepanel.cpp`:

```cpp
namespace {
// Q10: the picker has always been *.zip-filtered; drag-drop now agrees.
// Exactly one local .zip FILE - a folder, a multi-select, or any other
// type shows the no-drop cursor instead of a post-drop error.
QString singleDroppedZipPath(const QMimeData *mime)
{
    const QList<QUrl> urls = mime->urls();
    if (urls.size() != 1 || !urls.first().isLocalFile())
        return QString();
    const QString path = urls.first().toLocalFile();
    if (!path.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive) || !QFileInfo(path).isFile())
        return QString();
    return path;
}
}
```

`dragEnterEvent`: `if (!singleDroppedZipPath(event->mimeData()).isEmpty()) event->acceptProposedAction();`. `dropEvent`: same helper; empty → return; else `setArchivePath(path)`. Add the `<QUrl>`/`<QFileInfo>` includes if missing.
- [ ] **Step 2: Build + manual gate** — drag onto a panel: (a) one zip → accepted, loads; (b) a folder → no-drop cursor; (c) two zips at once → no-drop cursor; (d) a PDF → no-drop cursor; (e) a folder renamed `*.zip` → no-drop cursor.
- [ ] **Step 3: Commit** — `git commit -am "ArchivePanel: drag-drop accepts exactly one local .zip file (Q10)"`

---

# Milestone G — Summary, identical banner, filters, panel split (Q8)

### Task 14: `DataKind` + `ComparisonSummary` pure logic

**Files:**
- Modify: `src/archivecompare/archivecomparisonresult.h` (add `DataKind` + field)
- Create: `src/archivecompare/comparisonsummary.h`, `src/archivecompare/comparisonsummary.cpp` (add to the app target beside `crccomparator.cpp`)
- Test: `tests/unit/test_comparisonsummary.cpp` (new target in `tests/unit/CMakeLists.txt`, sources: the test + `comparisonsummary.cpp`, links `Qt6::Core Qt6::Test`)

**Interfaces (produced):**

```cpp
// archivecomparisonresult.h -
enum class DataKind {
    GerberData, // previewable family: Gerber layers + drill (Q2 folds drill in)
    OtherData,
};
// ArchiveEntry gains:  DataKind kind = DataKind::OtherData;

// comparisonsummary.h -
namespace ComparisonSummary {

struct TypeCounts {
    int identicalClasses = 0; // global: distinct (crc32,size) classes present on BOTH sides
    int pairs = 0;            // rows of differingPairs of this kind
    int onlyInA = 0;          // red CLASSES (not rows) per side - one-sided by nature
    int onlyInB = 0;
};

struct Summary {
    TypeCounts gerber;
    TypeCounts other;
    bool fullyIdentical() const; // zero pairs and zero onlyIn* in both kinds (Q8/Q1:
                                 // the precise "archives are identical" condition)
};

// Counts CLASSES, never rows (Q8): duplicate copies and collapsed nested
// archives must not inflate the numbers. Entries' kind fields must already
// be set (Task 15). Always summarizes the WHOLE result - never filtered rows.
Summary summarize(const ArchiveComparisonResult &result);

} // namespace ComparisonSummary
```

Counting rules: group each side's entries by `(crc32, sizeBytes)`; a class's kind = `GerberData` if **any** member is Gerber-kind (sides may disagree only via sniff edge cases — prefer Gerber so the summary and pair list agree). `identicalClasses`: classes whose key exists on both sides, counted once, by kind. `onlyInX`: classes on side X whose key is absent from the other side **and** whose representative row is `OnlyInThisArchive` (a yellow class is not "to check"). `pairs`: `differingPairs` rows; a pair's kind = Gerber if either endpoint entry is Gerber-kind.

- [ ] **Step 1: Write failing tests** (build small `ArchiveComparisonResult` fixtures by hand — no comparator dependency):

```cpp
void identicalClassesCountedOnceGlobally(); // same (crc,size) both sides, 2 copies in A
                                            //   → identicalClasses == 1 (not 2, not 3)
void duplicateRowsDoNotInflate();           // A has 3 rows of one class, B has 1 → still 1
void redCountedPerSideAsClasses();          // 2 red rows in A sharing one class → onlyInA == 1
void pairsCountedAsRows();                  // 2 differingPairs (cross product) → pairs == 2
void kindsSplitGerberVsOther();             // one green .gtl class + one green other class
                                            //   → gerber.identicalClasses==1, other.identicalClasses==1
void fullyIdenticalDetected();              // only green everywhere → true; add one red → false
void yellowClassIsNotToCheck();             // Different rows, key one-sided → onlyIn* == 0
```

- [ ] **Step 2: Verify they fail.**  - [ ] **Step 3: Implement.**  - [ ] **Step 4: Run green (full ctest).**
- [ ] **Step 5: Commit** — `git commit -am "ComparisonSummary: class-based, kind-split counts (Q8)"`

### Task 15: kind resolution — conclusive extensions trusted, unknown sniffed

**Files:**
- Modify: `src/archivecompare/comparisonsummary.h`, `src/archivecompare/comparisonsummary.cpp`
- Test: `tests/unit/test_comparisonsummary.cpp`

**Interfaces (produced):**

```cpp
// Q8 rule: a conclusive extension is trusted and nothing is read; an unknown
// extension is sniffed (readHead -> GerberFileType::classify). Cost stays
// proportional to the ambiguous files; accurate exactly where extensions lie
// (the real .TXT drill file must land in Gerber, so txt is NOT conclusive).
using HeadReader = std::function<QByteArray(const QString &relativePath)>;
void resolveKinds(QVector<ArchiveEntry> &entries, const HeadReader &readHead);
```

Tables (in the .cpp, `QSet<QString>` of lowercase suffixes):

```cpp
// Conclusive Gerber/drill - the old default list plus drill extensions:
gtl gbl gts gbs gto gbo gtp gbp gko gml gbr drl xln drd cmp sol stc sts plc ger
// Conclusive Other - documents/images/containers that are never layer data:
pdf doc docx xls xlsx png jpg jpeg bmp gif zip rar 7z tgz tar gz brd html htm
// Everything else (txt, csv, g1, x3b, mill, no extension, ...) -> sniff:
// classify(head) != NotDrawable ? GerberData : OtherData
```

This table serves **summary typing only** — it is not the deleted Q12 eligibility list, and there is no user-editable version of it. Note for the test target: it now needs `gerberfiletype.cpp` in its sources.

- [ ] **Step 1: Write failing tests** (reader = lambda over a `QHash<QString,QByteArray>`; track which paths were read):

```cpp
void conclusiveGerberExtensionNotRead();  // "a.gtl" → GerberData, reader NOT invoked
void conclusiveOtherExtensionNotRead();   // "spec.pdf" → OtherData, reader NOT invoked
void txtIsSniffed();                      // "drill.txt" head "M48\n" → GerberData;
                                          //   "notes.txt" head "hello" → OtherData
void unknownExtensionSniffed();           // "layer.g1" head "%FSLAX35Y35*%" → GerberData
void noExtensionSniffed();                // "readme" head "text" → OtherData
```

- [ ] **Step 2: Verify fail.**  - [ ] **Step 3: Implement.**  - [ ] **Step 4: Run green.**
- [ ] **Step 5: Commit** — `git commit -am "ComparisonSummary: kind resolution, conclusive extensions trusted, unknown sniffed (Q8)"`

### Task 16: summary line + identical banner on the page

**Files:**
- Modify: `src/archivecompare/archivecomparepage.h`, `src/archivecompare/archivecomparepage.cpp`

- [ ] **Step 1: Implement** — under the page title (full width, above `contentRow`): `summaryLabel_` (objectName `detailLabel`) and `identicalBanner_` (a `QLabel`, objectName `identicalBanner`, word-wrapped, hidden by default — a banner, deliberately not a modal: "customer resent what we already have" is the *common* outcome and must not tax a dialog). In `runComparison` after the outcome arrives: `resolveKinds` on both entry vectors (readers = `readEntryHead(path, p, GerberFileType::kClassifySniffBytes)` per side) **before** `showEntries`, then `summarize`. Format:

```cpp
// "Gerber: 9 identical · 2 pairs · A: 1 to check    Other: 5 identical"
// - identical/pairs are global, "to check" is per panel; zero-valued
//   fragments are omitted to keep the line readable.
QString formatTypeCounts(const QString &label, const ComparisonSummary::TypeCounts &c);
```

Banner text when `fullyIdentical()`: `tr("No differences - both archives contain the same data.")`; hidden otherwise; both labels cleared when a panel has an error or no comparison ran.
- [ ] **Step 2: Build + manual gate** — `test_pair_3`: summary reads "Gerber: N identical · 1 pair" (+ Other counts); no banner. Load the same archive on both sides: banner appears, summary all-identical. `test_pair_2`: pairs count 2, "to check" counts match the one-sided files.
- [ ] **Step 3: Commit** — `git commit -am "Page: type-split summary line and identical banner (Q8)"`

### Task 17: filtering in the tree model

**Files:**
- Modify: `src/archivecompare/archivetreemodel.h`, `src/archivecompare/archivetreemodel.cpp`
- Test: `tests/unit/test_archivetreemodel.cpp`

**Interfaces (produced):**

```cpp
struct TreeFilter {
    bool changedOnly = false;              // keep files with classification != Identical
    std::optional<DataKind> kind;          // nullopt = all types
};
// Rebuild-on-filter, not a proxy: the model keeps the last full entry list and
// rebuilds the visible tree from the subset. Folder auto-hide is free - folders
// are inferred from surviving paths (Q8: a folder whose children are all
// filtered out hides too).
void setFilter(const TreeFilter &filter);   // re-applies over the stored entries
bool isEmptyAfterFilter() const;            // page shows a message instead of a blank panel
```

`buildFrom(entries)` stores the full list, then applies the current filter. Criteria AND-combine.

- [ ] **Step 1: Write failing tests**:

```cpp
void changedOnlyHidesGreenFiles();      // green+yellow+red files → only yellow+red rows remain
void kindFilterHidesOtherKind();        // GerberData + OtherData files, kind=GerberData → Other gone
void filtersCombine();                  // changedOnly + Gerber → only changed Gerber rows
void emptiedFolderHides();              // folder whose only child is filtered out → folder row gone
void isEmptyAfterFilterReflectsResult();// all rows filtered → true; clear filter → false
void filterSurvivesRebuild();           // setFilter then buildFrom(new entries) → filter still applied
```

- [ ] **Step 2: Verify fail.**  - [ ] **Step 3: Implement.**  - [ ] **Step 4: Run green.**
- [ ] **Step 5: Commit** — `git commit -am "ArchiveTreeModel: changed-only and type filters with folder auto-hide (Q8)"`

### Task 18: panel split + control bar + empty message

**Files:**
- Create: `src/archivecompare/archiveheader.h`, `src/archivecompare/archiveheader.cpp` (add to app target)
- Modify: `src/archivecompare/archivepanel.h`, `src/archivecompare/archivepanel.cpp`, `src/archivecompare/archivecomparepage.h`, `src/archivecompare/archivecomparepage.cpp`

**Interfaces (produced):**

```cpp
// The header half of the old ArchivePanel: role label ("A"/"B"), current
// filename, Change... button, drag-drop target, inline error label. Split out
// (Q8) so one control bar can span both panels between the headers and trees.
class ArchiveHeader : public QWidget {
    Q_OBJECT
public:
    explicit ArchiveHeader(const QString &roleLabel, QWidget *parent = nullptr);
    QString archivePath() const;
    void showError(const QString &message);
    void clearError();
signals:
    void archiveChanged(const QString &path);
protected:
    void dragEnterEvent(QDragEnterEvent *event) override; // Task 13 rules, shared helper
    void dropEvent(QDropEvent *event) override;
};

// ArchivePanel keeps: tree view + model + delegate + showEntries() + drop
// support (both header and tree accept drops - move singleDroppedZipPath into
// a small shared header, e.g. archivedroputil.h, or duplicate the 12 lines
// with a cross-reference comment - implementer's call, note it in the commit).
// It LOSES: roleLabel_, archivePath_, fileNameLabel_, changeButton_,
// pickArchive, setArchivePath, archiveChanged - those move to ArchiveHeader.
// New: void setFilter(const TreeFilter&); bool isEmptyAfterFilter() const;
//      void showEmptyMessage(const QString&); // QStackedLayout over the tree
// ArchivePanel gains signal: void archiveDropped(const QString &path);
//      (forwarded by the page to its header's setter)
```

Page layout replaces the two fixed panels with a `QGridLayout` in the left column:

```
row 0:  headerA | headerB
row 1:  [ controlBar: "Only changes" QCheckBox · QComboBox {All types, Gerber, Other} ]  (columnSpan 2)
row 2:  panelA  | panelB
```

Panels/headers keep `setFixedWidth(252)` each; the bar spans both. Filter changes call `panelX_->setFilter(...)` on **both** panels (one bar → unambiguous that it acts on both — Q8) and then set/clear each panel's empty message: `tr("No %1files to show")` where `%1` is `"changed "` and/or `"Gerber "`/`"other "` per active filters. Filter state lives in the page members for the session; never persisted; summary is computed **before** filtering and never changes with it.

- [ ] **Step 1: Create `ArchiveHeader`** by moving the header block out of `ArchivePanel` (constructor lines building `treeCardHeader`, `pickArchive`, `setArchivePath`, Task 8's error label, Task 13's drop validation).
- [ ] **Step 2: Slim `ArchivePanel`** to the tree + `showEntries` + drops + the three new filter/empty-message methods (empty message via `QStackedLayout`: page 0 tree, page 1 centered `QLabel`).
- [ ] **Step 3: Rewire the page** — grid layout as above; connect both headers' `archiveChanged` and both panels' `archiveDropped` to the existing re-compare path (a drop on a tree routes to that side's header via `headerX_->…` setter — expose `void ArchiveHeader::setArchivePathFromDrop(const QString&)` public calling the private setter); control-bar signals re-apply filters + empty messages.
- [ ] **Step 4: Build + full ctest** — green (UI-only task; models untouched).
- [ ] **Step 5: Manual gate** — `test_pair_2`: toggle "Only changes": both trees thin to yellow/red together, summary unchanged; select "Gerber": Other rows vanish both sides; filters survive re-picking an archive (state persists within the session); engineer both filters so one tree empties → its message shows, never a blank panel; drop a zip on a header **and** on a tree — both work; inline error still displays on a bad archive.
- [ ] **Step 6: Commit** — `git commit -am "Split ArchivePanel, spanning filter bar, empty-state messages (Q8)"`

---

### Task 19: final verification + close the loop

**Files:**
- Modify: `docs/superpowers/specs/2026-07-27-content-first-archive-matching-design.md`, `docs/superpowers/specs/2026-07-27-content-based-drawability-design.md` (status lines → "Implemented", cite this plan)
- Modify: `docs/superpowers/plans/2026-07-27-archive-compare-open-questions.md` (header status: implemented on branch)

- [ ] **Step 1: Full suite** — `ctest --test-dir build --output-on-failure`: every target green (report MAX_PATH failures explicitly if the deep worktree path bites — they are environmental, not regressions, but must be named).
- [ ] **Step 2: Grep gates** — all must return nothing:
  - `grep -rn "QMessageBox" src/archivecompare/` (Q7 constraint)
  - `grep -rn "isRecognized\|addUserExtension\|userRecognizedExtensions" src/ tests/` (Q12)
  - `grep -rn "selfDuplicate" src/ tests/` (Q1)
  - `grep -rn "only new\|only old" src/` (A/B symmetry)
- [ ] **Step 3: End-to-end manual pass** — all three `Test_data` pairs; for `test_pair_3` confirm the headline result: ~24 green content classes across the renamed root folder, exactly 1 pair, summary agreeing; for `test_pair_1` (synthetic) confirm the duplicate layer set collapses green and the honest "2 files changed" story from the gap review's Q1 findings shows.
- [ ] **Step 4: Update the three docs' status lines; commit** — `git commit -am "Docs: mark Q1-Q15 designs implemented on archive-compare-content-first"`
- [ ] **Step 5: STOP.** Do not merge, do not push, do not touch `main` — integration is the user's explicit per-instance call (standing gate). Report done with the verification evidence.

---

## Self-review notes (already applied)

- **Spec coverage:** Q1→Tasks 3–5; Q2→9–10; Q3→1–2; Q4→6–7; Q7 constraint→7–8 (+timing in 8); Q8→14–18; Q9→11–12 (+Q9's stale-comment fix in 11); Q10→13; Q12→9–10; Q5/Q6/Q11/Q13/Q14/Q15 produce no code by decision.
- **Order-sensitive couplings:** Task 6 temporarily downgrades the page to top-level-only reads — restored by Tasks 7–8; Tasks 9–10 may need a combined commit to keep every intermediate tree state compiling (allowed, stated inline).
- **Type consistency check:** `PairReason::{SameName, OnlyExtensionCandidate}`, `Drawability::{ExcellonDrill, Gerber, NotDrawable}`, `DataKind::{GerberData, OtherData}`, `kClassifySniffBytes`, `TreeFilter`, `NestedExpansion`, `ArchiveComparison::Outcome` — used identically in every task that references them.
