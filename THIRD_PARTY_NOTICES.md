# Chroma 0.1.0 — notices and source code

## Modified version notice — 2026-09-27

Chroma is an independent, modified version of Prism Launcher 10.0.5, based on
upstream commit `16e541f4483241455ffb674bfe975e20158001a2`. The Chroma changes made
on 2026-09-26 and 2026-09-27 replace the launcher home, instance cards, navigation
and modpack browsing UI; add accent preferences, pinned instances and direct Prism
profile selection; and add Windows packaging, tests and documentation. The original
copyright notices and licenses remain in the source. Chroma is not affiliated with
or endorsed by Prism Launcher, Modrinth, Microsoft or Mojang.

The launcher is distributed under **GNU GPL version 3 only**, without warranty.
See [LICENSE](LICENSE) for the complete terms and [COPYING.md](COPYING.md) for the
upstream component notices, including Prism Launcher, PolyMC and MultiMC credits.
This supplement does not replace or narrow those notices or license grants.

## Get the matching source

The [Chroma 0.1.0 release](https://github.com/nikitasokolov01/chroma/releases/tag/v0.1.0)
provides these source archives beside the Windows binaries, at no charge:

| Archive | Contents |
| --- | --- |
| `Chroma-0.1.0-source.tar.gz` | The exact application revision, build/package scripts, notices, and populated Git submodules; revision recorded in `SOURCE-REVISION.txt`. |
| `Chroma-0.1.0-dependency-sources.tar.gz` | Matching dependency source archives, vcpkg ports, patches, build recipes and package provenance. |
| `qt-everywhere-src-6.5.3.tar.xz` | Complete unmodified Qt 6.5.3 source, including the Qt modules and bundled third-party code used by this build. |

The Qt archive SHA-256 is
`7cda4d119aad27a3887329cfc285f2aba5da85601212bcb0aea27bd6b7b544cb`.
Build instructions are in the application source under `docs/CHROMA.md` and
`scripts/build-chroma.ps1`. The [source repository](https://github.com/nikitasokolov01/chroma)
also retains the history and upstream origin. GitHub's automatically generated
source download alone does not include the populated libnbt++ submodule; use the
explicit source asset above for the complete application tree.

## Windows release components

Full license texts and component copyright notices are in `docs/licenses/` in
the source, and `licenses/release/` in the Windows package. The additional
`licenses/` files in that package retain the installed vcpkg notices.

| Component | Version / origin | License information |
| --- | --- | --- |
| Qt Core, GUI, Widgets, Network, OpenGL, XML, SVG, image-format and platform plugins | Qt 6.5.3, official MSVC 2019 x64 distribution | LGPL-3.0 / GPL alternatives as specified by each source file; third-party components retain their own licenses. |
| Qt Network Authorization | Qt 6.5.3 | GPL-3.0-only for this open-source distribution. It is not covered by the LGPL option used by many other Qt modules. |
| libnbt++ | `531449ba1c930c98e0bcf5d332b237a8566f9d78` | LGPL-3.0-or-later; source and full license included. |
| LocalPeer / Qt Solutions | Bundled source | BSD-style license; Copyright 2013 Digia Plc and/or its subsidiaries. |
| qdcss | Bundled source | LGPL-3.0-only; Copyright 2023 kumquat-ir. |
| rainbow / KGuiAddons-derived code | Bundled source | LGPL-2.0-or-later; original notices in `COPYING.md` and source. |
| MurmurHash2 | Bundled source | Austin Appleby public-domain dedication, including the incremental modifications. |
| cmark | 0.31.1 | BSD-2-Clause; retained component notices also cover derived code. |
| libarchive | 3.8.2 | BSD-style licenses and per-file notices. |
| libqrencode | 4.1.1 | LGPL-2.1-or-later. |
| toml++ | 3.4.0 | MIT. |
| zlib | 1.3.1 | zlib. |
| zstd | 1.5.7 | BSD-3-Clause / GPL-2.0 alternatives; original notices retained. |
| LZ4 | 1.10.0 | BSD-2-Clause for the library. |
| LZO | 2.10 | GPL-2.0-or-later. |
| liblzma / XZ | 5.8.1 | 0BSD for liblzma; other source tools have their own licenses. |
| bzip2 | 1.0.8 | bzip2 license. |
| NSIS installer stub | 3.12, zlib compression | NSIS zlib/libpng license; the complete upstream `COPYING` is included. |
| Microsoft Visual C++ runtime | 14.44.35211, x64 | Microsoft redistribution terms; a separate proprietary runtime, not relicensed under the GPL. |

The launcher Java helper libraries are built from the included `libraries/launcher`
and `libraries/javacheck` source; their original GPL, linking-exception and Apache
notices remain. Java itself, Minecraft, mods and downloaded modpacks are not bundled
with the Windows release and have their own terms.

Qt is dynamically linked. Recipients can rebuild the application and its libraries,
replace compatible Qt DLLs and plugins alongside `chroma.exe`, and run their modified
version. No Chroma license term prohibits reverse engineering to debug modifications
to LGPL libraries. The complete source and build recipes are provided for rebuilding
statically linked components as well.

The `qt-6.5.3/` notice directory preserves the original attribution files and referenced
license texts from `qtbase`, `qtsvg`, `qtnetworkauth` and `qtimageformats`. It intentionally
includes optional/example/test component notices as a superset; that does not imply
every optional component is shipped. Its index describes each entry's Qt usage.

## Artwork and icons

Original Prism Launcher logos and branding from `program_info/` are retained under
**Creative Commons Attribution-ShareAlike 4.0 International**, credited to Prism
Launcher contributors. Source: [Prism Launcher](https://github.com/PrismLauncher/PrismLauncher).
The full original license is `program_info/LICENSE`, also included in the binary
license bundle as `Prism-branding-CC-BY-SA-4.0.txt`. Chroma changes the application
name and interface; the retained upstream artwork is not claimed as new Chroma art.

Other retained icons include the Batch, Material Design, Breeze and Oxygen sets.
Their copyright and permission notices remain in `COPYING.md`; relevant resource
sidecars, the SIL Open Font License and Apache 2.0 text are included in the license
bundle.

## Interface fonts

The Chroma interface bundles static **Nunito** weights 700, 800 and 900 and
**DM Sans** weights 400, 500 and 700 from the official Google Fonts service.
The font files are unmodified and are distributed under the **SIL Open Font
License, version 1.1**.

- Nunito: Copyright 2014 The Nunito Project Authors
  (https://github.com/googlefonts/nunito).
- DM Sans: Copyright 2014 The DM Sans Project Authors
  (https://github.com/googlefonts/dm-fonts).

The complete licenses are retained in
[`Nunito-OFL.txt`](launcher/resources/fonts/Nunito-OFL.txt) and
[`DMSans-OFL.txt`](launcher/resources/fonts/DMSans-OFL.txt), and embedded in the
application resources with the fonts. Exact font download sources are recorded
in [`launcher/resources/fonts/README.md`](launcher/resources/fonts/README.md).

## Service identity

This preview retains Prism's public Microsoft OAuth client ID for sign-in, so
Microsoft's consent screen can identify Prism Launcher. A client ID is public
application identification, not a client secret or an account token. Chroma remains
an independent application; retaining that identifier is not a claim of endorsement
or a grant of rights to Microsoft's services. Service terms and registration
requirements are separate from the open-source licenses above. See
[release licensing notes](https://github.com/nikitasokolov01/chroma/blob/main/docs/RELEASE-LICENSING.md)
and the [Chroma privacy statement](https://github.com/nikitasokolov01/chroma/blob/main/PRIVACY.md).
