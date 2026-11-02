#!/usr/bin/env python3
"""Rasterises the chessnut SVG pieces (Apache 2.0, Alexis Luengas) into
assets/pieces/chessnut.png. Run only when the piece artwork changes
(requires: pip install cairosvg pillow).

Layout: two rows of 12 cells, order wK wQ wR wB wN wP bK bQ bR bB bN bP.
Row 0 uses 160 px cells (board), row 1 uses 64 px cells (small icons), so the
app can pick the closest size and avoid blurry or aliased downscaling."""
import io, os
import cairosvg
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "assets-src", "pieces", "chessnut")
NAMES = ["wK", "wQ", "wR", "wB", "wN", "wP", "bK", "bQ", "bR", "bB", "bN", "bP"]
BIG, SMALL = 160, 64
atlas = Image.new("RGBA", (BIG * 12, BIG + SMALL), (0, 0, 0, 0))
for i, n in enumerate(NAMES):
    for size, y in ((BIG, 0), (SMALL, BIG)):
        png = cairosvg.svg2png(url=os.path.join(SRC, n + ".svg"), output_width=size, output_height=size)
        atlas.alpha_composite(Image.open(io.BytesIO(png)).convert("RGBA"), (i * size, y))
out = os.path.join(ROOT, "assets", "pieces", "chessnut.png")
# Pieces use few colours: a 256-colour palette (with alpha) is visually lossless
# and ~4x smaller than full RGBA.
atlas.quantize(colors=256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE).save(out, optimize=True)
print(out, os.path.getsize(out), "bytes")

# Large white knight used for the Chess card artwork in the hub.
png = cairosvg.svg2png(url=os.path.join(SRC, "wN.svg"), output_width=256, output_height=256)
icon = Image.open(io.BytesIO(png)).convert("RGBA")
icon_out = os.path.join(ROOT, "assets", "pieces", "knight_icon.png")
icon.quantize(colors=128, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE).save(icon_out, optimize=True)
print(icon_out, os.path.getsize(icon_out), "bytes")
