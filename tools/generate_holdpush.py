#!/usr/bin/env python3
"""Generate holdpush_* textures — hold progress ring (256x256, matches overlay frame)."""
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFilter
except ImportError:
    raise SystemExit("Pillow required: pip install pillow")

SIZE = 256
MARGIN = 8
STROKE = 18
CORNER = 36
# 0%, 10%, …, 100%
STAGES = list(range(0, 101, 10))

# Cyan highlight on dark track — easier to read than dim gray/white
TRACK_RGBA = (30, 35, 55, 170)
PROGRESS_RGBA = (0, 255, 245, 255)
COMPLETE_RGBA = (120, 255, 250, 255)
GLOW_RGBA = (0, 255, 245, 140)


def _ring_mask() -> Image.Image:
    mask = Image.new("L", (SIZE, SIZE), 0)
    md = ImageDraw.Draw(mask)
    outer = [MARGIN, MARGIN, SIZE - MARGIN - 1, SIZE - MARGIN - 1]
    inner = [
        MARGIN + STROKE,
        MARGIN + STROKE,
        SIZE - MARGIN - STROKE - 1,
        SIZE - MARGIN - STROKE - 1,
    ]
    md.rounded_rectangle(outer, radius=CORNER, fill=255)
    md.rounded_rectangle(inner, radius=max(4, CORNER - STROKE), fill=0)
    return mask


def _tint_mask(mask: Image.Image, rgba: tuple[int, int, int, int]) -> Image.Image:
    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    px = out.load()
    mpx = mask.load()
    r, g, b, a = rgba
    for y in range(SIZE):
        for x in range(SIZE):
            m = mpx[x, y]
            if m:
                px[x, y] = (r, g, b, a * m // 255)
    return out


def _perimeter_segments() -> tuple[int, int, int, int, int, int, int]:
    """Midline of ring stroke; clockwise from left-center: left→top→right→bottom."""
    x0 = MARGIN + STROKE // 2
    y0 = MARGIN + STROKE // 2
    x1 = SIZE - MARGIN - STROKE // 2 - 1
    y1 = SIZE - MARGIN - STROKE // 2 - 1
    cy = SIZE // 2
    w = x1 - x0
    h = y1 - y0
    perimeter = 2 * (w + h)
    return x0, y0, x1, y1, cy, w, h, perimeter


def _draw_progress_lines(draw: ImageDraw.ImageDraw, length: int, width: int, color: tuple) -> None:
    x0, y0, x1, y1, cy, w, h, perimeter = _perimeter_segments()
    if length <= 0:
        return
    remaining = min(length, perimeter)
    seg1 = cy - y0
    if remaining > 0:
        d = min(remaining, seg1)
        if d > 0:
            draw.line([(x0, cy), (x0, cy - d)], fill=color, width=width)
        remaining -= d
    if remaining > 0:
        d = min(remaining, w)
        if d > 0:
            draw.line([(x0, y0), (x0 + d, y0)], fill=color, width=width)
        remaining -= d
    if remaining > 0:
        d = min(remaining, h)
        if d > 0:
            draw.line([(x1, y0), (x1, y0 + d)], fill=color, width=width)
        remaining -= d
    if remaining > 0:
        d = min(remaining, w)
        if d > 0:
            draw.line([(x1, y1), (x1 - d, y1)], fill=color, width=width)


def _apply_ring_mask(img: Image.Image, mask: Image.Image) -> Image.Image:
  clipped = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
  clipped.paste(img, mask=mask)
  return clipped


def make_track_ring(mask: Image.Image) -> Image.Image:
    return _tint_mask(mask, TRACK_RGBA)


def make_stage(mask: Image.Image, pct: int) -> Image.Image:
    if pct >= 100:
        return _tint_mask(mask, COMPLETE_RGBA)

    base = make_track_ring(mask)
    if pct <= 0:
        return base

    _, _, _, _, _, _, _, perimeter = _perimeter_segments()
    length = int(perimeter * pct / 100)

    glow = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    _draw_progress_lines(gd, length, STROKE + 6, GLOW_RGBA)
    glow = glow.filter(ImageFilter.GaussianBlur(radius=3))

    prog = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    pd = ImageDraw.Draw(prog)
    _draw_progress_lines(pd, length, STROKE, PROGRESS_RGBA)
    prog = _apply_ring_mask(prog, mask)

    out = Image.alpha_composite(base, glow)
    out = Image.alpha_composite(out, prog)
    return out


def generate(out_dir: Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    mask = _ring_mask()
    ring = make_track_ring(mask)
    ring.save(out_dir / "holdpush_ring.png")
    print(f"Wrote {out_dir / 'holdpush_ring.png'}")

    for pct in STAGES:
        path = out_dir / f"holdpush_{pct}.png"
        make_stage(mask, pct).save(path)
        print(f"Wrote {path}")


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    generate(root / "res")
