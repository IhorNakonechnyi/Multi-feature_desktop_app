# PCB Gadget

A small Windows desktop tool for comparing two archives of PCB manufacturing
data and seeing exactly what changed between them.

Pick two `.zip` archives, and the app pairs their contents by file content
rather than by filename, classifies every file as identical, different, or
present in only one archive, and previews the differing Gerber and drill layers
as a colour-coded overlay so a change can be read visually instead of by
comparing filenames.

## What it does

- **Content-first pairing.** Files are matched by CRC and content class, so a
  renamed file is still recognised as the same file, and a file with an
  unchanged name but changed content is still reported as different.
- **Nested archives.** Archives inside archives are expanded and compared at
  every level.
- **Gerber / drill overlay.** Differing RS-274X (Gerber) and Excellon (drill)
  pairs are rendered on top of one another so added and removed copper, pads,
  and holes are directly visible.
- **Filtering and summary.** Results can be narrowed to changed files only, or
  to a particular file type, with a summary line describing the whole comparison.

## Privacy and network behaviour

**The application makes no network calls of any kind.** There is no update
check, no telemetry, no crash reporting, and no outbound connection of any sort.
Every input is a local file that you choose yourself through a file dialog.

The only thing it writes outside its own folder is a single settings file:

```
%APPDATA%\PcbSupplierWorkflow\PcbSupplierWorkflow.ini
```

which stores the last folder you browsed for each of the two archives — two
paths, nothing else. No file contents, no filenames, no identifiers.

If you are reviewing this application before deployment, see
**[docs/privacy-verification.md](docs/privacy-verification.md)**. It lists the
checks behind these claims and the exact commands to reproduce every one of them
yourself, without taking our word for any of it.

## Requirements

- Windows 10 or 11, 64-bit.
- Nothing else. The distributed package is self-contained — the Qt runtime ships
  alongside the executable. There is no installer and nothing is written to the
  registry.

## Running the distributed package

Unzip anywhere and run `PcbSupplierWorkflow.exe`. The executable is not
code-signed, so Windows SmartScreen will show a warning on first launch; choose
*More info* → *Run anyway*, or verify the SHA-256 published with the release
first.

## Building from source

Requires Qt 6.8.2 (MSVC 2022 x64), CMake 3.21+, and Visual Studio 2022.

```powershell
cmake -S . -B build "-DCMAKE_PREFIX_PATH=C:/Qt/6.8.2/msvc2022_64"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`CMAKE_PREFIX_PATH` is required — CMake cannot find Qt without it. `ctest` needs
the `-C` flag because this is a multi-config generator; omit it and every test
reports "Not Run" instead of failing. `C:\Qt\6.8.2\msvc2022_64\bin` must be on
`PATH` when running the tests, or they exit with `0xc0000135` (a missing-DLL
error, not a test failure).

QuaZip and zlib are fetched automatically at configure time via CMake
`FetchContent`, pinned to release tags. They are build-time downloads only —
the application itself never accesses the network at runtime.

## Licence

This project is MIT licensed — see [LICENSE](LICENSE).

It links Qt (LGPL v3) and QuaZip (LGPL v2.1) as separate DLLs, and includes zlib
and bzip2. Full third-party licence texts and attribution are in
[licenses/](licenses/) — see [licenses/NOTICE.md](licenses/NOTICE.md).
