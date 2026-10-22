# How to build and package a release

Everything here is manual and deliberately so — there is no CI and no release
script. This document is the process. Run it from the repository root in
PowerShell.

Total time: roughly 10-15 minutes, almost all of it the build.

---

## Before you start

- **Close the app if it is running.** The link step fails with `LNK1168`
  otherwise, because the running process holds a lock on the executable.
- Decide the version number and make sure it matches in **three** places:
  `project(... VERSION x.y.z)` in `CMakeLists.txt`, and `FILEVERSION` /
  `PRODUCTVERSION` / the two version strings in `resources/app.rc`, and
  `setApplicationVersion` in `src/main.cpp`. They are
  duplicated by hand; nothing checks that they agree.

## 1. Configure into a clean directory

```powershell
cmake -S . -B build-release "-DCMAKE_PREFIX_PATH=C:/Qt/6.8.2/msvc2022_64"
```

`CMAKE_PREFIX_PATH` is required — without it CMake cannot find Qt. Quote the
whole `-D` argument; unquoted it gets mangled and surfaces later as a
misleading "Cannot find source file".

Use a directory that does not already exist. A stale build directory will lie
to you: CMake never deletes previously-copied DLLs, so a library you just
removed stays sitting there and the change looks like it failed.

This step downloads zlib and QuaZip via `FetchContent`. It is the only moment
in the whole process that touches the network.

## 2. Build

```powershell
cmake --build build-release --config Release
```

**Never pipe this command.** `cmake --build ... | tail` reports the *pipe's*
exit code, so a failed build reads as success. If you want to capture output,
redirect to a file and check `$LASTEXITCODE`.

## 3. Run the tests

```powershell
$env:PATH = "C:\Qt\6.8.2\msvc2022_64\bin;$env:PATH"
ctest --test-dir build-release -C Release --output-on-failure
```

`-C Release` is mandatory. This is a multi-config generator; omit it and every
test reports "Not Run" rather than failing loudly, which looks like success at
a glance.

The `PATH` line is mandatory too — without it every test exits `0xc0000135`,
which is a DLL-not-found error, not a broken test.

Expect **19/19 passed**. One of them, `test_nonetworkdependency`, inspects the
built executable rather than any source file; see step 5 for why that matters.

## 4. Assemble a clean package folder

```powershell
$dist = "build-dist\PcbSupplierWorkflow-1.8.7-win64"
Remove-Item build-dist -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $dist -Force | Out-Null
Copy-Item build-release\Release\PcbSupplierWorkflow.exe $dist
Copy-Item build-release\Release\quazip1-qt6.dll $dist
```

Start from an empty folder every time, for the same reason as step 1.

## 5. Deploy the Qt runtime

```powershell
& "C:\Qt\6.8.2\msvc2022_64\bin\windeployqt.exe" --release --no-translations `
    --no-system-d3d-compiler --no-opengl-sw `
    --exclude-plugins qpdf,qtuiotouchplugin `
    "$dist\PcbSupplierWorkflow.exe"
```

**`--exclude-plugins qpdf,qtuiotouchplugin` is load-bearing. Do not drop it.**

`windeployqt` deploys plugins based on what Qt offers, not on what the
application actually links, and two of them drag Qt's entire network stack into
the package on their own:

- `imageformats/qpdf.dll` imports `Qt6Pdf.dll`, which imports `Qt6Network.dll`.
- `generic/qtuiotouchplugin.dll` receives touch events over UDP and imports
  `Qt6Network.dll` directly.

Either one alone brings back `Qt6Network.dll`, the `tls/` plugin directory and
the `networkinformation/` plugin directory — which contradicts the zero-network
claim in `docs/privacy-verification.md`. Unlinking `Qt6::Pdf` in `CMakeLists.txt`
is necessary but **not sufficient**; this flag is the other half of the fix.

Do **not** try to fix this by deleting DLLs from the folder by hand. Removing
`Qt6Network.dll` while something still imports it produces a `0xc0000135`
failure at startup. Remove the cause, then redeploy.

Also do not exclude `qsvg` or drop `Qt6Svg.dll`. The sidebar icons are SVG and
load through that plugin; without it they silently disappear.

### Verify it worked

```powershell
(Select-String -Path windeploy.log -Pattern "To be deployed").Line
```

should name exactly `Qt6Core Qt6Gui Qt6Svg Qt6Widgets`, and:

```powershell
Get-ChildItem $dist -Recurse -File |
    Where-Object { $_.Name -match '(?i)^(Qt6Network|Qt6Pdf)\.dll$' }
```

should print nothing.

## 6. Add the documents

```powershell
Copy-Item -Recurse licenses "$dist\licenses"
Copy-Item LICENSE "$dist\LICENSE.txt"
Copy-Item README.md "$dist\README.md"
New-Item -ItemType Directory -Path "$dist\docs" -Force | Out-Null
Copy-Item docs\privacy-verification.md "$dist\docs\privacy-verification.md"
```

The `licenses/` folder is not optional. Qt and QuaZip are LGPL, and shipping
their DLLs is distribution — the licence texts and `NOTICE.md` have to travel
with the binary.

## 7. Smoke-test the deployed executable

Run `$dist\PcbSupplierWorkflow.exe` directly, from Explorer, **not** from Qt
Creator and not from a shell that has Qt on `PATH`. Qt Creator injects
`C:\Qt\6.8.2\msvc2022_64\bin` into its run environment, so an app that would
die on someone else's machine launches perfectly there and tells you nothing.

Check: it starts at all, the sidebar icon and window icon are visible, the
theme is dark, and a real comparison of two archives works end to end.

## 8. Zip and hash

```powershell
Compress-Archive -Path $dist -DestinationPath "build-dist\PcbSupplierWorkflow-1.8.7-win64.zip" -Force
Get-FileHash "build-dist\PcbSupplierWorkflow-1.8.7-win64.zip" -Algorithm SHA256
```

Record the hash. Send it to recipients **in the message body**, separately from
the file — a hash that travels inside the download proves nothing.

## 9. Publishing

Nothing here is automated on purpose. Upload the zip wherever it is going,
share the link, and include the SHA-256 and a note that the executable is not
code-signed so Windows SmartScreen will warn on first run.

If you are tagging the release in git, tag by name rather than pushing all
tags — `git push origin v1.8.7`, not `git push --tags`. There are local-only
tags in this repository that are not meant to be published.

---

## Notes for next time

- **The build is not reproducible.** MSVC embeds timestamps and a PDB
  signature, so two builds of identical source produce different bytes. Do not
  promise anyone a matching hash from a rebuild; point them at the source
  commit instead.
- **Debug builds cannot be distributed at all.** They link the debug C runtime
  (`MSVCP140D.dll`, `ucrtbased.dll`), which is not redistributable and exists
  only on machines with Visual Studio installed. Release is the only shippable
  configuration.
- **Consider `RelWithDebInfo`** instead of `Release` if you want a `.pdb` kept
  locally for diagnosing a crash report. It ships the same runtime and the same
  DLLs; you simply also get symbols. Keep the `.pdb` out of the zip.
