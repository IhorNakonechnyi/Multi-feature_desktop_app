# Third-party notices

PCB Gadget (`PcbSupplierWorkflow`) is distributed under the MIT licence — see
`LICENSE` at the repository root. That covers this project's own source only.

The distributed package also contains the third-party components listed below.
Their licences are reproduced in this folder and are **not** superseded by the
MIT licence above.

## Qt 6.8.2

- **Licence:** LGPL v3 — `Qt-LGPL-3.0.txt`
- **Copyright:** The Qt Company Ltd. and other contributors
- **Linkage:** dynamic. Shipped as `Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Widgets.dll`,
  `Qt6Svg.dll` and the Qt plugin folders (`platforms/`, `imageformats/`,
  `iconengines/`, `styles/`, `generic/`).
- **Source:** https://download.qt.io/archive/qt/6.8/6.8.2/single/

This build uses the **open-source** edition of Qt, not a commercial licence.

## QuaZip 1.7.2

- **Licence:** LGPL v2.1, with the bundled MiniZip code under its own permissive
  terms — both reproduced in `QuaZip-COPYING.txt`
- **Copyright:** Sergey A. Tachenov and contributors
- **Linkage:** dynamic. Shipped as `quazip1-qt6.dll`.
- **Source:** https://github.com/stachenov/quazip (tag `v1.7.2`)

## zlib 1.3.1

- **Licence:** zlib licence — `zlib-LICENSE.txt`
- **Copyright:** Jean-loup Gailly and Mark Adler
- **Linkage:** static, compiled into `quazip1-qt6.dll`.
- **Source:** https://github.com/madler/zlib (tag `v1.3.1`)

## bzip2

- **Licence:** BSD-style — `bzip2-LICENSE.txt`
- **Copyright:** Julian R. Seward
- **Linkage:** static, bundled inside QuaZip and compiled into `quazip1-qt6.dll`.
- **Source:** distributed within the QuaZip source tree above, under `lib/bzip2/`.

## Relinking (LGPL)

Qt and QuaZip are both **dynamically linked**, and their DLLs ship as separate
files alongside the executable. Anyone receiving this package may therefore
replace either library with a modified build of their own by substituting the
corresponding DLL, without needing anything further from us. This is the
mechanism by which the LGPL relinking requirement is satisfied — no static
linking of an LGPL component occurs anywhere in this build.
