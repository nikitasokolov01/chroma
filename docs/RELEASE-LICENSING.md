# Release licensing and source availability

## v1.0.0 local preparation

Version 1.0.0 is being prepared locally and is not yet published. Its release set
uses `Chroma-1.0.0-source.tar.gz`, `Chroma-1.0.0-dependency-sources.tar.gz`, and the
matching complete Qt source archive alongside the installer and portable ZIP.
Retain the component licenses and notices described below. The prior 0.3.0 audit
remains a historical record; it is not a fresh audit of a new package.

For an uncommitted local candidate, `archive-chroma-source.py --working-tree`
captures tracked changes and nonignored new source files with a content manifest
and the actual base and submodule revisions. Excluded local tools, profile data,
and environment files are not read into the source archive. Follow the
[local release procedure](CHROMA.md#release-versions-and-windows-installer), inspect
the final package inventory, and regenerate checksums after adding all sources.
Do not substitute the old 0.3.0 launcher source for the 1.0.0 build.

## Historical 0.3.0 distribution record

This records the Chroma 0.3.0 Windows release preparation on **2026-09-29**.
It is a distribution record and maintainer guide, not a legal guarantee.
The controlling texts are [LICENSE](../LICENSE), [COPYING.md](../COPYING.md),
[program_info/LICENSE](../program_info/LICENSE) and the retained component licenses.
The [third-party notices](../THIRD_PARTY_NOTICES.md) contain the dated modification
notice, component inventory and recipient source instructions.

## Corresponding source

Publish all three source assets with the installer and portable ZIP on the
[same release page](https://github.com/nikitasokolov01/chroma/releases/tag/v0.3.0):

1. `Chroma-0.3.0-source.tar.gz`: created by `scripts/archive-chroma-source.py` from
   the released Git revision, with submodule contents and `SOURCE-REVISION.txt`.
2. `Chroma-0.3.0-dependency-sources.tar.gz`: exact dependency archives and matching
   vcpkg port files, patches, build scripts and provenance from the release build.
3. `qt-everywhere-src-6.5.3.tar.xz`: the complete official Qt source archive. SHA-256:
   `7cda4d119aad27a3887329cfc285f2aba5da85601212bcb0aea27bd6b7b544cb`.

The explicit application archive includes libnbt++ at
`531449ba1c930c98e0bcf5d332b237a8566f9d78`; GitHub's automatic source ZIP does not
populate this submodule. Do not replace a matching source asset with source for a
later release. Keep source downloads available alongside each binary release,
without an additional charge. The release description and packaged README point
recipients to these assets. This release uses network source access under GPLv3
section 6(d), rather than relying only on an upstream link or a written offer.

The scripts and source permit rebuilding the launcher and its static libraries.
Qt DLLs and plugins are replaceable. Keep instructions sufficient to install and
run compatible modified builds, and do not add restrictions on recipients' GPL/LGPL
rights. These points follow [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html),
especially sections 1, 5 and 6, and [Qt's open-source licensing guidance](https://www.qt.io/development/open-source-lgpl-obligations).

## Notice preservation

- Preserve the original source headers, `LICENSE`, `COPYING.md` and branding
  license. The dated Chroma modification statement supplements the upstream credits.
- Package `THIRD_PARTY_NOTICES.md`, this document and `docs/licenses/` with both
  Windows distributions. The packaging script places the latter under
  `licenses/release/` and also retains installed dependency notices under `licenses/`.
- Qt Network Authorization 6.5.3 is GPL-3.0-only in its open-source source headers.
  Describing every Qt component as LGPL would be inaccurate. Other Qt modules and
  bundled third-party code retain the options stated in their own source files.
- The exact Qt license bundle is generated from the official 6.5.3 source archive,
  including 58 attribution entries from the four shipped module families. It
  includes a conservative superset of optional, example and test notices.
- Retained Prism artwork is CC BY-SA 4.0. Preserve attribution and the license;
  document changes if the artwork is modified in a later release. Copyright
  permission does not imply trademark endorsement.
- NSIS 3.12 uses zlib compression for this release. Its original complete license
  file is included. Microsoft runtime DLLs retain Microsoft's terms and are not
  described as open-source Chroma components.

## Microsoft runtime redistribution

The build uses Visual Studio 2022 Community. Its [developer license](https://visualstudio.microsoft.com/license-terms/vs2022-ga-community/)
allows individual application development and organizational development of
applications released under OSI-approved licenses (§1). Section 4 grants distribution
rights for object code on the [official Visual Studio 2022 REDIST list](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution).
That list explicitly includes Community and unmodified individual runtime binaries
from Microsoft's official redistributable downloads. This is the developer's
redistribution grant; the runtime end-user license alone is not that grant.

The package includes unmodified release CRT DLLs from Microsoft's signed 14.44.35211
x64 redistributable and its original license at
`licenses/release/Microsoft-Visual-Cpp-Runtime-14.44.rtf`. It contains no debug runtime.
Distributors must retain the Microsoft notices and require recipients to accept the
terms protecting this separate Microsoft code, as required by Community §4(b).
Microsoft's restrictions apply to its runtime; they do not replace or restrict
Chroma's GPL license or the other libraries' licenses. See also Microsoft's
[runtime redistribution guidance](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170).

## API registrations and independent identity

The upstream README separates the legal duty to follow its licenses from a request
made as **basic courtesy** to use a fork's own API credentials and clearly identify
the fork. See the preserved [upstream README](UPSTREAM_README.md). That courtesy
request is not an added GPL condition.

At the project owner's request, the preview retains Prism's public Microsoft OAuth
client ID. Microsoft documents a client ID as a public application identifier;
that alone does not establish permission to use another application's registration.
Chroma's name, documentation and release page identify the independent fork and the
Prism identity that may appear during Microsoft consent. No private Microsoft client
secret, personal account token, CurseForge key or Imgur key is supplied with this
release. See [Microsoft's application-type documentation](https://learn.microsoft.com/en-us/entra/identity-platform/msal-client-applications).

Microsoft's [identity-platform terms](https://learn.microsoft.com/en-us/legal/microsoft-identity-platform/terms-of-use)
tie an Application ID to the registered application (§3.1), restrict assignment
(§3.3), and require an accessible online privacy statement (§2.1.7). These are
service requirements, separate from GPL distribution permission. The reviewed text
does not expressly settle whether this particular fork may share Prism's public
identifier; this audit does not claim Microsoft's or Prism's authorization. A
Chroma-owned approved registration removes that ambiguity for a future release.
The [Chroma privacy statement](../PRIVACY.md) documents local account/profile storage
and the services contacted by launcher features.

## Release checks

Build packages into an empty directory. Never package a working portable launcher
folder: it may contain accounts, worlds, instances and authentication tokens.
The source archiver reads tracked Git content plus the pinned submodule, excluding
local `.tools/`, `.chroma-test/`, `dist/` and profile data. Review the actual staged
archive inventory and screenshots before publication, verify the published source
downloads and checksums, and retain this inventory with the release.

The preparation review found no new tracked candidate credential/config files or
token-pattern matches in new text sources. That limited check is not a claim that
pattern scanning can identify every secret. Screenshots used for project docs are
from the temporary test profiles rather than a personal Minecraft account.
