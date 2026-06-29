#!/usr/bin/env python3
"""Generate res/overlay.png — judgment approach ring (matches holdpush frame style)."""
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError:
    raise SystemExit("Pillow required: pip install pillow")

SIZE = 256
STROKE = 16
CORNER = 36
MARGIN = 8

root = Path(__file__).resolve().parents[1]
out = root / "res" / "overlay.png"

img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
mask = Image.new("L", (SIZE, SIZE), 0)
md = ImageDraw.Draw(mask)

outer = [MARGIN, MARGIN, SIZE - MARGIN - 1, SIZE - MARGIN - 1]
inner = [MARGIN + STROKE, MARGIN + STROKE, SIZE - MARGIN - STROKE - 1, SIZE - MARGIN - STROKE - 1]
md.rounded_rectangle(outer, radius=CORNER, fill=255)
md.rounded_rectangle(inner, radius=max(4, CORNER - STROKE), fill=0)

# Cyan-white ring tint baked into texture for visibility on dark grid
ring = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
pixels = ring.load()
mask_px = mask.load()
for y in range(SIZE):
    for x in range(SIZE):
        if mask_px[x, y]:
            pixels[x, y] = (0, 255, 245, 230)

img = Image.alpha_composite(img, ring)
img.save(out)
print(f"Wrote {out}")
