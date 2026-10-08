# Prism Launcher Program Info

This is Prism Launcher's program info which contains information about:

- Application name and logo (and branding in general)
- Various URLs and API endpoints
- Desktop file

## Chroma logo

Chroma builds use `chroma.png`, the approved logo with a C-shaped opening in
Prism's original colors. The header, window icon and About dialog share
`chroma.svg`, which embeds that PNG to preserve the approved artwork. Windows
uses `chroma.ico`; macOS uses `chroma.icns`; Linux also installs `chroma_256.png`.

To regenerate the exports, run `python scripts/generate-chroma-icons.py` from
the repository root with Pillow installed. Export generation only resizes or
packages the approved image. Attribution and CC BY-SA 4.0 terms are recorded in
`THIRD_PARTY_NOTICES.md` and `program_info/LICENSE`.
