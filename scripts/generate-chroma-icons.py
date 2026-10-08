"""Export application icons from the approved Chroma PNG. Requires Pillow."""

import base64
from pathlib import Path

from PIL import Image


def main():
    assets = Path(__file__).resolve().parents[1] / "program_info"
    source = assets / "chroma.png"
    with Image.open(source) as original:
        image = original.convert("RGBA")
    if image.width != image.height:
        raise ValueError("The approved logo must be square")
    image.resize((256, 256), Image.Resampling.LANCZOS).save(assets / "chroma_256.png")
    image.save(
        assets / "chroma.ico",
        sizes=[(size, size) for size in (16, 20, 24, 32, 40, 48, 64, 128, 256)],
    )
    image.save(assets / "chroma.icns")
    # Qt and Linux expect an SVG resource. Embed the approved raster unchanged;
    # this is an SVG container, not a traced or regenerated version of the logo.
    encoded = base64.b64encode(source.read_bytes()).decode("ascii")
    svg = (
        '<svg xmlns="http://www.w3.org/2000/svg" '
        'xmlns:xlink="http://www.w3.org/1999/xlink" '
        f'width="{image.width}" height="{image.height}" '
        f'viewBox="0 0 {image.width} {image.height}">\n'
        '  <title>Chroma</title>\n'
        '  <desc>Prism Launcher logo adapted with a C-shaped opening. '
        'Original contributors: AutiOne, Boba, ely, Fulmine, gon sawa, Pankakes, '
        'tobimori, Zeke. CC BY-SA 4.0. '
        'Source: https://github.com/PrismLauncher/PrismLauncher</desc>\n'
        f'  <image width="{image.width}" height="{image.height}" '
        f'xlink:href="data:image/png;base64,{encoded}"/>\n'
        '</svg>\n'
    )
    (assets / "chroma.svg").write_text(svg, encoding="utf-8")
    print("Exported Chroma SVG, 256px PNG, Windows ICO and macOS ICNS")


if __name__ == "__main__":
    main()
