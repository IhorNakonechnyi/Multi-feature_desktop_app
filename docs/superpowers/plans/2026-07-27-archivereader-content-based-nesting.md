# ArchiveReader: decide "is this a nested archive?" by content, not by name

## Context

`ArchiveReader` answers "is this entry a nested archive?" from the **filename**, in
two separate places that must agree with each other. They don't, and the name is
the wrong thing to ask.

This surfaced as Q3 of the Archive Compare gap review
(`docs/superpowers/plans/2026-07-27-archive-compare-open-questions.md`). Two real
bugs, both silent:

**1. A folder whose name ends in `.zip` breaks every byte read.**
`readEntryBytes()` (`src/archivecompare/archivereader.cpp:127`) scans the path for
the next `".zip/"` boundary and opens that prefix as a nested archive. Given a
plain *folder* named `gerber.zip/`, the stored entry is `gerber.zip/top.gtl` —
there is no entry called `gerber.zip`, the lookup fails, and empty bytes come
back. The UI then reports "Neither side has drawable content", which is a lie
about the file rather than a report of the read failure. This happens whenever
someone extracts an archive, keeps the folder name, and re-zips the parent.

**2. A file named `*.zip` that isn't a zip disappears entirely.**
`readEntriesFrom()` treats any entry ending in `.zip` as a nested archive: it
decompresses it and tries to open it. On failure it `continue`s — so the entry is
never added to the file list. It is not shown in the tree, not compared, not
counted. A corrupt nested archive, or any file that merely happens to be named
`.zip`, is invisible.

Intended outcome: the reader stops guessing from names. An entry is a nested
archive only if its bytes actually open as one; a path is resolved against the
entries that actually exist. Nothing silently vanishes, and an unreadable entry
says so.

**Scope note.** Q1 (content-first matching) and Q2 (content-based drawability)
are designed but unimplemented. This plan implements neither. It does expose the
`readEntryHead()` helper Q2 will need, because Q2 would otherwise duplicate the
same path-resolution walk in the same file — write it once, here.

## Approach

### 1. Resolve paths against entries that exist

Replace the `".zip/"` string split with a walk driven by the archive's actual
contents. New helper in the anonymous namespace of `archivereader.cpp`, absorbing
the existing `findStoredEntryName()` (which already does the `\`→`/`
normalisation and case-insensitive compare needed here):

```cpp
struct ResolvedStep {
    QString storedName;    // raw stored name, as setCurrentFile() needs it; empty = no match
    bool    isFinal;       // storedName covers the whole remaining path
    QString rest;          // remainder after storedName + '/', when !isFinal
};
ResolvedStep resolveStep(QuaZip &zip, const QString &normalizedRemaining);
```

Rules, in this order:

1. A stored name equal to the whole remaining path → `isFinal = true`.
2. Otherwise the **longest** stored name `S` where the remaining path starts with
   `S + "/"` → descend into `S`.
3. Otherwise no match.

**Exact-match-before-prefix is what fixes bug 1.** With a folder `gerber.zip/`,
the full path `gerber.zip/top.gtl` exists as a stored entry, so rule 1 reads it
directly and `gerber.zip` is never treated as an archive. Longest-prefix in rule 2
is needed because a nested archive's own stored name can itself contain `/`
(`wrapper/nested.zip` is one entry, not two).

`readEntryBytes()` becomes: resolve a step; if final, read and return; otherwise
read the bytes, open them as a nested `QuaZip`, and continue with `rest`. If a
descent target fails to open as a zip, the path is bogus — return empty.

### 2. Stop dropping entries that aren't really archives

In `readEntriesFrom()`, keep `endsWith(".zip")` as a cheap **candidate filter** —
without it every entry would have to be decompressed, destroying the
central-directory-only fast path that Q1's matching depends on — but treat the
content as the decider.

Every failure path that currently `continue`s (`nestedFile.open()`,
`buffer.open()`, `nestedZip.open()`) must instead append the entry as an ordinary
`RawFileEntry`, using the `crc` and `uncompressedSize` already in hand from the
central directory. Name narrows the search; content decides; nothing vanishes.

### 3. Distinguish "unreadable" from "empty"

`readEntryBytes()` currently returns an empty `QByteArray` both for "no such
entry" and for "an entry that is genuinely zero bytes". Add an optional out-param:

```cpp
QByteArray readEntryBytes(const QString &archiveFilePath,
                          const QString &entryRelativePath,
                          bool *found = nullptr);
```

Defaulted, so existing call sites are unaffected. In
`ArchiveComparePage::showSelectedPair()` (`archivecomparepage.cpp:335`), use it to
report `could not read "<path>" from the archive` instead of the current
misleading "no drawable content" — that message should only appear when the file
was read and parsed to nothing.

### 4. Expose the header-read helper Q2 needs

```cpp
QByteArray readEntryHead(const QString &archiveFilePath,
                         const QString &entryRelativePath,
                         qint64 maxBytes,
                         bool *found = nullptr);
```

Same walk; only the final read differs (`file.read(maxBytes)` rather than
`readAll()`). Intermediate nested-archive levels still need their full bytes to be
opened — unavoidable. Nothing calls this yet; Q2 will.

## Files

- `src/archivecompare/archivereader.h` — `found` out-param on `readEntryBytes`,
  new `readEntryHead`, doc comments updated (the current ones describe the
  `".zip/"` scanning strategy in detail and must be rewritten).
- `src/archivecompare/archivereader.cpp` — `resolveStep()` replacing
  `findStoredEntryName()`; both public readers rewritten around it;
  `readEntriesFrom()` non-archive fallback.
- `src/archivecompare/archivecomparepage.cpp` — read-failure status text.
- `tests/unit/test_archivereader.cpp` — new cases below.
- `tests/unit/CMakeLists.txt` — no change; the `test_archivereader` target and its
  QuaZip link already exist.

## Tests

Existing helper `addEntry(QuaZip&, name, content)` plus `QTemporaryDir` already
builds real archives, including genuinely backslash-separated names — the new
fixtures need no new machinery. Note that `readArchive()` currently has **no**
coverage at all; cases 3–5 are its first.

1. Folder named `*.zip`: entry `gerber.zip/top.gtl` → `readEntryBytes` returns the
   content. *(fails today)*
2. Same, nested deeper: `wrapper/gerber.zip/sub/top.gtl`. *(fails today)*
3. `readArchive` lists a non-zip file named `notes.zip` with its correct crc and
   size, instead of dropping it. *(fails today)*
4. `readEntryBytes` reads that same misnamed file's bytes. *(fails today)*
5. `readArchive` still flattens a genuine nested zip to `nested.zip/inner.gtl`.
6. Exact match beats prefix descent: an archive holding both a real zip entry
   `a.zip` and an entry `a.zip/x.gtl` — asking for `a.zip/x.gtl` returns the
   direct entry.
7. `readEntryHead` returns only the first N bytes of a large entry.
8. `readEntryHead` works through a real nested archive.
9. Missing entry → empty bytes and `found == false`.
10. Zero-length entry → empty bytes and `found == true`.

Existing cases (`simpleTopLevelEntry`, `plainSubfolderEntryNoNestedZip`,
`nestedZipInsideSubfolder`, `backslashSeparatedEntryNoNestedZip`,
`missingEntryReturnsEmpty`) must pass unchanged.

## Verification

```powershell
cmake --build build
ctest --test-dir build -R archivereader --output-on-failure
ctest --test-dir build --output-on-failure   # nothing else regressed
```

End-to-end, since the bug is invisible in unit tests alone: build two small
archives containing a **folder** named `gerber.zip/` with a real `.gtl` inside,
differing between the two. Launch the app, load both, select the resulting pair —
the overlay must render. Before this change it shows "Neither side has drawable
content".

Then repeat with a text file named `notes.zip` present in only one archive: it
must appear as a row in the tree. Today it is absent entirely.

## Follow-ups, deliberately not here

- **Skipping recursion when a nested archive's CRC matches on both sides.** Igor's
  rule, correct and safe in the equal-CRC direction only. It needs both archives
  in hand, so it belongs to the comparison layer, not the reader. Filed under Q4.
- **Recursing into archives that aren't named `.zip`** (`.rar`, `.7z`, or a nested
  zip with an odd extension). Also Q4, and it reopens the RAR licensing question.
