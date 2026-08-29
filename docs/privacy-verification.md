# Privacy and network verification

**Application:** PCB Gadget (`PcbSupplierWorkflow.exe`), version 1.8.0
**Platform:** Windows 10/11, 64-bit
**Audience:** anyone reviewing this application before deployment

This document states what the application does with data, and — more usefully —
gives you the exact commands to verify each claim yourself. Nothing here asks
you to take the claims on trust. Every check below can be run against the
package you were given, without the source code and without a debugger.

---

## Summary of claims

| Claim | How to verify |
|---|---|
| Makes no network connections | §1 runtime check, §2 binary check |
| Contains no networking code | §3 source check |
| Writes exactly one settings file | §4 |
| Reads only files you choose | §5 |
| Downloads nothing at runtime | §2, §3 |

---

## 1. Runtime check — it makes no connections

The strongest check, and the one that needs no trust in the source.

**Block it at the firewall and confirm nothing changes.** Run as Administrator,
adjusting the path:

```powershell
New-NetFirewallRule -DisplayName "PCB Gadget - block all outbound" `
    -Direction Outbound -Program "C:\path\to\PcbSupplierWorkflow.exe" `
    -Action Block -Profile Any
```

Then use the application normally — load two archives, compare them, open the
Gerber overlay. Every feature continues to work, because nothing it does
requires a network. Remove the rule afterwards with:

```powershell
Remove-NetFirewallRule -DisplayName "PCB Gadget - block all outbound"
```

**Watch it live with Process Monitor.** In Sysinternals Process Monitor, set
these filters:

```
Process Name  is  PcbSupplierWorkflow.exe   then Include
Operation     begins with  TCP              then Include
Operation     begins with  UDP              then Include
```

Exercise the application with the filter active. The result list stays empty.

To capture file activity as well, replace the two Operation filters with
`Category is Write Disk` — see §4 for what you should expect to see.

## 2. Binary check — no networking library is linked

The executable's import table lists every DLL it depends on. Run this against
the shipped executable:

```powershell
$b = [IO.File]::ReadAllBytes("PcbSupplierWorkflow.exe")
$s = [Text.Encoding]::ASCII.GetString($b)
[regex]::Matches($s, '[A-Za-z0-9_.-]+\.dll') |
    ForEach-Object { $_.Value.ToLower() } | Sort-Object -Unique
```

Or, from a Visual Studio Developer Command Prompt:

```
dumpbin /dependents PcbSupplierWorkflow.exe
```

**No network library appears** — there is no `Qt6Network.dll`, no `WS2_32.dll`,
no `WININET.dll`, no `WINHTTP.dll`. The dependency list is limited to the Qt GUI
stack, the C runtime, and the zip library.

The deployed folder also contains **no** `Qt6Network.dll`, **no** `tls/` plugin
directory, and **no** `networkinformation/` plugin directory. Earlier internal
builds contained all three; §2.1 explains why, because it is worth knowing that
this was checked rather than assumed.

### 2.1 What you *will* find, and why it is not a contradiction

Run the same scan across every file in the package rather than just the
executable:

```powershell
Get-ChildItem -Recurse -File | ForEach-Object {
    $s = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($_.FullName))
    if ($s -match '(?i)(ws2_32|wsock32|wininet|winhttp|urlmon|Qt6Network|Qt6Pdf)\.dll') {
        $_.Name
    }
}
```

This prints exactly one file: **`Qt6Core.dll`**, matching on `ws2_32.dll`.

We are pointing this out rather than leaving you to find it. `Qt6Core.dll` is
Qt's core library — not our code — and it imports three Windows networking
libraries:

| Library | What it is for |
|---|---|
| `ws2_32.dll` | Winsock. Qt's core library imports it for local operations such as resolving the machine's own host name. |
| `mpr.dll`, `netapi32.dll` | Windows network *file path* support — UNC paths and mapped network drives. Needed so the file dialog can open an archive stored on a network share. |

**Importing a library is not the same as using it to connect anywhere.** No code
in this application calls any of them (§3), and the decisive evidence is the
runtime test in §1: block the executable at the firewall and every feature still
works, because nothing it does requires a network.

We cannot remove these imports without shipping a custom build of Qt itself.
They are present in every Qt application on Windows, including ones that have no
networking whatsoever.

### 2.2 What was removed to get here

Two Qt plugins were deliberately excluded from this package, because each one
dragged Qt's entire network stack in behind it:

- **`imageformats/qpdf.dll`** — the PDF image-format plugin. It imports
  `Qt6Pdf.dll`, which imports `Qt6Network.dll`. The application never reads PDFs.
- **`generic/qtuiotouchplugin.dll`** — Qt's TUIO touch-input plugin, which
  receives touch events over UDP and therefore imports `Qt6Network.dll` directly.
  The application is mouse-and-keyboard only.

With both excluded, the Qt modules deployed are exactly `Qt6Core`, `Qt6Gui`,
`Qt6Svg` and `Qt6Widgets`. Neither plugin was ever referenced by application
code; both were being added automatically by Qt's deployment tool.

## 3. Source check — no networking code exists

The full source is published at the repository this release was built from. The
runtime and binary checks above prove nothing *happens*; this one proves no code
path exists at all, including paths that clicking around the UI would never
reach.

```
grep -rniE "QNetwork|QTcpSocket|QUdpSocket|QSslSocket|QHostAddress|QWebEngine|QDesktopServices|curl|http://|https://|WinHttp|WSAStartup" src/
```

This returns **zero matches** across the entire `src/` tree.

The dependencies (QuaZip, zlib) are fetched by CMake at *build* time from pinned
release tags, and are compiled into the package you received. The application
performs no downloads when it runs.

## 4. What it writes

Exactly one file, created on first exit:

```
%APPDATA%\PcbSupplierWorkflow\PcbSupplierWorkflow.ini
```

It holds two values: the folder last browsed for archive A, and the folder last
browsed for archive B. That is the complete contents. No file names, no file
contents, no machine or user identifiers, no history, no logs.

**Note for reviewers:** a folder path can itself be sensitive, since a directory
name may contain a customer or project name. It stays on the local machine and
is never transmitted, but we mention it rather than describe the file as
containing "nothing of interest".

To inspect it:

```powershell
Get-Content "$env:APPDATA\PcbSupplierWorkflow\PcbSupplierWorkflow.ini"
```

To reset the application to a clean state, delete that file. Nothing else
persists — there are no registry keys, no files in `ProgramData`, no temporary
files left behind, and no logs.

## 5. What it reads

Only files chosen explicitly through a file dialog: the two `.zip` archives
being compared, and the Gerber/drill files contained inside them. The file
dialog opens at the user's Downloads folder by default.

The application never scans the filesystem, never enumerates directories it was
not pointed at, and never opens a file the user did not select.

## 6. Code signing

The executable is **not code-signed**. Windows SmartScreen will therefore warn
on first launch, and the file will carry a mark-of-the-web flag if it was
downloaded rather than copied from a network share.

Verify the package integrity against the SHA-256 published alongside it:

```powershell
Get-FileHash PcbSupplierWorkflow-1.8.0-win64.zip -Algorithm SHA256
```

## 7. Third-party components

The package includes Qt 6.8.2 (LGPL v3), QuaZip 1.7.2 (LGPL v2.1), zlib 1.3.1,
and bzip2. All are dynamically or statically linked as described in
`licenses/NOTICE.md`, which ships in the package along with the full licence
texts.

---

## Questions this document does not answer

If your review needs something not covered here — a specific scanner report, a
build reproduced on your own machine, or code signing before deployment — please
ask. The build is a plain CMake/MSVC build and can be reproduced from source
with the commands in the README.
