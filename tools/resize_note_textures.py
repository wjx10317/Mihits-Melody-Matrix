#!/usr/bin/env python3
"""Upscale note textures to 256x256 to match res/background.png."""
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    raise SystemExit("Pillow required: pip install pillow")

ROOT = Path(__file__).resolve().parents[1] / "res"
TARGET = 256

ITEM_TEXTURES = (
    "background.png",
    "tap.png",
    "slider.png",
    "overlay.png",
    "holdpush_ring.png",
    "holdpush_0.png",
    "holdpush_10.png",
    "holdpush_20.png",
    "holdpush_30.png",
    "holdpush_40.png",
    "holdpush_50.png",
    "holdpush_60.png",
    "holdpush_70.png",
    "holdpush_80.png",
    "holdpush_90.png",
    "holdpush_100.png",
)

for name in ITEM_TEXTURES:
    path = ROOT / name
    if not path.exists():
        print(f"skip missing {path}")
        continue
    img = Image.open(path).convert("RGBA")
    if img.size == (TARGET, TARGET):
        print(f"ok {name} already {TARGET}x{TARGET}")
        continue
    out = img.resize((TARGET, TARGET), Image.Resampling.LANCZOS)
    out.save(path)
    print(f"resized {name} {img.size} -> {TARGET}x{TARGET}")
